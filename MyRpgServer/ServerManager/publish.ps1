$ErrorActionPreference = 'Stop'

$projectPath = Join-Path $PSScriptRoot 'ServerManager.csproj'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$outputPath = Join-Path $repositoryRoot 'artifacts\ServerManager'

dotnet publish $projectPath `
    --configuration Release `
    --runtime win-x64 `
    --self-contained true `
    --output $outputPath `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:DebugType=None

Write-Host "Published: $outputPath\RpgServerManager.exe"
