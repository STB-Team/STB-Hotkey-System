# Repacks favoritesmenu.swf with our edited ActionScript (hotkey badge).
#
# Source SWF + unpacked AS live under Untarnished/. We only edit AS sources; the
# binary SWF is rebuilt here via JPEXS FFDec CLI (-replace, AS1/2). Output goes to
# build/swf/ and, if -Deploy is passed, is copied next to the DLL deploy dir.
#
# Usage:
#   .\tools\repack_swf.ps1                 # build/swf/favoritesmenu.swf
#   .\tools\repack_swf.ps1 -Deploy "C:\...\mods\STB Hotkey System\Interface"

param(
	[string]$Ffdec  = "C:\Program Files (x86)\FFDec\ffdec-cli.exe",
	[string]$Deploy = "C:\Skyrim\[STB] Mod Organizer\mods\STB Hotkey System\Interface"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

$srcSwf = Join-Path $root "Untarnished\favoritesmenu.swf"
$outDir = Join-Path $root "build\swf"
$outSwf = Join-Path $outDir "favoritesmenu.swf"

# scriptPath inside the SWF  ->  edited .as source on disk
$scripts = @{
	"\__Packages\FavoritesListEntry" = Join-Path $root "Untarnished\scripts\__Packages\FavoritesListEntry.as"
}

if (-not (Test-Path $Ffdec))  { throw "FFDec CLI not found: $Ffdec" }
if (-not (Test-Path $srcSwf)) { throw "Source SWF not found: $srcSwf" }
New-Item -ItemType Directory -Force $outDir | Out-Null

# -replace applies all (scriptPath, asFile) pairs in one pass.
$args = @($srcSwf, $outSwf)
foreach ($kv in $scripts.GetEnumerator()) {
	if (-not (Test-Path $kv.Value)) { throw "AS source missing: $($kv.Value)" }
	$args += $kv.Key
	$args += $kv.Value
}

& $Ffdec -replace @args
if ($LASTEXITCODE -ne 0) { throw "ffdec -replace failed ($LASTEXITCODE)" }
Write-Host "Repacked -> $outSwf"

if ($Deploy -ne "") {
	New-Item -ItemType Directory -Force $Deploy | Out-Null
	Copy-Item $outSwf (Join-Path $Deploy "favoritesmenu.swf") -Force
	Write-Host "Deployed -> $Deploy"
}
