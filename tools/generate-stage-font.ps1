$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$outputPath = Join-Path (Split-Path $PSScriptRoot) 'resources/ui/stage_font.png'
New-Item -ItemType Directory -Force (Split-Path $outputPath) | Out-Null
# ASCII 32..95, 16 columns, 48x64 cells. Runtime only loads the resulting PNG.
$bitmap = [Drawing.Bitmap]::new(768, 256, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$font = [Drawing.Font]::new('Consolas', 46, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
$format = [Drawing.StringFormat]::new()
$format.Alignment = [Drawing.StringAlignment]::Center
$format.LineAlignment = [Drawing.StringAlignment]::Center
$graphics.Clear([Drawing.Color]::Transparent)
$graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
try {
    for ($index = 0; $index -lt 64; ++$index) {
        $rect = [Drawing.RectangleF]::new(($index % 16) * 48, [Math]::Floor($index / 16) * 64, 48, 64)
        $graphics.DrawString(([char]($index + 32)).ToString(), $font, [Drawing.Brushes]::White, $rect, $format)
    }
    $bitmap.Save($outputPath, [Drawing.Imaging.ImageFormat]::Png)
} finally {
    $format.Dispose()
    $font.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}
