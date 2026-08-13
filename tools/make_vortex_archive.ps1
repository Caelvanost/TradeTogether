param(
    [string]$ProjectRoot,
    [string]$Version = "0.7.0"
)

$ErrorActionPreference = "Stop"

# If no project root is explicitly supplied, derive it from this script's
# location: <project>\tools\make_vortex_archive.ps1 -> <project>.
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
}

# Normalize paths. In particular, avoid passing "%~dp0" from cmd.exe: its
# trailing backslash can interact badly with the closing quote and produce an
# invalid Windows path such as ...\TradeTogether.dll\".
$ProjectRoot = $ProjectRoot.Trim().Trim('"')
while ($ProjectRoot.EndsWith('\') -or $ProjectRoot.EndsWith('/')) {
    $ProjectRoot = $ProjectRoot.Substring(0, $ProjectRoot.Length - 1)
}
$ProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)

$packageDir = Join-Path $ProjectRoot "package"
$dataDir = Join-Path $packageDir "Data"
$pluginDir = Join-Path (Join-Path $dataDir "SKSE") "Plugins"
$dllPath = Join-Path $pluginDir "TradeTogether.dll"
$distDir = Join-Path $ProjectRoot "dist"
$archivePath = Join-Path $distDir ("TradeTogether-v{0}-Vortex.zip" -f $Version)

if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) {
    throw "DLL introuvable: $dllPath. Compile d'abord avec build_release.bat."
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null

if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

# Put Data\ at the root of the archive so the ZIP is directly importable by Vortex.
Compress-Archive -LiteralPath $dataDir -DestinationPath $archivePath -CompressionLevel Optimal

Write-Host "[TradeTogether] Archive Vortex creee:" -ForegroundColor Green
Write-Host "  $archivePath"
Write-Host "[TradeTogether] Contenu: TradeTogether.dll + TradeTogether.ini"
