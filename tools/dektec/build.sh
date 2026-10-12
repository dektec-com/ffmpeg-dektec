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
  --cdtapi-ref R  with --vcpkg, first build CDTAPI at the branch, tag or commit R of its
                  repository into that tree, through an overlay of the port of DekTec's
                  vcpkg registry, so that FFmpeg is tested on a CDTAPI that is not
                  released yet; with --enable-libcdtapi-nmos, its feature nmos too
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
cdtapi_ref=
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
        --cdtapi-ref) cdtapi_ref=$2; shift 2 ;;
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

# The commit that the ref $1 of the repository at $2 names: the ref itself when it is a
# whole commit, else the one branch or tag of that name, an annotated tag peeled.
resolve_ref() {
    case $1 in
        *[!0-9a-f]*|"") ;;
        *) [ ${#1} -eq 40 ] && { echo "$1"; return; }
           die "'$1' looks like a short commit; give all 40 digits, or a branch or a tag" ;;
    esac
    listed=$(git ls-remote "$2" "$1" "$1^{}") || die "cannot list the refs of $2"
    names=$(echo "$listed" | awk '$2 !~ /\^\{\}$/ { print $2 }' | sort -u)
    [ -n "$names" ] || die "$2 has no branch or tag '$1'"
    [ "$(echo "$names" | wc -l)" -eq 1 ] ||
        die "'$1' names more than one ref of $2: $(echo $names); give the whole name"
    peeled=$(echo "$listed" | awk '$2 ~ /\^\{\}$/ { print $1 }')
    [ -n "$peeled" ] && echo "$peeled" || echo "$listed" | awk '{ print $1; exit }'
}

# Builds CDTAPI at $cdtapi_ref into the vcpkg tree at $vcpkg, for $triplet, as CDTAPI's
# plan 0040 has it before every release, after gst-dektec's plan 0026. The overlay is the
# ports of DekTec's vcpkg registry, with the cdtapi port's vcpkg_from_github, which
# fetches the release, replaced by vcpkg_from_git at the commit. The commit is written
# into the portfile, so vcpkg builds again for another one; CDTAPI is built as a release
# is, with the same options, install and package. The overlays are made in
# build/vcpkg-overlays and are not kept.
build_cdtapi_ref() {
    registry=https://github.com/dektec-com/dektec-vcpkg-registry.git
    overlay=$src/build/vcpkg-overlays
    rm -rf "$overlay"
    git clone -q --depth 1 "$registry" "$overlay/registry" ||
        die "cannot fetch DekTec's vcpkg registry"
    portfile=$overlay/registry/ports/cdtapi/portfile.cmake
    repo=$(sed -n 's/^ *REPO \([^ )]*\).*/\1/p' "$portfile")
    [ "$(grep -c '^vcpkg_from_github(' "$portfile")" -eq 1 ] && [ -n "$repo" ] &&
        grep -q 'REF "v${VERSION}"' "$portfile" ||
        die "the registry's cdtapi port does not fetch one release with vcpkg_from_github;" \
            "the overlay cannot replace its source"
    url=https://github.com/$repo
    commit=$(resolve_ref "$cdtapi_ref" "$url")
    awk -v url="$url" -v commit="$commit" -v ref="$cdtapi_ref" '
        /^vcpkg_from_github\(/ {
            print "## Made by tools/dektec/build.sh of ffmpeg-dektec: built from " ref
            print "## at commit " commit " instead of from its release."
            print "vcpkg_from_git("
            print "  OUT_SOURCE_PATH SOURCE_PATH"
            print "  URL " url
            print "  REF " commit ")"
            skip = 1
        }
        skip { if (index($0, ")")) skip = 0; next }
        { print }' "$portfile" > "$portfile.new" && mv "$portfile.new" "$portfile"
    echo "CDTAPI from $url at $commit ($cdtapi_ref)"

    port=cdtapi
    case " $* " in
        *" --enable-libcdtapi-nmos "*) port="cdtapi[nmos]" ;;
    esac
    # An overlay of a version that is installed already is not built again: it goes.
    "$vcpkg/vcpkg" remove "cdtapi:$triplet" --recurse >/dev/null 2>&1 || true
    ports=$overlay/registry/ports
    [ $platform = linux ] || ports=$(cygpath -m "$ports")
    "$vcpkg/vcpkg" install "$port:$triplet" --overlay-ports="$ports" ||
        die "vcpkg could not build $port at $cdtapi_ref"
}

# Where cdtapi.pc is.
if [ -n "$cdtapi_ref" ] && [ -z "$vcpkg" ]; then
    die "--cdtapi-ref builds CDTAPI into a vcpkg tree; give it with --vcpkg"
fi
if [ -n "$vcpkg" ]; then
    [ -n "$cdtapi" ] && die "give --cdtapi or --vcpkg, not both"
    if [ -z "$triplet" ]; then
        case $platform-$crt in
            windows-md) triplet=x64-windows ;;
            windows-mt) triplet=x64-windows-static ;;
            *)          triplet=x64-linux ;;
        esac
    fi
    [ $platform = linux ] || vcpkg=$(cygpath -u "$vcpkg")
    [ -z "$cdtapi_ref" ] || build_cdtapi_ref "$@"
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
