#include "pch.h"
#include "AudioPlaybackConnector.h"

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void SetupFlyout();
void SetupMenu();
winrt::fire_and_forget ConnectDevice(DevicePicker, std::wstring_view);
winrt::fire_and_forget ConnectDevice(DevicePicker, DeviceInformation);
void SetupDevicePicker();
void SetupSvgIcon();
void UpdateNotifyIcon();
void ShowDevicePicker();
void ActivateExistingInstance();
void RemoveAudioPlaybackConnection(std::wstring const& deviceId);
void OnConnectionClosed(std::uintptr_t sessionId);
void PostToUiThread(std::function<void()> work);
void RunUiThreadQueue();
void SetDeviceDisplayStatus(DevicePicker const& picker, DeviceInformation const& device, std::wstring_view status, DevicePickerDisplayStatusOptions options);
winrt::fire_and_forget ReconnectAll();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	g_hInst = hInstance;

	// Single-instance gate: the mutex only has to exist, ownership is never taken.
	// The handle lives until wWinMain returns; the kernel releases it on process exit
	// (including crashes or a forced kill), so no stale lock can be left behind.
	// The Local\ namespace gives one instance per logon session, so multiple users and
	// Remote Desktop sessions do not interfere. Never use Global\: another session would
	// be blocked while FindWindow cannot see its window, making the app look dead.
	HANDLE mutexRaw = CreateMutexW(nullptr, FALSE, SINGLE_INSTANCE_MUTEX_NAME);
	const DWORD mutexError = GetLastError(); // must be read immediately after CreateMutexW
	wil::unique_handle singleInstanceMutex(mutexRaw);
	if (!singleInstanceMutex)
	{
		// Could not open an existing mutex (for example due to an ACL): treat it as
		// "already running" and never start a second instance.
		LOG_LAST_ERROR();
		return EXIT_SUCCESS;
	}
	if (mutexError == ERROR_ALREADY_EXISTS)
	{
		// Another instance is running: let it show the device picker as visible feedback.
		ActivateExistingInstance();
		return EXIT_SUCCESS;
	}

	// The UI thread must be a single-threaded apartment. XAML Islands require it, and the
	// threading of the whole app depends on it: co_await on a WinRT async operation and
	// co_await on a winrt::apartment_context resume in the apartment that started them. In an
	// STA that is this thread; in the MTA (the init_apartment() default) it is any MTA thread,
	// i.e. a thread-pool thread, which would then race with the UI thread on the connection
	// table.
	winrt::init_apartment(winrt::apartment_type::single_threaded);
	g_uiThreadId = GetCurrentThreadId();
	// Leave the STA before returning. Declared before every WinRT local of this function, so
	// those locals are destroyed first; the WinRT globals are released explicitly after the
	// message loop. Without this, a process that had shown the DevicePicker hangs after
	// wWinMain returns instead of exiting.
	auto apartmentGuard = wil::scope_exit([] { winrt::uninit_apartment(); });

	bool supported = false;
	try
	{
		using namespace winrt::Windows::Foundation::Metadata;

		supported = ApiInformation::IsTypePresent(winrt::name_of<DesktopWindowXamlSource>()) &&
			ApiInformation::IsTypePresent(winrt::name_of<AudioPlaybackConnection>());
	}
	catch (winrt::hresult_error const&)
	{
		supported = false;
		LOG_CAUGHT_EXCEPTION();
	}
	if (!supported)
	{
		TaskDialog(nullptr, nullptr, L"Unsupported Operating System", nullptr, L"AudioPlaybackConnector is not supported on this operating system version.", TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
		return EXIT_FAILURE;
	}

	WNDCLASSEXW wcex = {
		.cbSize = sizeof(wcex),
		.lpfnWndProc = WndProc,
		.hInstance = hInstance,
		.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_AUDIOPLAYBACKCONNECTOR)),
		.hCursor = LoadCursorW(nullptr, IDC_ARROW),
		.lpszClassName = WINDOW_CLASS_NAME,
		.hIconSm = wcex.hIcon
	};

	RegisterClassExW(&wcex);

	// When parent window size is 0x0 or invisible, the dpi scale of menu is incorrect. Here we set window size to 1x1 and use WS_EX_LAYERED to make window looks like invisible.
	g_hWnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TOPMOST, WINDOW_CLASS_NAME, nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
	FAIL_FAST_LAST_ERROR_IF_NULL(g_hWnd);
	FAIL_FAST_IF_WIN32_BOOL_FALSE(SetLayeredWindowAttributes(g_hWnd, 0, 0, LWA_ALPHA));

	DesktopWindowXamlSource desktopSource;
	auto desktopSourceNative2 = desktopSource.as<IDesktopWindowXamlSourceNative2>();
	winrt::check_hresult(desktopSourceNative2->AttachToWindow(g_hWnd));
	winrt::check_hresult(desktopSourceNative2->get_WindowHandle(&g_hWndXaml));

	g_xamlCanvas = Canvas();
	desktopSource.Content(g_xamlCanvas);

	LoadSettings();
	SetupFlyout();
	SetupMenu();
	SetupDevicePicker();
	SetupSvgIcon();

	g_nid.hWnd = g_niid.hWnd = g_hWnd;
	wcscpy_s(g_nid.szTip, L"AudioPlaybackConnector");
	UpdateNotifyIcon();

	WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");
	LOG_LAST_ERROR_IF(WM_TASKBAR_CREATED == 0);

	PostMessageW(g_hWnd, WM_CONNECTDEVICE, 0, 0);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0))
	{
		BOOL processed = FALSE;
		winrt::check_hresult(desktopSourceNative2->PreTranslateMessage(&msg, &processed));
		if (!processed)
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	// Release every WinRT object owned by the UI thread while the STA is still alive. Globals
	// would otherwise be destroyed by the CRT after wWinMain returns, i.e. after
	// uninit_apartment (or, without it, in a torn-down apartment), which hangs the process.
	std::vector<std::function<void()>> pendingWork;
	{
		std::lock_guard lock(g_uiThreadQueueLock);
		g_uiThreadQueueClosed = true; // late DevicePicker events are dropped from now on
		pendingWork.swap(g_uiThreadQueue);
	}
	pendingWork.clear();

	g_devicePicker = nullptr;
	g_xamlMenu = nullptr;
	g_xamlFlyout = nullptr;
	try
	{
		desktopSource.Content(nullptr);
	}
	CATCH_LOG();
	g_xamlCanvas = nullptr;
	desktopSourceNative2 = nullptr;
	try
	{
		desktopSource.Close();
	}
	CATCH_LOG();
	desktopSource = nullptr;

	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
	{
		// Stop a pending reconnect from touching the app state once shutdown has started.
		g_shuttingDown = true;
		g_reconnecting = false;

		// In reconnect mode save first: SaveSettings reads the device ids from the table.
		if (g_reconnect)
		{
			SaveSettings();
		}

		// Detach the table before closing anything, so nothing can modify it while it is
		// being iterated. Close() raises StateChanged synchronously; the handler only posts
		// WM_CONNECTIONCLOSED, which is either discarded together with the window or finds
		// the table empty, so it has no effect.
		auto connections = std::move(g_audioPlaybackConnections);
		g_audioPlaybackConnections.clear();

		for (auto& connection : connections)
		{
			connection.second.Connection.Close();
			SetDeviceDisplayStatus(g_devicePicker, connection.second.Device, {}, DevicePickerDisplayStatusOptions::None);
		}

		if (!g_reconnect)
		{
			SaveSettings(); // not reconnecting: write an empty lastDevices list
		}

		Shell_NotifyIconW(NIM_DELETE, &g_nid);
		PostQuitMessage(0);
		break;
	}
	case WM_SETTINGCHANGE:
		if (lParam && CompareStringOrdinal(reinterpret_cast<LPCWCH>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
		{
			UpdateNotifyIcon();
		}
		break;
	case WM_NOTIFYICON:
		switch (LOWORD(lParam))
		{
		case NIN_SELECT:
		case NIN_KEYSELECT:
			ShowDevicePicker();
			break;
		case WM_RBUTTONUP: // Menu activated by mouse click
			g_menuFocusState = FocusState::Pointer;
			break;
		case WM_CONTEXTMENU:
		{
			if (g_menuFocusState == FocusState::Unfocused)
				g_menuFocusState = FocusState::Keyboard;

			auto dpi = GetDpiForWindow(hWnd);
			Point point = {
				static_cast<float>(GET_X_LPARAM(wParam) * USER_DEFAULT_SCREEN_DPI / dpi),
				static_cast<float>(GET_Y_LPARAM(wParam) * USER_DEFAULT_SCREEN_DPI / dpi)
			};

			SetWindowPos(g_hWndXaml, 0, 0, 0, 0, 0, SWP_NOZORDER | SWP_SHOWWINDOW);
			SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 1, 1, SWP_SHOWWINDOW);
			SetForegroundWindow(hWnd);

			g_xamlMenu.ShowAt(g_xamlCanvas, point);
		}
		break;
		}
		break;
	case WM_SHOWDEVICEPICKER:
		ShowDevicePicker();
		break;
	case WM_CONNECTIONCLOSED:
		OnConnectionClosed(static_cast<std::uintptr_t>(wParam));
		break;
	case WM_RUNONUITHREAD:
		RunUiThreadQueue();
		break;
	case WM_CONNECTDEVICE:
		if (g_reconnect)
		{
			for (const auto& i : g_lastDevices)
			{
				ConnectDevice(g_devicePicker, i);
			}
			g_lastDevices.clear();
		}
		break;
	default:
		if (WM_TASKBAR_CREATED && message == WM_TASKBAR_CREATED)
		{
			UpdateNotifyIcon();
		}
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}
	return 0;
}

