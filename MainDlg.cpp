#include "stdafx.h"
#include "resource.h"

#include "MainDlg.h"
#include "TextDlg.h"
#include "SettingsDlg.h"
#include "Functions.h"
#include "SelectionCapture.h"
#include "VaultMarkerFormatter.h"
#include "update.h"
#include "version.h"

BOOL CMainDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	// Center the dialog on the screen.
	CenterWindow();

	// Set icons.
	ReloadMainIcon();

	// Load and apply config.
	m_config.emplace();
	ApplyUiLanguage();
	ApplyMouseAndKeyboardHotKeys();
	ConfigToGui();

	if(lInitParam || m_config->m_hideWndOnStartup)
	{
		m_hideDialog = true;
		// Setting WS_EX_NOACTIVATE is a workaround for preventing the dialog
		// from stealing focus on startup.
		ModifyStyleEx(0, WS_EX_NOACTIVATE);
	}

	// Init and show tray icon.
	InitNotifyIconData();

	if(!m_config->m_hideTrayIcon)
	{
		Shell_NotifyIcon(NIM_ADD, &m_notifyIconData);
	}

	// Start timer to check for updates.
	if(m_config->m_checkForUpdates)
	{
		SetTimer(TIMER_UPDATE_CHECK, 1000 * 10, NULL); // 10sec
	}

	RememberIniFileState();
	SetTimer(TIMER_CONFIG_RELOAD, 500, NULL);

	return TRUE;
}

void CMainDlg::OnDestroy()
{
	UninitMouseAndKeyboardHotKeys();
	KillTimer(TIMER_CONFIG_RELOAD);

	if(!m_config->m_hideTrayIcon)
	{
		Shell_NotifyIcon(NIM_DELETE, &m_notifyIconData);
	}

	if(m_config->m_checkForUpdates)
	{
		KillTimer(TIMER_UPDATE_CHECK);
	}

	// From GDI handle checks, not all icons are freed automatically.
	::DestroyIcon(SetIcon(nullptr, TRUE));
	::DestroyIcon(SetIcon(nullptr, FALSE));
	::DestroyIcon(CStatic(GetDlgItem(IDC_MAIN_ICON)).SetIcon(nullptr));
}

void CMainDlg::OnWindowPosChanging(LPWINDOWPOS lpWndPos)
{
	if(m_hideDialog && (lpWndPos->flags & SWP_SHOWWINDOW))
	{
		ModifyStyleEx(WS_EX_NOACTIVATE, 0);
		lpWndPos->flags &= ~SWP_SHOWWINDOW;
		m_hideDialog = false;
	}
}

LRESULT CMainDlg::OnNotify(int idCtrl, LPNMHDR pnmh)
{
	switch(pnmh->idFrom)
	{
	case IDC_MAIN_SYSLINK:
		switch(pnmh->code)
		{
		case NM_CLICK:
		case NM_RETURN:
			if((int)(DWORD_PTR)ShellExecute(m_hWnd, L"open", ((PNMLINK)pnmh)->item.szUrl, NULL, NULL, SW_SHOWNORMAL) <= 32)
			{
				CString title;
				title.LoadString(IDS_ERROR);

				CString text;
				text.LoadString(IDS_ERROR_OPEN_ADDRESS);
				text += L"\n";
				text += ((PNMLINK)pnmh)->item.szUrl;

				MessageBox(text, title, MB_ICONERROR);
			}
			break;
		}
		break;
	}

	SetMsgHandled(FALSE);
	return 0;
}

void CMainDlg::OnHotKey(int nHotKeyID, UINT uModifiers, UINT uVirtKey)
{
	if(nHotKeyID == 1)
	{
		ShowCapturedText(false, ::GetForegroundWindow());
	}
}

void CMainDlg::OnTimer(UINT_PTR nIDEvent)
{
	if(nIDEvent == TIMER_CONFIG_RELOAD)
	{
		CheckForIniChanges();
		return;
	}

	if(nIDEvent == TIMER_UPDATE_CHECK)
	{
		KillTimer(TIMER_UPDATE_CHECK);

		if(UpdateCheckInit(m_hWnd, UWM_UPDATE_CHECKED))
		{
			if(UpdateCheckQueue())
			{
				m_checkingForUpdates = true;
			}
			else
			{
				UpdateCheckCleanup();
			}
		}

		if(!m_checkingForUpdates)
		{
			SetTimer(TIMER_UPDATE_CHECK, 1000 * 60 * 60, NULL); // 1h
		}
	}
}

