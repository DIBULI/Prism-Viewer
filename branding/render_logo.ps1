param(
    [string]$OutputDirectory = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

function New-RoundedRectanglePath {
    param(
        [System.Drawing.RectangleF]$Rectangle,
        [float]$Radius
    )

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $diameter = $Radius * 2.0
    $path.AddArc($Rectangle.X, $Rectangle.Y, $diameter, $diameter, 180, 90)
    $path.AddArc($Rectangle.Right - $diameter, $Rectangle.Y,
                 $diameter, $diameter, 270, 90)
    $path.AddArc($Rectangle.Right - $diameter,
                 $Rectangle.Bottom - $diameter,
                 $diameter, $diameter, 0, 90)
    $path.AddArc($Rectangle.X, $Rectangle.Bottom - $diameter,
                 $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-PrismPngBytes {
    param([int]$Size)

    $bitmap = New-Object System.Drawing.Bitmap(
        $Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode =
        [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode =
        [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.ScaleTransform($Size / 256.0, $Size / 256.0)

    $background = [System.Drawing.ColorTranslator]::FromHtml('#0B1830')
    $path = New-RoundedRectanglePath `
        ([System.Drawing.RectangleF]::new(8, 8, 240, 240)) 52
    $brush = New-Object System.Drawing.SolidBrush($background)
    $graphics.FillPath($brush, $path)
    $brush.Dispose()
    $path.Dispose()

    $rays = @(
        @('#2DD4BF', 151, 112, 224, 74),
        @('#38BDF8', 157, 122, 229, 108),
        @('#6366F1', 157, 134, 229, 148),
        @('#A855F7', 151, 144, 224, 182)
    )
    foreach ($ray in $rays) {
        $pen = New-Object System.Drawing.Pen(
            [System.Drawing.ColorTranslator]::FromHtml($ray[0]), 12)
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $graphics.DrawLine($pen, $ray[1], $ray[2], $ray[3], $ray[4])
        $pen.Dispose()
    }

    $inputPen = New-Object System.Drawing.Pen(
        [System.Drawing.ColorTranslator]::FromHtml('#35D0C5'), 14)
    $inputPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $inputPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawLine($inputPen, 30, 128, 82, 128)
    $inputPen.Dispose()
    $inputBrush = New-Object System.Drawing.SolidBrush(
        [System.Drawing.ColorTranslator]::FromHtml('#35D0C5'))
    $graphics.FillEllipse($inputBrush, 23, 121, 14, 14)
    $inputBrush.Dispose()

    $pTop = [System.Drawing.PointF]::new(87, 57)
    $pCenter = [System.Drawing.PointF]::new(126, 128)
    $pTip = [System.Drawing.PointF]::new(165, 128)
    $pBottom = [System.Drawing.PointF]::new(87, 199)

    $facets = @(
        @('#DFF8F5', @($pTop, $pCenter, $pBottom)),
        @('#CBE7FF', @($pTop, $pTip, $pCenter)),
        @('#D8D8FF', @($pCenter, $pTip, $pBottom))
    )
    foreach ($facet in $facets) {
        $facetBrush = New-Object System.Drawing.SolidBrush(
            [System.Drawing.ColorTranslator]::FromHtml($facet[0]))
        $graphics.FillPolygon($facetBrush, $facet[1])
        $facetBrush.Dispose()
    }

    $outline = New-Object System.Drawing.Pen(
        [System.Drawing.Color]::White, 6)
    $outline.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $graphics.DrawPolygon($outline, @($pTop, $pTip, $pBottom))
    $outline.Dispose()

    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $stream.ToArray()
    $stream.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    return $bytes
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = @()
foreach ($size in $sizes) {
    $images += ,(New-PrismPngBytes $size)
}

[System.IO.File]::WriteAllBytes(
    (Join-Path $OutputDirectory 'prism-mark-256.png'),
    $images[$images.Count - 1])

# The legacy Windows resource compiler used by the current build host rejects
# PNG-compressed multi-image ICO files. Save a native 32-bit HICON instead;
# the Viewer uses the embedded 256 px PNG at runtime and this ICO is only the
# executable shell icon.
$pngStream = New-Object System.IO.MemoryStream(,$images[2])
$iconBitmap = New-Object System.Drawing.Bitmap($pngStream)
$iconHandle = $iconBitmap.GetHicon()
$icon = [System.Drawing.Icon]::FromHandle($iconHandle)
$iconPath = Join-Path $OutputDirectory 'prism-viewer.ico'
$iconStream = [System.IO.File]::Create($iconPath)
$icon.Save($iconStream)
$iconStream.Dispose()
$icon.Dispose()
$iconBitmap.Dispose()
$pngStream.Dispose()

Write-Host "Wrote prism-mark-256.png and prism-viewer.ico to $OutputDirectory"
