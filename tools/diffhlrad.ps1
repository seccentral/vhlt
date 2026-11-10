<#
.SYNOPSIS
    Differential harness: hlrad vs hlrad-ng must write byte-identical output.

.DESCRIPTION
    Implements Phase 0 of docs/research/hlrad-optimizations.md.

    For every .map in -MapDir the script compiles the map once with the
    standard tools (hlcsg, hlbsp, hlvis, -threads 1 for determinism), copies
    the compiled map to one work directory per tool, and runs hlrad in one
    directory and hlrad-ng in the other with identical arguments. The full
    .bsp bytes, the optional .dlit/.inc files and the exported entity text
    (ripent -export, compiler key dropped) are compared. This repeats for
    every thread count in -ThreadCounts, always comparing the pair at the
    same count. On a differing byte the first differing offset and the owning
    BSP lump are reported.

    With -Determinism each tool also runs twice per thread count and the two
    runs of the same tool are compared, so a thread count where hlrad itself
    is not run-to-run stable is caught instead of being masked by parity.

    With -UpdateBaseline this run's ng-side hashes become <OutDir>/baseline.tsv;
    later runs fail when the ng hashes drift.

.PARAMETER MapDir
    Directory containing the .map files to test. Required.

.PARAMETER LightsRad
    Optional lights.rad file, copied next to each map and passed with -lights.

.PARAMETER WadDir
    Optional directory with the WADs the maps reference. The worldspawn "wad"
    key is rewritten to this directory (base names only), the same way
    tools/regress.ps1 does it.

.PARAMETER OutDir
    Output root. Defaults to build/diffhlrad. Runs land in
    <OutDir>/<RunName>, the reference hashes in <OutDir>/baseline.tsv.

.PARAMETER RunName
    Name of this run. Defaults to the current date and time.

.PARAMETER RadArgs
    Arguments for both hlrad and hlrad-ng, as one space-separated string.
    Defaults to "-extra". The thread count is added by the script.

.PARAMETER ThreadCounts
    Thread counts to test, comma- or space-separated. Defaults to
    "1,2,4,8,16". hlrad and hlrad-ng are always compared at the same count.

.PARAMETER Determinism
    Run each tool twice per thread count and fail on run-to-run differences.

.PARAMETER UpdateBaseline
    Write this run's ng-side hashes to <OutDir>/baseline.tsv instead of
    comparing.

.PARAMETER Jobs
    Number of maps to test at the same time. Defaults to 1.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/diffhlrad.ps1 `
        -MapDir "C:\Program Files\J.A.C.K" `
        -WadDir "G:\SteamLibrary\steamapps\common\Half-Life\valve" `
        -ThreadCounts "1,2,4,8,16"

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/diffhlrad.ps1 `
        -MapDir "C:\Program Files\J.A.C.K" `
        -WadDir "G:\SteamLibrary\steamapps\common\Half-Life\valve" `
        -RadArgs "-extra -bounce 8" -Determinism -UpdateBaseline
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MapDir,

    [string]$LightsRad,

    [string]$WadDir,

    [string]$OutDir = '',

    [string]$RunName = (Get-Date -Format 'yyyyMMdd-HHmmss'),

    [string]$RadArgs = '-extra',

    [string]$ThreadCounts = '1,2,4,8,16',

    [switch]$Determinism,

    [switch]$UpdateBaseline,

    [int]$Jobs = 1
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$binDir = Join-Path $repoRoot 'build\windows\bin'
if (-not $OutDir) {
    $OutDir = Join-Path $PSScriptRoot '..\build\diffhlrad'
}
$mapDirPath = (Resolve-Path -LiteralPath $MapDir).Path
$outDirPath = [IO.Path]::GetFullPath($OutDir)
$runDir = Join-Path $outDirPath $RunName

foreach ($tool in 'hlcsg', 'hlbsp', 'hlvis', 'hlrad', 'hlrad-ng', 'ripent') {
    $exe = Join-Path $binDir "$tool.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Missing $exe. Configure and build the windows preset with -DVHLT_HLRAD_NG=ON first."
    }
}

$maps = @(Get-ChildItem -LiteralPath $mapDirPath -Filter '*.map' -File | Sort-Object Name)
if ($maps.Count -eq 0) {
    throw "No .map files found in $mapDirPath"
}

