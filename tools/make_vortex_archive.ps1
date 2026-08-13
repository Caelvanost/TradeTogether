param(
    [string]$ProjectRoot,
    [string]$Version = "0.7.1"
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
$pluginDir = Join-Path $packageDir "Data\SKSE\Plugins"
$dllPath = Join-Path $pluginDir "TradeTogether.dll"
$clientIniPath = Join-Path $pluginDir "TradeTogether.ini"
$hostPackageDir = Join-Path $ProjectRoot "optional\Host\package"
$hostIniPath = Join-Path $hostPackageDir "Data\SKSE\Plugins\TradeTogether.ini"
$fomodSourceDir = Join-Path $ProjectRoot "fomod"
$moduleConfigPath = Join-Path $fomodSourceDir "ModuleConfig.xml"
$infoPath = Join-Path $fomodSourceDir "info.xml"
$stageDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot "out\fomod-stage"))
$distDir = Join-Path $ProjectRoot "dist"
$archivePath = Join-Path $distDir ("TradeTogether-v{0}-Vortex.zip" -f $Version)

if (-not (Test-Path -LiteralPath $dllPath -PathType Leaf)) {
    throw "DLL introuvable: $dllPath. Compile d'abord avec build_release.bat."
}
foreach ($requiredPath in @($clientIniPath, $hostIniPath, $moduleConfigPath, $infoPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Fichier FOMOD requis introuvable: $requiredPath"
    }
}

$clientIniContent = Get-Content -LiteralPath $clientIniPath -Raw
$hostIniContent = Get-Content -LiteralPath $hostIniPath -Raw
if ($clientIniContent -notmatch '(?ms)^\[Network\].*?^RelayMode=0\s*$' -or
    $clientIniContent -notmatch '(?ms)^\[Network\].*?^AutoRemoteFromSTR=1\s*$') {
    throw "Le profil Client / Local doit garder RelayMode=0 et AutoRemoteFromSTR=1."
}
if ($hostIniContent -notmatch '(?ms)^\[Network\].*?^RelayMode=1\s*$' -or
    $hostIniContent -notmatch '(?ms)^\[Network\].*?^AutoRemoteFromSTR=0\s*$') {
    throw "Le profil Host doit activer RelayMode=1 et desactiver AutoRemoteFromSTR."
}

try {
    [void][xml](Get-Content -LiteralPath $moduleConfigPath -Raw)
    [void][xml](Get-Content -LiteralPath $infoPath -Raw)
} catch {
    throw "Metadonnees FOMOD XML invalides: $($_.Exception.Message)"
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null

if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

$outRoot = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot "out")).TrimEnd('\')
if (-not $stageDir.StartsWith("$outRoot\", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Le repertoire temporaire FOMOD est hors du dossier out: $stageDir"
}
if (Test-Path -LiteralPath $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}

$corePluginDir = Join-Path $stageDir "00 Core\Data\SKSE\Plugins"
$clientPluginDir = Join-Path $stageDir "10 Client Local\Data\SKSE\Plugins"
$hostPluginDir = Join-Path $stageDir "20 Host\Data\SKSE\Plugins"
$fomodStageDir = Join-Path $stageDir "fomod"
New-Item -ItemType Directory -Force -Path `
    $corePluginDir, `
    $clientPluginDir, `
    $hostPluginDir, `
    $fomodStageDir | Out-Null

Copy-Item -LiteralPath $dllPath -Destination (Join-Path $corePluginDir "TradeTogether.dll") -Force
Copy-Item -LiteralPath $clientIniPath -Destination (Join-Path $clientPluginDir "TradeTogether.ini") -Force
Copy-Item -LiteralPath $hostIniPath -Destination (Join-Path $hostPluginDir "TradeTogether.ini") -Force
Copy-Item -Path (Join-Path $fomodSourceDir "*") -Destination $fomodStageDir -Recurse -Force

Compress-Archive `
    -Path (Join-Path $stageDir "*") `
    -DestinationPath $archivePath `
    -CompressionLevel Optimal `
    -Force

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $entries = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
    $requiredEntries = @(
        "00 Core/Data/SKSE/Plugins/TradeTogether.dll",
        "10 Client Local/Data/SKSE/Plugins/TradeTogether.ini",
        "20 Host/Data/SKSE/Plugins/TradeTogether.ini",
        "fomod/ModuleConfig.xml",
        "fomod/info.xml"
    )

    foreach ($requiredEntry in $requiredEntries) {
        if ($entries -notcontains $requiredEntry) {
            throw "Entree FOMOD absente de l'archive: $requiredEntry"
        }
    }
} finally {
    $archive.Dispose()
}

Write-Host "[TradeTogether] Archive Vortex creee:" -ForegroundColor Green
Write-Host "  $archivePath"
Write-Host "[TradeTogether] Contenu: FOMOD Host / Client Local + TradeTogether.dll"
