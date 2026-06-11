# External Roblox Dumper

## What Is Roblox Dumper?

Roblox Dumper is a tool that connects to a running Roblox game and automatically scans memory to find offsets for various classes and properties. It generates offset files in multiple formats that you can use in your own external/projects.


-- Made by jonah, added mac support by SX65

## Setup Guide
- https://dumper.jonah.cool/setup-guide

## macOS Setup

The macOS build attaches to `RobloxPlayer`, reads memory via Mach APIs, and parses Mach-O sections. Most dump stages also need the in-game control script (via Cloudflare tunnel) to exchange data with Roblox.

### Requirements

- macOS on Apple Silicon or Intel
- [Roblox](https://www.roblox.com/download) installed
- Xcode Command Line Tools (`xcode-select --install`)
- CMake and Ninja (`brew install cmake ninja`)
- Join the [Roblox Dumper game](https://dumper.jonah.cool/setup-guide) when running the dumper

### One-time system configuration

These steps are required for memory access. You only need to do them once.

**1. Enable Developer Mode**

```bash
sudo DevToolsSecurity -enable
```

Reboot after running this.

**2. Allow your terminal**

Open **System Settings → Privacy & Security → Developer Tools** and enable the terminal app you use (Terminal, iTerm, Cursor, etc.).

**3. Disable SIP debugging restrictions**

Roblox memory reads require `task_for_pid`. SIP blocks that unless debugging restrictions are lifted.

1. Shut down the Mac
2. Boot to Recovery Mode (hold power → **Options** on Apple Silicon)
3. Open **Terminal** from the Utilities menu
4. Run:

```bash
csrutil enable --without debug
```

5. Reboot

> Do **not** run the dumper with `sudo`. Running as root disables the debugger entitlements the binary needs.

### Build

```bash
cmake --preset macos-release
cmake --build out/macos-release
```

The signed binary is written to `out/macos-release/roblox-dumper`.

### Run (order matters)

The dumper hosts a control server on port **8080**. The Roblox game talks to it through a Cloudflare tunnel. If the tunnel starts before the dumper, every request fails with `connection refused`.

**Terminal 1 — start the dumper and leave it running**

```bash
./run_dumper.sh
```

Wait until you see:

```
Control server started on port 8080
Waiting up to 120s for the game to connect via tunnel...
```

**Terminal 2 — start the tunnel**

```bash
./cloudflare_tunnel.sh
```

Copy the `https://....trycloudflare.com` URL from the tunnel output. The script refuses to start if nothing is listening on port 8080.

**In Roblox**

1. Join the [Dumper game](https://dumper.jonah.cool/setup-guide)
2. Paste the full tunnel URL (including `https://`) into the in-game UI
3. Switch back to Terminal 1 — you should see `Game connected (poll received). Starting dump...`

The dumper writes `offsets.h`, `offsets.json`, `offsets.py`, and `offsets.cs` in the project root when finished.

### macOS troubleshooting

| Symptom | Fix |
|---------|-----|
| `connection refused` on port 8080 in tunnel logs | Start `./run_dumper.sh` **before** `./cloudflare_tunnel.sh`. Keep the dumper terminal open. |
| `Failed to attach to Roblox` / `task_for_pid` errors | Enable Developer Mode, allow your terminal under Developer Tools, run `csrutil enable --without debug`, reboot. Never use `sudo`. |
| `Developer mode is disabled` from `run_dumper.sh` | Run `sudo DevToolsSecurity -enable` and reboot. |
| `SIP debugging restrictions are active` | Boot to Recovery Mode and run `csrutil enable --without debug`, then reboot. |
| Dumper exits before game connects | Paste the tunnel URL in the Dumper game within 120 seconds of starting `run_dumper.sh`. |
| Tunnel shows QUIC warnings | Normal on some networks. cloudflared falls back to HTTP/2 automatically. |
| Port 8080 already in use | Quit whatever is bound to 8080, then restart the dumper. |

## Offset Files

Pre-generated offset files are available in multiple formats:

- C++ Header: [offsets.h](https://dumper.jonah.cool/offsets.h)
- Python: [offsets.py](https://dumper.jonah.cool/offsets.py)
- C#: [offsets.cs](https://dumper.jonah.cool/offsets.cs)
- JSON: [offsets.json](https://dumper.jonah.cool/offsets.json)

View the guide here - [Link](https://dumper.jonah.cool/)

