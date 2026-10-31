#!/bin/sh
# Adds .deb files to a signed APT repository tree, served as-is by GitHub Pages.
#
#   publish.sh <deb-dir> <site-dir> <gpg-key-id>
#
# Every version stays in the pool, so `apt install bandsight=<version>` can roll
# back. Re-running for the same version replaces that file rather than adding a
# second copy. Signs with the key in the caller's GNUPGHOME.
set -eu

DEBS=$1
SITE=$2
KEY=$3
SUITE=stable
COMP=main
ARCH=amd64
URL=https://isuru-rana.github.io/bandsight

POOL="$SITE/pool/$COMP/b/bandsight"
DIST="$SITE/dists/$SUITE"
BIN="$DIST/$COMP/binary-$ARCH"
mkdir -p "$POOL" "$BIN"
cp -f "$DEBS"/*.deb "$POOL/"

# Paths in Packages are relative to the repository root, so scan from there.
# --multiversion keeps every version listed, not only the newest.
(cd "$SITE" && dpkg-scanpackages --multiversion "pool" /dev/null) > "$BIN/Packages" 2>/dev/null
gzip -9nc "$BIN/Packages" > "$BIN/Packages.gz"

# Release lists every index with its size and hashes; apt refuses an index
# whose hash does not match, so these are computed from the files just written.
hashes() {   # tool
    for f in "$COMP/binary-$ARCH/Packages" "$COMP/binary-$ARCH/Packages.gz"; do
        printf ' %s %16d %s\n' "$(cd "$DIST" && "$1" "$f" | cut -d' ' -f1)" \
            "$(wc -c < "$DIST/$f")" "$f"
    done
}
{
    echo "Origin: Bandsight"
    echo "Label: Bandsight"
    echo "Suite: $SUITE"
    echo "Codename: $SUITE"
    echo "Date: $(LC_ALL=C date -Ru)"
    echo "Architectures: $ARCH"
    echo "Components: $COMP"
    echo "Description: Bandsight network usage monitor"
    echo "MD5Sum:"
    hashes md5sum
    echo "SHA256:"
    hashes sha256sum
} > "$DIST/Release"

rm -f "$DIST/InRelease" "$DIST/Release.gpg"
gpg --batch --yes --pinentry-mode loopback --passphrase '' --local-user "$KEY" \
    --clearsign -o "$DIST/InRelease" "$DIST/Release"
gpg --batch --yes --pinentry-mode loopback --passphrase '' --local-user "$KEY" \
    --armor --detach-sign -o "$DIST/Release.gpg" "$DIST/Release"
gpg --batch --yes --armor --export "$KEY" > "$SITE/key.gpg"

# GitHub Pages would otherwise run the tree through Jekyll.
touch "$SITE/.nojekyll"
cat > "$SITE/index.html" <<HTML
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Bandsight APT repository</title></head>
<body>
<h1>Bandsight APT repository</h1>
<p>Debian 12+ and Ubuntu 24.04+, amd64. Add the source once:</p>
<pre>curl -fsSL $URL/key.gpg | sudo gpg --dearmor -o /usr/share/keyrings/bandsight.gpg
echo "deb [signed-by=/usr/share/keyrings/bandsight.gpg] $URL $SUITE $COMP" | sudo tee /etc/apt/sources.list.d/bandsight.list
sudo apt update &amp;&amp; sudo apt install bandsight</pre>
<p>Updates then arrive with <code>sudo apt update &amp;&amp; sudo apt upgrade</code>.
Source: <a href="https://github.com/Isuru-rana/bandsight">github.com/Isuru-rana/bandsight</a></p>
</body></html>
HTML