void ShowDevicePicker()
{
	using namespace winrt::Windows::UI::Popups;

	RECT iconRect;
	auto hr = Shell_NotifyIconGetRect(&g_niid, &iconRect);
	if (FAILED(hr))
	{
		LOG_HR(hr);
		return;
	}

	auto dpi = GetDpiForWindow(g_hWnd);
	Rect rect = {
		static_cast<float>(iconRect.left * USER_DEFAULT_SCREEN_DPI / dpi),
		static_cast<float>(iconRect.top * USER_DEFAULT_SCREEN_DPI / dpi),
		static_cast<float>((iconRect.right - iconRect.left) * USER_DEFAULT_SCREEN_DPI / dpi),
		static_cast<float>((iconRect.bottom - iconRect.top) * USER_DEFAULT_SCREEN_DPI / dpi)
	};

	SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_HIDEWINDOW);
	SetForegroundWindow(g_hWnd);

	try
	{
		g_devicePicker.Show(rect, Placement::Above);
	}
	catch (winrt::hresult_error const&)
	{
		// Show() throws if the picker is already open; letting it escape WndProc would
		// terminate the process.
		LOG_CAUGHT_EXCEPTION();
	}
}

void ActivateExistingInstance()
{
	// The second instance only wakes up the existing one, then exits.
	HWND existing = nullptr;
	for (int i = 0; i < 10 && !existing; ++i)
	{
		// Covers the race where both instances start almost simultaneously and the
		// window does not exist yet.
		existing = FindWindowW(WINDOW_CLASS_NAME, nullptr);
		if (!existing)
		{
			Sleep(50);
		}
	}
	if (!existing)
	{
		return;
	}

	DWORD pid = 0;
	GetWindowThreadProcessId(existing, &pid);
	AllowSetForegroundWindow(pid);
	PostMessageW(existing, WM_SHOWDEVICEPICKER, 0, 0);
}

