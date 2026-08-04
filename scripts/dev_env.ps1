# Imports an MSVC x64 developer environment into the current PowerShell session.
#
#   . .\scripts\dev_env.ps1
#   cmake --preset windows-debug
#
# Discovery is done with vswhere rather than a hardcoded path so the script works
# against Build Tools and full Visual Studio installs, and across VS updates.
# Windows only; on Linux/macOS the compilers are already on PATH.

param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = 'Stop'

$programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
$vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at $vswhere. Install Visual Studio 2022 Build Tools with the 'Desktop development with C++' workload."
}

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsPath) {
    throw "No Visual Studio installation with the MSVC x64 toolset was found. Install the 'Desktop development with C++' workload."
}

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat not found at $vcvars."
}

# Run vcvars in a child cmd and copy the resulting environment back out.
& cmd.exe /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        Set-Item -Path ("Env:" + $matches[1]) -Value $matches[2]
    }
}

# Build Tools ships CMake and Ninja but does not put them on PATH.
foreach ($tool in @('CMake\CMake\bin', 'Ninja')) {
    $dir = Join-Path $vsPath ('Common7\IDE\CommonExtensions\Microsoft\' + $tool)
    if ((Test-Path $dir) -and ($env:PATH -notlike "*$dir*")) {
        $env:PATH = $dir + ';' + $env:PATH
    }
}

if (-not $VcpkgRoot) {
    throw "VCPKG_ROOT is not set. Clone https://github.com/microsoft/vcpkg, run bootstrap-vcpkg.bat, then set VCPKG_ROOT or pass -VcpkgRoot."
}
if (-not (Test-Path (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'))) {
    throw "VCPKG_ROOT '$VcpkgRoot' does not look like a vcpkg checkout (scripts\buildsystems\vcpkg.cmake missing)."
}
$env:VCPKG_ROOT = (Resolve-Path $VcpkgRoot).Path

Write-Host "MSVC:   $vsPath"
Write-Host "cmake:  $((Get-Command cmake).Source)"
Write-Host "ninja:  $((Get-Command ninja).Source)"
Write-Host "vcpkg:  $env:VCPKG_ROOT"