if ($LightsRad) {
    $LightsRad = (Resolve-Path -LiteralPath $LightsRad).Path
}

if ($WadDir) {
    $WadDir = (Resolve-Path -LiteralPath $WadDir).Path
}

$radArgList = @($RadArgs -split '\s+' | Where-Object { $_ })
$threadCountsArg = @()
foreach ($token in ($ThreadCounts -split '[,\s]+' | Where-Object { $_ })) {
    $value = 0
    if (-not [int]::TryParse($token, [ref]$value)) {
        throw "Invalid thread count '$token' in -ThreadCounts"
    }
    $threadCountsArg += $value
}
$threadCountsArg = @($threadCountsArg | Sort-Object -Unique)
if ($threadCountsArg.Count -eq 0) {
    throw "ThreadCounts must contain at least one value"
}

New-Item -ItemType Directory -Force -Path $runDir | Out-Null

# Each job owns one map and its work directories, so jobs never share files.
$worker = {
    param($MapPath, $RunDir, $BinDir, $LightsRadPath, $WadDirPath, $RadArgList, $ThreadCountsArg, $DeterminismFlag)

    $ErrorActionPreference = 'Stop'
    $map = [IO.Path]::GetFileNameWithoutExtension($MapPath)
    $workDir = Join-Path $RunDir (Join-Path 'work' $map)
    $stageDir = Join-Path $workDir 'stage'
    New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
    $mapFile = Join-Path $stageDir "$map.map"
    Copy-Item -LiteralPath $MapPath -Destination $mapFile -Force
    if ($LightsRadPath) {
        Copy-Item -LiteralPath $LightsRadPath -Destination (Join-Path $stageDir 'lights.rad') -Force
    }
    if ($WadDirPath) {
        $latin1 = [Text.Encoding]::GetEncoding(28591)
        $text = [IO.File]::ReadAllText($mapFile, $latin1)
        $text = [regex]::Replace($text, '(?m)^"wad"\s+"([^"]*)"', {
            param($match)
            $resolved = foreach ($entry in ($match.Groups[1].Value -split ';')) {
                $name = [IO.Path]::GetFileName($entry)
                if ($name) {
                    Join-Path $WadDirPath $name
                }
            }
            '"wad" "' + ($resolved -join ';') + '"'
        })
        [IO.File]::WriteAllText($mapFile, $text, $latin1)
    }

    function Get-HexHash([byte[]]$Bytes) {
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            return (($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString('x2') }) -join '')
        }
        finally {
            $sha.Dispose()
        }
    }

    function Get-FileHexHash([string]$Path) {
        return Get-HexHash ([IO.File]::ReadAllBytes($Path))
    }

    function Get-NormalizedBspHash([string]$Path) {
        $latin1 = [Text.Encoding]::GetEncoding(28591)
        $text = $latin1.GetString([IO.File]::ReadAllBytes($Path))
        $text = [regex]::Replace(
            $text,
            'ZHLT v[^()]*?\([A-Z][a-z]{2} [ 0-9]{2} [0-9]{4}\)',
            'ZHLT vX (XXXXXXXXXXX)')
        return Get-HexHash ($latin1.GetBytes($text))
    }

    function Get-NormalizedEntHash([string]$Path) {
        $text = [IO.File]::ReadAllText($Path)
        $text = [regex]::Replace($text, '(?m)^"compiler"\s+".*"\r?\n?', '')
        return Get-HexHash ([Text.Encoding]::UTF8.GetBytes($text))
    }

    function Compare-Bytes([byte[]]$Ref, [byte[]]$Other) {
        $limit = [Math]::Min($Ref.Length, $Other.Length)
        for ($i = 0; $i -lt $limit; $i++) {
            if ($Ref[$i] -ne $Other[$i]) {
                return [pscustomobject]@{
                    Equal    = $false
                    Offset   = $i
                    ByteRef  = $Ref[$i]
                    ByteOther = $Other[$i]
                    Truncated = $false
                }
            }
        }
        if ($Ref.Length -ne $Other.Length) {
            return [pscustomobject]@{
                Equal     = $false
                Offset    = $limit
                ByteRef   = -1
                ByteOther = -1
                Truncated = $true
            }
        }
        return [pscustomobject]@{ Equal = $true; Offset = -1; ByteRef = 0; ByteOther = 0; Truncated = $false }
    }

    $lumpNames = @(
        'entities', 'planes', 'textures', 'vertexes', 'visibility', 'nodes',
        'texinfo', 'faces', 'lighting', 'clipnodes', 'leafs', 'marksurfaces',
        'edges', 'surfedges', 'models'
    )

    function Get-BspLumpName([byte[]]$Bsp, [int]$Offset) {
        for ($i = 0; $i -lt $lumpNames.Count; $i++) {
            $base = 4 + $i * 8
            $off = [BitConverter]::ToInt32($Bsp, $base)
            $len = [BitConverter]::ToInt32($Bsp, $base + 4)
            if ($Offset -ge $off -and $Offset -lt ($off + $len)) {
                return "lump $i $($lumpNames[$i])"
            }
        }
        return 'bsp header / outside lumps'
    }

    function Compare-Files([string]$RefPath, [string]$NgPath, [switch]$Bsp) {
        if (-not (Test-Path -LiteralPath $RefPath -PathType Leaf)) {
            if (-not (Test-Path -LiteralPath $NgPath -PathType Leaf)) {
                return [pscustomobject]@{ Equal = $true; Note = 'absent' }
            }
            return [pscustomobject]@{ Equal = $false; Note = 'file exists only in the hlrad-ng work dir' }
        }
        if (-not (Test-Path -LiteralPath $NgPath -PathType Leaf)) {
            return [pscustomobject]@{ Equal = $false; Note = 'file exists only in the hlrad work dir' }
        }
        $refBytes = [IO.File]::ReadAllBytes($RefPath)
        $ngBytes = [IO.File]::ReadAllBytes($NgPath)
        if (Get-HexHash $refBytes -eq Get-HexHash $ngBytes) {
            return [pscustomobject]@{ Equal = $true; Note = 'equal' }
        }
        $cmp = Compare-Bytes $refBytes $ngBytes
        if ($Bsp) {
            $lump = Get-BspLumpName $refBytes $cmp.Offset
            if ($cmp.Truncated) {
                $note = "first difference at offset $($cmp.Offset): sizes differ " +
                    "($($refBytes.Length) vs $($ngBytes.Length) bytes)"
            }
            else {
                $note = "first difference at offset $($cmp.Offset) ($($lump)): " +
                    "hlrad 0x$($cmp.ByteRef.ToString('x2')), hlrad-ng 0x$($cmp.ByteOther.ToString('x2'))"
            }
            return [pscustomobject]@{ Equal = $false; Note = $note }
        }
        if ($cmp.Truncated) {
            return [pscustomobject]@{ Equal = $false; Note = "sizes differ ($($refBytes.Length) vs $($ngBytes.Length) bytes)" }
        }
        return [pscustomobject]@{
            Equal = $false
            Note  = "first difference at offset $($cmp.Offset): 0x$($cmp.ByteRef.ToString('x2')) vs 0x$($cmp.ByteOther.ToString('x2'))"
        }
    }

    function Run-Rad([string]$ExeName, [string]$WorkDirPath, [int]$Threads) {
        Push-Location $WorkDirPath
        try {
            $log = Join-Path $WorkDirPath "$ExeName.log"
            $args = @('-threads', "$Threads") + $RadArgList + @("$map.map")
            $sw = [Diagnostics.Stopwatch]::StartNew()
            & (Join-Path $BinDir "$ExeName.exe") @args *> $log
            $sw.Stop()
            if ($LASTEXITCODE -ne 0) {
                $tail = (Get-Content -LiteralPath $log -Tail 25) -join [Environment]::NewLine
                throw "$ExeName (-threads $Threads) failed with exit code $LASTEXITCODE`n$tail"
            }
            return $sw.Elapsed.TotalSeconds
        }
        finally {
            Pop-Location
        }
    }

    function Export-Ent([string]$WorkDirPath) {
        Push-Location $WorkDirPath
        try {
            & (Join-Path $BinDir 'ripent.exe') -export "$map.bsp" *> (Join-Path $WorkDirPath 'ripent.log')
            if ($LASTEXITCODE -ne 0) {
                throw "ripent -export failed with exit code $LASTEXITCODE"
            }
        }
        finally {
            Pop-Location
        }
    }

    function Get-RunArtifacts([string]$WorkDirPath) {
        Export-Ent $WorkDirPath
        $bspPath = Join-Path $WorkDirPath "$map.bsp"
        $dlitPath = Join-Path $WorkDirPath "$map.dlit"
        $incPath = Join-Path $WorkDirPath "$map.inc"
        $dlitHash = '-'
        if (Test-Path -LiteralPath $dlitPath -PathType Leaf) {
            $dlitHash = Get-FileHexHash $dlitPath
        }
        $incHash = '-'
        if (Test-Path -LiteralPath $incPath -PathType Leaf) {
            $incHash = Get-FileHexHash $incPath
        }
        return [pscustomobject]@{
            bsp      = $bspPath
            bspNorm  = Get-NormalizedBspHash $bspPath
            dlit     = $dlitPath
            dlitHash = $dlitHash
            inc      = $incPath
            incHash  = $incHash
            ent      = Join-Path $WorkDirPath "$map.ent"
            entNorm  = Get-NormalizedEntHash (Join-Path $WorkDirPath "$map.ent")
            bspBytes = (Get-Item -LiteralPath $bspPath).Length
        }
    }

    try {
        # Compile once with the standard tools; single-threaded for determinism.
        Push-Location $stageDir
        try {
            foreach ($step in @(
                @{ Name = 'hlcsg'; Args = @('-threads', '1', "$map.map") },
                @{ Name = 'hlbsp'; Args = @('-threads', '1', "$map.map") },
                @{ Name = 'hlvis'; Args = @('-threads', '1', '-full', "$map.map") }
            )) {
                $log = Join-Path $stageDir "$($step.Name).log"
                & (Join-Path $BinDir "$($step.Name).exe") @($step.Args) *> $log
                if ($LASTEXITCODE -ne 0) {
                    $tail = (Get-Content -LiteralPath $log -Tail 25) -join [Environment]::NewLine
                    throw "$($step.Name) failed with exit code $LASTEXITCODE`n$tail"
                }
            }
        }
        finally {
            Pop-Location
        }

        $rows = @()
        foreach ($threads in $ThreadCountsArg) {
            $runs = @(@{ Side = 'ref'; Tool = 'hlrad'; Run = '' })
            $runs += @{ Side = 'ng'; Tool = 'hlrad-ng'; Run = '' }
            if ($DeterminismFlag) {
                $runs = @(
                    @{ Side = 'ref'; Tool = 'hlrad'; Run = '-r1' },
                    @{ Side = 'ng'; Tool = 'hlrad-ng'; Run = '-r1' },
                    @{ Side = 'ref'; Tool = 'hlrad'; Run = '-r2' },
                    @{ Side = 'ng'; Tool = 'hlrad-ng'; Run = '-r2' }
                )
            }

            $runDirs = @{}
            $runTimes = @{}
            foreach ($run in $runs) {
                $dir = Join-Path $workDir "$($run.Side)-t$threads$($run.Run)"
                New-Item -ItemType Directory -Force -Path $dir | Out-Null
                Copy-Item -Path (Join-Path $stageDir '*') -Destination $dir -Force -Recurse
                $runTimes["$($run.Side)$($run.Run)"] = Run-Rad $run.Tool $dir $threads
                $runDirs["$($run.Side)$($run.Run)"] = $dir
            }

            $errors = @()
            $row = [pscustomobject]@{
                map      = $map
                threads  = $threads
                parity   = $true
                bspNorm  = '-'
                dlit     = '-'
                inc      = '-'
                ent      = '-'
                bspBytes = 0
                refSec   = 0.0
                ngSec    = 0.0
                error    = $null
                detRef   = $null
                detNg    = $null
            }

            $refRun = if ($DeterminismFlag) { '-r1' } else { '' }
            $ngRun = if ($DeterminismFlag) { '-r1' } else { '' }
            $refDir = $runDirs["ref$refRun"]
            $ngDir = $runDirs["ng$ngRun"]
            $row.refSec = [Math]::Round($runTimes["ref$refRun"], 2)
            $row.ngSec = [Math]::Round($runTimes["ng$ngRun"], 2)

            $refArt = Get-RunArtifacts $refDir
            $ngArt = Get-RunArtifacts $ngDir

            $row.bspNorm = $ngArt.bspNorm
            $row.dlit = $ngArt.dlitHash
            $row.inc = $ngArt.incHash
            $row.ent = $ngArt.entNorm
            $row.bspBytes = $ngArt.bspBytes

            $bspCmp = Compare-Files $refArt.bsp $ngArt.bsp -Bsp
            if (-not $bspCmp.Equal) {
                $row.parity = $false
                $errors += "bsp: $($bspCmp.Note)"
            }
            $dlitCmp = Compare-Files $refArt.dlit $ngArt.dlit
            if (-not $dlitCmp.Equal -and $dlitCmp.Note -ne 'absent') {
                $row.parity = $false
                $errors += "dlit: $($dlitCmp.Note)"
            }
            $incCmp = Compare-Files $refArt.inc $ngArt.inc
            if (-not $incCmp.Equal -and $incCmp.Note -ne 'absent') {
                $row.parity = $false
                $errors += "inc: $($incCmp.Note)"
            }
            if ($refArt.entNorm -ne $ngArt.entNorm) {
                $row.parity = $false
                $errors += 'ent: exported entity text differs'
            }

            if ($DeterminismFlag) {
                foreach ($side in 'ref', 'ng') {
                    $a = Get-RunArtifacts $runDirs["${side}-r1"]
                    $b = Get-RunArtifacts $runDirs["${side}-r2"]
                    $detErrors = @()
                    if ($a.bspNorm -ne $b.bspNorm) {
                        $cmp = Compare-Files $a.bsp $b.bsp -Bsp
                        $detErrors += "bsp: $($cmp.Note)"
                    }
                    if ($a.dlitHash -ne $b.dlitHash) {
                        $detErrors += 'dlit differs between runs'
                    }
                    if ($a.incHash -ne $b.incHash) {
                        $detErrors += 'inc differs between runs'
                    }
                    if ($a.entNorm -ne $b.entNorm) {
                        $detErrors += 'ent differs between runs'
                    }
                    if ($side -eq 'ref') {
                        $row.detRef = $detErrors.Count -eq 0
                    }
                    else {
                        $row.detNg = $detErrors.Count -eq 0
                    }
                    if ($detErrors.Count -gt 0) {
                        $errors += "determinism ($side): " + ($detErrors -join '; ')
                    }
                }
            }

            $row.error = if ($errors.Count -gt 0) { $errors -join ' | ' } else { $null }
            $rows += $row
        }

        return $rows
    }
    catch {
        return @([pscustomobject]@{
            map      = $map
            threads  = -1
            parity   = $false
            bspNorm  = '-'
            dlit     = '-'
            inc      = '-'
            ent      = '-'
            bspBytes = 0
            refSec   = 0.0
            ngSec    = 0.0
            error    = $_.Exception.Message
            detRef   = $null
            detNg    = $null
        })
    }
}

