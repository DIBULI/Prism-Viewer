import hashlib
import json
import shutil
import struct
import subprocess
import zipfile
from pathlib import Path

W = Path('/Users/xiangfuli/Documents/ChatGPT/RK3576')
E = W / 'artifacts/21312-sample-delivery-20261116'
CAPTURE = 'capture-19700102-001803-0b034161cdbc'
D = E / CAPTURE
GOLD = W / 'artifacts/viewer-image-export-20261116/real21312-images'
OUT = W / 'deliverables/21312V0-dataset-sample-20261116'
ROOT = 'Prism-21312V0-camera-sample'
SUPPORT = E / 'package-support'

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()

def save(path, obj):
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + '\n')

def jpeg_size(b):
    assert b[:2] == b'\xff\xd8' and b[-2:] == b'\xff\xd9'
    at = 2
    while at < len(b):
        assert b[at] == 255
        while b[at] == 255:
            at += 1
        marker = b[at]
        at += 1
        if marker in (0x01, *range(0xd0, 0xd9)):
            continue
        length = int.from_bytes(b[at:at + 2], 'big')
        assert length >= 2 and at + length <= len(b)
        if marker in (0xc0, 0xc1, 0xc2):
            return int.from_bytes(b[at + 5:at + 7], 'big'), int.from_bytes(b[at + 3:at + 5], 'big')
        assert marker != 0xda, 'SOF absent before SOS'
        at += length
    raise ValueError('JPEG geometry missing')

OUT.mkdir(parents=True, exist_ok=True)
SUPPORT.mkdir(exist_ok=False)
decoded = E / 'reference-validation'
decoded.mkdir(exist_ok=False)
files = {}
for item in json.loads((E / 'transfer.json').read_text()):
    p = D / item['name']
    assert p.stat().st_size == item['size'] and sha(p) == item['sha256'] and item['source_unchanged']
    files[f'dataset/{CAPTURE}/{p.name}'] = p

source = json.loads((E / 'source.json').read_text())
assert (D / 'dataset.info').read_text() == source['info']
manifest = dict(line.split('=', 1) for line in source['info'].splitlines())
assert manifest['device_model'] == '21312V0' and manifest['complete'] == manifest['finalized'] == '1'
refs, cameras = [], []
for cam in range(4):
    raw = cam in (1, 2)
    rows = [json.loads(line) for line in (D / f'camera{cam}.jsonl').read_text().splitlines()]
    stream = D / f'camera{cam}.{"pr8-stream" if raw else "mjpg-stream"}'
    expected_count = 62 if raw else 369
    assert len(rows) == expected_count == int(manifest[f'camera{cam}_images'])
    offset, previous = 0, None
    with stream.open('rb') as f:
        for i, row in enumerate(rows):
            assert row['offset'] == offset and row['bytes'] > 0 and row['dropped_before'] == 0
            assert row['source_bits'] == 8 and row['timing_mode'] == 4
            payload = f.read(row['bytes'])
            assert len(payload) == row['bytes']
            if raw:
                assert payload[:7] == b'PRG8\x01\x50\x08'
                assert struct.unpack_from('<III', payload, 8) == (4096, 3072, 4096 * 3072)
                assert payload[37] == cam and payload[38] == row['timing_mode']
                assert struct.unpack_from('<I', payload, 24)[0] == row['frame_id']
                assert struct.unpack_from('<Q', payload, 40)[0] == row['timestamp_us']
                assert struct.unpack_from('<I', payload, 52)[0] == row['trigger_sequence']
                assert struct.unpack_from('<I', payload, 56)[0] == row['bytes'] - 80
            else:
                assert jpeg_size(payload) == (1280, 1024)
            if previous:
                assert row['timestamp_us'] > previous['timestamp_us']
                assert row['timestamp_us'] - previous['timestamp_us'] in ((500000,) if raw else (83333, 83334))
                assert row['frame_id'] - previous['frame_id'] == (3 if raw else 1)
                assert row['trigger_sequence'] - previous['trigger_sequence'] == (6 if raw else 1)
            if i in (0, len(rows) - 1):
                refnum = 0 if i == 0 else 1
                ext = 'png' if raw else 'jpg'
                gold = GOLD / f'camera{cam}/{refnum:08d}_{row["timestamp_us"]}.{ext}'
                assert gold.is_file()
                arc = f'reference/camera{cam}/{"first" if refnum == 0 else "last"}_{row["timestamp_us"]}.{ext}'
                files[arc] = gold
                ref = dict(camera=cam, row_index=i, source_stream=f'dataset/{CAPTURE}/{stream.name}',
                           source_record=row, reference_image=arc, reference_sha256=sha(gold))
                if raw:
                    pgm = decoded / f'camera{cam}-{refnum}.pgm'
                    run = subprocess.run([str(E / 'decode_raw8'), str(stream), str(offset), str(row['bytes']), str(pgm)], text=True, capture_output=True, check=True)
                    cmp = subprocess.run([str(E / 'compare_reference'), str(pgm), str(gold)], text=True, capture_output=True, check=True)
                    ref.update(native_pixel_sha256=cmp.stdout.strip(), decoder_result=run.stdout.strip(), all_reference_pixels_match=True)
                else:
                    assert payload == gold.read_bytes()
                    ref['reference_is_byte_exact_original_jpeg'] = True
                refs.append(ref)
            offset += row['bytes']
            previous = row
        assert f.read(1) == b'' and offset == stream.stat().st_size
    cameras.append(dict(camera=cam, width=4096 if raw else 1280, height=3072 if raw else 1024,
                        stored_fps=2 if raw else 12, frames=len(rows), bytes=offset,
                        first_timestamp_us=rows[0]['timestamp_us'], last_timestamp_us=rows[-1]['timestamp_us'],
                        codec='PRG8 lossless native RAW8' if raw else 'JPEG', all_index_ranges_headers_and_cadence_valid=True))