void SetupFlyout()
{
	TextBlock textBlock;
	textBlock.Text(L"All connections will be closed.\nExit anyway?");
	textBlock.Margin({ 0, 0, 0, 12 });

	// Not static: a function-local static would be released by the CRT after the STA is
	// gone. The Click handler holds its own reference, released together with the flyout.
	CheckBox checkbox;
	checkbox.IsChecked(g_reconnect);
	checkbox.Content(winrt::box_value(L"Reconnect on next start"));

	Button button;
	button.Content(winrt::box_value(L"Exit"));
	button.HorizontalAlignment(HorizontalAlignment::Right);
	button.Click([checkbox](const auto&, const auto&) {
		g_reconnect = checkbox.IsChecked().Value();
		PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
	});

	StackPanel stackPanel;
	stackPanel.Children().Append(textBlock);
	stackPanel.Children().Append(checkbox);
	stackPanel.Children().Append(button);

	Flyout flyout;
	flyout.ShouldConstrainToRootBounds(false);
	flyout.Content(stackPanel);

	g_xamlFlyout = flyout;
}

void SetDeviceDisplayStatus(DevicePicker const& picker, DeviceInformation const& device, std::wstring_view status, DevicePickerDisplayStatusOptions options)
{
	// The picker is agile, but a throwing status update must not escape a fire_and_forget
	// coroutine: an unhandled hresult_error there terminates the process.
	try
	{
		picker.SetDisplayStatus(device, winrt::hstring(status), options);
	}
	catch (winrt::hresult_error const&)
	{
		LOG_CAUGHT_EXCEPTION();
	}
}

