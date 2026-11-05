#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Proves bandsight-cli needs no Qt: a fresh configure with the GUI off and Qt
# hidden from CMake even where it is installed, a build of the CLI and its
# tests, those tests run, and no Qt library in the binary's dependencies.
#
# usage: cli_without_qt.sh <source dir> <scratch build dir>
set -eu
src=$1
work=$2

rm -rf "$work"
mkdir -p "$work"
cmake -S "$src" -B "$work" -DBANDSIGHT_BUILD_GUI=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
      > "$work/configure.log" 2>&1 || { cat "$work/configure.log"; exit 1; }
cmake --build "$work" --parallel --target bandsight-cli test_bandsight_cli test_history_db test_top_terminal \
      > "$work/build.log" 2>&1 || { tail -50 "$work/build.log"; exit 1; }

# No GUI target was even configured.
if [ -e "$work/bandsight" ]; then
    echo "the GUI was built with BANDSIGHT_BUILD_GUI=OFF" >&2
    exit 1
fi
if readelf -d "$work/bandsight-cli" | grep -i 'qt' ; then
    echo "bandsight-cli links Qt" >&2
    exit 1
fi
(cd "$work" && ctest --output-on-failure -R '^(bandsight_cli|history_db|top_terminal)$')
echo "bandsight-cli builds and passes its tests without Qt"
