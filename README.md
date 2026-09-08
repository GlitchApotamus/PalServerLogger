# PalServerLogger

[![GitHub repository](https://img.shields.io/badge/GitHub-GlitchApotamus%2FPalServerLogger-blue?style=flat-square&logo=github)](https://github.com/GlitchApotamus/PalServerLogger)
[![GitHub Release](https://img.shields.io/github/v/release/GlitchApotamus/PalServerLogger?style=flat-square)](https://github.com/GlitchApotamus/PalServerLogger/releases)
[![Auto Release](https://github.com/GlitchApotamus/PalServerLogger/actions/workflows/release.yml/badge.svg?branch=main)](https://github.com/GlitchApotamus/PalServerLogger/actions/workflows/release.yml)
![Static Badge](https://img.shields.io/badge/kofi-glitch-blue?style=plastic&logo=ko-fi&logoColor=blue&label=kofi&labelColor=purple&color=green&link=https%3A%2F%2Fko-fi.com%2Fglitchapotamus)

# Description
Because the native Palworld server discards log data upon exit, traditional logging methods often fail to capture the complete lifecycle of the server. This logger solves that by running a highly optimized hook architecture that captures console and debug output, writes it to session-based log files, and can stream the same log lines live over a websocket for bots and external tools.

# Installation instructions
Download, extract and drop both files into \Pal\Binaries\Win64.

Restart the server and you will see a new folder "PalServerLogs" where you will find timestamped log files. A new one is generated every time you restart the server.

The mod also starts a websocket listener on port 8765 by default, so a Discord bot or monitoring script can keep a constant live connection to the server output without blocking the game thread. The default bind is loopback-only (`127.0.0.1`) and the websocket requires a shared secret token before it will accept a connection.

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

On first boot, the mod automatically generates a logger_config.json file inside your PalServerLogs/config/ directory. You can edit this file to adjust file retention limits, customize timestamps, and control the live websocket stream.

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
* `websocket_host` (String, Default: "127.0.0.1"):<br>
Controls the bind address. Use `127.0.0.1` for local-only access or a LAN IP if you intentionally want remote clients on the same trusted network.
* `websocket_secret` (String, Default: "change-me-to-a-long-random-secret"):<br>
Required for websocket auth. Connections without the matching token are rejected with `401 Unauthorized`.
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
    "websocket_host": "127.0.0.1",
    "websocket_secret": "change-me-to-a-long-random-secret",
    "debug_hooks": false
}
```

If an existing `logger_config.json` is missing any supported fields, the mod will automatically add the missing values on startup and keep the user's current settings intact. This means you do not have to delete the file just to get new defaults such as `filename_timestamp_format`, websocket auth, or debug tracing.

# Main features

* **Zero-Config Injection:** Uses a professional DLL Proxy (`d3d9.dll`) to auto-load mods when the server starts.
* **Auto-Discovery:** Automatically generates configuration files on first boot if they are missing.
* **Localization Support:** Fully configurable input/output translation mapping via `d3d9_config.json` for custom languages and text strings.
* **Streamlined Logging:** Captures game and console output from multiple Windows logging paths and writes it cleanly to a session-based file.
* **Live Websocket Feed:** Opens a dedicated websocket server so a Discord bot or other script can maintain a constant connection and receive new log lines in real time.
* **Async Background Threading:** Writes files and broadcasts websocket messages on background threads so the game/server loop is not blocked by disk I/O or socket traffic.
* **Automatic Rotation:** Keeps only the newest configured log files and deletes older ones automatically.
* **Stable Architecture:** Built with thread-safe queueing and injection logic to prevent server crashes and boot-time deadlocks.

# Websocket security and payload format

The websocket listener is intended to be used as a private local stream. By default it binds to `127.0.0.1` and requires a shared secret token before it will accept a connection. This prevents random machines from reading the log stream simply by knowing the IP and port.

Clients may authenticate with either an `Authorization: Bearer <token>` header or a query string token such as `?token=<secret>`.

Example payloads:

```json
{"type":"status","message":"PalServerLogger websocket connected"}
{"type":"log","message":"[2026-09-07 19:56:14] [ModLoader]: [SUCCESS] Injected PalDefender.dll"}
```

The websocket runs independently from the main game loop, which keeps the server responsive while external bots stay connected.

# Recommended usage

For a Discord bot or local dashboard, the safest setup is:

* bind to `127.0.0.1`
* keep `websocket_enabled` enabled
* set `websocket_secret` to a strong random value
* connect from the same machine or a trusted private network only

Avoid exposing the websocket to the public internet unless you also add TLS termination and a proper firewall policy.

# Requirements

Currently only support windows based servers. Linux is planned but may be a while. This mod does not require UE4SS, only the d3d9.dll that's shipped with it.

# Source Code

Find the latest updates, releases, and source code on GitHub: [GlitchApotamus/PalServerLogger](https://github.com/GlitchApotamus/PalServerLogger)