// Closes every active connection and reconnects the same devices after a short delay.
// This is the workaround for the "connected but silent" state: the close makes the phone
// see an A2DP disconnect (its playback pauses) and tears down the whole AVDTP session,
// and the reconnect starts a completely fresh one whose stream reaches the output device.
winrt::fire_and_forget ReconnectAll()
{
	if (g_shuttingDown || g_reconnecting || g_audioPlaybackConnections.empty())
	{
		co_return;
	}

	g_reconnecting = true;
	auto reconnectGuard = wil::scope_exit([] { g_reconnecting = false; });

	// Captured on the UI thread: resume_after() resumes on a thread-pool thread, and the
	// picker and the connection table must only be touched from the UI thread again. This
	// only returns to the UI thread because it is an STA (see wWinMain); a context captured
	// in the MTA would resume on any MTA thread.
	winrt::apartment_context uiContext;

	std::vector<DeviceInformation> devices;
	try
	{
		devices.reserve(g_audioPlaybackConnections.size());
		for (const auto& entry : g_audioPlaybackConnections)
		{
			devices.push_back(entry.second.Device);
		}

		for (const auto& device : devices)
		{
			SetDeviceDisplayStatus(g_devicePicker, device, L"Reconnecting", DevicePickerDisplayStatusOptions::ShowProgress);
			RemoveAudioPlaybackConnection(std::wstring(device.Id()));
		}

		// Give the phone time to notice the disconnect (and pause its media) and the audio
		// engine time to tear the old stream down before the fresh connection is opened.
		co_await winrt::resume_after(RECONNECT_SETTLE_DELAY);
		co_await uiContext;
		WI_ASSERT(GetCurrentThreadId() == g_uiThreadId);

		if (g_shuttingDown)
		{
			co_return;
		}

		for (const auto& device : devices)
		{
			ConnectDevice(g_devicePicker, device);
		}
	}
	catch (winrt::hresult_error const&)
	{
		LOG_CAUGHT_EXCEPTION();
	}
	catch (...)
	{
		// A fire_and_forget coroutine must not let any exception escape.
	}
}

void SetupMenu()
{
	// https://docs.microsoft.com/en-us/windows/uwp/design/style/segoe-ui-symbol-font
	FontIcon reconnectIcon;
	reconnectIcon.Glyph(L"\xE72C");

	MenuFlyoutItem reconnectItem;
	reconnectItem.Text(L"Reconnect");
	reconnectItem.Icon(reconnectIcon);
	reconnectItem.Click([](const auto&, const auto&) {
		ReconnectAll();
	});

	FontIcon settingsIcon;
	settingsIcon.Glyph(L"\xE713");

	MenuFlyoutItem settingsItem;
	settingsItem.Text(L"Bluetooth Settings");
	settingsItem.Icon(settingsIcon);
	settingsItem.Click([](const auto&, const auto&) {
		winrt::Windows::System::Launcher::LaunchUriAsync(Uri(L"ms-settings:bluetooth"));
	});

	FontIcon closeIcon;
	closeIcon.Glyph(L"\xE8BB");

	MenuFlyoutItem exitItem;
	exitItem.Text(L"Exit");
	exitItem.Icon(closeIcon);
	exitItem.Click([](const auto&, const auto&) {
		if (g_audioPlaybackConnections.size() == 0)
		{
			PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
			return;
		}

		RECT iconRect;
		auto hr = Shell_NotifyIconGetRect(&g_niid, &iconRect);
		if (FAILED(hr))
		{
			LOG_HR(hr);
			return;
		}

		auto dpi = GetDpiForWindow(g_hWnd);

		SetWindowPos(g_hWnd, HWND_TOPMOST, iconRect.left, iconRect.top, 0, 0, SWP_HIDEWINDOW);
		g_xamlCanvas.Width(static_cast<float>((iconRect.right - iconRect.left) * USER_DEFAULT_SCREEN_DPI / dpi));
		g_xamlCanvas.Height(static_cast<float>((iconRect.bottom - iconRect.top) * USER_DEFAULT_SCREEN_DPI / dpi));

		g_xamlFlyout.ShowAt(g_xamlCanvas);
	});

	MenuFlyout menu;
	menu.Items().Append(reconnectItem);
	menu.Items().Append(settingsItem);
	menu.Items().Append(exitItem);
	menu.Opened([reconnectItem](const auto& sender, const auto&) {
		// Reconnecting only makes sense when there is a session to rebuild, and it must not
		// be started again while the previous attempt is still settling.
		reconnectItem.IsEnabled(!g_shuttingDown && !g_reconnecting && !g_audioPlaybackConnections.empty());

		auto menuItems = sender.as<MenuFlyout>().Items();
		auto itemsCount = menuItems.Size();
		if (itemsCount > 0)
		{
			menuItems.GetAt(itemsCount - 1).Focus(g_menuFocusState);
		}
		g_menuFocusState = FocusState::Unfocused;
	});
	menu.Closed([](const auto&, const auto&) {
		ShowWindow(g_hWnd, SW_HIDE);
	});

	g_xamlMenu = menu;
}

