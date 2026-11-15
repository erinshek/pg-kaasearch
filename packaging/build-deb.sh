#!/bin/sh
# Build a .deb package of pg_kaasearch for one PostgreSQL major version.
#
#   packaging/build-deb.sh PG_MAJOR VERSION [OUTDIR]
#   packaging/build-deb.sh 17 1.0.0~rc1 dist
#
# Needs postgresql-server-dev-PG_MAJOR, make, a C compiler and dpkg-deb. The package is named
# postgresql-PG_MAJOR-kaasearch and depends on postgresql-PG_MAJOR.
#
# The package version keeps the "~" of a pre-release, so that 1.0.0~rc1 sorts before 1.0.0, but the
# file name has none: GitHub replaces "~" in the names of release files.
#   version  1.0.0~rc1-1.noble   file  postgresql-17-kaasearch_1.0.0-rc1-1.noble_amd64.deb
set -eu

PG=$1
VERSION=$2
OUT=${3:-dist}

PG_CONFIG=/usr/lib/postgresql/$PG/bin/pg_config
NAME=postgresql-$PG-kaasearch
ARCH=$(dpkg --print-architecture)
CODENAME=$(. /etc/os-release && echo "$VERSION_CODENAME")
DEBVER=$VERSION-1.$CODENAME
FILEVER=$(echo "$DEBVER" | tr '~' '-')
ROOT=$(mktemp -d)

# with_llvm=no: no JIT bitcode, so clang is not needed
make clean PG_CONFIG="$PG_CONFIG" >/dev/null
make PG_CONFIG="$PG_CONFIG" with_llvm=no
make install PG_CONFIG="$PG_CONFIG" with_llvm=no DESTDIR="$ROOT"

chmod 755 "$ROOT"
find "$ROOT" -name "*.so" -exec strip --strip-unneeded {} +
mkdir -p "$ROOT/DEBIAN" "$ROOT/usr/share/doc/$NAME"
cp LICENSE "$ROOT/usr/share/doc/$NAME/copyright"
cat > "$ROOT/DEBIAN/control" <<CONTROL
Package: $NAME
Version: $DEBVER
Architecture: $ARCH
Maintainer: pg_kaasearch <https://github.com/erinshek/pg-kaasearch>
Depends: postgresql-$PG, libc6
Section: database
Priority: optional
Homepage: https://github.com/erinshek/pg-kaasearch
Description: Full-text search for the Karakalpak language in PostgreSQL $PG
 PostgreSQL extension pg_kaasearch: a text search configuration "kaasearch" that
 finds every form of a Karakalpak word, searches Latin and Cyrillic text alike
 and reads the apostrophe spelling.
CONTROL

mkdir -p "$OUT"
dpkg-deb --build --root-owner-group "$ROOT" "$OUT/${NAME}_${FILEVER}_${ARCH}.deb"
rm -rf "$ROOT"
