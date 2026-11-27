# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Cloud build helper: get JieLi Windows toolchain 2.5.3 onto a GitHub Windows runner
# without any clicks, and export FM1_PI32_TOOLCHAIN for the next steps.
# Tries, in order: unpacking with 7-Zip (plus archives inside), innoextract,
# then a silent install with a time limit. Prints what it finds when it fails.
$ErrorActionPreference = 'Stop'
$url = 'https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe'
$tmp = $env:RUNNER_TEMP
$exe = Join-Path $tmp 'jl-2.5.3.exe'

curl.exe -fL --retry 5 --retry-delay 10 -o $exe $url
if ($LASTEXITCODE -ne 0) { throw 'Download of the JieLi toolchain failed.' }
Write-Host ("Downloaded {0:N1} MB, SHA-256 {1}" -f ((Get-Item $exe).Length / 1MB), (Get-FileHash $exe -Algorithm SHA256).Hash)

$text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe))
$kind = if ($text -match 'Inno Setup') { 'inno' } elseif ($text -match 'Nullsoft') { 'nsis' }
        elseif ($text -match 'InstallShield') { 'installshield' } elseif ($text -match 'WixBundle') { 'wix' } else { 'unknown' }
Write-Host "Installer type: $kind"
Write-Host '--- 7-Zip listing (first lines) ---'
7z l $exe 2>&1 | Select-Object -First 40 | ForEach-Object { Write-Host $_ }

$roots = [Collections.Generic.List[string]]::new()
function Find-Pi32 {
    foreach ($root in $roots) {
        if (!(Test-Path $root)) { continue }
        $hit = Get-ChildItem $root -Recurse -Filter clang.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\pi32\\bin\\clang\.exe$' } | Select-Object -First 1
        if ($hit) { return $hit }
    }
}
function Done($clang) {
    $pi32 = Split-Path (Split-Path $clang.FullName)
    Write-Host "Toolchain: $pi32"
    "FM1_PI32_TOOLCHAIN=$pi32" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
    exit 0
}

# 1. Unpack with 7-Zip, then unpack any archives found inside.
$x = Join-Path $tmp 'jl-7z'
$roots.Add($x)
7z x $exe "-o$x" -y 2>&1 | Out-Null
for ($pass = 0; $pass -lt 2; ++$pass) {
    Get-ChildItem $x -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in '.7z', '.zip', '.cab', '.msi', '.tar', '.xz', '.exe' -and $_.FullName -notmatch '_x\\' -and $_.Length -gt 1MB } |
        ForEach-Object { 7z x $_.FullName ("-o" + $_.FullName + '_x') -y 2>&1 | Out-Null }
}
if ($c = Find-Pi32) { Done $c }

# 2. Inno Setup: innoextract reads it without running it.
if ($kind -eq 'inno') {
    choco install innoextract -y --no-progress | Out-Null
    $i = Join-Path $tmp 'jl-inno'
    $roots.Add($i)
    innoextract -s -e -d $i $exe
    if ($c = Find-Pi32) { Done $c }
}

# 3. Silent install, never longer than 10 minutes per attempt.
$roots.Add('C:\JL'); $roots.Add('C:\Program Files'); $roots.Add('C:\Program Files (x86)'); $roots.Add($env:LOCALAPPDATA)
$tries = switch ($kind) {
    'inno' { @('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /DIR=C:\JL') }
    'nsis' { @('/S /D=C:\JL') }
    default { @('/S /D=C:\JL', '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /DIR=C:\JL', '/quiet /norestart') }
}
foreach ($arguments in $tries) {
    Write-Host "Silent install: $arguments"
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
    if (!$p.WaitForExit(600000)) {
        Write-Host 'The installer did not finish in 10 minutes (it is probably waiting for clicks). Stopping it.'
        Get-Process | Where-Object { $_.Path -eq $exe } | Stop-Process -Force -ErrorAction SilentlyContinue
        $p | Stop-Process -Force -ErrorAction SilentlyContinue
    } else { Write-Host "Installer exit code: $($p.ExitCode)" }
    if ($c = Find-Pi32) { Done $c }
}

Write-Host '--- What was unpacked (first 150 entries) ---'
Get-ChildItem $x -Recurse -ErrorAction SilentlyContinue | Select-Object -First 150 | ForEach-Object { Write-Host $_.FullName }
throw 'pi32\bin\clang.exe was not found. See the listing above.'