void CMainDlg::OnDpiChanged(UINT nDpiX, UINT nDpiY, PRECT pRect)
{
	ReloadMainIcon();
}

void CMainDlg::OnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	bool ctrlKey = (CButton(GetDlgItem(IDC_CHECK_CTRL)).GetCheck() != BST_UNCHECKED);
	bool altKey = (CButton(GetDlgItem(IDC_CHECK_ALT)).GetCheck() != BST_UNCHECKED);
	bool shiftKey = (CButton(GetDlgItem(IDC_CHECK_SHIFT)).GetCheck() != BST_UNCHECKED);

	if(!ctrlKey && !altKey && !shiftKey)
	{
		CString title;
		title.LoadString(IDS_MAINDLG_WARNING_MODIFIER_TITLE);

		CString text;
		text.LoadString(IDS_MAINDLG_WARNING_MODIFIER_TEXT);

		if(MessageBox(text, title, MB_ICONWARNING | MB_YESNO) != IDYES)
		{
			return;
		}
	}

	int mouseKey;
	auto keysComboWnd = CComboBox(GetDlgItem(IDC_COMBO_KEYS));
	switch(keysComboWnd.GetCurSel())
	{
	case 0:
		mouseKey = VK_LBUTTON;
		break;

	case 1:
		mouseKey = VK_RBUTTON;
		break;

	case 2:
		mouseKey = VK_MBUTTON;
		break;
	}

	HotKey& mouseHotKey = m_config->m_mouseHotKey;
	mouseHotKey.ctrl = ctrlKey;
	mouseHotKey.alt = altKey;
	mouseHotKey.shift = shiftKey;
	mouseHotKey.key = mouseKey;

	m_config->SaveToIniFile();
	RememberIniFileState();

	ApplyMouseAndKeyboardHotKeys();

	CButton(GetDlgItem(IDOK)).EnableWindow(FALSE);
}

void CMainDlg::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	ShowWindow(SW_HIDE);
}

void CMainDlg::OnShowIni(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CSettingsDlg settingsDlg;
	INT_PTR nRet = settingsDlg.DoModal();
	if(nRet == IDOK)
	{
		ReloadConfigFromIni();
		RememberIniFileState();
	}
}

void CMainDlg::OnExitButton(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	Exit();
}

void CMainDlg::OnConfigChanged(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CButton(GetDlgItem(IDOK)).EnableWindow();
}

LRESULT CMainDlg::OnMouseHookClicked(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	//CPoint ptEvent{ static_cast<int>(wParam), static_cast<int>(lParam) };

	// Instead of using the mouse hook coordinates, we query the cursor position
	// again, because the hooked coordinates end up incorrect for DPI-unaware
	// applications in high DPI environment.

	const HotKey& mouseHotKey = m_config->m_mouseHotKey;
	bool prependTagMarker =
		mouseHotKey.key == VK_RBUTTON &&
		mouseHotKey.ctrl &&
		!mouseHotKey.alt &&
		!mouseHotKey.shift;
	ShowCapturedText(prependTagMarker, reinterpret_cast<HWND>(wParam));

	return 0;
}

void CMainDlg::ShowCapturedText(bool prependTagMarker, HWND targetWindow)
{
	if(m_textDialogOpen)
		return;

	m_textDialogOpen = true;
	if(m_registeredHotKey)
	{
		::UnregisterHotKey(m_hWnd, 1);
		m_registeredHotKey = false;
	}

	CPoint pt;
	GetCursorPos(&pt);
	CString selectedText = CaptureCurrentSelection(targetWindow);
	if(prependTagMarker)
	{
		selectedText = AddVaultSectionMarkers(
			std::wstring(selectedText.GetString(), selectedText.GetLength())).c_str();
	}
	CTextDlg dlgText(
		*m_config,
		selectedText,
		m_codeDefaultDestinationLabel,
		m_manualDefaultDestinationLabel);
	dlgText.DoModal(NULL, reinterpret_cast<LPARAM>(&pt));
	m_textDialogOpen = false;
	ApplyMouseAndKeyboardHotKeys();
	m_codeDefaultDestinationLabel = dlgText.GetCodeDefaultDestinationLabel();
	m_manualDefaultDestinationLabel = dlgText.GetManualDefaultDestinationLabel();
	ReconcileClippingDefaultLabels();
}

