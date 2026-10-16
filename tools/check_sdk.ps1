<#
  Bright Times - checks that the After Effects SDK and the build tools are ready.

  Usage (PowerShell, from the repository root):
    powershell -ExecutionPolicy Bypass -File tools\check_sdk.ps1 -SdkRoot "C:\Users\Velich\AfterEffectsSDK_26.5_win"
#>
param(
    [string]$SdkRoot = "C:\Users\Velich\AfterEffectsSDK_26.5_win"
)

$ok = $true

# Installers update PATH / CUDA_PATH in the registry; reload them so a window
# opened before the installation still sees the new tools.
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
            [Environment]::GetEnvironmentVariable("Path", "User")
$machineCuda = [Environment]::GetEnvironmentVariable("CUDA_PATH", "Machine")
if ($machineCuda) { $env:CUDA_PATH = $machineCuda }
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
    "Headers\AE_PluginData.h", "Headers\PrSDKAESupport.h", "Headers\SP\SPBasic.h",
    "Util\entry.h", "Util\Param_Utils.h",
    "Resources\PiPLtool.exe"
)
foreach ($f in $files) {
    if (Test-Path (Join-Path $examples $f)) { Good $f } else { Bad $f }
}

# API used by the plug-in. Searched in every SDK header (declarations move
# between files across SDK releases); the file where each one lives is shown.
$symbols = @(
    "PF_PluginDataCB2",
    "PF_Cmd_SMART_RENDER_GPU",
    "EffectWantsCheckedOutFramesToMatchRenderPixelFormat",
    "PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT",
    "PF_OutFlag2_SUPPORTS_GPU_RENDER_F32",
    "PF_OutFlag2_SUPPORTS_THREADED_RENDERING",
    "PF_GPUDeviceSuite1",
    "PF_PixelFormat_GPU_BGRA128",
    "PF_ADD_POPUPX",
    "PF_ADD_FLOAT_SLIDERX",
    "PF_ADD_CHECKBOXX"
)
$headers = @(Get-ChildItem -Path (Join-Path $examples "Headers"), (Join-Path $examples "Util") -Recurse -Filter "*.h" -ErrorAction SilentlyContinue)
foreach ($sym in $symbols) {
    $hit = $headers | Select-String -Pattern $sym -SimpleMatch -CaseSensitive -List | Select-Object -First 1
    if ($hit) { Good "$sym  ($($hit.Path.Substring($examples.Length + 1)))" } else { Bad "$sym (not found in SDK headers)" }
}
# Informational: optional registration macro (the plug-in does not need it).
$reg = $headers | Select-String -Pattern "PF_REGISTER_EFFECT_EXT2" -SimpleMatch -CaseSensitive -List | Select-Object -First 1
if ($reg) { Write-Host "  [INFO]    PF_REGISTER_EFFECT_EXT2 in $($reg.Path.Substring($examples.Length + 1))" }
else      { Write-Host "  [INFO]    PF_REGISTER_EFFECT_EXT2 not in this SDK - not needed, the plug-in registers directly" }
$ver = Select-String -Path (Join-Path $examples "Headers\AE_Effect.h") -Pattern "#define\s+PF_PLUG_IN_(VERSION|SUBVERS)\s+(\d+)" |
       ForEach-Object { $_.Matches[0].Groups[2].Value }
if ($ver) { Write-Host "  API version: $($ver -join '.')" }

Write-Host "`n== Build tools ==" -ForegroundColor Cyan
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$cmake = $null
$cmd = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmd) { $cmake = $cmd.Source }
if (-not $cmake -and (Test-Path $vswhere)) {
    foreach ($vsPath in (& $vswhere -all -products * -property installationPath)) {
        $candidate = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $candidate) { $cmake = $candidate; break }
    }
}
if ($cmake) { Good ("cmake: " + (& $cmake --version | Select-Object -First 1) + "  ($cmake)") }
else { Bad "cmake (Visual Studio Installer -> Modify -> check 'C++ CMake tools for Windows')" }

if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName
    if ($vs) { Good "Visual Studio C++: $vs" } else { Bad "Visual Studio with 'Desktop development with C++'" }
} else {
    Bad "Visual Studio 2022/2026 (Desktop development with C++)"
}

# GPU backends (optional)
$nvcc = $null
if ($env:CUDA_PATH -and (Test-Path (Join-Path $env:CUDA_PATH "bin\nvcc.exe"))) { $nvcc = Join-Path $env:CUDA_PATH "bin\nvcc.exe" }
if (-not $nvcc) {
    $cudaBase = "${env:ProgramFiles}\NVIDIA GPU Computing Toolkit\CUDA"
    if (Test-Path $cudaBase) {
        $hit = Get-ChildItem $cudaBase -Directory | ForEach-Object { Join-Path $_.FullName "bin\nvcc.exe" } |
               Where-Object { Test-Path $_ } | Select-Object -Last 1
        if ($hit) { $nvcc = $hit }
    }
}
if ($nvcc) {
    $nvccVer = (& $nvcc --version | Select-String "release") -replace ".*release ", ""
    Good "CUDA Toolkit: $nvccVer  ($nvcc)"
} else {
    Warn "CUDA Toolkit not found - CUDA backend off (OpenCL still works on NVIDIA)"
}

# OpenCL needs no SDK: the plug-in loads OpenCL.dll from the GPU driver at runtime.
if (Test-Path "$env:SystemRoot\System32\OpenCL.dll") { Good "OpenCL runtime (OpenCL.dll from the GPU driver)" } else { Warn "OpenCL.dll not found - OpenCL GPU path unavailable on this machine (CPU still works)" }

Write-Host ""
if ($ok) {
    Write-Host "Everything required is in place." -ForegroundColor Green
    exit 0
} else {
    Write-Host "Some required items are missing (see [MISSING] above)." -ForegroundColor Red
    exit 1
}
