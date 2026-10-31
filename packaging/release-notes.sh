#!/bin/sh
# Prints the CHANGELOG.md section for one version, for the GitHub release text.
#
#   release-notes.sh <version>      e.g. 0.3.0
#
# Fails when the version has no section, or an empty one: a release must not go
# out without saying what it added and removed.
set -eu
VERSION=$1
CHANGELOG=${2:-$(dirname "$0")/../CHANGELOG.md}

NOTES=$(awk -v v="$VERSION" '
    /^## \[/ { if (on) exit; on = index($0, "## [" v "]") == 1; next }
    on' "$CHANGELOG" | sed -e '/./,$!d')   # drop leading blank lines

if [ -z "$(printf '%s' "$NOTES" | tr -d '[:space:]')" ]; then
    echo "release-notes: CHANGELOG.md has no section for $VERSION" >&2
    exit 1
fi
printf '%s\n' "$NOTES"
