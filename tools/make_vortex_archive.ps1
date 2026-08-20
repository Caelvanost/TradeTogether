param(
    [string]$ProjectRoot,
    [string]$Version = "0.9.4-strpm"
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
$distDir = Join-Path $ProjectRoot "dist"
$archivePath = Join-Path $distDir ("TradeTogether-v{0}-Vortex.zip" -f $Version)
$verifyScript = Join-Path $PSScriptRoot "verify_release_dll.ps1"

if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) {
    throw "DLL introuvable: $dllPath. Compile d'abord avec build_release.bat."
}
if (-not (Test-Path -LiteralPath $iniPath -PathType Leaf)) {
    throw "INI introuvable: $iniPath"
}

# verify_release_dll.ps1 throws on failure. Do not inspect $LASTEXITCODE here:
# PowerShell script invocations do not reliably set it on success, and a null
# value was causing the packaging script to exit before creating the archive.
& $verifyScript -DllPath $dllPath -ExpectedVersion $Version

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

Compress-Archive `
    -Path (Join-Path $packageDir "*") `
    -DestinationPath $archivePath `
    -CompressionLevel Optimal `
    -Force

if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    throw "Archive creation failed: $archivePath"
}

Write-Host "[TradeTogether] Archive Vortex creee:" -ForegroundColor Green
Write-Host "  $archivePath"