bool CMainDlg::QueryIniFileState(IniFileState& state) const
{
	WIN32_FILE_ATTRIBUTE_DATA data{};
	CPath iniFilePath = UserConfig::GetIniFilePath();
	if(!GetFileAttributesEx(iniFilePath, GetFileExInfoStandard, &data))
	{
		DWORD error = GetLastError();
		state = {};
		return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
	}

	state.exists = true;
	state.lastWriteTime = data.ftLastWriteTime;
	state.size = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) |
		data.nFileSizeLow;
	return true;
}

bool CMainDlg::IniFileStatesEqual(
	const IniFileState& first,
	const IniFileState& second)
{
	return first.exists == second.exists &&
		(!first.exists ||
			(first.size == second.size &&
				CompareFileTime(&first.lastWriteTime, &second.lastWriteTime) == 0));
}

void CMainDlg::RememberIniFileState()
{
	IniFileState state;
	if(QueryIniFileState(state))
		m_appliedIniFileState = state;
	m_hasPendingIniFileState = false;
}

void CMainDlg::CheckForIniChanges()
{
	IniFileState currentState;
	if(!QueryIniFileState(currentState))
		return;

	if(IniFileStatesEqual(currentState, m_appliedIniFileState))
	{
		m_hasPendingIniFileState = false;
		return;
	}

	// Require the same changed state on two consecutive timer ticks. Editors
	// often replace INI files in several steps; this avoids loading a partial
	// write while still applying changes in about one second.
	if(!m_hasPendingIniFileState ||
		!IniFileStatesEqual(currentState, m_pendingIniFileState))
	{
		m_pendingIniFileState = currentState;
		m_hasPendingIniFileState = true;
		return;
	}

	ReloadConfigFromIni();
	m_appliedIniFileState = currentState;
	m_hasPendingIniFileState = false;
}

void CMainDlg::ReloadConfigFromIni()
{
	LANGID oldUiLanguage = m_config->m_uiLanguage;
	bool oldCheckForUpdates = m_config->m_checkForUpdates;
	bool oldHideTrayIcon = m_config->m_hideTrayIcon;

	// Always unregister first. ApplyMouseAndKeyboardHotKeys deliberately keeps
	// an already registered keyboard shortcut, so re-registering is required
	// when an external INI edit changes one non-zero shortcut to another.
	UninitMouseAndKeyboardHotKeys();
	m_config.emplace();
	ReconcileClippingDefaultLabels();

	if(oldUiLanguage != m_config->m_uiLanguage)
		ApplyUiLanguage();

	ApplyMouseAndKeyboardHotKeys();
	ConfigToGui();

	if(m_config->m_hideTrayIcon != oldHideTrayIcon)
	{
		Shell_NotifyIcon(m_config->m_hideTrayIcon ? NIM_DELETE : NIM_ADD, &m_notifyIconData);
	}

	if(m_config->m_checkForUpdates != oldCheckForUpdates && !m_checkingForUpdates)
	{
		if(m_config->m_checkForUpdates)
			SetTimer(TIMER_UPDATE_CHECK, 1000 * 10, NULL); // 10sec
		else
			KillTimer(TIMER_UPDATE_CHECK);
	}
}

void CMainDlg::ReconcileClippingDefaultLabels()
{
	auto reconcileDefaultLabel = [](const std::vector<ClippingDestinationInfo>& destinations, CString& label)
	{
		for(const auto& destination : destinations)
		{
			if(destination.label.CompareNoCase(label) == 0)
			{
				label = destination.label;
				return;
			}
		}
		if(destinations.empty())
			label.Empty();
		else
			label = destinations.front().label;
	};
	reconcileDefaultLabel(m_config->m_codeClippingDestinations, m_codeDefaultDestinationLabel);
	reconcileDefaultLabel(m_config->m_manualClippingDestinations, m_manualDefaultDestinationLabel);
}

