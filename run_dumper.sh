#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${ROOT}/out/macos-release/roblox-dumper"

if [[ ! -x "${BINARY}" ]]; then
    echo "Build the dumper first:"
    echo "  cmake --preset macos-release && cmake --build out/macos-release"
    exit 1
fi

if [[ "${EUID}" -eq 0 ]]; then
    echo "Do not run with sudo on macOS."
    echo "Use: ${ROOT}/run_dumper.sh"
    exit 1
fi

if ! DevToolsSecurity -status 2>/dev/null | grep -qi "enabled"; then
    echo "Developer mode is disabled."
    echo "Enable it with: sudo DevToolsSecurity -enable"
    echo "Then reboot and run this script again."
    exit 1
fi

CSR_BLOCKED="$(python3 - <<'PY'
import ctypes
lib = ctypes.CDLL("/usr/lib/libSystem.B.dylib")
config = ctypes.c_uint32()
if not hasattr(lib, "csr_get_active_config"):
    print("unknown")
elif lib.csr_get_active_config(ctypes.byref(config)) != 0:
    print("unknown")
elif (config.value & 0x4) == 0:
    print(f"yes:{hex(config.value)}")
else:
    print(f"no:{hex(config.value)}")
PY
)"

if [[ "${CSR_BLOCKED}" == yes:* ]]; then
    CSR_VALUE="${CSR_BLOCKED#yes:}"
    echo "SIP debugging restrictions are active (csr_active_config=${CSR_VALUE})."
    echo "Boot to Recovery Mode, open Terminal, then run:"
    echo "  csrutil enable --without debug"
    echo "Reboot, then run this script again."
    exit 1
fi

echo ""
echo "Setup order:"
echo "  1. Keep THIS terminal open (dumper listens on port 8080)"
echo "  2. Open a second terminal and run: ./cloudflare_tunnel.sh"
echo "  3. Join the Roblox Dumper game and paste the tunnel URL"
echo ""

exec "${BINARY}"
