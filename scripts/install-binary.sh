#!/bin/sh
set -eu

PREFIX=${PREFIX:-/usr/local}
BINDIR=${BINDIR:-"$PREFIX/bin"}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ ! -x "$SCRIPT_DIR/bin/MiniDB" ]; then
    echo "Error: expected executable at $SCRIPT_DIR/bin/MiniDB" >&2
    exit 1
fi

install -d "$BINDIR"
install -m 0755 "$SCRIPT_DIR/bin/MiniDB" "$BINDIR/MiniDB"

echo "Installed MiniDB to $BINDIR/MiniDB"

case ":$PATH:" in
    *":$BINDIR:"*)
        echo "Run: MiniDB --version"
        ;;
    *)
        echo "Warning: $BINDIR is not on PATH."
        echo "Add it to PATH, then run: MiniDB --version"
        ;;
esac
