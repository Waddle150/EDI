#!/usr/bin/env bash

OS_TYPE="unknown"
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS_TYPE="linux"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    OS_TYPE="windows"
fi

if [ "$OS_TYPE" == "linux" ]; then
    if [ "$EUID" -ne 0 ]; then
        echo "Error: Please run with sudo on Linux."
        exit 1
    fi
elif [ "$OS_TYPE" == "windows" ]; then
    net session > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "Error: idk"
        exit 1
    fi
fi
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

if [ "$OS_TYPE" == "linux" ]; then
    PKG_DIR="${1:-/}"
    BIN_DIR="$PKG_DIR/usr/bin"
    MIME_DIR="$PKG_DIR/usr/share/mime/applications"
elif [ "$OS_TYPE" == "windows" ]; then
    DEFAULT_WIN_DIR="/c/Program Files/EDI_Tools"
    PKG_DIR="${1:-$DEFAULT_WIN_DIR}"
    BIN_DIR="$PKG_DIR"
    MIME_DIR="$PKG_DIR"
else
    echo "Unsupported OS: $OSTYPE"
    exit 1
fi



mkdir -p "$BIN_DIR"
mkdir -p "$MIME_DIR"

install -Dm755 "$SCRIPT_DIR/edi_runner" "$BIN_DIR/edi_runner"
echo "Runner Installed"

install -Dm755 "$SCRIPT_DIR/edi_compiler" "$BIN_DIR/edi_compiler"
echo "Compiler Installed"

install -Dm644 "$SCRIPT_DIR/edi.xml" "$MIME_DIR/edi.xml"
echo "XML Entered"

echo "Done"
