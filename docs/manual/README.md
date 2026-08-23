# Prism Viewer 操作手册构建说明

本目录保存 Prism Viewer 中文操作手册的生成脚本和公开截图素材。最终 PDF 位于：

`docs/Prism-Viewer-1.0.0-用户操作手册.pdf`

## 目录

- `generate_prism_viewer_manual.py`：生成 28 页 A4 PDF。
- `redact_viewer_screenshots.py`：对 Camera 预览区域做像素化处理。
- `assets/screenshots/`：可公开的设备界面截图。
- `assets/screenshots/redacted/`：已经打码的 Camera 截图。
- `assets/private/`：本地临时存放未打码截图；该目录内容不会提交到 Git。

## 依赖

```bash
python3 -m pip install reportlab pillow
```

macOS 会自动使用系统 CJK 字体。Linux 可安装 Noto CJK 字体，或设置：

```bash
export PRISM_MANUAL_CJK_FONT=/path/to/CJK-Regular.ttf
export PRISM_MANUAL_CJK_BOLD_FONT=/path/to/CJK-Bold.ttf
```

## 生成 PDF

从 Viewer 仓库根目录运行：

```bash
python3 docs/manual/generate_prism_viewer_manual.py
```

脚本会覆盖 `docs/Prism-Viewer-1.0.0-用户操作手册.pdf`。

## 更新 Camera 截图并打码

不要把未打码的 Camera 截图提交到仓库。将以下三个 1200 x 768 JPEG 文件放入任意私有目录：

- `02-camera-stream.jpeg`
- `03-camera-exposure.jpeg`
- `04-camera-metadata.jpeg`

然后运行：

```bash
python3 docs/manual/redact_viewer_screenshots.py \
  --source-dir /path/to/private-screenshots
```

脚本只输出已经像素化的 PNG 到 `docs/manual/assets/screenshots/redacted/`。生成后应逐张检查，确认四路 Camera 画面均已打码，再重新生成 PDF。

## 发布前检查

1. 使用 Poppler 将全部页面渲染为 PNG。
2. 检查文字、表格、页码和截图没有裁切或重叠。
3. 确认 Camera 截图只使用 `redacted/` 中的文件。
4. 确认双 IMU 截图的说明仍注明：标准产品默认标配单 IMU。
