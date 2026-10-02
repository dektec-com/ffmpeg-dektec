#!/bin/sh
#
# Writes the README.txt of a release package: release-readme.sh TAG CDTAPI_TAG FFMPEG,
# where FFMPEG is the package's ffmpeg program, which tells how it was configured.
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

tag=$1
cdtapi=$2
ffmpeg=$3

title="DekTec's FFmpeg $tag"
echo "$title"
echo "$title" | sed 's/./=/g'
cat <<EOF

FFmpeg ${tag%%-dektec*} with DekTec's input and output device, dektec, and the sdi
format, built on CDTAPI $cdtapi, DekTec's open-source C API for its cards.

  bin/ffmpeg, bin/ffprobe, bin/ffplay

The DekTec driver for the card is installed separately:
https://www.dektec.com/downloads/SDK/

  ffmpeg -sources dektec                    lists the cards' ports
  ffplay -f dektec -i <serial>:<port>       shows what an SDI input receives
  ffmpeg -i <file> -f dektec <serial>:<port>  sends a file through an SDI output

On a SMPTE 2110 port, -nmos_registry <url> or -nmos_registry auto registers the streams
with an NMOS registry, so that a controller can connect them; see the dektec device in
ffmpeg-devices.

Licence: FFmpeg is LGPL 2.1 or later (COPYING.LGPLv2.1, LICENSE.md); this build has no
GPL or non-free parts. The licences of the libraries linked in, CDTAPI (BSD-3-Clause),
dtnmos (BSD-3-Clause), libcurl, civetweb and those they use, are in licenses/. The source
code these programs are built from is published beside this package, as
ffmpeg-dektec-$tag-source.tar.xz.

Configuration:
EOF
"$ffmpeg" -hide_banner -buildconf 2>&1 | grep -e '--' | sed 's/^ */  /'
