#!/usr/bin/env bash
set -euo pipefail

INSTALL_DIR="${HOME}/.local/cloudflared"
ARCH="$(uname -m)"

install_cloudflared() {
    local asset=""
    case "${ARCH}" in
        arm64|aarch64)
            asset="cloudflared-darwin-arm64.tgz"
            ;;
        x86_64|amd64)
            asset="cloudflared-darwin-amd64.tgz"
            ;;
        *)
            echo "Unsupported architecture: ${ARCH}"
            exit 1
            ;;
    esac

    local temp_dir
    temp_dir="$(mktemp -d)"
    trap 'rm -rf "${temp_dir}"' RETURN

    echo "Downloading cloudflared..."
    curl -fsSL "https://github.com/cloudflare/cloudflared/releases/latest/download/${asset}" \
        -o "${temp_dir}/${asset}"

    mkdir -p "${INSTALL_DIR}"
    tar -xzf "${temp_dir}/${asset}" -C "${INSTALL_DIR}"
    chmod +x "${INSTALL_DIR}/cloudflared"
    echo "cloudflared installed to ${INSTALL_DIR}/cloudflared"
}

if command -v cloudflared >/dev/null 2>&1; then
    echo "cloudflared is already installed"
    CLOUDFLARED_BIN="$(command -v cloudflared)"
elif [[ -x "${INSTALL_DIR}/cloudflared" ]]; then
    echo "cloudflared found in ${INSTALL_DIR}"
    CLOUDFLARED_BIN="${INSTALL_DIR}/cloudflared"
else
    echo "cloudflared not found, installing..."
    install_cloudflared
    CLOUDFLARED_BIN="${INSTALL_DIR}/cloudflared"
fi

export PATH="${INSTALL_DIR}:${PATH}"

if ! nc -z 127.0.0.1 8080 2>/dev/null; then
    echo ""
    echo "ERROR: Nothing is listening on port 8080."
    echo "Start the dumper FIRST in another terminal:"
    echo "  ./run_dumper.sh"
    echo ""
    echo "Then run this tunnel script again."
    exit 1
fi

echo "Port 8080 is up. Starting tunnel..."
echo "Copy the https://....trycloudflare.com URL into the Roblox Dumper game."
exec "${CLOUDFLARED_BIN}" tunnel --url http://127.0.0.1:8080
