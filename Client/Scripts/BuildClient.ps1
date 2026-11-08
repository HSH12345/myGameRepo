<#
.SYNOPSIS
Build the same RPG.uproject for Windows or Android.
.EXAMPLE
.\Scripts\BuildClient.ps1 -Platform Windows -Action Check
.EXAMPLE
.\Scripts\BuildClient.ps1 -Platform Android -Action Package
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Windows', 'Android')][string]$Platform,
    [ValidateSet('Check', 'Build', 'Package')][string]$Action = 'Check',
    [ValidateSet('Development', 'Shipping')][string]$Configuration = 'Development',
    [string]$EngineRoot = 'F:\HSH\PF\UE\UE_5.8',
    [string]$AndroidSdkRoot = $env:ANDROID_HOME,
    [string]$AndroidNdkRoot,
    [string]$JavaRoot,
    [switch]$NoXGEShaderCompile,
    [switch]$Preview
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'RPG.uproject'
$profiles = Get-Content -LiteralPath (Join-Path $projectRoot 'Config\BuildProfiles.json') -Raw | ConvertFrom-Json
$profile = $profiles.$Platform
$buildTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$automationTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$archiveDirectory = Join-Path $projectRoot ('Saved\Builds\' + $profile.archiveFolder + '\' + $Configuration)
$issues = [System.Collections.Generic.List[string]]::new()

function Require-Path([string]$Path, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path) -or !(Test-Path -LiteralPath $Path)) {
        $issues.Add($Description)
    }
}

Require-Path $projectFile 'RPG.uproject is missing.'
Require-Path $buildTool 'UE Build.bat is missing. Set -EngineRoot to the UE 5.8 directory.'
Require-Path $automationTool 'UE RunUAT.bat is missing.'
$sharedRoot = $env:RPG_SHARED_WORLD_ROOT
$sharedConfig = Join-Path $projectRoot 'Config\SharedWorldRoot.txt'
if ([string]::IsNullOrWhiteSpace($sharedRoot) -and (Test-Path -LiteralPath $sharedConfig)) {
    $sharedRoot = (Get-Content -LiteralPath $sharedConfig -Raw).Trim()
}
if ([string]::IsNullOrWhiteSpace($sharedRoot)) {
    $issues.Add('Set RPG_SHARED_WORLD_ROOT or Config/SharedWorldRoot.txt to the server SharedWorld directory.')
} else {
    Require-Path (Join-Path $sharedRoot 'Core\CoordinateSystem.h') 'SharedWorld/Core/CoordinateSystem.h is missing.'
}

