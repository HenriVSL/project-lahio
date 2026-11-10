#!/bin/sh
# package.sh - build both release packages of Project Lähiö from one staged folder (user rule: every release ships an
# installer AND a portable zip).
#   dist/package.sh <version> [file-tag] [setup-version]
#     release:  dist/package.sh 1.2
#     RC:       dist/package.sh "1.2 RC2" 1.2-rc2 1.1.99.2
#   <version> = GAME_VERSION (checked in lahio.exe). Setup98 needs a numeric version (1-4 numbers); an RC uses one just
#   below its release (1.2 RC2 = 1.1.99.2) so the final 1.2 setup upgrades an RC install in place.
# Needs a fresh VC6 lahio.exe (vmb/w98 build lahio) and the art sheets. Output:
#   dist/out/lahio-<tag>.zip          portable: LAHIO\ folder, unzip anywhere, run lahio.exe
#   dist/out/lahio-<tag>-setup.exe    Setup98 installer (manifest dist/lahio-setup.ini; kit dev/setup98, owner 86box-d1)
# and copies both to Shared/release/ (= M:\release).
set -e
VER="$1"; TAG="${2:-$1}"; SVER="${3:-$1}"
[ -n "$VER" ] || { echo "usage: dist/package.sh <version> [file-tag]"; exit 2; }
cd "$(dirname "$0")/.."
ROOT=$(pwd)
ST=dist/LAHIO
OUT=dist/out
PACK=~/Documents/86box/Shared/dev/setup98/tools/pack98
REL=~/Documents/86box/Shared/release

strings lahio.exe | grep -q "Version $VER" || { echo "lahio.exe is not version $VER (GAME_VERSION in src/common.h?)"; exit 1; }
rm -rf "$ST"; mkdir -p "$ST/art" "$ST/sfx" "$OUT"
cp lahio.exe "$ST/"
cp dist/README.TXT "$ST/README.TXT"
# the art sheets the game loads (both headers name them), summer + winter pages
for f in $(grep -ohE 'art\\\\[A-Za-z0-9_]+\.bmp' art/art_world.h art/art_ui.h | sed 's/art\\\\//' | sort -u); do
    cp "art/$f" "$ST/art/"
done
cp sfx/*.wav "$ST/sfx/"
echo "staged: $(ls $ST/art | wc -l | tr -d ' ') art sheets, $(ls $ST/sfx | wc -l | tr -d ' ') sounds"

# portable zip
rm -f "$OUT/lahio-$TAG.zip"
(cd dist && zip -qr "out/lahio-$TAG.zip" LAHIO)

# installer: version + output name into a temporary copy of the manifest (same folder: its paths are relative)
M=dist/.setup-build.ini
sed -e "s/^Version=.*/Version=$SVER/" -e "s|^OutFile=.*|OutFile=out/lahio-$TAG-setup.exe|" dist/lahio-setup.ini > "$M"
"$PACK" -check "$M" > /dev/null
"$PACK" -q "$M"
"$PACK" -t "$OUT/lahio-$TAG-setup.exe"
rm -f "$M"

cp "$OUT/lahio-$TAG.zip" "$OUT/lahio-$TAG-setup.exe" "$REL/"
ls -l "$OUT/lahio-$TAG.zip" "$OUT/lahio-$TAG-setup.exe"
echo "copied to $REL"
