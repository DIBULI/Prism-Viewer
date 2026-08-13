# Prism Logo

Prism 的核心图形是“单束光进入棱镜并分成四路”：

- 四条出射光代表四路同步相机；
- 棱镜代表相机、双 IMU 和时间系统的融合；
- 左侧单束青色光代表统一时基和统一控制入口；
- 圆角深色底保证在 Windows 任务栏、Linux desktop、macOS Dock 和设备列表中清晰。

## Assets

- `prism-mark.svg`：正方形产品标识，透明画布外无额外留白；
- `prism-logo.svg`：标识与 PRISM 字标的横向组合；
- `prism-mark-256.png`：应用运行时图标和 Linux 安装图标；
- `prism-viewer.icns`：macOS App Bundle 的多分辨率 Finder、Dock 和
  Launchpad 图标；
- `prism-viewer.ico`：兼容旧版 Windows resource compiler 的 executable 图标；
  Viewer 运行时使用 256 px PNG，因此标题栏和任务栏仍使用高清源。

SVG 是主资产，PNG/ICO 是由同一几何定义生成的派生文件。不要从 PNG 再反向
缩放生成新尺寸。

## Colors

| Name | Hex | Usage |
| --- | --- | --- |
| Midnight | `#0B1830` | 图标背景、主字标 |
| Sync teal | `#35D0C5` | 输入光束、统一时基 |
| Camera teal | `#2DD4BF` | Camera 0 |
| Camera blue | `#38BDF8` | Camera 1 |
| Camera indigo | `#6366F1` | Camera 2 |
| Camera violet | `#A855F7` | Camera 3 |

## Usage

- 图标尺寸小于 48 px 时只使用 `prism-mark`；
- 横向空间充足时使用 `prism-logo`；
- 不改变四条出射光顺序，不添加渐变、投影或描边文字；
- 图标外围至少保留图标宽度 8% 的安全空间；
- 深色背景上使用完整圆角底板，不单独取出白色棱镜。
