$ErrorActionPreference = 'Stop'
& dotnet run --project "$PSScriptRoot/Build/GameBuild.csproj" -c Release -- $PSScriptRoot
if ($LASTEXITCODE -ne 0) { throw 'ECS C# 构建失败' }
