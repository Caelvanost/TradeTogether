param(
    [string]$ProjectRoot,
    [string]$Version = "0.9.8-strpm"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
}

$ProjectRoot = $ProjectRoot.Trim().Trim('"')
while ($ProjectRoot.EndsWith('\') -or $ProjectRoot.EndsWith('/')) {
    $ProjectRoot = $ProjectRoot.Substring(0, $ProjectRoot.Length - 1)
}
$ProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)

$packageDir = Join-Path $ProjectRoot "package"
$dataDir = Join-Path $packageDir "Data"
$pluginDir = Join-Path $dataDir "SKSE\Plugins"
$dllPath = Join-Path $pluginDir "TradeTogether.dll"
$iniPath = Join-Path $pluginDir "TradeTogether.ini"
$distDir = Join-Path $ProjectRoot "dist"
$archivePath = Join-Path $distDir ("TradeTogether-v{0}-Vortex.zip" -f $Version)
$verifyScript = Join-Path $PSScriptRoot "verify_release_dll.ps1"

if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) {
    throw "DLL introuvable: $dllPath. Compile d'abord avec build_release.bat."
}
if (-not (Test-Path -LiteralPath $iniPath -PathType Leaf)) {
    throw "INI introuvable: $iniPath"
}

& $verifyScript -DllPath $dllPath -ExpectedVersion $Version

# The same working tree is also used by the UDP branch. Git does not remove
# untracked package directories when switching branches, so old FOMOD folders
# such as "00 Core" may remain locally. They must never enter an STRPM archive.
$legacyEntries = @("00 Core", "10 LAN", "20 Remote Host", "30 Remote Client", "fomod")
$foundLegacyEntries = @()
foreach ($entry in $legacyEntries) {
    $entryPath = Join-Path $packageDir $entry
    if (Test-Path -LiteralPath $entryPath) {
        $foundLegacyEntries += $entry
    }
}
if ($foundLegacyEntries.Count -gt 0) {
    Write-Host ("[TradeTogether] Ignoring legacy UDP/FOMOD package residue: " + ($foundLegacyEntries -join ", ")) -ForegroundColor Yellow
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

# STRPM is a simple Vortex package. Archive only the Data directory instead of
# package\* so stale folders from another branch cannot be shipped accidentally.
Compress-Archive `
    -Path $dataDir `
    -DestinationPath $archivePath `
    -CompressionLevel Optimal `
    -Force

if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    throw "Archive creation failed: $archivePath"
}

Write-Host "[TradeTogether] Archive Vortex creee:" -ForegroundColor Green
Write-Host "  $archivePath"
