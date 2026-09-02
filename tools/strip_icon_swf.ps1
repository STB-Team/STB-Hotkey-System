# Strips a UI-mod SWF down to a single exported symbol + its dependencies, producing a
# clean import source for the runtime icon injection.
#
# An unstripped menu SWF carries the whole UI mod's symbol table and its SkyUI
# __Packages classes. Those are exported assets inside a movie the game imports, which is
# exactly the footgun the icon-injector pattern warns about. This keeps only the symbol
# we actually attach.
#
# Usage:
#   .\tools\strip_icon_swf.ps1 -In "favoritesmenu.swf" -Out "STB_Keycaps.swf"
#   .\tools\strip_icon_swf.ps1 -In src.swf -Out out.swf -Symbol STBKeycap -Ffdec "C:\...\ffdec-cli.exe"

param(
    [Parameter(Mandatory = $true)][string]$In,
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Symbol = "STBKeycap",
    [string]$Ffdec  = "C:\Program Files (x86)\FFDec\ffdec-cli.exe"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

if (-not (Test-Path $Ffdec)) { throw "FFDec CLI not found: $Ffdec" }
if (-not (Test-Path $In))    { throw "Input SWF not found: $In" }

# FFDec chokes on paths with brackets/spaces, so stage everything in a plain temp dir.
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("swfstrip_" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force $work | Out-Null
try {
    $src = Join-Path $work "in.swf"
    Copy-Item $In $src -Force

    Write-Host "1/5 dumping tags..."
    $tags = Join-Path $work "tags.txt"
    & $Ffdec -dumpSWF $src > $tags 2>&1

    # Locate the exported symbol's character id.
    $line = Select-String -Path $tags -Pattern ("ExportAssets \(chid: (\d+), exp: ""{0}""" -f [regex]::Escape($Symbol)) |
            Select-Object -First 1
    if (-not $line) { throw "No export named '$Symbol' in $In" }
    $rootId = [int]$line.Matches[0].Groups[1].Value
    Write-Host "    '$Symbol' = character $rootId"

    Write-Host "2/5 computing dependency closure..."
    $ids = & python (Join-Path $root "tools\swf_closure.py") $tags $rootId
    if ($LASTEXITCODE -ne 0) { throw "closure computation failed" }
    $idList = $ids.Trim()

    Write-Host "3/5 removing unrelated characters..."
    $stripped = Join-Path $work "stripped.swf"
    if ($idList) {
        & $Ffdec -removeCharacter $src $stripped @($idList -split '\s+')
        if ($LASTEXITCODE -ne 0) { throw "removeCharacter failed" }
    } else {
        Copy-Item $src $stripped -Force
    }

    Write-Host "4/5 dropping leftover imports and timeline scripts..."
    & $Ffdec -dumpSWF $stripped > (Join-Path $work "tags2.txt") 2>&1
    # Top-level ImportAssets / DoAction rows are dead once the menu symbols are gone.
    $drop = Select-String -Path (Join-Path $work "tags2.txt") -Pattern '^[0-9a-f]+:\s{3}(\d+)\.\s+(ImportAssets2?|DoAction)\b' |
            ForEach-Object { [int]$_.Matches[0].Groups[1].Value } |
            Sort-Object -Descending
    $final = Join-Path $work "final.swf"
    if ($drop) { & $Ffdec -remove $stripped $final @($drop) } else { Copy-Item $stripped $final -Force }
    if (-not (Test-Path $final)) { throw "residual tag removal failed" }

    Write-Host "5/5 verifying..."
    & $Ffdec -export symbolClass (Join-Path $work "sym") $final | Out-Null
    $csv = Get-ChildItem (Join-Path $work "sym") -Filter *.csv -Recurse | Select-Object -First 1
    $exports = if ($csv) { Get-Content $csv.FullName } else { @() }
    if (-not ($exports -match [regex]::Escape($Symbol))) { throw "'$Symbol' missing from the stripped file" }
    & $Ffdec -export sprite (Join-Path $work "render") $final | Out-Null
    $frames = @(Get-ChildItem (Join-Path $work "render") -Recurse -Filter *.png -ErrorAction SilentlyContinue).Count
    if ($frames -eq 0) { throw "'$Symbol' renders nothing after stripping" }

    Copy-Item $final $Out -Force
    $before = (Get-Item $In).Length
    $after  = (Get-Item $Out).Length
    Write-Host ("done: {0} -> {1} bytes ({2} exports, {3} rendered frames)" -f $before, $after, $exports.Count, $frames)
    Write-Host "  -> $Out"
} finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
