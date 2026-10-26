# Imports and analyzes the ten Rev.1 overlay programs (work/ghidra/jp_rev1_*.exe), rebuilds
# each overlay's own range with RefunctionRange.java and exports annotations, COP2 sites and C.
# -Reprocess: programs already imported; only rebuild the ranges and re-export.
param([switch]$Reprocess)
$ws = if ($env:T3_WORKSPACE) { $env:T3_WORKSPACE } else { (Resolve-Path "$PSScriptRoot\..\..").Path }
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:TMP = "$ws\_tools\tmp"; $env:TEMP = $env:TMP
$w = "$ws\Tekken3Remake\work"
$t = "$ws\Tekken3Remake\tools\ghidra"
$ah = "$ws\_tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat"
$log = "$ws\_tools\ghidra_overlays.log"
$mode = @("arcade", "practice", "force", "volley")
$seeds = @{}
foreach ($line in Get-Content "$t\overlay_seeds.txt") {
    if ($line -match '^\s*#' -or $line -notmatch '\S') { continue }
    $p = $line -split '\s+'; $seeds[$p[0]] = $p[1]
}
"" | Set-Content $log
foreach ($f in Get-ChildItem "$w\ghidra\jp_rev1_*.exe") {
    $name = $f.BaseName -replace '^jp_rev1_', ''
    # L suffix: plain 0x8xxxxxxx literals are negative Int32 in PowerShell.
    $base = if ($mode -contains $name) { 0x800B0A10L } else { 0x800B9378L }
    $end = 0x80010000L + $f.Length - 0x800
    $range = @(('0x{0:X8}' -f $base), ('0x{0:X8}' -f $end), "$w\decomp")
    # Quoted: analyzeHeadless.bat (cmd) splits unquoted arguments at commas.
    if ($seeds.ContainsKey($name)) { $range += '"' + $seeds[$name] + '"' }
    "=== $name $range" | Add-Content $log
    if ($Reprocess) {
        & $ah "$w\ghidra\proj" T3 -process $f.Name -noanalysis -scriptPath $t `
            -postScript RefunctionRange.java @range `
            -postScript ExportCop2Sites.java "$w\decomp" *>> $log
    } else {
        & $ah "$w\ghidra\proj" T3 -import $f.FullName -overwrite -scriptPath $t `
            -postScript ApplyAnnotations.java "$t\tekken3.h" "$t\symbols.txt" `
            -postScript RefunctionRange.java @range `
            -postScript ExportCop2Sites.java "$w\decomp" *>> $log
    }
    "exit $LASTEXITCODE" | Add-Content $log
}
"DONE" | Add-Content $log
