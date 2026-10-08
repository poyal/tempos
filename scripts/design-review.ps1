param([Parameter(Mandatory)][string]$Results,[string]$Output='')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$source=(Resolve-Path -LiteralPath $Results).Path
if(-not $Output){$Output=Join-Path $source 'review'}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$font=[Drawing.Font]::new('Segoe UI',12)
$brush=[Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(225,233,245))
function Sheet($Name,$Width,$Height,$Items){
  $bitmap=[Drawing.Bitmap]::new($Width,$Height)
  $graphics=[Drawing.Graphics]::FromImage($bitmap)
  try {
    $graphics.Clear([Drawing.Color]::FromArgb(18,25,39))
    $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    foreach($item in $Items){
      $path=Join-Path $source ($item.file+'.png')
      if(-not (Test-Path -LiteralPath $path)){throw "Missing capture: $path"}
      $capture=[Drawing.Image]::FromFile($path)
      try {
        $graphics.DrawString($item.label,$font,$brush,[single]$item.x,[single]($item.y-24))
        $graphics.DrawImage($capture,[int]$item.x,[int]$item.y,[int]$item.width,[int]$item.height)
      } finally {$capture.Dispose()}
    }
    $bitmap.Save((Join-Path $Output ($Name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
  } finally {$graphics.Dispose();$bitmap.Dispose()}
}
try {
  $names=@('Clock','Weather','CPU','GPU','Memory','System','Network','Disk','Calendar')
  for($group=0;$group -lt 3;$group++){
    $items=@()
    for($row=0;$row -lt 3;$row++){
      $kind=$group*3+$row; $y=36+$row*380
      $items+=@{file="design-kind-$kind-size-0";label="$($names[$kind]) / S";x=16;y=$y;width=160;height=160}
      $items+=@{file="design-kind-$kind-size-1";label='Slim';x=196;y=$y;width=336;height=72}
      $items+=@{file="design-kind-$kind-size-2";label='M';x=196;y=($y+113);width=336;height=160}
      $items+=@{file="design-kind-$kind-size-3";label='L';x=552;y=$y;width=336;height=336}
    }
    Sheet "widgets-$group" 904 1156 $items
  }
  $items=@()
  for($i=0;$i -lt 9;$i++){$items+=@{file="design-theme-$i";label="Theme $i";x=(16+($i%3)*352);y=(36+[math]::Floor($i/3)*200);width=336;height=160}}
  Sheet 'themes' 1072 608 $items
  $states=@('Clear day','Cloudy','Rain','Snow','Clear night','Rain night','Unknown sky','Stale','Authentication error')
  $items=@()
  for($i=0;$i -lt 9;$i++){$items+=@{file="design-state-1-$i";label=$states[$i];x=(16+($i%3)*280);y=(36+[math]::Floor($i/3)*300);width=252;height=252}}
  Sheet 'weather-states' 840 908 $items
  $items=@()
  for($i=0;$i -lt 4;$i++){$items+=@{file="design-settings-$i";label="Settings tab $i";x=(16+($i%2)*860);y=(36+[math]::Floor($i/2)*800);width=840;height=740}}
  Sheet 'settings' 1728 1590 $items
  Sheet 'calendar-xl' 1032 900 @(@{file='design-kind-8-size-4';label='Calendar XL / 1008 x 840 DIP';x=12;y=36;width=1008;height=840})
  Sheet 'calendar-overflow' 1032 900 @(@{file='design-state-8-10';label='Calendar XL / long titles and overflow';x=12;y=36;width=1008;height=840})
  $items=@()
  for($kind=2;$kind -lt 8;$kind++){
    for($i=0;$i -lt 3;$i++){$items+=@{file="design-state-$kind-$($i+9)";label="$($names[$kind]) / $(@('Loading','Error','Long text')[$i])";x=(16+$i*280);y=(36+($kind-2)*300);width=252;height=252}}
  }
  Sheet 'device-states' 840 1808 $items
} finally {$font.Dispose();$brush.Dispose()}
Get-ChildItem -LiteralPath $Output -Filter *.png | Select-Object Name,Length
