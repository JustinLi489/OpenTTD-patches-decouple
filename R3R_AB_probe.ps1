# R3R / JGRPP A-B probe helper.
# !! ASCII-ONLY FILE, DO NOT SAVE AS UTF-8, DO NOT ADD NON-ASCII TEXT !!
# Windows PowerShell 5.1 reads a BOM-less .ps1 as the ANSI codepage (GBK here);
# a UTF-8 encoded non-ASCII character then decodes into 2 wrong bytes and the
# quotes/braces go unbalanced -> "missing terminator" / "MissingTypename".
# All Chinese UI text lives in R3R_AB_probe.cmd (saved as GBK).
#
# Real log format (train_cmd.cpp R3RPerfDumpAndReset):
#   PERF frames=128 fps=5.2 frameMs avg=192.3 max=.. | ... | chains=911 vehs=42555 maxChain=126 segs=0 artic=28534 waitCouple=122
#   PERF-TICK loco=9354.7/s 316ms/s | coupleH=../s ..ms/s | plat=.. | resv=.. | edgeGate=.. | ctrl=1858.5/s 2030ms/s | coll=.. | spd=.. | vp=.. | dump=12ms/s | mov=92233.8/s
# so "key=RATE/s MSms/s" for the section timers, and plain "key=value" for the rest.
param(
  [string]$Mode   = 'parseR3R',
  [string]$Work   = 'D:\r3r_probe\AB',
  [string]$ExeDir = ''
)
$ErrorActionPreference = 'Continue'

