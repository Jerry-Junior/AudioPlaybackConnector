#pragma once

#include "resource.h"

using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Audio;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Hosting;
namespace fs = std::filesystem;

constexpr wchar_t WINDOW_CLASS_NAME[] = L"AudioPlaybackConnector";
constexpr UINT WM_NOTIFYICON = WM_APP + 1;
constexpr UINT WM_CONNECTDEVICE = WM_APP + 2;
constexpr UINT WM_SHOWDEVICEPICKER = WM_APP + 3; // used by a second instance to wake the running one
// Posted by a connection's StateChanged handler (which runs on a background thread) when the
// connection reaches Closed. wParam is the session id; handled on the UI thread.
constexpr UINT WM_CONNECTIONCLOSED = WM_APP + 4;
// Posted by PostToUiThread: drain g_uiThreadQueue on the UI thread. DevicePicker raises its
// events on background (RPC) threads even though the UI thread is an STA, because C++/WinRT
// delegates are agile and COM does not marshal the callbacks back.
constexpr UINT WM_RUNONUITHREAD = WM_APP + 5;

// Delay between closing every connection and opening the fresh ones: Close() tears the A2DP
// session down (the phone pauses its media), so the phone and the audio engine need a moment
// to settle before a new connection is opened.
constexpr auto RECONNECT_SETTLE_DELAY = std::chrono::seconds(1);

// Single-instance mutex name. The Local\ namespace is per logon session, so multiple
// users and Remote Desktop sessions do not interfere. Never switch to Global\: another
// session would be blocked while FindWindow cannot see its window, making the app look dead.
constexpr wchar_t SINGLE_INSTANCE_MUTEX_NAME[] = L"Local\\AudioPlaybackConnector-{2daaabdd-2402-4023-bc3e-b6e93fad567b}";

HINSTANCE g_hInst;
HWND g_hWnd;
HWND g_hWndXaml;
// The UI thread (an STA running the message loop) owns g_audioPlaybackConnections, the
// device picker and the XAML objects. Only used for debug assertions.
DWORD g_uiThreadId = 0;
// Work posted from background threads by PostToUiThread and run by WM_RUNONUITHREAD. The
// lock only guards the vector; the items themselves always run on the UI thread.
std::mutex g_uiThreadQueueLock;
std::vector<std::function<void()>> g_uiThreadQueue;
// Set (under the lock) when the message loop has ended: later work is dropped so no WinRT
// object is left in the queue to be released after the STA is gone.
bool g_uiThreadQueueClosed = false;
Canvas g_xamlCanvas = nullptr;
Flyout g_xamlFlyout = nullptr;
MenuFlyout g_xamlMenu = nullptr;
FocusState g_menuFocusState = FocusState::Unfocused;
DevicePicker g_devicePicker = nullptr;

struct AudioPlaybackSession
{
	DeviceInformation Device{ nullptr };
	AudioPlaybackConnection Connection{ nullptr };
	// Identifies this session object. A StateChanged(Closed) notification is delivered to the
	// UI thread asynchronously and can arrive after the app already removed or replaced the
	// session; such a late event (or a late failed-connect cleanup) must not remove a newer
	// session for the same device. uintptr_t so the id fits in the WPARAM of
	// WM_CONNECTIONCLOSED; ids are only compared for equality, so wrapping on 32-bit builds
	// (after 2^32 connections) is harmless.
	std::uintptr_t Id = 0;
};

// Accessed from the UI thread only (see g_uiThreadId).
std::unordered_map<std::wstring, AudioPlaybackSession> g_audioPlaybackConnections;
std::uintptr_t g_nextSessionId = 1;
bool g_reconnecting = false;
bool g_shuttingDown = false;
HICON g_hIconLight = nullptr;
HICON g_hIconDark = nullptr;
NOTIFYICONDATAW g_nid = {
	.cbSize = sizeof(g_nid),
	.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP,
	.uCallbackMessage = WM_NOTIFYICON,
	.uVersion = NOTIFYICON_VERSION_4
};
NOTIFYICONIDENTIFIER g_niid = {
	.cbSize = sizeof(g_niid)
};
UINT WM_TASKBAR_CREATED = 0;
bool g_reconnect = false;
std::vector<std::wstring> g_lastDevices;

#include "Util.hpp"
#include "SettingsUtil.hpp"
#include "Direct2DSvg.hpp"
