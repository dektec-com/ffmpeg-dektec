# Builds FFmpeg with DekTec's devices on Windows: sets up MSVC's x64 tools, as a
# "x64 Native Tools" prompt does, and runs build.sh in MSYS2's bash with them on the
# path. The arguments are build.sh's; see build.sh --help. PowerShell takes a bare --
# for itself, so quote the one before configure's options:
#
#     tools\dektec\build.ps1 --cdtapi C:\cdtapi --fate '--' --disable-doc
#
# Needs Visual Studio 2019 or later with the C++ tools, MSYS2 with make, diffutils and
# pkgconf (pacman -S make diffutils pkgconf), and nasm on the path.
#
# This file is part of FFmpeg.
#
# FFmpeg is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# FFmpeg is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with FFmpeg; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

$ErrorActionPreference = "Stop"

function Fail([string]$Message) {
    Write-Error "build.ps1: $Message"
    exit 1
}

# MSYS2: MSYS2_ROOT, or where its installer puts it.
$Msys = $env:MSYS2_ROOT
if (-not $Msys) { $Msys = "C:\msys64" }
$Bash = Join-Path $Msys "usr\bin\bash.exe"
if (-not (Test-Path $Bash)) { Fail "no MSYS2 at $Msys; set MSYS2_ROOT" }

# MSVC's x64 environment, taken from vcvars64.bat unless cl is already on the path.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $VsWhere)) { Fail "no Visual Studio found (vswhere.exe)" }
    $Vs = & $VsWhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $Vs) { Fail "no Visual Studio with the C++ x64 tools" }
    $VcVars = Join-Path $Vs "VC\Auxiliary\Build\vcvars64.bat"
    foreach ($Line in (& cmd.exe /c "`"$VcVars`" >nul && set")) {
        if ($Line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2])
        }
    }
}
if (-not (Get-Command nasm.exe -ErrorAction SilentlyContinue)) {
    # Where NASM's installer puts it, for all users or for one.
    $Nasm = @((Join-Path $env:ProgramFiles "NASM"), (Join-Path $env:LOCALAPPDATA "bin\NASM")) |
        Where-Object { Test-Path (Join-Path $_ "nasm.exe") } | Select-Object -First 1
    if ($Nasm) { $env:PATH = "$Nasm;$env:PATH" }
    else { Fail "nasm is not on the path" }
}

# MSYS2's bash with the Windows path after its own, so that it finds cl, link and nasm;
# its own link.exe, a different program, comes after MSVC's.
$env:MSYS2_PATH_TYPE = "inherit"
$env:MSYSTEM = "MSYS"
$env:CHERE_INVOKING = "1"
$Script = (Join-Path $PSScriptRoot "build.sh") -replace '\\', '/'
& $Bash -lc "PATH=`"`$(cygpath -u '$(Split-Path (Get-Command cl.exe).Source)'):`$PATH`" exec sh '$Script' `"`$@`"" build.sh @args
exit $LASTEXITCODE