$firebaseRoot = Join-Path $projectRoot 'Content\Firebase'
Require-Path (Join-Path $firebaseRoot 'include\firebase\app.h') 'Firebase C++ headers are missing.'
if ($Platform -eq 'Windows') {
    foreach ($library in @('firebase_app.lib', 'firebase_auth.lib', 'firebase_analytics.lib')) {
        Require-Path (Join-Path $firebaseRoot ('libs\windows\VS2019\MD\x64\Release\' + $library)) ('Missing Windows dependency: ' + $library)
    }
} else {
    foreach ($library in @('libfirebase_app.a', 'libfirebase_auth.a', 'libfirebase_analytics.a')) {
        Require-Path (Join-Path $firebaseRoot ('libs\android\arm64-v8a\' + $library)) ('Missing Android dependency: ' + $library)
    }
    Require-Path (Join-Path $projectRoot 'Source\RPG\Firebase_UPL.xml') 'Firebase Android Gradle bridge is missing.'
    # Source headers alone do not mean the installed engine includes Android support.
    Require-Path (Join-Path $EngineRoot 'Engine\Build\Android\Java') 'UE Android platform files are missing. Install Android support for this engine in Epic Games Launcher > Options.'
    Require-Path (Join-Path $EngineRoot 'Engine\Binaries\Android\UnrealGame.target') 'UE Android game target receipt is missing. Install Android support for this engine.'
    $sdkSettingsFile = Join-Path $EngineRoot 'Engine\Config\Android\Android_SDK.json'
    Require-Path $sdkSettingsFile 'Engine Android_SDK.json is missing.'
    if (Test-Path -LiteralPath $sdkSettingsFile) {
        $sdkSettings = Get-Content -LiteralPath $sdkSettingsFile -Raw | ConvertFrom-Json
        if ([string]::IsNullOrWhiteSpace($AndroidSdkRoot)) {
            $AndroidSdkRoot = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
        }
        Require-Path (Join-Path $AndroidSdkRoot ('platforms\' + $sdkSettings.platforms + '\android.jar')) ('Install SDK platform ' + $sdkSettings.platforms + ' required by this engine.')
        Require-Path (Join-Path $AndroidSdkRoot ('build-tools\' + $sdkSettings.'build-tools' + '\aapt2.exe')) ('Install Android build-tools ' + $sdkSettings.'build-tools' + '.')
        if ([string]::IsNullOrWhiteSpace($AndroidNdkRoot)) {
            $AndroidNdkRoot = Join-Path $AndroidSdkRoot ('ndk\' + $sdkSettings.ndk)
        }
        Require-Path (Join-Path $AndroidNdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe') ('Install engine NDK ' + $sdkSettings.ndk + ' or explicitly set -AndroidNdkRoot to an engine-supported NDK.')
    }
    if ([string]::IsNullOrWhiteSpace($JavaRoot)) {
        $studioJava = Join-Path ${env:ProgramFiles} 'Android\Android Studio\jbr'
        if (Test-Path -LiteralPath (Join-Path $studioJava 'bin\java.exe')) { $JavaRoot = $studioJava }
        else { $JavaRoot = $env:JAVA_HOME }
    }
    if ([string]::IsNullOrWhiteSpace($JavaRoot)) { $issues.Add('Set -JavaRoot to the engine-supported JDK directory.') }
    else { Require-Path (Join-Path $JavaRoot 'bin\java.exe') 'Java executable is missing.' }
}

$arguments = @($profile.target, $profile.platform, $Configuration, "-Project=$projectFile", '-WaitMutex', '-NoHotReloadFromIDE')
$executable = $buildTool
if ($Platform -eq 'Android') { $arguments += "-architecture=$($profile.architecture)" }
if ($Action -eq 'Package') {
    $executable = $automationTool
    $arguments = @('BuildCookRun', "-project=$projectFile", "-target=$($profile.target)", "-platform=$($profile.platform)", "-clientconfig=$Configuration",
        '-build', '-cook', '-stage', '-pak', '-iostore', '-package', '-archive', "-archivedirectory=$archiveDirectory", '-utf8output', '-unattended', '-nop4')
    if ($Platform -eq 'Android') { $arguments += @("-cookflavor=$($profile.cookFlavor)", "-architecture=$($profile.architecture)") }
    if ($NoXGEShaderCompile) { $arguments += '-AdditionalCookerOptions=-noxgeshadercompile' }
}
Write-Output "Project: $projectFile"
Write-Output "Profile: $Platform / $Configuration / $Action"
Write-Output "Archive: $archiveDirectory"
if ($Preview) {
    Write-Output ('Command: & "' + $executable + '" ' + (($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '))
}
if ($issues.Count -gt 0) {
    foreach ($issue in $issues) { Write-Output ('[MISSING] ' + $issue) }
    throw 'Build prerequisites are incomplete. No build or SDK installation was started.'
}
Write-Output 'Dependency file checks passed. This does not certify compilation, packaging, SDK version compatibility, or device operation.'
if ($Action -eq 'Check' -or $Preview) { return }

# Override tool locations only in this process, and restore even on failure.
$previousSdk = $env:ANDROID_HOME
$previousNdk = $env:NDKROOT
$previousJava = $env:JAVA_HOME
try {
    if ($Platform -eq 'Android') {
        $env:ANDROID_HOME = $AndroidSdkRoot
        $env:NDKROOT = $AndroidNdkRoot
        $env:JAVA_HOME = $JavaRoot
    }
    & $executable @arguments
    if ($LASTEXITCODE -ne 0) { throw "UE $Action failed with exit code $LASTEXITCODE." }
} finally {
    $env:ANDROID_HOME = $previousSdk
    $env:NDKROOT = $previousNdk
    $env:JAVA_HOME = $previousJava
}