$results = @()
for ($i = 0; $i -lt $maps.Count; $i += $Jobs) {
    $last = [Math]::Min($i + $Jobs - 1, $maps.Count - 1)
    $chunk = @($maps[$i..$last])
    $jobList = @()
    foreach ($map in $chunk) {
        $jobList += Start-Job -Name $map.BaseName -ScriptBlock $worker -ArgumentList @(
            $map.FullName, $runDir, $binDir, $LightsRad, $WadDir, $radArgList, $threadCountsArg, [bool]$Determinism
        )
    }
    $jobList | Wait-Job | Out-Null
    foreach ($jobItem in $jobList) {
        $output = Receive-Job -Job $jobItem
        if ($jobItem.State -eq 'Failed') {
            $message = ($jobItem.ChildJobs[0].JobStateInfo.Reason | Out-String)
            $results += [pscustomobject]@{ map = $jobItem.Name; threads = -1; parity = $false; bspNorm = '-'; dlit = '-'; inc = '-'; ent = '-'; bspBytes = 0; refSec = 0.0; ngSec = 0.0; error = $message.Trim(); detRef = $null; detNg = $null }
        }
        elseif ($output) {
            $results += $output
        }
        else {
            $results += [pscustomobject]@{ map = $jobItem.Name; threads = -1; parity = $false; bspNorm = '-'; dlit = '-'; inc = '-'; ent = '-'; bspBytes = 0; refSec = 0.0; ngSec = 0.0; error = 'no result returned'; detRef = $null; detNg = $null }
        }
        Remove-Job -Job $jobItem -Force
    }
}