readme = '''21312V0 相机数据集样例（此前称 21315）

用途：供 DIBULIStudio 适配混合相机数据集读取与图像导出。
实际型号：21312V0；设备序列号：1b78953e9fc85455。
数据集：capture-19700102-001803-0b034161cdbc。
录制时长约 30 秒，四路共 862 帧；这是实际设备录制，原始 9 个文件保持不变。

cam0、cam3：1.3M，1280×1024，JPEG，落盘 12 FPS，各 369 帧。
cam1、cam2：12M，4096×3072，原生 RAW8 无损压缩，落盘 2 FPS，各 62 帧。
本段仅录制相机，没有 IMU、GNSS、LiDAR；不要要求不存在的传感器文件。

从 dataset/ 下包含 dataset.info 的目录导入。
各 cameraN.jsonl 是相应 stream 的逐帧索引；按 offset/bytes 提取图像。
RAW8 的 .pr8-stream 不能直接当作连续裸像素读取，需先解压 PRG8。
FORMAT.txt 给出适配要点；reference_decoder/ 提供独立 C++17 解码示例。
reference/ 含每路首帧和末帧，便于对照。RAW PNG 保留原生 Bayer 单字节像素，
未去马赛克、白平衡或调整亮度；直接用普通图片软件查看不等于最终彩色效果。

样例检查：9 个原始文件传输前后 SHA256 一致；862 条索引、帧头、尺寸和时间间隔检查通过。
8 张对照图中，4 张 JPEG 与原始帧字节相同，4 张 RAW PNG 已逐像素对照独立解码结果。
没有对整段所有帧做完整像素解码，亦不将短片段检查视为长时间稳定性测试。
SHA256SUMS 用于验证本样例包文件。原始 metadata 保留录制时的状态与字段。
'''
fmt = '''DIBULIStudio 适配要点

1. 识别 format=prism-mixed-dataset-v1 和 device_model=21312V0。
   dataset.info 是 UTF-8 的 key=value；不要按“所有相机都同一种编码/尺寸”处理。
2. 对每路读取 cameraN.jsonl。每行 JSON 的 offset、bytes 是对应二进制 stream
   中的字节范围，使用 64 位整数处理范围和 timestamp_us。
3. cam0/3 的 .mjpg-stream 是一幅幅 JPEG 拼接，按索引提取即可保存为 .jpg。
   JPEG 内可能含 Prism APP15 元数据；标准 JPEG 解码器可解码，提取时保留原始字节。
4. cam1/2 的 .pr8-stream 每帧为 80 字节 PRG8 头加无损 block/Rice 数据。
   解压得到 width*height 字节，每像素一个 RAW8 Bayer 值。不得扩充为 RAW12，
   不得把压缩数据直接展示为 RAW。reference_decoder 提供当前 SDK 的头解析和解码代码。
   导出 native Bayer PNG 时保持 8 位灰度存储；彩色预览需另做 Bayer 转换，不能凭
   “灰度 PNG”推断相机是黑白，也不要在没有型号定义的情况下猜 Bayer 编号的排列。
5. 保留各路 timestamp_us、frame_id、trigger_sequence、timing_mode 以及原始头中的
   exposure_us/gain_x1024。不要跨相机按数组下标配对，不要把 frame_id 非 +1 直接判为丢帧。
   此样例声明 independent-camera-timestamps-not-a-synchronized-set；时间值不能当 UTC。
   数据集名中的 19700102 也不能当作真实采集日期。显示/对齐时使用原始时间字段及声明。
6. recording_scope=camera-only，缺少其他传感器文件属于此样例的正常组成。
7. reference/index.json 记录首末对照图在完整原始文件中的真实 offset/bytes 和行号，
   不是重新打包的子集索引。JPEG 可直接比字节；RAW 可解压后逐像素比对。
'''
decoder_doc = f'''原生 RAW8 独立解码示例

包含当前 SDK 的 raw8_header.hpp、raw8_rice.hpp、raw_header.hpp，以及示例主程序。
不依赖 Qt、libusb、zstd；不连接设备。支持此样例使用的 PRG8 编码。

编译（在本目录内，使用支持 C++17 的编译器）：
  c++ -std=c++17 -O2 decode_raw8.cpp -o decode_raw8

解码 cam1 第一帧（从本目录执行）：
  ./decode_raw8 ../dataset/{CAPTURE}/camera1.pr8-stream 0 2211550 cam1-first.pgm

其他帧使用 cameraN.jsonl 的 offset 和 bytes。输出 PGM P5，4096×3072，8 位原生 Bayer。
输出已存在会拒绝覆盖。示例检查边界、结构和帧头，不能将成功解码等同于完整载荷校验。
若集成到 DIBULIStudio，可直接调用 parseRaw8Header 和 decodeRaw8RicePayload，
然后将像素交给图像处理或 PNG 写入接口。
'''
for name, value in [('README.txt', readme), ('FORMAT.txt', fmt), ('decoder-README.txt', decoder_doc)]:
    p = SUPPORT / name
    p.write_text(value)
    files['reference_decoder/README.txt' if name == 'decoder-README.txt' else name] = p