// Removes the tracked session for a device and then closes it. The entry is detached from the
// table before Close() is called, so the table is never modified while an iterator into it is
// live. Close() raises StateChanged(Closed) synchronously; the handler only posts
// WM_CONNECTIONCLOSED, and by the time that is processed the session is gone, so
// OnConnectionClosed finds nothing to do.
void RemoveAudioPlaybackConnection(std::wstring const& deviceId)
{
	WI_ASSERT(GetCurrentThreadId() == g_uiThreadId);

	auto it = g_audioPlaybackConnections.find(deviceId);
	if (it == g_audioPlaybackConnections.end())
	{
		return;
	}

	AudioPlaybackConnection connection = it->second.Connection;
	g_audioPlaybackConnections.erase(it);
	try
	{
		connection.Close();
	}
	catch (winrt::hresult_error const&)
	{
		LOG_CAUGHT_EXCEPTION();
	}
}

// Handles WM_CONNECTIONCLOSED on the UI thread: a connection reached the Closed state, usually
// because the remote device disconnected. sessionId identifies the session whose connection
// raised the event. If the app already removed that session (disconnect button, failed
// connect, ReconnectAll, shutdown) or replaced it with a newer one for the same device, no
// entry matches and nothing happens.
void OnConnectionClosed(std::uintptr_t sessionId)
{
	WI_ASSERT(GetCurrentThreadId() == g_uiThreadId);

	auto it = g_audioPlaybackConnections.begin();
	while (it != g_audioPlaybackConnections.end() && it->second.Id != sessionId)
	{
		++it;
	}
	if (it == g_audioPlaybackConnections.end())
	{
		return;
	}

	AudioPlaybackSession session = std::move(it->second);
	g_audioPlaybackConnections.erase(it);

	SetDeviceDisplayStatus(g_devicePicker, session.Device, {}, DevicePickerDisplayStatusOptions::None);
	try
	{
		// Release the connection's resources. If this raises StateChanged again, the posted
		// WM_CONNECTIONCLOSED finds no session.
		session.Connection.Close();
	}
	catch (winrt::hresult_error const&)
	{
		LOG_CAUGHT_EXCEPTION();
	}
}

// Runs work on the UI thread. Safe to call from any thread and never blocks: the item is
// queued and WM_RUNONUITHREAD is posted. Used for DevicePicker events, which arrive on
// background threads. Work posted after the message loop has ended is dropped.
void PostToUiThread(std::function<void()> work)
{
	{
		std::lock_guard lock(g_uiThreadQueueLock);
		if (g_uiThreadQueueClosed)
		{
			return;
		}
		g_uiThreadQueue.push_back(std::move(work));
	}
	// If posting fails, the item stays queued and runs with the next successful post.
	LOG_IF_WIN32_BOOL_FALSE(PostMessageW(g_hWnd, WM_RUNONUITHREAD, 0, 0));
}

