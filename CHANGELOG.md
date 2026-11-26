# Changelog

All notable changes to this project are documented in this file.

This file starts at 1.3.2. Earlier versions were released by the upstream project
[ysc3839/AudioPlaybackConnector](https://github.com/ysc3839/AudioPlaybackConnector).

## [Unreleased]

### Fixed
- The connection table was accessed from several threads without synchronization (data
  race, undefined behavior). The UI thread was initialized as a multi-threaded apartment
  (`winrt::init_apartment()` default), so `co_await` on WinRT async operations in
  `ConnectDevice` and `co_await` on the captured `apartment_context` in `ReconnectAll`
  resumed on thread-pool threads rather than the UI thread, contrary to what 1.3.4 stated;
  and `StateChanged`, which the system raises on a background thread when the remote device
  disconnects, erased entries directly.
  - The UI thread is now a single-threaded apartment (as XAML Islands require), so those
    coroutines resume on the UI thread.
  - The `StateChanged` handler no longer touches app state: it posts
    `WM_CONNECTIONCLOSED` with the session id, and the UI thread removes and closes the
    matching session.
  - Debug builds assert that the connection table is only accessed from the UI thread.

## [1.3.4] - 2026-11-22

### Added
- A "Reconnect" item in the tray context menu. It closes every active connection and
  reconnects the same devices one second later; this is the workaround for the
  "connected but silent" state (a connection established while the phone is already
  playing may stay silent even though audio reaches the PC). Closing tears the AVDTP
  session down (the phone pauses its playback), and the fresh connection starts a new
  stream that reaches the output device.
- The item is disabled while nothing is connected or a reconnect is already in flight,
  and the affected devices show "Reconnecting" while the old stream is torn down.

### Changed
- The reconnect delay resumes on the UI thread before the device picker or the connection
  table is touched again, so the connection table is never accessed from two threads.
- Each connection session carries an id: a late `StateChanged` event or a late
  failed-connect cleanup can no longer remove a newer session for the same device.

## [1.3.3] - 2026-11-09

### Fixed
- Disconnecting a device from the picker (and cleaning up a failed connection attempt)
  could erase a dangling iterator in `g_audioPlaybackConnections`. Closing an
  `AudioPlaybackConnection` raises `StateChanged(Closed)` synchronously, and that handler
  removes the device from the table; the caller then erased the same, now invalidated,
  iterator (undefined behavior). The entry is now detached from the table before the
  connection is closed, so the handler's lookup fails instead of erasing it a second time.

### Notes
- Disconnecting one of several simultaneously connected devices still disconnects all of
  them. This was reproduced after the fix above: the app only closes the connection of the
  selected device, so the propagation happens inside the Windows A2DP sink stack, which
  does not expose per-device teardown through `AudioPlaybackConnection`.

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
