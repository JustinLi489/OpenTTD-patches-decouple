# R3R release full-build helper.
# ASCII ONLY on purpose: PowerShell 5.1 reads a BOM-less .ps1 as ANSI(936), so any
# non-ASCII byte here would corrupt the script (KI-16 / R3R_AB_probe lessons).
# Called by R3R_release_fullbuild.cmd so that no inline -Command one-liner is
# needed (those break on quoting depending on the outer shell).
param(
    [string]$Action = 'precheck',
    [string]$Ninja = '',
    [string]$BuildDir = '',
    [int]$Jobs = 2,
    [string]$Log = '',
    [string]$Exe = ''
)

$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::GetEncoding(936) } catch { }

switch ($Action.ToLowerInvariant()) {

    'precheck' {
        $os = Get-CimInstance Win32_OperatingSystem
        Write-Host ("  Total RAM      : " + [math]::Round($os.TotalVisibleMemorySize / 1MB, 1) + " GB")
        $freeGB = [math]::Round($os.FreePhysicalMemory / 1MB, 2)
        Write-Host ("  Free RAM (now) : " + $freeGB + " GB")
        if ($freeGB -lt 1.2) {
            Write-Host "  [WARN] under 1.2 GB free: cl.exe gets paged out and the build freezes"
            Write-Host "  [WARN] (KI-24). Close CodeBuddy and start again."
        } else {
            Write-Host "  OK: enough RAM"
        }
        $p = @(Get-Process -Name 'openttd' -ErrorAction SilentlyContinue)
        if ($p.Count -gt 0) {
            Write-Host ("  [FATAL] openttd.exe is RUNNING (pid " + (($p | ForEach-Object { $_.Id }) -join ',') + ")")
            Write-Host "  [FATAL] the linker cannot overwrite a running exe (LNK1168)."
            Write-Host "  [FATAL] close the game and start this script again."
            exit 96
        }
        Write-Host "  OK: openttd.exe not running, LNK1168 impossible"
        exit 0
    }

    'build' {
        if (-not (Test-Path -LiteralPath $Ninja))    { Write-Host ("  [FATAL] ninja not found: " + $Ninja); exit 97 }
        if (-not (Test-Path -LiteralPath $BuildDir)) { Write-Host ("  [FATAL] build dir not found: " + $BuildDir); exit 97 }
        Write-Host ("  ninja     : " + $Ninja)
        Write-Host ("  build dir : " + $BuildDir)
        Write-Host ("  jobs      : " + $Jobs)
        Write-Host ("  live log  : " + $Log)
        Write-Host ""
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        & $Ninja -C $BuildDir -j $Jobs openttd 2>&1 | Tee-Object -FilePath $Log
        $rc = $LASTEXITCODE
        $sw.Stop()
        Write-Host ""
        Write-Host ("  ninja exit code : " + $rc)
        Write-Host ("  elapsed         : " + [int]$sw.Elapsed.TotalMinutes + " min " + $sw.Elapsed.Seconds + " s")
        Write-Host ("  full log        : " + $Log)
        exit $rc
    }

    'verify' {
        if (-not (Test-Path -LiteralPath $Exe)) {
            Write-Host "  [FAIL] openttd.exe was NOT produced."
            exit 1
        }
        $e = Get-Item -LiteralPath $Exe
        Write-Host ("  openttd.exe : " + $e.Length + " bytes   " + $e.LastWriteTime)

        # Freshness gate: the link always writes the exe at the very end of this
        # run, so a fresh exe is seconds/minutes old.  An old timestamp means the
        # link never happened (e.g. LNK1168 because the game was still running)
        # and the "verify" below would otherwise happily bless a stale binary.
        $age = (Get-Date) - $e.LastWriteTime
        if ($age.TotalMinutes -gt 10) {
            Write-Host ("  [FAIL] this exe is " + [int]$age.TotalMinutes + " min old - it was NOT relinked in this run.")
            Write-Host "  [FAIL] the compile or the link did not produce a new binary."
            exit 3
        }
        # Size gate.  The CRT name scan is useless here: both build dirs link the
        # CRT statically (triplet x64-windows-static), so "ucrtbased.dll" never
        # appears even in the Debug exe (measured).  Size is the reliable signal
        # on this project: Debug exe ~50 MB, RelWithDebInfo release exe ~22 MB.
        Write-Host ("  size        : " + [math]::Round($e.Length / 1MB, 1) + " MB   (release ~22 MB, debug ~50 MB)")
        if ($e.Length -gt 35MB) {
            Write-Host "  [BAD] this exe is far too big - it looks like a DEBUG build."
            Write-Host "  [BAD] the release optimisation flags did not apply."
            exit 2
        }
        Write-Host "  OK: size is in the release range"
        exit 0
    }

    default {
        Write-Host ("  [FATAL] unknown action '" + $Action + "' (use precheck|build|verify)")
        exit 98
    }
}