// Handles WM_RUNONUITHREAD: runs every queued item on the UI thread.
void RunUiThreadQueue()
{
	WI_ASSERT(GetCurrentThreadId() == g_uiThreadId);

	// Take the whole batch first: an item may post more work or re-enter the message loop.
	std::vector<std::function<void()>> work;
	{
		std::lock_guard lock(g_uiThreadQueueLock);
		work.swap(g_uiThreadQueue);
	}
	for (auto& item : work)
	{
		try
		{
			item();
		}
		CATCH_LOG(); // an exception must not escape WndProc
	}
}

winrt::fire_and_forget ConnectDevice(DevicePicker picker, DeviceInformation device)
{
	// Must start on the UI thread: the co_awaits below resume in the calling apartment.
	WI_ASSERT(GetCurrentThreadId() == g_uiThreadId);

	const std::wstring deviceId(device.Id());
	if (g_audioPlaybackConnections.find(deviceId) != g_audioPlaybackConnections.end())
	{
		// Already connecting or connected: reuse the existing session. Creating a second
		// connection object would let the failure path close the old one.
		co_return;
	}

	SetDeviceDisplayStatus(picker, device, L"Connecting", DevicePickerDisplayStatusOptions::ShowProgress | DevicePickerDisplayStatusOptions::ShowDisconnectButton);

	bool success = false;
	std::wstring errorMessage;
	std::uintptr_t sessionId = 0;

	try
	{
		auto connection = AudioPlaybackConnection::TryCreateFromId(device.Id());
		if (connection)
		{
			sessionId = g_nextSessionId++;

			AudioPlaybackSession session;
			session.Device = device;
			session.Connection = connection;
			session.Id = sessionId;
			g_audioPlaybackConnections.emplace(deviceId, std::move(session));

			connection.StateChanged([sessionId](const auto& sender, const auto&) {
				// Raised on a background thread when the remote device disconnects, and
				// synchronously on the caller's thread from Close(). Never touch app state
				// here: hand the session id to the UI thread, which owns the connection table.
				// g_hWnd is written once before any connection exists. If the window is
				// already gone (shutdown), PostMessageW fails harmlessly: WM_DESTROY has
				// closed every connection.
				if (sender.State() == AudioPlaybackConnectionState::Closed)
				{
					PostMessageW(g_hWnd, WM_CONNECTIONCLOSED, static_cast<WPARAM>(sessionId), 0);
				}
			});

			co_await connection.StartAsync();
			auto result = co_await connection.OpenAsync();
			// co_await on a WinRT async operation resumes in the calling apartment, i.e. the
			// UI thread (an STA).
			WI_ASSERT(GetCurrentThreadId() == g_uiThreadId);

			switch (result.Status())
			{
			case AudioPlaybackConnectionOpenResultStatus::Success:
				success = true;
				break;
			case AudioPlaybackConnectionOpenResultStatus::RequestTimedOut:
				success = false;
				errorMessage = L"The request timed out";
				break;
			case AudioPlaybackConnectionOpenResultStatus::DeniedBySystem:
				success = false;
				errorMessage = L"The operation was denied by the system";
				break;
			case AudioPlaybackConnectionOpenResultStatus::UnknownFailure:
				success = false;
				winrt::throw_hresult(result.ExtendedError());
				break;
			}
		}
		else
		{
			success = false;
			errorMessage = L"Unknown error";
		}
	}
	catch (winrt::hresult_error const& ex)
	{
		success = false;
		errorMessage.resize(64);
		while (1)
		{
			auto result = swprintf(errorMessage.data(), errorMessage.size(), L"%s (0x%08X)", ex.message().c_str(), static_cast<uint32_t>(ex.code()));
			if (result < 0)
			{
				errorMessage.resize(errorMessage.size() * 2);
			}
			else
			{
				errorMessage.resize(result);
				break;
			}
		}
		LOG_CAUGHT_EXCEPTION();
	}

	auto it = g_audioPlaybackConnections.find(deviceId);
	if (it != g_audioPlaybackConnections.end() && it->second.Id == sessionId)
	{
		if (success)
		{
			SetDeviceDisplayStatus(picker, device, L"Connected", DevicePickerDisplayStatusOptions::ShowDisconnectButton);
		}
		else
		{
			// Only clean up the session this call created: a late failure must not close a
			// newer session for the same device (for example one created by ReconnectAll).
			RemoveAudioPlaybackConnection(deviceId);
			SetDeviceDisplayStatus(picker, device, errorMessage, DevicePickerDisplayStatusOptions::ShowRetryButton);
		}
	}
	else if (!success && it == g_audioPlaybackConnections.end())
	{
		// Do not overwrite the status of a newer session that already took over the device.
		SetDeviceDisplayStatus(picker, device, errorMessage, DevicePickerDisplayStatusOptions::ShowRetryButton);
	}
}

