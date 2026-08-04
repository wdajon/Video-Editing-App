# Installs the Qt version ReelForge is pinned to (see docs/adr/003-qt-acquisition.md).
#
#   .\scripts\install_qt.ps1 -Prefix A:\Qt
#
# Downloads official Qt binaries from download.qt.io via aqtinstall, which needs
# no Qt account and therefore works unattended in CI. The Linux equivalent is the
# same aqt invocation with host "linux" and arch "linux_gcc_64".

param(
    [string]$Prefix = 'A:\Qt',
    [string]$Version = '6.10.3',
    [string]$Arch = 'win64_msvc2022_64'
)

$ErrorActionPreference = 'Stop'

# qtsvg is not listed as a module for 6.10: Qt SVG ships inside the base package.
# Adding it here fails the whole install with "packages were not found".
$modules = @('qtshadertools', 'qtimageformats')

python -m pip install --quiet --upgrade aqtinstall
if ($LASTEXITCODE -ne 0) { throw "pip install aqtinstall failed" }

# aqtinstall's py7zr extraction fails intermittently part-way through (see the
# comment in .github/workflows/ci.yml). A half-extracted tree is not resumable,
# so every attempt starts from a clean directory.
$versionDir = Join-Path $Prefix $Version
for ($attempt = 1; $attempt -le 3; $attempt++) {
    if (Test-Path $versionDir) { Remove-Item -Recurse -Force $versionDir }

    python -m aqt install-qt windows desktop $Version $Arch -m $modules -O $Prefix
    if ($LASTEXITCODE -eq 0) { break }

    if ($attempt -eq 3) { throw "aqt install-qt failed on all 3 attempts" }
    Write-Warning "aqt install-qt failed (attempt $attempt of 3), retrying"
    Start-Sleep -Seconds ($attempt * 15)
}

$qtRoot = Join-Path $Prefix "$Version\msvc2022_64"
if (-not (Test-Path (Join-Path $qtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw "Qt install finished but Qt6Config.cmake is missing under $qtRoot"
}

Write-Host ""
Write-Host "Qt $Version installed to $qtRoot"
Write-Host "Set QT_ROOT before configuring:"
Write-Host "    `$env:QT_ROOT = '$qtRoot'"