save(SUPPORT / 'index.json', {'dataset': CAPTURE, 'model': '21312V0', 'references': refs})
files['reference/index.json'] = SUPPORT / 'index.json'
for name in ('decode_raw8.cpp', 'raw8_header.hpp', 'raw8_rice.hpp', 'raw_header.hpp'):
    files[f'reference_decoder/{name}'] = E / name

checksums = {name: sha(path) for name, path in sorted(files.items())}
(SUPPORT / 'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name, digest in checksums.items()))
files['SHA256SUMS'] = SUPPORT / 'SHA256SUMS'
archive = OUT / f'{ROOT}-20261116.zip'
assert not archive.exists()
with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as z:
    for name, path in sorted(files.items()):
        z.write(path, f'{ROOT}/{name}')
with zipfile.ZipFile(archive) as z:
    assert len(z.namelist()) == len(set(z.namelist())) == len(files)
    for name, path in sorted(files.items()):
        h = hashlib.sha256()
        with z.open(f'{ROOT}/{name}') as f:
            for b in iter(lambda: f.read(1024 * 1024), b''):
                h.update(b)
        assert h.hexdigest() == sha(path)
digest = sha(archive)
(OUT / f'{archive.name}.sha256').write_text(f'{digest}  {archive.name}\n')
summary = dict(model='21312V0', former_name='21315', device_serial=source['serial'], source_boot=source['boot'],
               capture=CAPTURE, original_files=9, original_bytes=sum(p.stat().st_size for p in D.iterdir()),
               recording_scope='camera-only', cameras=cameras, reference_images=8,
               reference_raw_all_pixels_compared=4, reference_jpeg_byte_exact=4,
               full_recording_pixel_decode=False, archive_files=len(files), archive=str(archive),
               archive_bytes=archive.stat().st_size, archive_sha256=digest,
               archive_all_files_readback_hash_match=True, original_files_unchanged=True,
               remote_actions='read-only download; no acquisition, service, power or firmware changes',
               download_session='5400 exit0 closed', publication='none')
save(E / 'summary.json', summary)
print(json.dumps(summary, ensure_ascii=False, indent=2))