winrt::fire_and_forget ConnectDevice(DevicePicker picker, std::wstring_view deviceId)
{
	// Must start on the UI thread: the co_await below resumes in the calling apartment.
	WI_ASSERT(GetCurrentThreadId() == g_uiThreadId);

	DeviceInformation device = nullptr;
	try
	{
		device = co_await DeviceInformation::CreateFromIdAsync(deviceId);
	}
	catch (winrt::hresult_error const&)
	{
		// The device may have been removed or unpaired; the exception must not escape a
		// fire_and_forget coroutine (that terminates the process).
		LOG_CAUGHT_EXCEPTION();
	}

	if (device)
	{
		ConnectDevice(picker, device);
	}
}

void SetupDevicePicker()
{
	g_devicePicker = DevicePicker();
	winrt::check_hresult(g_devicePicker.as<IInitializeWithWindow>()->Initialize(g_hWnd));

	g_devicePicker.Filter().SupportedDeviceSelectors().Append(AudioPlaybackConnection::GetDeviceSelector());

	// DevicePicker raises these events on background (RPC) threads, not on the UI thread,
	// even though the UI thread is an STA: C++/WinRT delegates are agile, so COM does not
	// marshal the calls back. Read what is needed from the event args here, then do the
	// actual work on the UI thread, which owns the connection table and the window.
	g_devicePicker.DevicePickerDismissed([](const auto&, const auto&) {
		PostToUiThread([] {
			SetWindowPos(g_hWnd, nullptr, 0, 0, 0, 0, SWP_NOZORDER | SWP_HIDEWINDOW);
		});
	});
	g_devicePicker.DeviceSelected([](const auto&, const auto& args) {
		PostToUiThread([device = args.SelectedDevice()] {
			ConnectDevice(g_devicePicker, device);
		});
	});
	g_devicePicker.DisconnectButtonClicked([](const auto&, const auto& args) {
		PostToUiThread([device = args.Device()] {
			RemoveAudioPlaybackConnection(std::wstring(device.Id()));
			SetDeviceDisplayStatus(g_devicePicker, device, {}, DevicePickerDisplayStatusOptions::None);
		});
	});
}

void SetupSvgIcon()
{
	auto hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(1), L"SVG");
	FAIL_FAST_LAST_ERROR_IF_NULL(hRes);

	auto size = SizeofResource(g_hInst, hRes);
	FAIL_FAST_LAST_ERROR_IF(size == 0);

	auto hResData = LoadResource(g_hInst, hRes);
	FAIL_FAST_LAST_ERROR_IF_NULL(hResData);

	auto svgData = reinterpret_cast<const char*>(LockResource(hResData));
	FAIL_FAST_IF_NULL_ALLOC(svgData);

	const std::string_view svg(svgData, size);
	const int width = GetSystemMetrics(SM_CXSMICON), height = GetSystemMetrics(SM_CYSMICON);

	g_hIconLight = SvgTohIcon(svg, width, height, { 0, 0, 0, 1 });
	g_hIconDark = SvgTohIcon(svg, width, height, { 1, 1, 1, 1 });
}

void UpdateNotifyIcon()
{
	DWORD value = 0, cbValue = sizeof(value);
	LOG_IF_WIN32_ERROR(RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)", L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &cbValue));
	g_nid.hIcon = value != 0 ? g_hIconLight : g_hIconDark;

	if (!Shell_NotifyIconW(NIM_MODIFY, &g_nid))
	{
		if (Shell_NotifyIconW(NIM_ADD, &g_nid))
		{
			FAIL_FAST_IF_WIN32_BOOL_FALSE(Shell_NotifyIconW(NIM_SETVERSION, &g_nid));
		}
		else
		{
			LOG_LAST_ERROR();
		}
	}
}
