<#
  Bright Times - checks that the After Effects SDK and the build tools are ready.

  Usage (PowerShell, from the repository root):
    powershell -ExecutionPolicy Bypass -File tools\check_sdk.ps1 -SdkRoot "C:\Users\Velich\AfterEffectsSDK_26.5_win"
#>
param(
    [string]$SdkRoot = "C:\Users\Velich\AfterEffectsSDK_26.5_win"
)

$ok = $true
function Good($msg) { Write-Host "  [OK]      $msg" -ForegroundColor Green }
function Bad($msg)  { Write-Host "  [MISSING] $msg" -ForegroundColor Red; $script:ok = $false }
function Warn($msg) { Write-Host "  [WARN]    $msg" -ForegroundColor Yellow }

Write-Host "`n== After Effects SDK ==" -ForegroundColor Cyan
if (-not (Test-Path $SdkRoot)) {
    Bad "folder not found: $SdkRoot"
    exit 1
}

# Find <...>/Examples/Headers/AE_Effect.h (the zip may be nested)
$examples = $null
$candidates = @(Join-Path $SdkRoot "Examples") + @(Get-ChildItem -Path $SdkRoot -Directory -Recurse -Depth 2 -Filter "Examples" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
foreach ($c in $candidates) {
    if (Test-Path (Join-Path $c "Headers\AE_Effect.h")) { $examples = $c; break }
}
if (-not $examples) {
    Bad "Examples\Headers\AE_Effect.h not found under $SdkRoot"
    exit 1
}
Good "Examples folder: $examples"
if ($examples -ne (Join-Path $SdkRoot "Examples")) {
    Warn "SDK is nested: CMake finds it automatically, or pass -DAE_SDK_ROOT=`"$(Split-Path $examples -Parent)`""
}

$files = @(
    "Headers\A.h", "Headers\AEConfig.h", "Headers\AE_Effect.h", "Headers\AE_EffectCB.h",
    "Headers\AE_EffectCBSuites.h", "Headers\AE_EffectSuites.h", "Headers\AE_EffectPixelFormat.h",
    "Headers\AE_EffectGPUSuites.h", "Headers\AE_EffectVers.h", "Headers\AE_Macros.h",
    "Headers\AE_PluginData.h", "Headers\SP\SPBasic.h",
    "Util\entry.h", "Util\Param_Utils.h",
    "Resources\PiPLtool.exe"
)
foreach ($f in $files) {
    if (Test-Path (Join-Path $examples $f)) { Good $f } else { Bad $f }
}

$symbols = @(
    @("Headers\AE_PluginData.h",       "PF_REGISTER_EFFECT_EXT2"),
    @("Headers\AE_Effect.h",           "PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT"),
    @("Headers\AE_Effect.h",           "PF_OutFlag2_SUPPORTS_GPU_RENDER_F32"),
    @("Headers\AE_Effect.h",           "PF_OutFlag2_SUPPORTS_THREADED_RENDERING"),
    @("Headers\AE_EffectGPUSuites.h",  "PF_GPUDeviceSuite1"),
    @("Headers\AE_EffectPixelFormat.h","PF_PixelFormat_GPU_BGRA128"),
    @("Util\Param_Utils.h",            "PF_ADD_POPUPX"),
    @("Util\Param_Utils.h",            "PF_ADD_FLOAT_SLIDERX"),
    @("Util\Param_Utils.h",            "PF_ADD_CHECKBOXX")
)
foreach ($s in $symbols) {
    $path = Join-Path $examples $s[0]
    if ((Test-Path $path) -and (Select-String -Path $path -Pattern $s[1] -SimpleMatch -Quiet)) {
        Good "$($s[1])"
    } else {
        Bad "$($s[1]) (expected in $($s[0]))"
    }
}
$ver = Select-String -Path (Join-Path $examples "Headers\AE_Effect.h") -Pattern "#define\s+PF_PLUG_IN_(VERSION|SUBVERS)\s+(\d+)" |
       ForEach-Object { $_.Matches[0].Groups[2].Value }
if ($ver) { Write-Host "  API version: $($ver -join '.')" }

Write-Host "`n== Build tools ==" -ForegroundColor Cyan
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmake) { Good ("cmake: " + (& cmake --version | Select-Object -First 1)) } else { Bad "cmake (install CMake 3.21+ or use the one bundled with Visual Studio)" }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName
    if ($vs) { Good "Visual Studio C++: $vs" } else { Bad "Visual Studio with 'Desktop development with C++'" }
} else {
    Bad "Visual Studio 2022 (Desktop development with C++)"
}

# GPU backends (optional)
$nvcc = Get-Command nvcc -ErrorAction SilentlyContinue
if ($nvcc) { Good "CUDA Toolkit (nvcc) - build with -DBT_ENABLE_CUDA=ON" } else { Warn "CUDA Toolkit not found - CUDA backend off (OpenCL still works on NVIDIA)" }

$clHeader = $null
foreach ($root in @($env:CUDA_PATH, $env:OCL_ROOT, $env:INTELOCLSDKROOT) | Where-Object { $_ }) {
    if (Test-Path (Join-Path $root "include\CL\cl.h")) { $clHeader = $root; break }
}
if ($clHeader) { Good "OpenCL headers/lib: $clHeader" } else { Warn "OpenCL SDK not found (CUDA Toolkit or Khronos OpenCL-SDK). Without it build with -DBT_ENABLE_OPENCL=OFF" }

Write-Host ""
if ($ok) {
    Write-Host "Everything required is in place." -ForegroundColor Green
    exit 0
} else {
    Write-Host "Some required items are missing (see [MISSING] above)." -ForegroundColor Red
    exit 1
}
