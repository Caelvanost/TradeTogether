param(
    [string]$ProjectRoot,
    [string]$Version = "0.8.2-udp"
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
$pluginDir = Join-Path $packageDir "Data\SKSE\Plugins"
$dllPath = Join-Path $pluginDir "TradeTogether.dll"
$iniPath = Join-Path $pluginDir "TradeTogether.ini"
$fomodConfig = Join-Path $packageDir "fomod\ModuleConfig.xml"
$fomodInfo = Join-Path $packageDir "fomod\info.xml"
$fomodCoreDll = Join-Path $packageDir "00 Core\Data\SKSE\Plugins\TradeTogether.dll"
$clientIni = Join-Path $packageDir "10 Remote Client\Data\SKSE\Plugins\TradeTogether.ini"
$hostIni = Join-Path $packageDir "20 Remote Host\Data\SKSE\Plugins\TradeTogether.ini"
$distDir = Join-Path $ProjectRoot "dist"
$archivePath = Join-Path $distDir ("TradeTogether-v{0}-Vortex.zip" -f $Version)

foreach ($requiredFile in @($dllPath, $iniPath, $fomodConfig, $fomodInfo, $fomodCoreDll, $clientIni, $hostIni)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Fichier de package introuvable: $requiredFile. Compile d'abord avec build_release.bat."
    }
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null

if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

Compress-Archive `
    -Path (Join-Path $packageDir "*") `
    -DestinationPath $archivePath `
    -CompressionLevel Optimal `
    -Force

Write-Host "[TradeTogether] Archive Vortex/FOMOD creee:" -ForegroundColor Green
Write-Host "  $archivePath"
Write-Host "  Profils: Remote Client / Remote Host"