$failed = @($results | Where-Object { -not $_.parity })
foreach ($failure in ($failed | Sort-Object map, threads)) {
    Write-Host ("FAIL {0} t{1}: {2}" -f $failure.map, $failure.threads, $failure.error) -ForegroundColor Red
}

$rows = @($results | Sort-Object map, threads)
$runHashes = Join-Path $runDir 'hashes.tsv'
$lines = @(
    '# vhlt-diffhlrad v1',
    "# generated`t$(Get-Date -Format 'o')",
    "map`tthreads`tparity`tbspNorm`tdlit`tinc`tent`tbspBytes`trefSec`tngSec`tnote"
)
foreach ($result in $rows) {
    $note = if ($result.error) { $result.error } else { '' }
    $refSec = [string][Math]::Round($result.refSec, 2)
    $ngSec = [string][Math]::Round($result.ngSec, 2)
    $lines += "$($result.map)`t$($result.threads)`t$($result.parity)`t$($result.bspNorm)`t$($result.dlit)`t$($result.inc)`t$($result.ent)`t$($result.bspBytes)`t$refSec`t$ngSec`t$note"
}
Set-Content -LiteralPath $runHashes -Value $lines -Encoding ASCII

if ($Determinism) {
    $detFailed = @($results | Where-Object { ($null -ne $_.detRef -and -not $_.detRef) -or ($null -ne $_.detNg -and -not $_.detNg) })
    if ($detFailed.Count -gt 0) {
        foreach ($failure in $detFailed) {
            Write-Host "NONDETERMINISTIC $($failure.map) t$($failure.threads): ref=$($failure.detRef) ng=$($failure.detNg)" -ForegroundColor Yellow
        }
        Write-Host "Hashes written to $runHashes"
        exit 1
    }
}

