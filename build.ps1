param(
    [string]$Ndk = $env:ANDROID_NDK_HOME,
    [ValidateRange(1,32)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
if (-not $Ndk) { $Ndk = 'D:\android-ndk-r30-windows\android-ndk-r30' }
$builder = Join-Path $Ndk 'ndk-build.cmd'
if (-not (Test-Path -LiteralPath $builder)) { throw "NDK builder not found: $builder" }

Push-Location $PSScriptRoot
try {
    & $builder -C jni NDK_PROJECT_PATH=null APP_BUILD_SCRIPT=Android.mk NDK_APPLICATION_MK=Application.mk NDK_OUT=build/obj NDK_LIBS_OUT=build/libs "-j$Jobs"
    if ($LASTEXITCODE -ne 0) { throw "Native build failed: $LASTEXITCODE" }
    New-Item -ItemType Directory -Force -Path 'artifacts/arm64-v8a' | Out-Null
    Copy-Item -Force 'jni/build/libs/arm64-v8a/libModMenu.so' 'artifacts/arm64-v8a/libModMenu.so'
    Get-Item 'artifacts/arm64-v8a/libModMenu.so' | Select-Object Name, Length, @{N='SHA256';E={(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()}} | Format-Table -AutoSize
} finally { Pop-Location }
