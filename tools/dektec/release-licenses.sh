#!/bin/sh
#
# Copies the licences of the libraries a release links in from a vcpkg tree into a
# directory of the release package: release-licenses.sh INSTALLED DIR, where INSTALLED
# is the tree's installed/<triplet>. Each package's share/<package>/copyright becomes
# DIR/<package>.txt; vcpkg's own helper packages are left out.
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

installed=$1
dir=$2

mkdir -p "$dir"
for licence in "$installed"/share/*/copyright; do
    [ -f "$licence" ] || continue
    package=$(basename "$(dirname "$licence")")
    case $package in
        vcpkg-*) continue ;;
    esac
    cp "$licence" "$dir/$package.txt"
done
ls "$dir"