LRESULT CMainDlg::OnTaskbarCreated(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if(!m_config->m_hideTrayIcon)
	{
		// Reload icon since the DPI might have changed. From the documentation:
		// "On Windows 10, the taskbar also broadcasts this message when the DPI of
		// the primary display changes."
		m_trayIcon = LoadTrayIcon();
		m_notifyIconData.hIcon = m_trayIcon;

		Shell_NotifyIcon(NIM_ADD, &m_notifyIconData);

		// Necessary to apply the newly loaded icon in Windows 11 22H2.
		Shell_NotifyIcon(NIM_MODIFY, &m_notifyIconData);
	}

	return 0;
}

LRESULT CMainDlg::OnCustomTextifyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(lParam)
	{
	case 1:
		Exit();
		break;
	}

	return 0;
}

LRESULT CMainDlg::OnNotifyIcon(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if(wParam == 1)
	{
		switch(lParam)
		{
		case WM_LBUTTONUP:
			ShowWindow(SW_SHOWNORMAL);
			::SetForegroundWindow(GetLastActivePopup());
			break;

		case WM_RBUTTONUP:
			if(IsWindowEnabled())
			{
				::SetForegroundWindow(m_hWnd);
				NotifyIconRightClickMenu();
			}
			else
			{
				ShowWindow(SW_SHOWNORMAL);
				::SetForegroundWindow(GetLastActivePopup());
			}
			break;
		}
	}

	return 0;
}

LRESULT CMainDlg::OnBringToFront(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	ShowWindow(SW_SHOWNORMAL);
	::SetForegroundWindow(GetLastActivePopup());
	return 0;
}

LRESULT CMainDlg::OnUpdateChecked(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	UpdateCheckCleanup();

	if(m_config->m_checkForUpdates)
	{
		if(lParam == ERROR_SUCCESS)
		{
			DWORD dwUpdateVersion = UpdateCheckGetVersionLong();
			if(dwUpdateVersion && dwUpdateVersion > VER_FILE_VERSION_LONG)
			{
				HWND hPopup = IsWindowEnabled() ? m_hWnd : GetLastActivePopup().m_hWnd;
				UpdateTaskDialog(hPopup, UpdateCheckGetVersion());
			}

			UpdateCheckFreeVersion();

			SetTimer(TIMER_UPDATE_CHECK, 1000 * 60 * 60 * 24, NULL); // 24h
		}
		else
		{
			SetTimer(TIMER_UPDATE_CHECK, 1000 * 60 * 60, NULL); // 1h
		}
	}

	m_checkingForUpdates = false;

	if(m_closeWhenUpdateCheckDone)
	{
		::PostQuitMessage(0);
	}

	return 0;
}

LRESULT CMainDlg::OnExit(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	Exit();
	return 0;
}

void CMainDlg::ReloadMainIcon()
{
	UINT dpi = GetDpiForWindowWithFallback(m_hWnd);

	HICON mainIcon = nullptr;
	LoadIconWithScaleDown(
		ModuleHelper::GetResourceInstance(),
		MAKEINTRESOURCE(IDR_MAINFRAME),
		GetSystemMetricsForDpiWithFallback(SM_CXICON, dpi),
		GetSystemMetricsForDpiWithFallback(SM_CYICON, dpi),
		&mainIcon);
	CIcon prevMainIcon = SetIcon(mainIcon, TRUE);

	CIcon prevDlgMainIcon = CStatic(GetDlgItem(IDC_MAIN_ICON)).SetIcon(CopyIcon(mainIcon));

	HICON mainIconSmall = nullptr;
	LoadIconWithScaleDown(
		ModuleHelper::GetResourceInstance(),
		MAKEINTRESOURCE(IDR_MAINFRAME),
		GetSystemMetricsForDpiWithFallback(SM_CXSMICON, dpi),
		GetSystemMetricsForDpiWithFallback(SM_CYSMICON, dpi),
		&mainIconSmall);
	CIcon prevMainIconSmall = SetIcon(mainIconSmall, FALSE);
}

