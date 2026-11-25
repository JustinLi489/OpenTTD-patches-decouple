$log = 'd:\sourcecode of JGRPP\build\R3R_debug.log'
'---EXE---'
Get-Item 'd:\sourcecode of JGRPP\build\openttd.exe' | Select-Object Length,LastWriteTime | Format-List
'---SNAP---'
Select-String -Path $log -Pattern 'CPL-SNAP' -SimpleMatch | Select-Object -First 8 -ExpandProperty Line
'---MEETMISS---'
Select-String -Path $log -Pattern 'PFD-MEETMISS' -SimpleMatch | Select-Object -First 40 -ExpandProperty Line
'---MEET---'
Select-String -Path $log -Pattern 'PFD-MEET ' -SimpleMatch | Select-Object -First 8 -ExpandProperty Line
'---SCAN---'
Select-String -Path $log -Pattern 'CPL-SCAN' -SimpleMatch | Select-Object -Last 4 -ExpandProperty Line
'---PATHFOUND---'
Select-String -Path $log -Pattern 'CPL-PATHFOUND' -SimpleMatch | Select-Object -Last 4 -ExpandProperty Line
'---BEST---'
Select-String -Path $log -Pattern 'CPL-BEST' -SimpleMatch | Select-Object -Last 4 -ExpandProperty Line
'---HEADSTATIC---'
Select-String -Path $log -Pattern 'CPL-HEAD-STATIC' -SimpleMatch | Select-Object -Last 4 -ExpandProperty Line