# cmd.exe/CRT turns a trailing \" inside a quoted argument into an escaped quote,
# so  -ExeDir "D:\a\b\"  reaches PowerShell as  D:\a\b"  and Join-Path then fails
# with "Illegal characters in path". Strip stray quotes / trailing backslash.
function Clean-Path([string]$p) {
  if ([string]::IsNullOrEmpty($p)) { return '' }
  $p = $p.Trim()
  while ($p.Length -gt 3 -and ($p.EndsWith('"') -or $p.EndsWith('\') -or $p.EndsWith(' '))) {
    $p = $p.Substring(0, $p.Length - 1)
  }
  return $p
}
$Work   = Clean-Path $Work
$ExeDir = Clean-Path $ExeDir

# Files here are written by cmd.exe (GBK) on some days and by PowerShell (UTF-8) on
# others; decode strictly as UTF-8 and fall back to codepage 936 when that fails.
function Read-Lines([string]$p) {
  if (-not (Test-Path -LiteralPath $p)) { return @() }
  try { $bytes = [System.IO.File]::ReadAllBytes($p) } catch { return @() }
  if ($bytes.Length -eq 0) { return @() }
  $strict = New-Object System.Text.UTF8Encoding($false, $true)
  $text = $null
  try   { $text = $strict.GetString($bytes) }
  catch { $text = [System.Text.Encoding]::GetEncoding(936).GetString($bytes) }
  $text = $text.TrimStart([char]0xFEFF)
  return @($text -split "`r`n|`n")
}
function Out([string]$k, [string]$v) { Write-Output ("{0}={1}" -f $k, $v) }

if ($Work -ne '' -and -not (Test-Path -LiteralPath $Work)) { New-Item -ItemType Directory -Path $Work -Force | Out-Null }

# "key=12.3/s 45.6ms/s" -> ms value (and rate in h2['key_calls'])
function Parse-Pairs([string]$s, $h, $h2) {
  foreach ($m in [regex]::Matches($s, '([A-Za-z_][A-Za-z0-9_]*)=([0-9]+(?:\.[0-9]+)?)/s\s+([0-9]+(?:\.[0-9]+)?)ms/s')) {
    $h[$m.Groups[1].Value]  = $m.Groups[3].Value
    $h2[$m.Groups[1].Value] = $m.Groups[2].Value
  }
  # rate-only "key=12.3/s" (e.g. mov=92233.8/s, steps=../s, skipped=../s) -> $h2
  foreach ($m in [regex]::Matches($s, '([A-Za-z_][A-Za-z0-9_]*)=([0-9]+(?:\.[0-9]+)?)/s')) {
    if (-not $h2.ContainsKey($m.Groups[1].Value)) { $h2[$m.Groups[1].Value] = $m.Groups[2].Value }
  }
  # plain "key=value" (frames=128, avg=192.3, dump=12ms/s, maxChain=..) -> $h
  foreach ($m in [regex]::Matches($s, '([A-Za-z_][A-Za-z0-9_]*)=([0-9]+(?:\.[0-9]+)?)')) {
    if (-not $h.ContainsKey($m.Groups[1].Value)) { $h[$m.Groups[1].Value] = $m.Groups[2].Value }
  }
}

# ---------------------------------------------------------------- parseR3R
if ($Mode -eq 'parseR3R') {
  $log = Join-Path $ExeDir 'R3R_perf.log'
  Out 'r3r_perf_log' $log
  if (-not (Test-Path -LiteralPath $log)) { Out 'error' 'R3R_perf.log NOT FOUND (wrong game dir, or game never entered the game loop)'; exit 0 }
  $lines = @(Read-Lines $log)
  Out 'log_lines' $lines.Count

  # pair each PERF-TICK with the PERF line written just before it
  $pairs = New-Object System.Collections.Generic.List[object]
  $curPerf = ''
  foreach ($l in $lines) {
    if ($l -match '^PERF-TICK') { $pairs.Add([pscustomobject]@{ perf = $curPerf; tick = $l }) }
    elseif ($l -match '^PERF ') { $curPerf = $l }
  }
  Out 'perf_pairs' $pairs.Count

  # Preferred: the LAST window that is really running (fps>1 and loco ms>0.1).
  # Fallback: the window that looks most like running (loco first, then fps) so we
  # never emit a file that contains nothing but an error line.
  $sel = $null
  $best = $null
  $bestScore = -1.0
  foreach ($p in $pairs) {
    $a = @{}; $b = @{}; Parse-Pairs $p.perf $a $b
    $c = @{}; $d = @{}; Parse-Pairs $p.tick $c $d
    $fps  = if ($a.ContainsKey('fps'))  { [double]$a['fps'] }  else { 0 }
    $loco = if ($c.ContainsKey('loco')) { [double]$c['loco'] } else { 0 }
    $o = [pscustomobject]@{ perf = $a; tick = $c; tickCalls = $d; rawPerf = $p.perf; rawTick = $p.tick }
    if (($loco * 1000000.0 + $fps) -gt $bestScore) { $bestScore = ($loco * 1000000.0 + $fps); $best = $o }
    if ($fps -gt 1.0 -and $loco -gt 0.1) { $sel = $o }
  }

  $use = $sel
  $quality = 'running'
  if ($sel -eq $null) { $use = $best; $quality = 'not_running' }

  if ($use -eq $null) {
    Out 'error' 'R3R_perf.log has no PERF window at all (did the game reach the main loop?)'
    Out 'window_quality' 'unknown'
    Out 'log_state' 'unknown'
    exit 0
  }

  Out 'window_quality' $quality
  Out 'log_state' $quality
  if ($quality -eq 'not_running') {
    Out 'error' 'no RUNNING window (fps>1 and loco ms>0): the game was most likely paused while sampling. Let the trains run unpaused, read fps, then redo menu [1].'
  }

  Out 'raw_perf' $use.rawPerf
  Out 'raw_perf_tick' $use.rawTick
  $a = $use.perf; $c = $use.tick
  foreach ($k in @('fps','frames','avg','chains','vehs','maxChain','segs','artic','waitCouple','dbgEdge','skipped','foldCheck','dbgWrite','posHelper')) {
    if ($a.ContainsKey($k)) { Out "w_$k" $a[$k] }
  }
  $fps = if ($a.ContainsKey('fps')) { [double]$a['fps'] } else { 0 }
  if ($fps -le 0) {
    Out 'error2' 'selected window has fps<=0, cannot convert to ms/frame'
  } else {
    foreach ($k in @('loco','ctrl','coll','spd','vp','plat','resv','edgeGate','coupleH','dec','wpr','stk','rev','sub','resid','nl','dump')) {
      if ($c.ContainsKey($k)) {
        $ms = [double]$c[$k]
        Out "t_${k}_ms_per_s" $c[$k]
        Out "t_${k}_ms_per_frame" ('{0:F3}' -f ($ms / $fps))
      }
    }
    if ($use.tickCalls.ContainsKey('loco')) { Out 't_loco_calls_per_frame' ('{0:F1}' -f ([double]$use.tickCalls['loco'] / $fps)) }
    if ($use.tickCalls.ContainsKey('mov'))  { Out 't_mov_iters_per_frame'  ('{0:F1}' -f ([double]$use.tickCalls['mov']  / $fps)) }
    if ($use.tickCalls.ContainsKey('ctrl') -and $use.tickCalls.ContainsKey('mov')) {
      $cc = [double]$use.tickCalls['ctrl']
      if ($cc -gt 0) { Out 't_mov_per_ctrl_call' ('{0:F1}' -f ([double]$use.tickCalls['mov'] / $cc)) }
    }
    if ($a.ContainsKey('chains') -and [double]$a['chains'] -gt 0 -and $c.ContainsKey('loco')) {
      Out 't_loco_ms_per_chain_per_frame' ('{0:F2}' -f (([double]$c['loco'] / $fps) / [double]$a['chains']))
    }
  }
  exit 0
}

# ---------------------------------------------------------------- report
if ($Mode -eq 'report') {
  function Load-Data([string]$p) {
    $h = @{}
    foreach ($l in (Read-Lines $p)) { if ($l -match '^([^=]+)=(.*)$') { $h[$Matches[1]] = $Matches[2] } }
    return $h
  }
  $r = Load-Data (Join-Path $Work 'data_R3R.txt')
  $g = Load-Data (Join-Path $Work 'data_JGRPP.txt')
  # Manual copy of the R3R in-game framerate window: same instrument as vanilla,
  # so it is the reference for the "state" comparison when the auto-parsed log is
  # stale (e.g. the last [1] run happened while the game was paused).
  $rw = Load-Data (Join-Path $Work 'data_R3R_win.txt')
  $o = New-Object System.Collections.Generic.List[string]
  if ($r.Count -eq 0) { $o.Add('!! data_R3R.txt is MISSING/empty -> run menu [1] first') }
  if ($g.Count -eq 0) { $o.Add('!! data_JGRPP.txt is MISSING/empty -> run menu [2] first') }
  $o.Add('=== R3R  vs  unmodified JGRPP 0.72.4 : A/B report ===')
  $o.Add('save        : ' + $g['save'])
  $o.Add('R3R exe     : ' + $r['exe'])
  $o.Add('vanilla exe : ' + $g['exe'])
  $o.Add('vanilla loadable (y/n) : ' + $g['loadable'])
  $rstate = $r['state']
  if ([string]::IsNullOrEmpty($rstate)) { $rstate = $rw['state'] }
  $o.Add('state       : R3R=' + $rstate + '  vanilla=' + $g['state'])
  if ($rstate -ne $g['state'] -or $g['state'] -eq '') { $o.Add('  !! STATE MISMATCH -> timings not comparable, redo one side') }
  $o.Add('')
  $o.Add('--- R3R (auto, R3R_perf.log, chosen window) ---')
  if ($r['window_quality'] -eq 'not_running') {
    $o.Add('  !! the R3R log had NO running window (game was paused while sampling)')
    $o.Add('     -> the numbers below are for reference only, NOT comparable')
  }
  $o.Add('  window_quality=' + $r['window_quality'] + '  log_state=' + $r['log_state'])
  $o.Add('  frames/window=' + $r['w_frames'] + '  fps=' + $r['w_fps'] + '  (frameMs avg=' + $r['w_avg'] + ')')
  $o.Add('  chains=' + $r['w_chains'] + '  vehs=' + $r['w_vehs'] + '  maxChain=' + $r['w_maxChain'] + '  artic=' + $r['w_artic'] + '  segs=' + $r['w_segs'])
  $o.Add('  loco        = ' + $r['t_loco_ms_per_frame'] + ' ms/frame  (' + $r['t_loco_ms_per_s'] + ' ms/s)')
  $o.Add('    ctrl      = ' + $r['t_ctrl_ms_per_frame'] + ' ms/frame   [native TrainController]')
  $o.Add('    spd       = ' + $r['t_spd_ms_per_frame']  + ' ms/frame   [native consist-wide speed]')
  $o.Add('    vp        = ' + $r['t_vp_ms_per_frame']   + ' ms/frame   [native UpdateViewport]')
  $o.Add('    coll      = ' + $r['t_coll_ms_per_frame'] + ' ms/frame')
  $o.Add('    plat+resv = ' + $r['t_plat_ms_per_frame'] + ' + ' + $r['t_resv_ms_per_frame'] + ' ms/frame  [R3R additions]')
  $o.Add('    edgeGate  = ' + $r['t_edgeGate_ms_per_frame'] + ' ms/frame  [R3R additions]')
  $o.Add('    coupleH   = ' + $r['t_coupleH_ms_per_frame'] + ' ms/frame  [R3R additions]')
  $o.Add('    dec/wpr/stk/rev = ' + $r['t_dec_ms_per_frame'] + ' / ' + $r['t_wpr_ms_per_frame'] + ' / ' + $r['t_stk_ms_per_frame'] + ' / ' + $r['t_rev_ms_per_frame'] + ' ms/frame  [R3R additions]')
  $o.Add('    sub = ' + $r['t_sub_ms_per_frame'] + ' ms/frame  resid = ' + $r['t_resid_ms_per_frame'] + ' ms/frame  (unexplained)')
  $o.Add('    nl  = ' + $r['t_nl_ms_per_frame'] + ' ms/frame  [PFE_GL_TRAINS minus loco: not the handler]')
  $o.Add('  calls/frame: loco=' + $r['t_loco_calls_per_frame'] + '  mov_iters=' + $r['t_mov_iters_per_frame'] + '  mov/ctrl=' + $r['t_mov_per_ctrl_call'])
  $o.Add('  loco per chain per frame = ' + $r['t_loco_ms_per_chain_per_frame'] + ' ms')
  $o.Add('')
  $o.Add('--- vanilla JGRPP (manual, in-game "framerate" window) ---')
  $o.Add('  fps=' + $g['fps'])
  $o.Add('  GL train ticks = ' + $g['GLtrain1'] + ' / ' + $g['GLtrain2'] + ' / ' + $g['GLtrain3'] + ' ms')
  $o.Add('  train primary(chain heads)=' + $g['train_primary'] + '  secondary=' + $g['train_secondary'] + '  totalvehicles=' + $g['totalvehicles'])
  $o.Add('  note: ' + $g['note'])
  $o.Add('')
  $o.Add('--- framerate window A/B (IDENTICAL in-game instrument on both builds) ---')
  $o.Add('  Both builds ship the same PerfMeasurer rows, so this table needs no')
  $o.Add('  cross-instrument assumptions: the "TRAINS" row is PFE_GL_TRAINS in both.')
  if ($rw.Count -eq 0) {
    $o.Add('  !! data_R3R_win.txt missing -> run R3R, open the in-game framerate window,')
    $o.Add('     copy its numbers into that file (see header of data_JGRPP.txt), press [3].')
  }
  $o.Add('')
  $o.Add('  row                      vanilla        R3R     R3R/vanilla')
  $rows = @(
    @('fps (render)',        'fps'),
    @('fps (simulation)',    'simfps'),
    @('game speed',          'gamespeed'),
    @('game loop total cur', 'loop_cur'),
    @('game loop total avg', 'loop_avg'),
    @('TRAINS cur  <- key',  'trains_cur'),
    @('TRAINS avg',          'trains_avg'),
    @('cargo cur',           'cargo_cur'),
    @('world cur',           'world_cur'),
    @('graphics cur',        'gfx_cur'),
    @('viewport cur',        'vp_cur'),
    @('video out cur',       'video_cur')
  )
  $ratioTxt = ''
  foreach ($row in $rows) {
    $lbl = $row[0]; $k = $row[1]
    $sa = [string]$g[$k]; $sb = [string]$rw[$k]
    if ([string]::IsNullOrEmpty($sa)) { $sa = '-' }
    if ([string]::IsNullOrEmpty($sb)) { $sb = '-' }
    $ratio = ''
    $da = 0.0; $db = 0.0
    if ([double]::TryParse($sa, [ref]$da) -and [double]::TryParse($sb, [ref]$db) -and $da -gt 0) {
      $ratio = '{0:F2}x' -f ($db / $da)
      if ($k -eq 'trains_cur') { $ratioTxt = $ratio }
    }
    $o.Add('  ' + $lbl.PadRight(23) + ' ' + $sa.PadLeft(10) + ' ' + $sb.PadLeft(10) + ' ' + $ratio.PadLeft(13))
  }
  $o.Add('')
  if ($ratioTxt -ne '') {
    $o.Add('  >>> VERDICT: the in-game TRAINS row went from ' + $g['trains_cur'] + ' ms to ' + $rw['trains_cur'] + ' ms  =  ' + $ratioTxt)
    $o.Add('      Same counter, same save, same instrument -> this single ratio is the')
    $o.Add('      cleanest measurement of the R3R regression. Read the branches below.')
  } else {
    $o.Add('  >>> VERDICT: fill trains_cur on BOTH sides to get the ratio.')
  }
  $o.Add('')
  $o.Add('=== how to read it ===')
  $o.Add('  (1) PRIMARY: the TRAINS row of the framerate window exists in BOTH builds,')
  $o.Add('      same counter, same save -> no cross-instrument assumption needed.')
  $o.Add('        ratio ~1x   -> no regression in the train tick, look elsewhere.')
  $o.Add('        ratio >>1x  -> the regression IS inside the train tick path, go to (3).')
  $o.Add('  (2) guard: a much lower R3R fps usually means a reduced game speed')
  $o.Add('      (R3R reported 0.20x), i.e. FEWER ticks per second. So a ratio >>1x is')
  $o.Add('      a lower bound on the per-tick regression, not an overstatement.')
  $o.Add('  (3) locate it with the R3R counters:')
  $o.Add('      A) R3R loco ms/frame accounts for nearly the whole TRAINS row')
  $o.Add('         => the cost is inside TrainLocoHandler, not cargo/motion.')
  $o.Add('      C) loco is only part of the row')
  $o.Add('         => part of the cost sits in the per-vehicle loop (cargo aging/motion).')
  $o.Add('      In both cases check ctrl vs spd vs vp: ctrl is the NATIVE TrainController,')
  $o.Add('      so if ctrl dominates, R3R made an untouched native path slower.')
  $o.Add('  (4) chain-count guard: vanilla train_primary should equal R3R chains;')
  $o.Add('      if it does, both builds load the SAME chain structure from the same file.')
  $o.Add('')
  $txt = ($o -join "`r`n")
  # Save as GBK(936): the cmd window runs "chcp 936" and then "type"s this file,
  # so a UTF-8 file would show every Chinese path as mojibake.
  $outFile = Join-Path $Work 'report.txt'
  [System.IO.File]::WriteAllText($outFile, $txt, [System.Text.Encoding]::GetEncoding(936))
  exit 0
}

Write-Output ('unknown mode: ' + $Mode)
exit 1
