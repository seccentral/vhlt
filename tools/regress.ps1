<#
.SYNOPSIS
    Compile reference maps and record or compare deterministic output hashes.

.DESCRIPTION
    Runs the full pipeline (hlcsg, hlbsp, hlvis -full, hlrad -extra) on every
    .map in -MapDir and hashes the resulting .bsp, .prt and entity text
    (ripent -export).

    Every tool runs with -threads 1. Threaded compiles are not deterministic:
    two runs with the same thread count can produce different .bsp and .prt
    files, so the regression gate must be single-threaded. Maps are compiled
    in parallel instead (see -Jobs), which uses the available cores while
    keeping each individual output stable.

    The .bsp entity lump contains a "compiler" key with __DATE__. The script
    normalises that date in the BSP bytes and drops the key from the exported
    .ent before hashing, so hashes stay comparable across days.

    Maps and WADs are third-party assets and must stay outside the repository.
    Put the .map files in a directory somewhere else and point -MapDir at it.

.PARAMETER MapDir
    Directory containing the .map files to compile. Required.

.PARAMETER LightsRad
    Optional lights.rad file. When set it is copied next to each map and
    passed to hlrad with -lights, which exercises texture lights.

.PARAMETER WadDir
    Optional directory containing the WADs the maps reference. When set, the
    worldspawn "wad" key is rewritten to list the matching WADs from this
    directory, so the same maps compile on hosts where the original absolute
    paths do not exist. hlcsg stores only the WAD base names in the BSP, so
    this does not change the hashed output.

.PARAMETER OutDir
    Output root. Defaults to build/regress. Runs are written to
    <OutDir>/<RunName> and the reference to <OutDir>/baseline.tsv.

.PARAMETER RunName
    Name of this run. Defaults to the current date and time.

.PARAMETER UpdateBaseline
    Write this run's hashes to <OutDir>/baseline.tsv instead of comparing.

.PARAMETER Jobs
    Number of maps to compile at the same time. Defaults to 3.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/regress.ps1 `
        -MapDir D:\vhlt-maps -LightsRad D:\vhlt-maps\lights.rad `
        -WadDir "D:\Half-Life\valve" -UpdateBaseline

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/regress.ps1 `
        -MapDir D:\vhlt-maps -LightsRad D:\vhlt-maps\lights.rad `
        -WadDir "D:\Half-Life\valve"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MapDir,

    [string]$LightsRad,

    [string]$WadDir,

    [string]$OutDir = (Join-Path $PSScriptRoot '..\build\regress'),

    [string]$RunName = (Get-Date -Format 'yyyyMMdd-HHmmss'),

    [switch]$UpdateBaseline,

    [int]$Jobs = 3
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$binDir = Join-Path $repoRoot 'build\windows\bin'
$mapDirPath = (Resolve-Path -LiteralPath $MapDir).Path
$outDirPath = [IO.Path]::GetFullPath($OutDir)
$runDir = Join-Path $outDirPath $RunName

