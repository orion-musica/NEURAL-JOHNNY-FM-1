# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Neural Johnny: one-click local install.
# 1. finds the JieLi toolchain   2. builds the firmware and the installer page
# 3. starts a local server and opens the installer in Chrome or Edge.
# Run from INSTALL-JOHNNY.bat in the repository root.
[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [int]$Port = 8766
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $root

function Say($text, $color = 'Gray') { Write-Host $text -ForegroundColor $color }
function Stop-WithMessage($text) {
    Say ''
    Say $text 'Yellow'
    Say ''
    Read-Host 'Nacisnij Enter, aby zamknac'
    exit 1
}

Say ''
Say '  Neural Johnny - instalacja lokalna' 'Green'
Say '  --------------------' 'DarkGray'
Say ''

$installer = Join-Path $root 'docs\install\index.html'
$built = (Test-Path -LiteralPath $installer) -and -not ([IO.File]::ReadAllText($installer).Contains('has not been built yet'))

if ($SkipBuild -and -not $built) {
    Stop-WithMessage 'Instalator nie jest jeszcze zbudowany. Uruchom bez -SkipBuild.'
}

if (-not $SkipBuild) {
    # 1. Toolchain
    $toolchain = $env:FM1_PI32_TOOLCHAIN
    if ([string]::IsNullOrWhiteSpace($toolchain)) {
        foreach ($candidate in @('C:\JL\pi32', 'D:\JL\pi32', "$env:ProgramFiles\JL\pi32")) {
            if (Test-Path -LiteralPath (Join-Path $candidate 'bin\clang.exe')) { $toolchain = $candidate; break }
        }
    }
    if ([string]::IsNullOrWhiteSpace($toolchain) -or -not (Test-Path -LiteralPath (Join-Path $toolchain 'bin\clang.exe'))) {
        Say 'Brakuje kompilatora JieLi (toolchain 2.5.3).' 'Yellow'
        Say 'Otwieram strone pobierania. Zainstaluj go (domyslnie do C:\JL), potem uruchom ten plik ponownie.'
        Start-Process 'https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe'
        Start-Process 'https://doc.zh-jieli.com/Tools/zh-cn/dev_tools/dev_env/jl_toolchain.html'
        Stop-WithMessage 'Po instalacji: jesli kompilator jest w innym folderze niz C:\JL\pi32, ustaw zmienna FM1_PI32_TOOLCHAIN.'
    }
    Say "Kompilator: $toolchain" 'Green'

    # 2. Source address (GPL: the installer page links the matching source)
    $source = $env:FM1_SOURCE_URL
    if ([string]::IsNullOrWhiteSpace($source)) {
        $remote = ''
        try { $remote = (& git -C $root config --get remote.origin.url 2>$null) } catch { }
        if ($remote -match 'github\.com[:/](.+?)(\.git)?$') {
            $source = "https://github.com/$($Matches[1])/archive/refs/heads/main.zip"
        }
    }
    if ([string]::IsNullOrWhiteSpace($source)) {
        Say ''
        Say 'Podaj adres repozytorium na GitHubie (np. https://github.com/orion-musica/NEURAL-JOHNNY-FM-1).'
        Say 'Jesli jeszcze go nie masz, nacisnij Enter - uzyje adresu tymczasowego, do testu lokalnego.'
        $answer = Read-Host 'Adres'
        if ($answer -match '^https://github\.com/[^/\s]+/[^/\s]+') {
            $source = ($answer.TrimEnd('/')) + '/archive/refs/heads/main.zip'
        } else {
            $source = 'https://github.com/orion-musica/NEURAL-JOHNNY-FM-1/archive/refs/heads/main.zip'
            Say 'Adres tymczasowy - przed publikacja zbuduj ponownie z prawdziwym adresem.' 'Yellow'
        }
    }
    Say "Zrodla: $source" 'Green'

    # 3. Build firmware + installer page (first run downloads the WASI SDK)
    Say ''
    Say 'Buduje firmware i instalator. Pierwszy raz moze to potrwac kilka minut...' 'Cyan'
    try {
        & (Join-Path $PSScriptRoot 'Make-Site.ps1') -Toolchain $toolchain -SourceUrl $source
    } catch {
        Say ''
        Say "BLAD BUDOWANIA: $($_.Exception.Message)" 'Green'
        Stop-WithMessage 'Skopiuj caly tekst z tego okna i wklej go Claude - poprawimy blad.'
    }
}

# 4. Serve and open the installer
Say ''
Say 'Podlacz FM-1 kablem USB i wlacz go. Zrob kopie zapasowa (M-Flasher), jesli jeszcze jej nie masz.' 'Yellow'
Say 'W przegladarce zezwol na dostep do urzadzen MIDI, gdy zapyta.' 'Yellow'
& (Join-Path $PSScriptRoot 'Serve-Site.ps1') -Port $Port -Page 'install/index.html'