if ($failed.Count -gt 0) {
    Write-Host "Hashes written to $runHashes"
    exit 1
}

$baselinePath = Join-Path $outDirPath 'baseline.tsv'
if ($UpdateBaseline) {
    New-Item -ItemType Directory -Force -Path $outDirPath | Out-Null
    Copy-Item -LiteralPath $runHashes -Destination $baselinePath -Force
    Write-Host "Baseline written to $baselinePath"
}
elseif (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
    Write-Host "No baseline at $baselinePath; wrote $runHashes"
    Write-Host 'Run once with -UpdateBaseline to create the reference.'
}
else {
    $baselineRows = @{}
    foreach ($line in Get-Content -LiteralPath $baselinePath) {
        if ($line.StartsWith('#') -or -not $line.Trim()) {
            continue
        }
        $fields = $line -split "`t"
        if ($fields.Count -lt 11) {
            continue
        }
        $baselineRows["$($fields[0])`t$($fields[1])"] = $fields
    }

    $mismatches = @()
    foreach ($result in $rows) {
        $key = "$($result.map)`t$($result.threads)"
        if (-not $baselineRows.ContainsKey($key)) {
            $mismatches += "$key missing from baseline"
            continue
        }
        $expected = $baselineRows[$key]
        foreach ($check in @(
            @{ Name = 'bspNorm'; Expected = $expected[3]; Actual = $result.bspNorm },
            @{ Name = 'dlit'; Expected = $expected[4]; Actual = $result.dlit },
            @{ Name = 'inc'; Expected = $expected[5]; Actual = $result.inc },
            @{ Name = 'ent'; Expected = $expected[6]; Actual = $result.ent }
        )) {
            if ($check.Expected -ne $check.Actual) {
                $mismatches += "$key.$($check.Name) expected $($check.Expected) but got $($check.Actual)"
            }
        }
    }

    if ($mismatches.Count -gt 0) {
        foreach ($mismatch in $mismatches) {
            Write-Host "MISMATCH $mismatch" -ForegroundColor Red
        }
        Write-Host "Hashes written to $runHashes"
        exit 1
    }
    Write-Host "Baseline OK: $($rows.Count) rows match $baselinePath"
}

$passed = @($rows | Where-Object { $_.parity })
Write-Host "Parity OK: hlrad == hlrad-ng on $($passed.Count)/$($rows.Count) runs"
foreach ($result in $passed) {
    Write-Host ("  {0,-20} t{1,-2} bsp {2}  ent {3}  hlrad {4}s  hlrad-ng {5}s" -f $result.map, $result.threads, $result.bspNorm.Substring(0, 12), $result.ent.Substring(0, 12), $result.refSec, $result.ngSec)
}
Write-Host "Hashes written to $runHashes"
exit 0