void CMainDlg::ApplyUiLanguage()
{
	LANGID uiLanguage = m_config->m_uiLanguage;

	if(!uiLanguage)
	{
		CRegKey regKey;

		DWORD dwError = regKey.Open(HKEY_CURRENT_USER, L"Software\\Textify", KEY_QUERY_VALUE);
		if(dwError == ERROR_SUCCESS)
		{
			DWORD dwLanguage;
			dwError = regKey.QueryDWORDValue(L"language", dwLanguage);
			if(dwError == ERROR_SUCCESS)
			{
				uiLanguage = static_cast<LANGID>(dwLanguage);
			}
		}
	}

	if(uiLanguage)
	{
		::SetThreadUILanguage(uiLanguage);
	}

	CString str;

	str.LoadString(IDS_MAINDLG_TITLE);
	SetWindowText(str);

	str.LoadString(IDS_MAINDLG_HOMEPAGE);

	CString headerStr;
	headerStr.LoadString(IDS_MAINDLG_HEADER);
	headerStr.Replace(L"%s", VER_FILE_VERSION_WSTR);
	headerStr += L"\n<a href=\"https://ramensoftware.com/textify\">" + str + L"</a>";
	SetDlgItemText(IDC_MAIN_SYSLINK, headerStr);

	str.LoadString(IDS_MAINDLG_MOUSE_SHORTCUT);
	SetDlgItemText(IDC_MOUSE_SHORTCUT, str);

	str.LoadString(IDS_MAINDLG_CTRL);
	SetDlgItemText(IDC_CHECK_CTRL, str);

	str.LoadString(IDS_MAINDLG_ALT);
	SetDlgItemText(IDC_CHECK_ALT, str);

	str.LoadString(IDS_MAINDLG_SHIFT);
	SetDlgItemText(IDC_CHECK_SHIFT, str);

	CComboBox keysComboWnd(GetDlgItem(IDC_COMBO_KEYS));
	int keysComboCurSel = keysComboWnd.GetCurSel();
	keysComboWnd.ResetContent();
	str.LoadString(IDS_MAINDLG_MOUSE_LEFT);
	keysComboWnd.AddString(str);
	str.LoadString(IDS_MAINDLG_MOUSE_RIGHT);
	keysComboWnd.AddString(str);
	str.LoadString(IDS_MAINDLG_MOUSE_MIDDLE);
	keysComboWnd.AddString(str);
	keysComboWnd.SetCurSel(keysComboCurSel);

	str.LoadString(IDS_MAINDLG_APPLY);
	SetDlgItemText(IDOK, str);

	str.LoadString(IDS_MAINDLG_ADVANCED);
	SetDlgItemText(IDC_ADVANCED, str);

	str.LoadString(IDS_MAINDLG_MORE_SETTINGS);
	SetDlgItemText(IDC_SHOW_INI, str);

	str.LoadString(IDS_MAINDLG_EXIT);
	SetDlgItemText(IDC_EXIT, str);
}

void CMainDlg::ApplyMouseAndKeyboardHotKeys()
{
	if(m_config->m_mouseHotKey.key != 0)
	{
		_ATLTRY
		{
			const HotKey & mouseHotKey = m_config->m_mouseHotKey;
			m_mouseGlobalHook.emplace(m_hWnd, UWM_MOUSEHOOKCLICKED,
				mouseHotKey.key, mouseHotKey.ctrl, mouseHotKey.alt, mouseHotKey.shift,
				m_config->m_excludedPrograms);
		}
		_ATLCATCH(e)
		{
			CString str = AtlGetErrorDescription(e);
			MessageBox(
				L"The following error has occurred during the initialization of Textify:\n" + str,
				L"Textify mouse hotkey initialization error", MB_ICONERROR);
		}
	}
	else
	{
		m_mouseGlobalHook.reset();
	}

	if(m_config->m_keybdHotKey.key != 0 && !m_registeredHotKey && !m_textDialogOpen)
	{
		m_registeredHotKey = RegisterConfiguredKeybdHotKey(m_config->m_keybdHotKey);
		if(!m_registeredHotKey)
		{
			CString str = AtlGetErrorDescription(HRESULT_FROM_WIN32(GetLastError()));
			MessageBox(
				L"The following error has occurred during the initialization of Textify:\n" + str,
				L"Textify keyboard hotkey initialization error", MB_ICONERROR);
		}
	}
	else if(m_config->m_keybdHotKey.key == 0 && m_registeredHotKey)
	{
		::UnregisterHotKey(m_hWnd, 1);
		m_registeredHotKey = false;
	}
}

void CMainDlg::UninitMouseAndKeyboardHotKeys()
{
	if(m_registeredHotKey)
	{
		::UnregisterHotKey(m_hWnd, 1);
		m_registeredHotKey = false;
	}

	m_mouseGlobalHook.reset();
}

