# R3R_shot_ocr.ps1 - OCR the newest screenshots with the built-in Windows OCR engine.
#
# Why: this assistant has no vision, so a screenshot pasted by the user cannot be
# read with a normal file-read tool. Windows ships Windows.Media.Ocr, which can be
# driven from Windows PowerShell 5.1 through the WinRT async bridge used below.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File R3R_shot_ocr.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File R3R_shot_ocr.ps1 -Count 4
#
# Writes UTF-8 (with BOM) to R3R_shot_ocr_out.txt next to this script and also
# prints it. CAVEAT: small digits are misread fairly often (e.g. 9.34 came back as
# "9-guard-4"), so use the result to understand WHAT a window says, never as a
# source of numeric measurements. Manual transcription is still required for the
# A/B probe.
#
# ASCII-ONLY FILE: PowerShell 5.1 reads a BOM-less .ps1 as the ANSI codepage, so
# any non-ASCII character here would decode into garbage and break the parser.
param([int]$Count = 2)

$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Runtime.WindowsRuntime

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
  $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]

function Await($op, $type) {
  $asTask = $asTaskGeneric.MakeGenericMethod($type)
  $netTask = $asTask.Invoke($null, @($op))
  $netTask.Wait(-1) | Out-Null
  $netTask.Result
}

$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime]
$null = [Windows.Graphics.Imaging.SoftwareBitmap, Windows.Graphics.Imaging, ContentType = WindowsRuntime]
$null = [Windows.Media.Ocr.OcrEngine, Windows.Media.Ocr, ContentType = WindowsRuntime]
$null = [Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime]

$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
if ($null -eq $engine) {
  foreach ($tag in @('zh-Hans-CN', 'zh-CN', 'en-US')) {
    try {
      $lang = New-Object Windows.Globalization.Language $tag
      $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang)
    } catch { }
    if ($null -ne $engine) { Write-Output ('OCR-ENGINE-FALLBACK ' + $tag); break }
  }
}
if ($null -eq $engine) { Write-Output 'OCR-ENGINE-UNAVAILABLE (no OCR language pack installed)'; exit 2 }
Write-Output ('OCR-ENGINE ' + $engine.RecognizerLanguage.LanguageTag)

$shots = Join-Path ([Environment]::GetFolderPath('MyPictures')) 'Screenshots'
if (-not (Test-Path -LiteralPath $shots)) { Write-Output ('no Screenshots folder: ' + $shots); exit 1 }

$files = @(Get-ChildItem -LiteralPath $shots -File | Sort-Object LastWriteTime -Descending | Select-Object -First $Count)
$out = New-Object System.Collections.Generic.List[string]

foreach ($f in $files) {
  $out.Add('========== ' + $f.Name + '  (' + $f.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') + ') ==========')
  try {
    $sf = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($f.FullName)) ([Windows.Storage.StorageFile])
    $stream = Await ($sf.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $bmp = Await ($decoder.GetSoftwareBitmapAsync([Windows.Graphics.Imaging.BitmapPixelFormat]::Bgra8, [Windows.Graphics.Imaging.BitmapAlphaMode]::Premultiplied)) ([Windows.Graphics.Imaging.SoftwareBitmap])
    $res = Await ($engine.RecognizeAsync($bmp)) ([Windows.Media.Ocr.OcrResult])
    $i = 0
    # NB: the extra parentheses are required - inside a method-call argument list a
    # top-level comma splits arguments, so '-f $i, $line.Text' would be misparsed.
    foreach ($line in $res.Lines) { $i++; $out.Add(('[{0:D2}] {1}' -f $i, $line.Text)) }
    if ($res.Lines.Count -eq 0) { $out.Add('(no text recognised)') }
  } catch {
    $out.Add('OCR-ERROR: ' + $_.Exception.Message)
  }
  $out.Add('')
}

$target = Join-Path $PSScriptRoot 'R3R_shot_ocr_out.txt'
$text = ($out -join "`r`n")
[System.IO.File]::WriteAllText($target, $text, (New-Object System.Text.UTF8Encoding($true)))
Write-Output $text
Write-Output ('WROTE ' + $target)
