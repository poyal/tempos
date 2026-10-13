$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$root=Split-Path $PSScriptRoot -Parent
$bitmap=[Drawing.Bitmap]::new(492,942,[Drawing.Imaging.PixelFormat]::Format24bppRgb)
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$bounds=[Drawing.Rectangle]::new(0,0,164,314)
$brush=[Drawing.Drawing2D.LinearGradientBrush]::new($bounds,[Drawing.Color]::FromArgb(40,79,157),[Drawing.Color]::FromArgb(112,162,204),90)
$icon=[Drawing.Icon]::new((Join-Path $root 'resources/tempos.ico'),192,192)
$font=[Drawing.Font]::new('Segoe UI',19,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
$small=[Drawing.Font]::new('Segoe UI',10,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
try {
  $graphics.ScaleTransform(3,3)
  $graphics.TextRenderingHint=[Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  $graphics.FillRectangle($brush,$bounds)
  $graphics.DrawIcon($icon,[Drawing.Rectangle]::new(50,76,64,64))
  $format=[Drawing.StringFormat]::new()
  try {
    $format.Alignment=[Drawing.StringAlignment]::Center
    $graphics.DrawString('Tempos',$font,[Drawing.Brushes]::White,[Drawing.RectangleF]::new(0,156,164,32),$format)
    $graphics.DrawString('DESKTOP WIDGETS',$small,[Drawing.Brushes]::White,[Drawing.RectangleF]::new(0,190,164,24),$format)
  } finally {$format.Dispose()}
  $bitmap.Save((Join-Path $root 'resources/installer-sidebar.bmp'),[Drawing.Imaging.ImageFormat]::Bmp)
} finally {$small.Dispose();$font.Dispose();$icon.Dispose();$brush.Dispose();$graphics.Dispose();$bitmap.Dispose()}
