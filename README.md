# PalServerLogger

[![GitHub repository](https://img.shields.io/badge/GitHub-GlitchApotamus%2FPalServerLogger-blue?style=flat-square&logo=github)](https://github.com/GlitchApotamus/PalServerLogger)
[![GitHub Release](https://img.shields.io/github/v/release/GlitchApotamus/PalServerLogger?style=flat-square)](https://github.com/GlitchApotamus/PalServerLogger/releases)
[![Auto Release](https://github.com/GlitchApotamus/PalServerLogger/actions/workflows/release.yml/badge.svg?branch=main)](https://github.com/GlitchApotamus/PalServerLogger/actions/workflows/release.yml)
![Static Badge](https://img.shields.io/badge/kofi-glitch-blue?style=plastic&logo=ko-fi&logoColor=blue&label=kofi&labelColor=purple&color=green&link=https%3A%2F%2Fko-fi.com%2Fglitchapotamus)

# Description
Because the native Palworld server discards log data upon exit, traditional logging methods often fail to capture the complete lifecycle of the server. This logger solves that by running a highly optimized dual-hook architecture that intercepts both the initial boot sequence and the Unreal Engine core logging system, outputting it cleanly to a session-based file.

# Installation instructions
Download, extract and drop both files into \Pal\Binaries\Win64.
> **Important Note for PalDefender Users:** 
> If you are using PalDefender, you must switch to using our `d3d9.dll` proxy loader as your primary injection point to take advantage of the new localization and translation features. You won't lose PalDefender functionality — Simply set `UsePalDefender` to `true` or add `"PalDefender.dll"` to your `load_dlls` array inside the newly generated `d3d9_config.json` alongside your other mods! By default, `UsePalDefender` is set to `false` to prevent errors on startup.

Restart the server and you will see a new folder "PalServerLogs" where you will find timestamped log files. A new one is generated every time your restart the server.

# Configuration & Translations
On first boot, the mod loader automatically generates a `d3d9_config.json` file in your binaries directory. This file controls which DLLs are loaded and handles text localization.

### How to Update or Customize Translations
If you want to translate the mod loader output into another language or modify the existing messages, open `d3d9_config.json` and locate the `"translations"` object block:

```json
{
    "load_dlls": [
        "PalServerLogger.dll"
    ],
    "UsePalDefender": false,
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
* **Customizing Text:** Simply edit the string value to the right of any key.
* **Dynamic Placeholders:** Keep tokens like `{0}` and `{1}` intact inside your custom strings, as they are dynamically replaced with runtime data (such as filenames or error codes).
* **Fallbacks:** If a key is missing or removed from your config file, the loader will automatically fall back to its built-in default strings safely.

## Mod Configuration File

On first boot, the mod automatically generates a logger_config.json file inside your PalServerLogs/config/ directory. You can edit this file to adjust file retention limits and customize timestamp outputs.

### Available Settings:
* `max_log_files` (Integer, Default: 5): <br>
Defines the maximum number of historical session log files to retain before automatically cleaning up and deleting the oldest log files on startup.
* `timestamp_format` (String, Default: "%Y-%m-%d %H:%M:%S"):<br>
Customizes the timestamp format prepended to every logged line using standard C++ `strftime` format specifiers.
<br><br>
```json
{
    "max_log_files": 5,
    "timestamp_format": "%Y-%m-%d %H:%M:%S"
}
```

# Main features

* **Zero-Config Injection:** Uses a professional DLL Proxy (`d3d9.dll`) to auto-load mods when the server starts.
* **Auto-Discovery:** Automatically generates configuration files on first boot if they are missing.
* **Localization Support:** Fully configurable input/output translation mapping via `d3d9_config.json` for custom languages and text strings.
* **Streamlined Logging:** Includes real-time, color-coded console feedback, allowing you to monitor mod status directly from the server terminal.
* **Stable Architecture:** Built with thread-safe injection to prevent server crashes and boot-time deadlocks.

# Requirements

Currently only support windows based servers. Linux is planned but may be a while. This mod does not require UE4SS, only the d3d9.dll that's shipped with it.

# Source Code

Find the latest updates, releases, and source code on GitHub: [GlitchApotamus/PalServerLogger](https://github.com/GlitchApotamus/PalServerLogger)
