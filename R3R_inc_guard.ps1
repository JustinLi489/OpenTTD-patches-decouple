# R3R incremental build guard  (PURE ASCII ONLY - PowerShell 5.1 reads this file as ANSI)
#
# Why this exists (KI-183, 2026-09-24):
#   In this tree ninja has NO usable MSVC header dependency info for a large part of
#   the object files (e.g. `ninja -t deps CMakeFiles/openttd_lib.dir/src/ground_vehicle.cpp.obj`
#   reports `#deps 0`). An incremental build after a class-layout change in src/*.h
#   therefore links object files compiled against DIFFERENT class layouts - a mixed-ABI
#   binary. That is exactly what crashed on loading TEST_T8701-2.sav with
#   `assert(weight != 0)` in Train::UpdateAcceleration (train_cmd.cpp:1314):
#   ground_vehicle.cpp.obj (old layout) wrote cached_weight at the old offset while
#   train_cmd.cpp.obj (new layout) read the new one.
#
# What it does:
#   Compares the newest src header / lang txt against the newest *.obj under build/.
#   If a header is newer, it deletes every *.obj (forcing a clean full rebuild) and
#   exits 2. Otherwise exits 0 and the incremental build is safe.
#
# Exit codes: 0 = incremental is safe, 2 = objects were dropped (full rebuild follows).

$ErrorActionPreference = 'Continue'

$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'
if (-not (Test-Path -LiteralPath $build)) {
    Write-Host 'GUARD: no build directory - nothing to check (full build anyway)'
    exit 0
}

$objs = @(Get-ChildItem -LiteralPath $build -Recurse -Filter '*.obj' -File -ErrorAction SilentlyContinue)
if ($objs.Count -eq 0) {
    Write-Host 'GUARD: no object files present - full build anyway'
    exit 0
}
$newestObj = ($objs | Measure-Object -Property LastWriteTime -Maximum).Maximum

# NOTE (2026-09-24, found on the first real run of this guard): -Include is NOT
# applied by PowerShell 5.1 for -LiteralPath + -Recurse, so
#   Get-ChildItem -LiteralPath <dir> -Recurse -Include '*.h'
# silently returned EVERY file below <dir>. A plain src\train_cmd.cpp was
# therefore reported as a "header/lang file", all 620 objects were deleted and
# an ordinary .cpp edit triggered a 30 minute full rebuild. The two file sets
# are filtered on the extension instead of relying on -Include.
$watch = @()
$srcDir = Join-Path $root 'src'
if (Test-Path -LiteralPath $srcDir) {
    $watch += @(Get-ChildItem -LiteralPath $srcDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -eq '.h' -or $_.Extension -eq '.hpp' })
}
$langDir = Join-Path $srcDir 'lang'
if (Test-Path -LiteralPath $langDir) {
    $watch += @(Get-ChildItem -LiteralPath $langDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -eq '.txt' })
}

$newer = @($watch | Where-Object { $_.LastWriteTime -gt $newestObj })
if ($newer.Count -eq 0) {
    Write-Host 'GUARD: incremental is safe (no header/lang file is newer than the newest object)'
    exit 0
}

Write-Host ("GUARD: {0} header/lang file(s) are newer than the newest object file" -f $newer.Count)
Write-Host ("GUARD: newest object = {0}" -f $newestObj.ToString('yyyy-MM-dd HH:mm:ss'))
foreach ($f in ($newer | Sort-Object LastWriteTime -Descending | Select-Object -First 8)) {
    $rel = $f.FullName
    if ($rel.StartsWith($root)) { $rel = $rel.Substring($root.Length).TrimStart('\') }
    Write-Host ("GUARD:   newer: {0}  ({1})" -f $rel, $f.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
}

$removed = 0
foreach ($o in $objs) {
    Remove-Item -LiteralPath $o.FullName -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path -LiteralPath $o.FullName)) { $removed++ }
}
Write-Host ("GUARD: REMOVED {0} object file(s) - upgrading this build to a FULL rebuild" -f $removed)
exit 2
