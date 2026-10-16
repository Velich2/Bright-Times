<#
  Bright Times - one-step Windows build. Works from any PowerShell window:
  finds CMake inside Visual Studio and the CUDA Toolkit by itself.

  Builds the plug-in and installs it into
  C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\BrightTimes
  (asks for administrator rights through the Windows UAC prompt if needed).

  Usage (from the repository root):
    powershell -ExecutionPolicy Bypass -File tools\build.ps1
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Cuda off
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -NoInstall

  Options:
    -SdkRoot    After Effects SDK folder
    -Cuda       auto (default: on if the CUDA Toolkit is found) | on | off
    -NoInstall  only build, do not copy into the Adobe plug-in folder
#>
param(
    [string]$SdkRoot = "C:\Users\Velich\AfterEffectsSDK_26.5_win",
    [ValidateSet("auto", "on", "off")][string]$Cuda = "auto",
    [string]$Config = "Release",
    [switch]$NoInstall,
    [switch]$Install   # kept for compatibility: installing is now the default
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
$target    = "${env:ProgramFiles}\Adobe\Common\Plug-ins\7.0\MediaCore\BrightTimes"
$targetAex = Join-Path $target "BrightTimes.aex"
if ($NoInstall) {
    Write-Host "`nNot installed (-NoInstall). Copy BrightTimes.aex to $target manually."
    exit 0
}

Step "Install"

# A running After Effects keeps the old plug-in file locked.
while (Get-Process -Name "AfterFX" -ErrorAction SilentlyContinue) {
    Write-Host "After Effects is running - it locks the plug-in file." -ForegroundColor Yellow
    $answer = Read-Host "Close After Effects and press Enter (or type s to skip installing)"
    if ($answer -eq "s") {
        Write-Host "Skipped. Copy $aex to $target later." -ForegroundColor Yellow
        exit 0
    }
}

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Quote([string]$s) { return "'" + $s.Replace("'", "''") + "'" }
$copyCmd = "try { New-Item -ItemType Directory -Force -Path $(Quote $target) -ErrorAction Stop | Out-Null; " +
           "Copy-Item -Force -ErrorAction Stop -Path $(Quote $aex) -Destination $(Quote $targetAex); exit 0 } catch { exit 1 }"

if (Test-Admin) {
    & powershell -NoProfile -ExecutionPolicy Bypass -Command $copyCmd | Out-Host
} else {
    Write-Host "Program Files needs administrator rights - confirm the Windows prompt (UAC)."
    try {
        Start-Process powershell -Verb RunAs -Wait -WindowStyle Hidden `
            -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", $copyCmd) | Out-Null
    } catch {
        Write-Host "The administrator prompt was cancelled." -ForegroundColor Yellow
    }
}

# Verify by content, whatever happened above.
$installed = (Test-Path $targetAex) -and
             ((Get-FileHash $aex).Hash -eq (Get-FileHash $targetAex).Hash)
if ($installed) {
    Write-Host "Installed: $targetAex" -ForegroundColor Green
    Write-Host "Start After Effects: Effect > Bright Times > Bright Times"
} else {
    Fail "Could not copy to $target. Close After Effects and run the script again, or copy $aex manually."
}
