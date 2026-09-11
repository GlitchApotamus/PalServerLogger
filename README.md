# PalServerLogger

[![GitHub repository](https://img.shields.io/badge/GitHub-GlitchApotamus%2FPalServerLogger-blue?style=flat-square&logo=github)](https://github.com/GlitchApotamus/PalServerLogger)
[![GitHub Release](https://img.shields.io/github/v/release/GlitchApotamus/PalServerLogger?style=flat-square)](https://github.com/GlitchApotamus/PalServerLogger/releases)
[![Auto Release](https://github.com/GlitchApotamus/PalServerLogger/actions/workflows/release.yml/badge.svg?branch=main)](https://github.com/GlitchApotamus/PalServerLogger/actions/workflows/release.yml)
![Static Badge](https://img.shields.io/badge/kofi-glitch-blue?style=plastic&logo=ko-fi&logoColor=blue&label=kofi&labelColor=purple&color=green&link=https%3A%2F%2Fko-fi.com%2Fglitchapotamus)

# Description
Because the native Palworld server discards log data upon exit, traditional logging methods often fail to capture the complete lifecycle of the server. This logger solves that by running a highly optimized hook architecture that captures console and debug output, writes it to session-based log files, and can stream the same log lines live over a websocket for bots and external tools.

## Native Windows vs Wine/Proton/AMP behavior

This logger is designed primarily for native Windows Palworld server installs. In a normal Windows environment, it captures output through the usual Win32 console/debug APIs such as `WriteConsole*`, `WriteFile`, and `OutputDebugString*`.

Under Wine, Proton, or AMP-managed server environments, the server may not emit through those same Windows console paths. In those cases, the output is often routed through a different runtime or file-backed logging path, and the logger may only see the early startup/breakpad text unless the fallback log-tail support is active.

In short:

* Native Windows server: expected to work with console-hook capture.
* Wine/Proton/AMP server: still supported, but output may be redirected or buffered differently; the logger falls back to monitoring likely log files when native console hooks do not receive the full stream.
* If you are running through AMP or a Linux container, use `debug_hooks` to confirm which capture path is active and whether the fallback is seeing the real log file.

# Installation instructions
Download, extract and drop both files into \Pal\Binaries\Win64.

> Version note: the folder layout changed in this release. The logger now stores its runtime config at `PalServerLogger/Config.json` instead of the older `PalServerLogs/config/logger_config.json` path. If an older installation still has the previous folder layout, the app automatically migrates its data and removes the legacy folder on startup.

Restart the server and you will see a new folder `PalServerLogger` where you will find the generated config file and timestamped log files. A new log is generated every time you restart the server.

The mod also starts a websocket listener on port 8765 by default, so a Discord bot or monitoring script can keep a constant live connection to the server output without blocking the game thread. The default bind is the configured host in `Config.json` (default: `0.0.0.0`), and the websocket requires a shared secret token before it will accept a connection.

# Configuration & Translations
On first boot, the mod loader automatically generates a `d3d9_config.json` file in your binaries directory. This file controls which DLLs are loaded and handles text localization.

## `load_dlls` behavior
The loader uses only the `load_dlls` array to decide which DLLs to inject. There is no longer a separate `UsePalDefender` toggle.

- To load PalDefender, add `"PalDefender.dll"` to `load_dlls`.
- To disable it, remove that entry from the array.
- If the config file is missing or malformed, the loader will repair it on startup and keep the existing valid entries intact.
- Unknown keys are removed on each load, and missing supported keys are added back in so the config stays aligned with the loader schema.

Example:

```json
{
    "load_dlls": [
        "PalServerLogger.dll",
        "PalDefender.dll"
    ],
    "translations": {
        "ModThreadStarted": "Mod thread started.",
        "ConfigMissing": "Config missing. Generating default d3d9_config.json...",
        "DefaultConfigCreated": "Default config created.",
        "AttemptingLoad": "Attempting to load: {0}",
        "InjectedSuccess": "Injected {0}",
        "InjectedFailed": "Failed to inject {0}. Windows Error Code: {1}",
        "ConfigErrorMissingArray": "JSON does not contain 'load_dlls' array.",
        "ConfigErrorOpening": "Could not open config at: {0}"
    }
}
```

### How to Update or Customize Translations
If you want to translate the mod loader output into another language or modify the existing messages, open `d3d9_config.json` and locate the `"translations"` object block.

* **Customizing Text:** Simply edit the string value to the right of any key.
* **Dynamic Placeholders:** Keep tokens like `{0}` and `{1}` intact inside your custom strings, as they are dynamically replaced with runtime data (such as filenames or error codes).
* **Fallbacks:** If a key is missing or removed from your config file, the loader will automatically fall back to its built-in default strings safely.

See the project changelog in [CHANGELOG.md](CHANGELOG.md) for release notes and version history.

## Mod Configuration File

On first boot, the mod automatically generates a `Config.json` file inside your `PalServerLogger/` directory. This replaces the older `PalServerLogs/config/logger_config.json` layout used in previous builds. If the legacy folder is still present, the app copies all non-config files into the new location and deletes the old folder after migration.

You can edit this file to adjust file retention limits, customize timestamps, and control the live websocket stream.

### Available Settings:
* `max_log_files` (Integer, Default: 5): <br>
Defines the maximum number of historical session log files to retain before automatically cleaning up and deleting the oldest log files on startup.
* `timestamp_format` (String, Default: "%Y-%m-%d %H:%M:%S"):<br>
Customizes the timestamp format prepended to every logged line and, by default, also controls the timestamp used in log filenames.
* `filename_timestamp_format` (String, Optional):<br>
Overrides only the filename timestamp format if you want line timestamps and file naming to use different formats. Invalid Windows filename characters are automatically replaced with `_`.
* `websocket_enabled` (Boolean, Default: true):<br>
Enables or disables the live websocket server used for external bots and monitoring tools.
* `websocket_port` (Integer, Default: 8765):<br>
Specifies which local port the websocket server will bind to.
* `websocket_host` (String, Default: "0.0.0.0"):<br>
Controls the bind address. Use `127.0.0.1` for local-only access or a LAN IP if you intentionally want remote clients on the same trusted network.
* `websocket_secret` (String, Auto-generated on first launch):<br>
Required for websocket auth. On first startup the mod generates a secure base64 secret using `openssl rand -base64 48` when available, stores it in the config, and replaces any placeholder or empty value automatically. Connections without the matching token are rejected with `401 Unauthorized`.
* `debug_hooks` (Boolean, Default: false):<br>
Enables extra logging around MinHook initialization and active console/debug capture paths to help diagnose compatibility issues in unusual environments.
<br><br>
```json
{
    "max_log_files": 5,
    "timestamp_format": "%Y-%m-%d %H:%M:%S",
    "filename_timestamp_format": "%Y%m%d_%H%M%S",
    "websocket_enabled": true,
    "websocket_port": 8765,
    "websocket_host": "0.0.0.0",
    "websocket_secret": "<auto-generated on first startup>",
    "debug_hooks": false
}
```

If an existing `logger_config.json` is missing any supported fields, the mod will automatically add the missing values on startup and keep the user's current settings intact. This means you do not have to delete the file just to get new defaults such as `filename_timestamp_format`, a fresh websocket secret, or debug tracing.

# Main features

* **Zero-Config Injection:** Uses a professional DLL Proxy (`d3d9.dll`) to auto-load mods when the server starts.
* **Auto-Discovery:** Automatically generates configuration files on first boot if they are missing.
* **Localization Support:** Fully configurable input/output translation mapping via `d3d9_config.json` for custom languages and text strings.
* **Streamlined Logging:** Captures game and console output from multiple Windows logging paths and writes it cleanly to a session-based file.
* **Live Websocket Feed:** Opens a dedicated websocket server so a Discord bot or other script can maintain a constant connection and receive new log lines in real time.
* **Async Background Threading:** Writes files and broadcasts websocket messages on background threads so the game/server loop is not blocked by disk I/O or socket traffic.
* **Automatic Rotation:** Keeps only the newest configured log files and deletes older ones automatically.
* **Stable Architecture:** Built with thread-safe queueing and injection logic to prevent server crashes and boot-time deadlocks.
* **Auto-Migration:** Detects the legacy `PalServerLogs` layout, copies non-config files into the new `PalServerLogger` structure, and removes the old directory after migration.
* **Self-Loop Protection:** Filters exact duplicate log lines and ignores the logger’s own websocket/fallback status output so it does not re-queue its own messages and spam the server.

# Websocket security and payload format

The websocket listener is intended to be used as a private local stream. By default it binds to the configured host (the generated default is `0.0.0.0`) and requires a shared secret token before it will accept a connection. This prevents random machines from reading the log stream simply by knowing the IP and port.

On first startup, the logger generates a secure `websocket_secret` automatically using `openssl rand -base64 48` when OpenSSL is available, then saves it in `PalServerLogger/Config.json`. If a config already exists with the old placeholder or an empty value, the launcher replaces it automatically on next boot. The app also logs startup details such as `[WEBSOCKET] listening on ws://<host>:<port>` and when the secret was generated.

To avoid self-echo loops, the logger now suppresses exact duplicate lines for a short cooldown window and ignores its own websocket/fallback status messages while still allowing legitimate repeated server logs to pass through. This keeps the live stream clean without hiding genuine output.

Clients may authenticate with either an `Authorization: Bearer <token>` header or a query string token such as `?token=<secret>`.

Example payloads:

```json
{"type":"status","message":"PalServerLogger websocket connected"}
{"type":"log","message":"[2026-09-07 19:56:14] [ModLoader]: [SUCCESS] Injected PalDefender.dll"}
```

The websocket runs independently from the main game loop, which keeps the server responsive while external bots stay connected.

# Recommended usage

For a Discord bot or local dashboard, the safest setup is:

* bind to `127.0.0.1` or `0.0.0.0` for local-only use, or to a trusted private network address if you intentionally need remote access
* keep `websocket_enabled` enabled
* leave the generated `websocket_secret` in place, or replace it only with another strong random value
* connect from the same machine or a trusted private network only

Avoid exposing the websocket to the public internet unless you also add TLS termination and a proper firewall policy.

# Example websocket client

A working example of a client connecting to this logger is available here: [GlitchApotamus/PalServerWebsocket](https://github.com/GlitchApotamus/PalServerWebsocket)

That project demonstrates how to connect to the logger websocket, authenticate with the configured secret, and stream incoming log messages from the server in real time.

# Requirements

The primary supported path is a native Windows Palworld server. The mod also includes compatibility handling for Wine/Proton/AMP-managed environments, but output capture is less predictable there because those setups often route server logs through a different runtime or file-based system instead of a standard Windows console.

This mod does not require UE4SS, only the `d3d9.dll` that's shipped with it.

# Source Code

Find the latest updates, releases, and source code on GitHub: [GlitchApotamus/PalServerLogger](https://github.com/GlitchApotamus/PalServerLogger)
