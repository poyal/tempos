param([Parameter(Mandatory)][string]$Results,[string]$Output='docs/assets/widgets-overview.png')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$source=(Resolve-Path -LiteralPath $Results).Path
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root $Output
$bitmap=[Drawing.Bitmap]::new(1088,730)
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$title=[Drawing.Font]::new('Malgun Gothic',24,[Drawing.FontStyle]::Bold)
$caption=[Drawing.Font]::new('Malgun Gothic',11)
$ink=[Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(25,52,86))
try {
  $graphics.Clear([Drawing.Color]::FromArgb(237,244,250))
  $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $graphics.DrawString('Tempos · 실제 위젯 화면',$title,$ink,24,22)
  $graphics.DrawString('9개 위젯 · M 크기 · 촬영용 예제 데이터',$caption,$ink,26,73)
  $kinds=@(0,1,8,2,3,4,5,6,7)
  for($i=0;$i -lt $kinds.Count;$i++) {
    $capture=[Drawing.Image]::FromFile((Join-Path $source "design-kind-$($kinds[$i])-size-2.png"))
    try {
      $x=24+($i%3)*352; $y=119+[Math]::Floor($i/3)*184
      $graphics.DrawImage($capture,[int]$x,[int]$y,336,160)
    } finally {$capture.Dispose()}
  }
  $graphics.DrawString('모든 위젯은 조회 전용 · 설정은 트레이에서 · 위치 이동은 정렬 모드에서',$caption,$ink,26,689)
  $bitmap.Save($output,[Drawing.Imaging.ImageFormat]::Png)
} finally {$graphics.Dispose();$bitmap.Dispose();$title.Dispose();$caption.Dispose();$ink.Dispose()}
Get-FileHash -LiteralPath $output -Algorithm SHA256
