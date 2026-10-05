param(
    [string]$SdkPath = $env:OPENHARMONY_SDK_PATH,
    [ValidateSet('arm64', 'x86_64')][string]$Arch = 'arm64',
    [ValidateSet('debug', 'release', 'all')][string]$Configuration = 'all',
    [switch]$Editor,
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'
$entry = Join-Path $PSScriptRoot '../../../TempSDK/OpenHarmony/build.ps1'
if (-not (Test-Path -LiteralPath $entry)) { throw '找不到 TempSDK/OpenHarmony/build.ps1，请保留配套适配目录。' }
& $entry @PSBoundParameters
