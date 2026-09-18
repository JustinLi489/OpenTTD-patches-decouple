# R3R_fix_gbk.ps1 - re-save a text file as GBK (codepage 936) without BOM.
#
# Why this exists: cmd.exe cannot reliably run a *UTF-8* .cmd that contains
# multi-byte characters (its parser miscounts byte offsets and starts executing
# garbage from the middle of a line -> "'xx' is not recognized as an internal or
# external command"). Any editor that silently saves as UTF-8 breaks the file.
# Run this after editing R3R_AB_probe.cmd / R3R_check.cmd / *.txt language files.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File R3R_fix_gbk.ps1 <file> [<file> ...]
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Files)
$ErrorActionPreference = 'Stop'

if (-not $Files -or $Files.Count -eq 0) {
  Write-Output 'usage: R3R_fix_gbk.ps1 <file> [<file> ...]'
  exit 1
}

$strict = New-Object System.Text.UTF8Encoding($false, $true)
$gbk = [System.Text.Encoding]::GetEncoding(936)

# Print where the non-ASCII bytes are, so a surprise corruption can be identified
# instead of silently "fixed" (e.g. a UTF-8 BOM, or Chinese text pasted into a file
# that is supposed to stay pure ASCII).
function Show-NonAscii($bytes, $encoding) {
  $idx = @()
  for ($i = 0; $i -lt $bytes.Length; $i++) { if ($bytes[$i] -gt 127) { $idx += $i; if ($idx.Count -ge 8) { break } } }
  Write-Output ("              nonAsciiBytes=" + $idx.Count + " firstOffsets=" + ($idx -join ','))
  foreach ($i in $idx) {
    $end = [Math]::Min($i + 12, $bytes.Length - 1)
    $slice = $bytes[$i..$end]
    Write-Output ("              @" + $i + " [" + (($slice | ForEach-Object { '{0:X2}' -f $_ }) -join ' ') + "] asGbk='" + ($encoding.GetString($slice)) + "'")
  }
}

foreach ($f in $Files) {
  if (-not (Test-Path -LiteralPath $f)) { Write-Output ("MISSING  " + $f); continue }
  $b = [System.IO.File]::ReadAllBytes($f)
  if ($b.Length -eq 0) { Write-Output ("EMPTY    " + $f); continue }

  $hasBom = ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)
  $nonAscii = $false
  foreach ($x in $b) { if ($x -gt 127) { $nonAscii = $true; break } }

  if (-not $nonAscii) {
    if ($hasBom) {
      [System.IO.File]::WriteAllBytes($f, $b[3..($b.Length - 1)])
      Write-Output ("STRIPPED-BOM  " + $f)
    } else {
      Write-Output ("ASCII-OK      " + $f)
    }
    continue
  }

  $text = $null
  try { $text = $strict.GetString($b) } catch { $text = $null }
  if ($text -eq $null) {
    # not valid UTF-8 -> assume it is already GBK
    Write-Output ("GBK-ALREADY   " + $f)
    Show-NonAscii $b $gbk
    continue
  }

  $text = $text.TrimStart([char]0xFEFF)
  [System.IO.File]::WriteAllBytes($f, $gbk.GetBytes($text))
  Write-Output ("CONVERTED->GBK " + $f)
}
exit 0
