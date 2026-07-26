# Description
Because the native Palworld server discards log data upon exit, traditional logging methods often fail to capture the complete lifecycle of the server. This logger solves that by running a highly optimized dual-hook architecture that intercepts both the initial boot sequence and the Unreal Engine core logging system, outputting it cleanly to a session-based file.

# Installation instructions
Download, extract and drop both files into \Pal\Binaries\Win64.
The d3d9 is highly universal. If you already have PalDefender installed on the server, just drop PalworldLogger.dll into the same folder and add "PalServerLogger.dll" to the d3d9_config.json.
Restart the server and you will see a new folder "PalServerLogs" where you will find timestamped log files. A new one is generated every time your restart the server.

# Main features
* **Zero-Config Injection:** Uses a professional DLL Proxy (`d3d9.dll`) to auto-load mods when the server starts.
* **Auto-Discovery:** Automatically generates configuration files on first boot if they are missing.
* **Streamlined Logging:** Includes real-time, color-coded console feedback, allowing you to monitor mod status directly from the server terminal.
* **Stable Architecture:** Built with thread-safe injection to prevent server crashes and boot-time deadlocks.

# Requirements
Currently only support windows based servers. Linux is planned but may be a while. This mod does not require UE4SS, only the d3d9.dll that's shipped with it.