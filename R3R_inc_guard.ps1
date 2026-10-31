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
#   EXCEPTION (2026-10-27): headers listed in $smallClosure are included by a single
#   translation unit; for those only the named objects are deleted and the run stays
#   incremental. Debug probes in such a header therefore cost one object, not 620.
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

# --- small-closure headers (2026-10-27) -------------------------------------------
# Some headers are included by exactly ONE translation unit, so a one-line probe in
# them does not need a full rebuild - only that single object has to be recompiled.
# Everything below still assumes "header newer than object => mixed ABI => drop all",
# which is right for widely included headers (train.h, vehicle_base.h, ...).
# MAINTENANCE RULE: only add a header here after verifying its include closure, e.g.
#   rg -n 'include "yapf_destrail.hpp"' src
# If a second .cpp starts including a header listed here, extend the value list in the
# same commit - otherwise that object is linked against the old header (mixed ABI).
$smallClosure = @{
    'src\pathfinder\yapf\yapf_destrail.hpp' = @('yapf_rail.cpp.obj')
}

$remaining = @()
foreach ($f in $newer) {
    $rel = $f.FullName
    if ($rel.StartsWith($root)) { $rel = $rel.Substring($root.Length).TrimStart('\') }
    if ($smallClosure.ContainsKey($rel)) {
        $targets = $smallClosure[$rel]
        $dropped = 0
        foreach ($t in $targets) {
            foreach ($o in @($objs | Where-Object { $_.Name -eq $t })) {
                Remove-Item -LiteralPath $o.FullName -Force -ErrorAction SilentlyContinue
                if (-not (Test-Path -LiteralPath $o.FullName)) { $dropped++ }
            }
        }
        Write-Host ("GUARD: small-closure header {0} -> dropped {1} of {2} object(s); the rest stay valid" -f $rel, $dropped, $targets.Count)
    } else {
        $remaining += $f
    }
}

if ($remaining.Count -eq 0) {
    Write-Host 'GUARD: incremental is safe (only small-closure headers changed)'
    exit 0
}

Write-Host ("GUARD: {0} header/lang file(s) are newer than the newest object file" -f $remaining.Count)
Write-Host ("GUARD: newest object = {0}" -f $newestObj.ToString('yyyy-MM-dd HH:mm:ss'))
foreach ($f in ($remaining | Sort-Object LastWriteTime -Descending | Select-Object -First 8)) {
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
