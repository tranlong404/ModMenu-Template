param(
    [string]$Ndk = $env:ANDROID_NDK_HOME,
    [ValidateRange(1,32)][int]$Jobs = 4,
    [switch]$RebuildCore
)
$ErrorActionPreference = 'Stop'
if (-not $Ndk) { $Ndk = 'D:\android-ndk-r30-windows\android-ndk-r30' }
$builder = Join-Path $Ndk 'ndk-build.cmd'
if (-not (Test-Path -LiteralPath $builder)) { throw "NDK builder not found: $builder" }

Push-Location $PSScriptRoot
try {
    if ($RebuildCore -or -not (Test-Path -LiteralPath 'artifacts/arm64-v8a/libModMenu.so')) {
        & ./build.ps1 -Ndk $Ndk -Jobs $Jobs
    }
    & $builder -C zygisk-loader NDK_PROJECT_PATH=null APP_BUILD_SCRIPT=Loader.mk NDK_APPLICATION_MK=Application.mk NDK_OUT=build/obj NDK_LIBS_OUT=build/libs "-j$Jobs"
    if ($LASTEXITCODE -ne 0) { throw "Zygisk loader build failed: $LASTEXITCODE" }
    & python tools/package_zygisk.py --ndk $Ndk
    if ($LASTEXITCODE -ne 0) { throw "Module packaging failed: $LASTEXITCODE" }
} finally { Pop-Location }
