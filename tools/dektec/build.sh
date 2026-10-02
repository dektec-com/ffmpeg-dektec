#!/bin/sh
#
# Builds FFmpeg with DekTec's devices against CDTAPI, on Linux and, from build.ps1, in
# MSYS2 with MSVC. CDTAPI comes from an installed prefix or a vcpkg tree; configure
# finds it through pkg-config.
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

set -e

usage() {
    cat <<EOF
Usage: $0 [options] [-- configure options]

Builds FFmpeg with the dektec input and output device and the sdi format.

  --cdtapi DIR    CDTAPI installed under DIR, which holds lib/pkgconfig/cdtapi.pc,
                  or Lib/pkgconfig/cdtapi.pc in the DekTec SDK's layout
  --vcpkg DIR     CDTAPI from the vcpkg tree at DIR, for the triplet below
  --triplet T     vcpkg triplet; x64-linux, or on Windows x64-windows with --crt md
                  and x64-windows-static with --crt mt
  --crt md|mt     on Windows, the C runtime: md, the DLL, by default, or mt, the static
                  one; CDTAPI must have been built with the same
  --build DIR     build directory; build/<platform>[-shared] by default
  --prefix DIR    where 'make install' puts FFmpeg; <build>/install by default
  --shared        build shared libraries instead of static ones
  --fate          run FATE's sdi tests and the device's after the build
  --install       run 'make install' after the build
  -j N            parallel jobs; the number of processors by default
  -h, --help      this text

Everything after -- goes to configure as it is.
EOF
}

die() {
    echo "$0: $*" >&2
    exit 1
}

src=$(cd "$(dirname "$0")/../.." && pwd)
case $(uname -s) in
    MINGW*|MSYS*) platform=windows ;;
    Linux)        platform=linux ;;
    *)            die "unsupported platform $(uname -s)" ;;
esac

cdtapi=
vcpkg=
triplet=
build=
prefix=
shared=no
fate=no
install=no
crt=md
jobs=$(nproc 2>/dev/null || echo 4)

while [ $# -gt 0 ]; do
    case $1 in
        --cdtapi)  cdtapi=$2; shift 2 ;;
        --vcpkg)   vcpkg=$2; shift 2 ;;
        --triplet) triplet=$2; shift 2 ;;
        --build)   build=$2; shift 2 ;;
        --prefix)  prefix=$2; shift 2 ;;
        --shared)  shared=yes; shift ;;
        --fate)    fate=yes; shift ;;
        --install) install=yes; shift ;;
        --crt)     crt=$2; shift 2 ;;
        -j)        jobs=$2; shift 2 ;;
        -j*)       jobs=${1#-j}; shift ;;
        -h|--help) usage; exit 0 ;;
        --)        shift; break ;;
        *)         usage >&2; die "unknown option $1" ;;
    esac
done

# Where cdtapi.pc is.
if [ -n "$vcpkg" ]; then
    [ -n "$cdtapi" ] && die "give --cdtapi or --vcpkg, not both"
    if [ -z "$triplet" ]; then
        case $platform-$crt in
            windows-md) triplet=x64-windows ;;
            windows-mt) triplet=x64-windows-static ;;
            *)          triplet=x64-linux ;;
        esac
    fi
    cdtapi=$vcpkg/installed/$triplet
fi
[ -n "$cdtapi" ] || die "give --cdtapi or --vcpkg; see --help"
[ $platform = linux ] || cdtapi=$(cygpath -u "$cdtapi")
pcdir=
for dir in "$cdtapi/lib/pkgconfig" "$cdtapi/Lib/pkgconfig" "$cdtapi/lib64/pkgconfig"; do
    if [ -f "$dir/cdtapi.pc" ]; then
        pcdir=$dir
        break
    fi
done
[ -n "$pcdir" ] || die "no cdtapi.pc under $cdtapi"
export PKG_CONFIG_PATH="$pcdir${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
echo "CDTAPI $(pkg-config --modversion cdtapi) from $pcdir"

if [ -z "$build" ]; then
    build=$src/build/$platform
    [ $shared = no ] || build=$build-shared
fi
[ -n "$prefix" ] || prefix=$build/install
mkdir -p "$build"
build=$(cd "$build" && pwd)

set -- --prefix="$prefix" --enable-libcdtapi "$@"
# A static CDTAPI names the system libraries it needs in Libs.private, which a shared
# libavdevice needs as much as a program does.
set -- --pkg-config-flags=--static "$@"
[ $shared = no ] || set -- --enable-shared --disable-static "$@"
if [ $platform = windows ]; then
    command -v cl >/dev/null 2>&1 ||
        die "cl is not on the path; run build.ps1, which sets up MSVC first"
    case $crt in
        md) set -- --toolchain=msvc --extra-cflags=-MD "$@" ;;
        mt) set -- --toolchain=msvc --extra-cflags=-MT "$@" ;;
        *)  die "--crt takes md or mt" ;;
    esac
fi

cd "$build"
echo "configure $*"
"$src/configure" "$@"
make -j"$jobs"
if [ $fate = yes ]; then
    # FATE runs the programs from the build tree: shared, they take the libraries next to
    # them, not an FFmpeg the system has installed.
    if [ $shared = yes ]; then
        libs=
        for lib in libavutil libswresample libswscale libavcodec libavformat \
                   libavfilter libavdevice; do
            libs=$libs${libs:+:}$build/$lib
        done
        export LD_LIBRARY_PATH="$libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        export PATH="$libs:$PATH"
    fi
    # On Windows a CDTAPI built as a DLL, and what it links, such as dtnmos with the NMOS
    # bridge, are found on the path, in the prefix's bin.
    if [ $platform = windows ] && [ -d "$cdtapi/bin" ]; then
        export PATH="$cdtapi/bin:$PATH"
    fi
    make -k fate-sdi fate-dektec
fi
[ $install = no ] || make install