foreach ($tool in 'hlcsg', 'hlbsp', 'hlvis', 'hlrad', 'ripent') {
    $exe = Join-Path $binDir "$tool.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Missing $exe. Configure and build the windows preset first."
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

New-Item -ItemType Directory -Force -Path $runDir | Out-Null

# Each job owns one map and one work directory, so the jobs never share files.
$worker = {
    param($MapPath, $RunDir, $BinDir, $LightsRadPath, $WadDirPath)

    $ErrorActionPreference = 'Stop'
    $map = [IO.Path]::GetFileNameWithoutExtension($MapPath)
    $workDir = Join-Path $RunDir (Join-Path 'work' $map)
    New-Item -ItemType Directory -Force -Path $workDir | Out-Null
    $mapFile = Join-Path $workDir "$map.map"
    Copy-Item -LiteralPath $MapPath -Destination $mapFile -Force
    if ($LightsRadPath) {
        Copy-Item -LiteralPath $LightsRadPath -Destination (Join-Path $workDir 'lights.rad') -Force
    }
    if ($WadDirPath) {
        $latin1 = [Text.Encoding]::GetEncoding(28591)
        $text = [IO.File]::ReadAllText($mapFile, $latin1)
        $text = [regex]::Replace($text, '(?m)^"wad"\s+"([^"]*)"$', {
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

    function Get-NormalizedEntHash([string]$Path) {
        $text = [IO.File]::ReadAllText($Path)
        $text = [regex]::Replace($text, '(?m)^"compiler"\s+".*"\r?\n?', '')
        return Get-HexHash ([Text.Encoding]::UTF8.GetBytes($text))
    }

    function Get-NormalizedBspHash([string]$Path) {
        $latin1 = [Text.Encoding]::GetEncoding(28591)
        $text = $latin1.GetString([IO.File]::ReadAllBytes($Path))
        $text = [regex]::Replace(
            $text,
            'ZHLT v([^()]*?)\([A-Z][a-z]{2} [ 0-9]{2} [0-9]{4}\)',
            'ZHLT v$1(XXXXXXXXXXX)')
        return Get-HexHash ($latin1.GetBytes($text))
    }

    try {
        Push-Location $workDir
        try {
            $steps = @(
                @{ Name = 'hlcsg'; Args = @('-threads', '1', "$map.map") },
                @{ Name = 'hlbsp'; Args = @('-threads', '1', "$map.map") },
                @{ Name = 'hlvis'; Args = @('-threads', '1', '-full', "$map.map") },
                @{ Name = 'hlrad'; Args = @('-threads', '1', '-extra') }
            )

            $radArgs = @('-threads', '1', '-extra')
            if ($LightsRadPath) {
                $radArgs += @('-lights', 'lights.rad')
            }
            $radArgs += "$map.map"
            $steps[3].Args = $radArgs

            foreach ($step in $steps) {
                $log = Join-Path $workDir "$($step.Name).log"
                & (Join-Path $BinDir "$($step.Name).exe") @($step.Args) *> $log
                if ($LASTEXITCODE -ne 0) {
                    $tail = (Get-Content -LiteralPath $log -Tail 25) -join [Environment]::NewLine
                    throw "$($step.Name) failed with exit code $LASTEXITCODE`n$tail"
                }
            }

            & (Join-Path $BinDir 'ripent.exe') -export "$map.bsp" *> (Join-Path $workDir 'ripent.log')
            if ($LASTEXITCODE -ne 0) {
                $tail = (Get-Content -LiteralPath (Join-Path $workDir 'ripent.log') -Tail 25) -join [Environment]::NewLine
                throw "ripent -export failed with exit code $LASTEXITCODE`n$tail"
            }
        }
        finally {
            Pop-Location
        }

        $bsp = Join-Path $workDir "$map.bsp"
        $prt = Join-Path $workDir "$map.prt"
        $ent = Join-Path $workDir "$map.ent"
        foreach ($file in $bsp, $prt, $ent) {
            if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
                throw "Expected output missing: $file"
            }
        }

        return [pscustomobject]@{
            map             = $map
            ok              = $true
            error           = $null
            bsp             = Get-FileHexHash $bsp
            bspDateFree     = Get-NormalizedBspHash $bsp
            prt             = Get-FileHexHash $prt
            ent             = Get-NormalizedEntHash $ent
            bspBytes        = (Get-Item -LiteralPath $bsp).Length
            prtBytes        = (Get-Item -LiteralPath $prt).Length
        }
    }
    catch {
        return [pscustomobject]@{
            map   = $map
            ok    = $false
            error = $_.Exception.Message
        }
    }
}

$results = @()
for ($i = 0; $i -lt $maps.Count; $i += $Jobs) {
    $last = [Math]::Min($i + $Jobs - 1, $maps.Count - 1)
    $chunk = @($maps[$i..$last])
    $jobList = @()
    foreach ($map in $chunk) {
        $jobList += Start-Job -Name $map.BaseName -ScriptBlock $worker -ArgumentList @(
            $map.FullName, $runDir, $binDir, $LightsRad, $WadDir
        )
    }
    $jobList | Wait-Job | Out-Null
    foreach ($jobItem in $jobList) {
        $output = Receive-Job -Job $jobItem
        if ($jobItem.State -eq 'Failed') {
            $message = ($jobItem.ChildJobs[0].JobStateInfo.Reason | Out-String)
            $results += [pscustomobject]@{ map = $jobItem.Name; ok = $false; error = $message.Trim() }
        }
        elseif ($output) {
            $results += $output
        }
        else {
            $results += [pscustomobject]@{ map = $jobItem.Name; ok = $false; error = 'no result returned' }
        }
        Remove-Job -Job $jobItem -Force
    }
}

$failed = @($results | Where-Object { -not $_.ok })
if ($failed.Count -gt 0) {
    foreach ($failure in $failed) {
        Write-Host "FAIL $($failure.map): $($failure.error)" -ForegroundColor Red
    }
    exit 1
}

$rows = @($results | Sort-Object map)
$runHashes = Join-Path $runDir 'hashes.tsv'
$lines = @(
    '# vhlt-regress v1',
    "# generated`t$(Get-Date -Format 'o')",
    "map`tbsp`tbspDateFree`tprt`tent`tbspBytes`tprtBytes"
)
foreach ($result in $rows) {
    $lines += "$($result.map)`t$($result.bsp)`t$($result.bspDateFree)`t$($result.prt)`t$($result.ent)`t$($result.bspBytes)`t$($result.prtBytes)"
}
Set-Content -LiteralPath $runHashes -Value $lines -Encoding ASCII

$baselinePath = Join-Path $outDirPath 'baseline.tsv'
if ($UpdateBaseline) {
    New-Item -ItemType Directory -Force -Path $outDirPath | Out-Null
    Copy-Item -LiteralPath $runHashes -Destination $baselinePath -Force
    Write-Host "Baseline written to $baselinePath"
    exit 0
}

if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
    Write-Host "No baseline at $baselinePath; wrote $runHashes"
    Write-Host 'Run once with -UpdateBaseline to create the reference.'
    exit 0
}

$baselineRows = @{}
foreach ($line in Get-Content -LiteralPath $baselinePath) {
    if ($line.StartsWith('#') -or -not $line.Trim()) {
        continue
    }
    $fields = $line -split "`t"
    $baselineRows[$fields[0]] = $fields
}

$mismatches = @()
foreach ($result in $rows) {
    if (-not $baselineRows.ContainsKey($result.map)) {
        $mismatches += "$($result.map) missing from baseline"
        continue
    }
    $expected = $baselineRows[$result.map]
    if ($expected.Count -lt 7) {
        $mismatches += "$($result.map): malformed baseline row"
        continue
    }
    $checks = @(
        @{ Name = 'bspDateFree'; Expected = $expected[2]; Actual = $result.bspDateFree },
        @{ Name = 'prt'; Expected = $expected[3]; Actual = $result.prt },
        @{ Name = 'ent'; Expected = $expected[4]; Actual = $result.ent }
    )
    foreach ($check in $checks) {
        if ($check.Expected -ne $check.Actual) {
            $mismatches += "$($result.map).$($check.Name) expected $($check.Expected) but got $($check.Actual)"
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

Write-Host "Regression OK: $($rows.Count) maps match $baselinePath"
foreach ($result in $rows) {
    Write-Host ("  {0,-12} bsp {1}  prt {2}  ent {3}" -f $result.map, $result.bspDateFree.Substring(0, 12), $result.prt.Substring(0, 12), $result.ent.Substring(0, 12))
}
Write-Host "Hashes written to $runHashes"
exit 0