bool CMainDlg::RegisterConfiguredKeybdHotKey(const HotKey& keybdHotKey)
{
	UINT hotKeyModifiers = MOD_NOREPEAT;

	if(keybdHotKey.ctrl)
		hotKeyModifiers |= MOD_CONTROL;

	if(keybdHotKey.alt)
		hotKeyModifiers |= MOD_ALT;

	if(keybdHotKey.shift)
		hotKeyModifiers |= MOD_SHIFT;

	return ::RegisterHotKey(m_hWnd, 1, hotKeyModifiers, keybdHotKey.key) != FALSE;
}

void CMainDlg::ConfigToGui()
{
	const HotKey& mouseHotKey = m_config->m_mouseHotKey;

	CButton(GetDlgItem(IDC_CHECK_CTRL)).SetCheck(mouseHotKey.ctrl ? BST_CHECKED : BST_UNCHECKED);
	CButton(GetDlgItem(IDC_CHECK_ALT)).SetCheck(mouseHotKey.alt ? BST_CHECKED : BST_UNCHECKED);
	CButton(GetDlgItem(IDC_CHECK_SHIFT)).SetCheck(mouseHotKey.shift ? BST_CHECKED : BST_UNCHECKED);

	auto keysComboWnd = CComboBox(GetDlgItem(IDC_COMBO_KEYS));
	switch(mouseHotKey.key)
	{
	case VK_LBUTTON:
		keysComboWnd.SetCurSel(0);
		break;

	case VK_RBUTTON:
		keysComboWnd.SetCurSel(1);
		break;

	case VK_MBUTTON:
		keysComboWnd.SetCurSel(2);
		break;

	default:
		keysComboWnd.SetCurSel(-1);
		break;
	}

	CButton(GetDlgItem(IDOK)).EnableWindow(FALSE);
}

void CMainDlg::InitNotifyIconData()
{
	m_trayIcon = LoadTrayIcon();

	m_notifyIconData.cbSize = NOTIFYICONDATA_V1_SIZE;
	m_notifyIconData.hWnd = m_hWnd;
	m_notifyIconData.uID = 1;
	m_notifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	m_notifyIconData.uCallbackMessage = UWM_NOTIFYICON;
	m_notifyIconData.hIcon = m_trayIcon;

	CString sWindowText;
	GetWindowText(sWindowText);
	wcscpy_s(m_notifyIconData.szTip, sWindowText);
}

HICON CMainDlg::LoadTrayIcon()
{
	HWND hTaskbarWnd = FindWindow(L"Shell_TrayWnd", NULL);
	UINT dpi = GetDpiForWindowWithFallback(hTaskbarWnd ? hTaskbarWnd : m_hWnd);

	HICON trayIcon = nullptr;
	LoadIconWithScaleDown(
		ModuleHelper::GetResourceInstance(),
		MAKEINTRESOURCE(IDR_MAINFRAME),
		GetSystemMetricsForDpiWithFallback(SM_CXSMICON, dpi),
		GetSystemMetricsForDpiWithFallback(SM_CYSMICON, dpi),
		&trayIcon);
	return trayIcon;
}

void CMainDlg::NotifyIconRightClickMenu()
{
	CMenu menu;
	menu.CreatePopupMenu();

	CString str;

	str.LoadString(IDS_TRAY_TEXTIFY);
	menu.AppendMenu(MF_STRING, RCMENU_SHOW, str);

	menu.AppendMenu(MF_SEPARATOR);

	str.LoadString(IDS_TRAY_EXIT);
	menu.AppendMenu(MF_STRING, RCMENU_EXIT, str);

	CPoint point;
	GetCursorPos(&point);
	int nCmd = menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, m_hWnd);
	switch(nCmd)
	{
	case RCMENU_SHOW:
		ShowWindow(SW_SHOWNORMAL);
		::SetForegroundWindow(GetLastActivePopup());
		break;

	case RCMENU_EXIT:
		Exit();
		break;
	}
}

void CMainDlg::Exit()
{
	if(m_checkingForUpdates)
	{
		UpdateCheckAbort();
		m_closeWhenUpdateCheckDone = true;
	}
	else
	{
		::PostQuitMessage(0);
	}
}
