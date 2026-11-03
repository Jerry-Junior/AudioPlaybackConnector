# AudioPlaybackConnector
**English** | [简体中文](README.zh_CN.md)

Bluetooth audio playback (A2DP Sink) connector for Windows 10 2004+.

Microsoft added Bluetooth A2DP Sink support in Windows 10 2004, but a third-party app is still required to manage connections.\
There are existing apps for this, but they cannot be hidden to the notification area and are not open source.\
So I wrote this app: a simple, modern and open-source alternative.

# Preview
![Preview](https://cdn.jsdelivr.net/gh/ysc3839/AudioPlaybackConnector@master/AudioPlaybackConnector.gif)

# Usage
* Download and run AudioPlaybackConnector from [releases](https://github.com/Jerry-Junior/AudioPlaybackConnector/releases).
* Add a Bluetooth device in system Bluetooth settings. You can right click the AudioPlaybackConnector icon in the notification area and select "Bluetooth Settings".
* Click the AudioPlaybackConnector icon and select the device you want to connect.
* Enjoy!

# Requirements
* Windows 10 version 2004 (10.0.19041) or later.
* The Bluetooth device must be paired in Windows Bluetooth settings first.
* Builds are provided for x86, x64, ARM and ARM64.

# Configuration
Settings are stored in `AudioPlaybackConnector.json`, next to the executable.

| Key | Type | Description |
| --- | --- | --- |
| `reconnect` | boolean | Reconnect the devices listed in `lastDevices` on the next start. |
| `lastDevices` | array of strings | Device ids that were connected when the app exited. |

* The file is written when the app exits.
* The folder containing the executable must be writable; otherwise settings are silently not saved (the app still runs).
* Deleting the file restores the defaults (`reconnect: false`, no saved devices).

# Single instance
Only one instance runs per logon session. Starting a second copy activates the running instance and opens its device picker instead of creating a second tray icon. Separate user sessions (including Remote Desktop sessions) each get their own instance.

# Build
Requirements:

* Visual Studio 2022 with the "Desktop development with C++" workload.
* Windows 10 SDK 10.0.19041 or later.
* NuGet (`nuget.exe`, or the NuGet support bundled with Visual Studio).

```powershell
nuget restore AudioPlaybackConnector.sln
msbuild AudioPlaybackConnector.sln /p:Configuration=Release /p:Platform=x64
```

`x86`, `ARM` and `ARM64` are also valid `Platform` values; ARM and ARM64 additionally require the corresponding MSVC ARM build tools component. The output is `\<Platform>\Release\AudioPlaybackConnector<Architecture>.exe` (`x64` produces `AudioPlaybackConnector64.exe`, `x86` produces `AudioPlaybackConnector32.exe`).

# Known limitations
* UI strings are English only. The previous gettext translation pipeline was never wired up and has been removed.
* The executable is unsigned, so Windows SmartScreen may warn when it is downloaded from the internet.
* A2DP Sink itself is provided by Windows; this app only manages the connection, it does not implement streaming or codecs.

# Feedback
Please include the Windows version and build number, the architecture (x86/x64/ARM/ARM64) and the steps to reproduce. Diagnostics emitted by WIL are visible in [DebugView](https://learn.microsoft.com/sysinternals/downloads/debugview), which helps a lot.

# License
MIT, see [LICENSE](LICENSE).
