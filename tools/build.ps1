<#
  Bright Times - one-step Windows build. Works from any PowerShell window:
  finds CMake inside Visual Studio and the CUDA Toolkit by itself.

  Usage (from the repository root):
    powershell -ExecutionPolicy Bypass -File tools\build.ps1
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Cuda off
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Install   (run as Administrator)

  Options:
    -SdkRoot   After Effects SDK folder
    -Cuda      auto (default: on if the CUDA Toolkit is found) | on | off
    -Install   copy BrightTimes.aex into the Adobe MediaCore plug-in folder
#>
param(
    [string]$SdkRoot = "C:\Users\Velich\AfterEffectsSDK_26.5_win",
    [ValidateSet("auto", "on", "off")][string]$Cuda = "auto",
    [string]$Config = "Release",
    [switch]$Install
)

$ErrorActionPreference = "Stop"
$repo  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo "build"

function Step($msg) { Write-Host "`n== $msg ==" -ForegroundColor Cyan }
function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

# Installers update PATH / CUDA_PATH in the registry; windows opened earlier do
# not see it. Reload both so a fresh terminal is not required.
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
            [Environment]::GetEnvironmentVariable("Path", "User")
$machineCuda = [Environment]::GetEnvironmentVariable("CUDA_PATH", "Machine")
if ($machineCuda) { $env:CUDA_PATH = $machineCuda }

Step "Tools"

# --- CMake: PATH first, then the copy bundled with Visual Studio
$cmake = $null
$cmd = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmd) { $cmake = $cmd.Source }
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPaths = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        foreach ($vs in $vsPaths) {
            $candidate = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $candidate) { $cmake = $candidate; break }
        }
    }
}
if (-not $cmake) {
    Fail ("CMake not found. In Visual Studio Installer -> Modify -> 'Desktop development with C++' " +
          "make sure 'C++ CMake tools for Windows' is checked (or: winget install Kitware.CMake).")
}
Write-Host "  CMake: $cmake"
Write-Host ("  " + (& $cmake --version | Select-Object -First 1))

# --- CUDA Toolkit: CUDA_PATH, then the default install folder (newest version)
$cudaRoot = $null
if ($env:CUDA_PATH -and (Test-Path (Join-Path $env:CUDA_PATH "bin\nvcc.exe"))) { $cudaRoot = $env:CUDA_PATH }
if (-not $cudaRoot) {
    $base = "${env:ProgramFiles}\NVIDIA GPU Computing Toolkit\CUDA"
    if (Test-Path $base) {
        $found = Get-ChildItem $base -Directory |
                 Where-Object { Test-Path (Join-Path $_.FullName "bin\nvcc.exe") } |
                 Sort-Object { [version]($_.Name.TrimStart('v')) } -Descending |
                 Select-Object -First 1
        if ($found) { $cudaRoot = $found.FullName }
    }
}
$useCuda = $false
if ($Cuda -eq "on" -and -not $cudaRoot) { Fail "CUDA Toolkit not found (nvcc.exe). Install it or use -Cuda off." }
if ($Cuda -ne "off" -and $cudaRoot) { $useCuda = $true }
if ($cudaRoot) { Write-Host "  CUDA Toolkit: $cudaRoot" } else { Write-Host "  CUDA Toolkit: not found (building without CUDA; OpenCL + CPU still work)" -ForegroundColor Yellow }

# --- Configure
function Configure([bool]$withCuda) {
    if (Test-Path (Join-Path $build "CMakeCache.txt")) { Remove-Item -Recurse -Force $build }
    $cmakeArgs = @("-S", $repo, "-B", $build, "-A", "x64", "-DAE_SDK_ROOT=$SdkRoot")
    if ($withCuda) {
        # Point Visual Studio at this toolkit directly: works even when the CUDA
        # Visual Studio integration was not installed (e.g. CUDA installed before VS).
        $cmakeArgs += @("-T", "cuda=$cudaRoot", "-DBT_ENABLE_CUDA=ON")
    } else {
        $cmakeArgs += "-DBT_ENABLE_CUDA=OFF"
    }
    Write-Host ("  cmake " + ($cmakeArgs -join " "))
    & $cmake @cmakeArgs | Out-Host   # keep cmake output out of the return value
    return ($LASTEXITCODE -eq 0)
}

Step ("Configure (CUDA " + $(if ($useCuda) { "on" } else { "off" }) + ")")
if (-not (Configure $useCuda)) {
    if ($useCuda -and $Cuda -eq "auto") {
        Write-Host "`nCUDA configuration failed - retrying without CUDA (GPU via OpenCL stays available)." -ForegroundColor Yellow
        Write-Host "Send the error above if you want the CUDA backend." -ForegroundColor Yellow
        $useCuda = $false
        if (-not (Configure $false)) { Fail "CMake configure failed (see above)." }
    } else {
        Fail "CMake configure failed (see above)."
    }
}

# --- Build
Step "Build ($Config)"
& $cmake --build $build --config $Config | Out-Host
if ($LASTEXITCODE -ne 0) { Fail "Build failed (see above)." }

$aex = Join-Path $build "$Config\BrightTimes.aex"
if (-not (Test-Path $aex)) { Fail "Build finished but $aex was not found." }
Write-Host "`nBuilt: $aex" -ForegroundColor Green
Write-Host ("GPU backends: OpenCL" + $(if ($useCuda) { " + CUDA" } else { "" }) + ", CPU fallback")

# --- Install
$target = "${env:ProgramFiles}\Adobe\Common\Plug-ins\7.0\MediaCore\BrightTimes"
if ($Install) {
    Step "Install"
    try {
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item -Force $aex $target
        Write-Host "Installed to $target - restart After Effects: Effect > Bright Times > Bright Times" -ForegroundColor Green
    } catch {
        Fail "Could not copy to $target. Run PowerShell as Administrator, or copy the file manually."
    }
} else {
    Write-Host "`nTo install: copy BrightTimes.aex to $target"
    Write-Host "(or rerun as Administrator with -Install), then restart After Effects."
}
