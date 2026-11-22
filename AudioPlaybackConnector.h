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
Canvas g_xamlCanvas = nullptr;
Flyout g_xamlFlyout = nullptr;
MenuFlyout g_xamlMenu = nullptr;
FocusState g_menuFocusState = FocusState::Unfocused;
DevicePicker g_devicePicker = nullptr;

struct AudioPlaybackSession
{
	DeviceInformation Device{ nullptr };
	AudioPlaybackConnection Connection{ nullptr };
	// Identifies this session object. StateChanged is raised synchronously from Close() and
	// can also arrive late for a connection the app already replaced; such an event (or a
	// late failed-connect cleanup) must not remove a newer session for the same device.
	uint64_t Id = 0;
};

std::unordered_map<std::wstring, AudioPlaybackSession> g_audioPlaybackConnections;
uint64_t g_nextSessionId = 1;
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
