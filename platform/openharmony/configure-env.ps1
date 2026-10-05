param(
    [string]$DevEcoPath = $env:DEVECO_STUDIO_PATH,
    [string]$DependenciesPath,
    [string]$ZigPath
)

$ErrorActionPreference = 'Stop'
$engineRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$DependenciesPath) {
    $DependenciesPath = Join-Path (Split-Path $engineRoot) 'OhosSDK'
}
if (!$DevEcoPath) {
    $installation = Get-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -eq 'DevEco Studio' } | Select-Object -First 1
    $DevEcoPath = $installation.InstallLocation
}
if (!$DevEcoPath) { throw 'Pass -DevEcoPath with the DevEco Studio installation directory.' }
if (!$ZigPath) { $ZigPath = Join-Path $DependenciesPath 'zig-windows-x86_64-0.13.0' }

$sdkPath = Join-Path $DevEcoPath 'sdk/default/openharmony'
$llvmPath = Join-Path $sdkPath 'native/llvm/bin'
$runtimePath = Join-Path $DependenciesPath 'OpenHarmony.NET.Runtime'
$crossPath = Join-Path $DependenciesPath 'PublishAotCross'
foreach ($required in @(
    (Join-Path $llvmPath 'clang.exe'),
    (Join-Path $llvmPath 'llvm-objcopy.exe'),
    (Join-Path $ZigPath 'zig.exe'),
    (Join-Path $runtimePath 'runtime.targets'),
    (Join-Path $crossPath 'package/fakeclang/win-x64/clang.exe')
)) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing OpenHarmony build dependency: $required" }
}

# These variables affect only this PowerShell process and programs launched from it.
$env:DEVECO_STUDIO_PATH = (Resolve-Path -LiteralPath $DevEcoPath).Path
$env:OPENHARMONY_SDK_PATH = (Resolve-Path -LiteralPath $sdkPath).Path
$env:OPENHARMONY_DOTNET_RUNTIME_PATH = (Resolve-Path -LiteralPath $runtimePath).Path
$env:OPENHARMONY_PUBLISH_AOT_CROSS_PATH = (Resolve-Path -LiteralPath $crossPath).Path
$env:PATH = (Resolve-Path -LiteralPath $ZigPath).Path + ';' + $llvmPath + ';' + $env:PATH
Write-Host "OpenHarmony environment configured for SDK: $env:OPENHARMONY_SDK_PATH"
