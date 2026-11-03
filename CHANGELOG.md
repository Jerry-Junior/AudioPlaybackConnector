# Changelog

All notable changes to this project are documented in this file.

This file starts at 1.3.2. Earlier versions were released by the upstream project
[ysc3839/AudioPlaybackConnector](https://github.com/ysc3839/AudioPlaybackConnector).

## [1.3.2] - 2026-11-03

### Added
- Single-instance enforcement: only one instance runs per logon session. Starting
  the app again activates the running instance and opens its device picker instead
  of creating a second process and tray icon.

### Fixed
- Crash when a saved device could no longer be found while reconnecting at startup:
  the exception escaped a `fire_and_forget` coroutine and terminated the process.
- Undefined behavior when closing multiple connections on exit: a `StateChanged`
  callback could erase an element of the connection map while it was being iterated.
- Two copies racing on `AudioPlaybackConnector.json` (the second copy could
  overwrite the settings with its own state).
- A second `DevicePicker::Show()` call could throw out of the window procedure.

### Changed
- Removed the unused gettext translation pipeline (`I18n.hpp`, `FnvHash.hpp`,
  `translate/`). `LoadTranslateData()` was never called, so every string already
  fell back to its English literal; runtime behavior is unchanged.
- Build toolset updated from v142 to v143 (Visual Studio 2022).

### Documentation
- README now documents requirements, configuration format, single-instance
  behavior, build steps and known limitations.
