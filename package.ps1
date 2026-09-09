# Assemble the FOMOD-installable archive.
#
#   .\package.ps1                       # -> package/STB Hotkey System.7z
#   .\package.ps1 -NoArchive            # leave the staged tree, don't compress
#
# The keycap SWFs are NOT in the repository -- they are built from SkyUI's and
# Untarnished's own art and belong to those authors (see flash/README.md). Build them
# locally into flash/SkyUI/ and flash/Untarnished/ first; a set that is missing is left
# out of the installer, and the script says which.

[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [switch]$NoArchive
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$stage = Join-Path $root 'package/stage'
$dll = Join-Path $root "build/$Configuration/STB_HotkeySystem.dll"

if (-not (Test-Path -LiteralPath $dll)) {
    throw "No plugin at $dll -- build it first (cmake --build build --config $Configuration)."
}

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

# --- always installed -----------------------------------------------------------------
$core = Join-Path $stage 'core'
New-Item -ItemType Directory -Path (Join-Path $core 'SKSE/Plugins') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $core 'Interface') -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $core 'SKSE/Plugins')
Copy-Item -Path (Join-Path $root 'dist/SKSE/Plugins/*.ini') -Destination (Join-Path $core 'SKSE/Plugins')
Copy-Item -Path (Join-Path $root 'dist/Interface/*.txt') -Destination (Join-Path $core 'Interface')

# --- one keycap set, chosen in the installer -------------------------------------------
foreach ($set in 'SkyUI', 'Untarnished') {
    $swf = Join-Path $root "flash/$set/STB_Keycaps.swf"
    if (-not (Test-Path -LiteralPath $swf)) {
        Write-Warning "flash/$set/STB_Keycaps.swf is missing -- the '$set' option will install nothing. See flash/$set/README.md."
        continue
    }
    $dest = Join-Path $stage "keycaps/$set/Interface"
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    Copy-Item -LiteralPath $swf -Destination $dest
}

Copy-Item -Path (Join-Path $root 'fomod') -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage

Write-Host "Staged:" -ForegroundColor Cyan
Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
    '  {0}' -f $_.FullName.Substring($stage.Length + 1)
}

if ($NoArchive) { return }

$sevenZip = Get-Command '7z.exe' -ErrorAction SilentlyContinue
if (-not $sevenZip) {
    Write-Warning "7z.exe not on PATH -- staged tree left at $stage, compress it yourself."
    return
}
$archive = Join-Path $root 'package/STB Hotkey System.7z'
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
& $sevenZip.Source a -t7z -mx=9 $archive (Join-Path $stage '*') | Out-Null
Write-Host "Wrote $archive" -ForegroundColor Green
