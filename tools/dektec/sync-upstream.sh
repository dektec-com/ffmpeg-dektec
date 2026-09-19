#!/bin/sh
#
# Rebases DekTec's commits onto FFmpeg's newest release, in a branch of its own:
# sync/<tag>. With --series same, the default, that is the newest release of the major
# version the branch stands on; with --series newest, the newest of all. The branch it
# starts from, dektec by default, stays as it is; adopting the result is a force push
# of it, which a person does.
#
# Prints what it did on its last line, for a caller to act on:
#   up-to-date <base>                   no release newer than the base
#   rebased <base> <tag> <branch>       the branch holds the commits on the new release
#   conflict <base> <tag> <commit>      the commit named did not apply; nothing is left
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

series=same
if [ "$1" = --series ]; then
    series=$2
    shift 2
fi
branch=${1:-dektec}
case $series in
    same|newest) ;;
    *) echo "$0: --series takes same or newest" >&2; exit 1 ;;
esac
upstream=${UPSTREAM_URL:-https://github.com/FFmpeg/FFmpeg.git}

die() {
    echo "$0: $*" >&2
    exit 1
}

git rev-parse --verify -q "$branch" >/dev/null || die "no branch $branch"
git fetch -q --no-tags "$upstream" "+refs/tags/n*:refs/tags/n*" ||
    die "cannot fetch FFmpeg's tags from $upstream"

# FFmpeg's releases are tagged nX.Y or nX.Y.Z; -dev tags and DekTec's own are not.
releases() {
    git tag -l 'n*' | grep -E '^n[0-9]+\.[0-9]+(\.[0-9]+)?$' | sort -V
}
base=$(git describe --tags --abbrev=0 "$branch" --match 'n[0-9]*' --exclude '*-*' 2>/dev/null) ||
    die "$branch stands on no release of FFmpeg"
if [ $series = same ]; then
    major=${base%%.*}
    newest=$(releases | grep "^$major\." | tail -n 1)
else
    newest=$(releases | tail -n 1)
fi

if [ "$(printf '%s\n%s\n' "$base" "$newest" | sort -V | tail -n 1)" = "$base" ]; then
    echo "up-to-date $base"
    exit 0
fi

sync=sync/$newest
git branch -f "$sync" "$branch"
if git rebase -q --onto "$newest" "$base" "$sync" >/dev/null 2>&1; then
    echo "rebased $base $newest $sync"
else
    commit=$(git rev-parse --short REBASE_HEAD 2>/dev/null || echo unknown)
    git rebase --abort
    git checkout -q "$branch"
    git branch -D -q "$sync"
    echo "conflict $base $newest $commit"
fi
