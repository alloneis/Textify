#include "stdafx.h"
#include "resource.h"

#include "TextDlg.h"
#include "WebAppLaunch.h"
#include "Functions.h"
#include "ClippingStore.h"

namespace
{
	constexpr UINT CLIPPING_DESTINATION_MENU_BASE = 30000;
	constexpr UINT_PTR TEXTIFY_SAVE_SHORTCUT_SUBCLASS_ID = 1;

	LRESULT CALLBACK TextifySaveShortcutSubclassProc(
		HWND hWnd,
		UINT message,
		WPARAM wParam,
		LPARAM lParam,
		UINT_PTR subclassId,
		DWORD_PTR referenceData)
	{
		if(message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
		{
			bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
			bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
			bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
			bool firstPress = (lParam & (1LL << 30)) == 0;
			UINT controlId = 0;
			if((wParam == L'L' || wParam == L'l') && !shift)
			{
				if(ctrl && !alt)
					controlId = IDC_ADD_TO_DB;
				else if(alt && !ctrl)
					controlId = IDC_ADD_TO_MANUAL_DB;
			}

			if(controlId != 0)
			{
				if(firstPress)
				{
					HWND root = ::GetAncestor(hWnd, GA_ROOT);
					::PostMessage(root, WM_COMMAND, MAKEWPARAM(controlId, BN_CLICKED), 0);
				}
				return 0;
			}
		}
		else if((message == WM_CHAR && wParam == 12) ||
			(message == WM_SYSCHAR && (wParam == L'L' || wParam == L'l')))
		{
			bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
			bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
			bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
			if(!shift && ((message == WM_CHAR && ctrl && !alt) ||
				(message == WM_SYSCHAR && alt && !ctrl)))
			{
				return 0;
			}
		}

		return ::DefSubclassProc(hWnd, message, wParam, lParam);
	}

	BOOL CALLBACK InstallTextifySaveShortcutSubclass(HWND hWnd, LPARAM lParam)
	{
		::SetWindowSubclass(
			hWnd,
			TextifySaveShortcutSubclassProc,
			TEXTIFY_SAVE_SHORTCUT_SUBCLASS_ID,
			0);
		return TRUE;
	}

	BOOL CALLBACK RemoveTextifySaveShortcutSubclass(HWND hWnd, LPARAM lParam)
	{
		::RemoveWindowSubclass(
			hWnd,
			TextifySaveShortcutSubclassProc,
			TEXTIFY_SAVE_SHORTCUT_SUBCLASS_ID);
		return TRUE;
	}

	size_t FindClippingDestinationIndex(
		const std::vector<ClippingDestinationInfo>& destinations,
		const CString& label)
	{
		if(!label.IsEmpty())
		{
			for(size_t i = 0; i < destinations.size(); i++)
			{
				if(destinations[i].label.CompareNoCase(label) == 0)
					return i;
			}
		}
		return destinations.size();
	}

	CString EscapeClippingDestinationLabelForUi(CString label)
	{
		label.Replace(L"&", L"&&");
		label.Replace(L"\t", L" ");
		label.Replace(L"\r", L" ");
		label.Replace(L"\n", L" ");
		return label;
	}

	void AppendStringToTextifyResults(CString& string, CString append, std::vector<int>& indexes);
	void GetAccessibleInfoFromPointMSAA(POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes);
	bool GetAccessibleInfoFromPointUIA(POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes);
	void GetAccessibleInfoFromPoint(TextRetrievalMethod method, POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes);
	CSize GetEditControlTextSize(CEdit window, LPCTSTR lpszString, int nMaxWidth = INT_MAX);
	CSize GetEditControlFirstLineSize(CEdit window, LPCTSTR lpszString);
	CSize TextSizeToEditClientSize(HWND hWnd, CEdit editWnd, CSize textSize);
	CSize EditClientSizeToTextSize(HWND hWnd, CEdit editWnd, CSize editClientSize);
}

BOOL CTextDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	CPoint& ptEvent = *reinterpret_cast<CPoint*>(lInitParam);

	CString strText = m_initialText;
	CRect rcAccObject{ ptEvent, CSize{ 0, 0 } };

	// Allows to steal focus.
	INPUT input{};
	SendInput(1, &input, sizeof(INPUT));

	if(!::SetForegroundWindow(m_hWnd))
	{
		//EndDialog(0);
		//return FALSE;
	}

	CEdit editWnd(GetDlgItem(IDC_EDIT));

	// Init font.
	if(!m_config.m_fontName.IsEmpty() || m_config.m_fontSize)
	{
		CFontHandle font(editWnd.GetFont());
		CLogFont fontAttributes(font);

		if(!m_config.m_fontName.IsEmpty())
		{
			wcscpy_s(fontAttributes.lfFaceName, m_config.m_fontName);
		}

		if(m_config.m_fontSize)
		{
			fontAttributes.lfHeight = -m_config.m_fontSize;
		}

		m_editFont = fontAttributes.CreateFontIndirect();
		editWnd.SetFont(m_editFont);
	}

	if(m_config.m_textBoxNonResiable)
	{
		ModifyStyle(WS_THICKFRAME, WS_BORDER);
	}

	InitWebAppButtons();
	InitAddToDbButtons();

	CString strDefaultText;
	strDefaultText.LoadString(IDS_DEFAULT_TEXT);

	if(m_config.m_unicodeSpacesToAscii)
	{
		UnicodeSpacesToAscii(strText);
	}

	if(strText.IsEmpty())
	{
		strText = strDefaultText;
	}

	editWnd.SetLimitText(0);
	editWnd.SetWindowText(strText);

	AdjustWindowLocationAndSize(ptEvent, rcAccObject, strText, strDefaultText);

	m_lastSelStart = 0;
	m_lastSelEnd = strText.GetLength();

	if(m_config.m_autoCopySelection)
	{
		SetClipboardText(strText);
	}

	m_wndEdit.SubclassWindow(editWnd);
	::EnumChildWindows(m_hWnd, InstallTextifySaveShortcutSubclass, 0);

	return TRUE;
}

void CTextDlg::OnDestroy()
{
	::EnumChildWindows(m_hWnd, RemoveTextifySaveShortcutSubclass, 0);
}

HBRUSH CTextDlg::OnCtlColorStatic(CDCHandle dc, CStatic wndStatic)
{
	if(wndStatic.GetDlgCtrlID() == IDC_EDIT)
	{
		return GetSysColorBrush(COLOR_WINDOW);
	}

	SetMsgHandled(FALSE);
	return NULL;
}

void CTextDlg::OnActivate(UINT nState, BOOL bMinimized, CWindow wndOther)
{
	if(nState == WA_INACTIVE && !m_pinned && !m_showingModalBrowserHost)
		EndDialog(0);
}

void CTextDlg::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(nID);
}

void CTextDlg::OnTextChange(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	MakePinned();
}

void CTextDlg::OnExitSizeMove()
{
	MakePinned();
}

void CTextDlg::OnCommand(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if(nID == IDC_ADD_TO_DB)
	{
		SaveCurrentTextToDatabase(
			ClippingDestination::code,
			m_codeDefaultDestinationLabel,
			false);
		return;
	}
	if(nID == IDC_ADD_TO_MANUAL_DB)
	{
		SaveCurrentTextToDatabase(
			ClippingDestination::manual,
			m_manualDefaultDestinationLabel,
			false);
		return;
	}

	int buttonIndex = nID - IDC_WEB_BUTTON_1;
	if(buttonIndex >= 0 && buttonIndex < static_cast<int>(m_config.m_webButtonInfos.size()))
	{
		CString selectedText;
		m_wndEdit.GetWindowText(selectedText);

		int start, end;
		m_wndEdit.GetSel(start, end);
		if(start < end)
		{
			selectedText = selectedText.Mid(start, end - start);
		}

		m_showingModalBrowserHost = true;
		ShowWindow(SW_HIDE);

		const auto& buttonInfo = m_config.m_webButtonInfos[buttonIndex];
		bool succeeded = CommandLaunch(buttonInfo.command, selectedText,
			buttonInfo.width, buttonInfo.height);

		if(!succeeded)
		{
			CString title;
			title.LoadString(IDS_ERROR);

			CString text;
			text.LoadString(IDS_ERROR_EXECUTE);
			text += L"\n";
			text += buttonInfo.command;

			MessageBox(text, title, MB_ICONERROR);
		}

		EndDialog(0);
	}
}

LRESULT CTextDlg::OnNotify(int idCtrl, LPNMHDR pnmh)
{
	if(pnmh->code == BCN_DROPDOWN)
	{
		if(idCtrl == IDC_ADD_TO_DB)
		{
			ShowClippingDestinationMenu(ClippingDestination::code, GetDlgItem(idCtrl));
			return 0;
		}
		if(idCtrl == IDC_ADD_TO_MANUAL_DB)
		{
			ShowClippingDestinationMenu(ClippingDestination::manual, GetDlgItem(idCtrl));
			return 0;
		}
	}

	SetMsgHandled(FALSE);
	return 0;
}

bool CTextDlg::SaveCurrentTextToDatabase(
	ClippingDestination destination,
	CString destinationLabel,
	bool makeDefault)
{
	CString text;
	m_wndEdit.GetWindowText(text);

	// Reload on every save so an externally edited Textify.ini takes effect
	// even in a Textify popup that was already open.
	UserConfig liveConfig;
	const auto& destinations = destination == ClippingDestination::code
		? liveConfig.m_codeClippingDestinations
		: liveConfig.m_manualClippingDestinations;
	auto& cachedDestinations = destination == ClippingDestination::code
		? m_config.m_codeClippingDestinations
		: m_config.m_manualClippingDestinations;
	cachedDestinations = destinations;
	const wchar_t* databaseName = destination == ClippingDestination::code
		? L"Code DB"
		: L"Manual DB";

	if(destinations.empty())
	{
		UpdateAddToDbButtonLabels();
		CString error;
		error.Format(
			L"Configure at least one labeled %s destination in "
			L"Textify.ini first.",
			databaseName);
		CString title;
		title.Format(L"Textify - Add to %s", databaseName);
		MessageBox(error, title, MB_ICONERROR);
		return false;
	}
	size_t destinationIndex = FindClippingDestinationIndex(destinations, destinationLabel);
	if(destinationIndex >= destinations.size())
	{
		if(makeDefault)
		{
			UpdateAddToDbButtonLabels();
			CString error;
			error.Format(
				L"The selected %s destination changed in Textify.ini. "
				L"Open the drop-down and choose it again.",
				databaseName);
			CString title;
			title.Format(L"Textify - Add to %s", databaseName);
			MessageBox(error, title, MB_ICONWARNING);
			return false;
		}
		destinationIndex = 0;
	}
	CString& defaultLabel = destination == ClippingDestination::code
		? m_codeDefaultDestinationLabel
		: m_manualDefaultDestinationLabel;
	// A menu choice becomes the session default immediately, even if the
	// subsequent disk write fails. Direct saves also reconcile a removed or
	// renamed default to the first configured entry.
	if(makeDefault || defaultLabel.CompareNoCase(destinations[destinationIndex].label) != 0)
		defaultLabel = destinations[destinationIndex].label;
	UpdateAddToDbButtonLabels();

	CString savedPath;
	CString error;
	if(!SaveNextClipping(destinations[destinationIndex].directory, text, savedPath, error))
	{
		CString title;
		title.Format(L"Textify - Add to %s", databaseName);
		MessageBox(error, title, MB_ICONERROR);
		return false;
	}

	EndDialog(IDOK);
	return true;
}

void CTextDlg::OnGetMinMaxInfo(LPMINMAXINFO lpMMI)
{
	lpMMI->ptMinTrackSize.x = m_minSize.cx;
	lpMMI->ptMinTrackSize.y = m_minSize.cy;
}

void CTextDlg::OnSize(UINT nType, CSize size)
{
	if(nType != SIZE_RESTORED && nType != SIZE_MAXIMIZED)
		return;

	CRect clientRect;
	GetClientRect(&clientRect);

	int numberOfWebAppButtons = static_cast<int>(m_config.m_webButtonInfos.size());

	CButton webAppButtonLast;
	if(numberOfWebAppButtons > 0)
	{
		CButton button(GetDlgItem(IDC_WEB_BUTTON_1 - 1 + numberOfWebAppButtons));
		if(button && button.IsWindowVisible())
		{
			webAppButtonLast = button;
		}
		else
		{
			numberOfWebAppButtons = 0;
		}
	}

	HDWP hDwp = ::BeginDeferWindowPos(numberOfWebAppButtons + 3);
	if(!hDwp)
		return;

	CButton codeDbButton(GetDlgItem(IDC_ADD_TO_DB));
	CButton manualDbButton(GetDlgItem(IDC_ADD_TO_MANUAL_DB));
	int codeDbButtonHeight = 0;
	int manualDbButtonHeight = 0;
	if(codeDbButton)
	{
		CRect rect;
		codeDbButton.GetWindowRect(&rect);
		codeDbButtonHeight = rect.Height();
	}
	if(manualDbButton)
	{
		CRect rect;
		manualDbButton.GetWindowRect(&rect);
		manualDbButtonHeight = rect.Height();
	}
	int firstButtonTop = clientRect.Height() - codeDbButtonHeight - manualDbButtonHeight;
	if(codeDbButton)
	{
		codeDbButton.DeferWindowPos(
			hDwp,
			nullptr,
			0,
			firstButtonTop,
			clientRect.Width(),
			codeDbButtonHeight,
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	}
	if(manualDbButton)
	{
		manualDbButton.DeferWindowPos(
			hDwp,
			nullptr,
			0,
			firstButtonTop + codeDbButtonHeight,
			clientRect.Width(),
			manualDbButtonHeight,
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	}
	if(webAppButtonLast)
	{
		CRect webAppButtonLastRect;
		webAppButtonLast.GetWindowRect(&webAppButtonLastRect);
		::MapWindowPoints(nullptr, m_hWnd, &webAppButtonLastRect.TopLeft(), 2);

		int newTop = firstButtonTop - webAppButtonLastRect.Height();
		int heightDelta = newTop - webAppButtonLastRect.top;
		firstButtonTop = newTop;

		webAppButtonLast.DeferWindowPos(hDwp, nullptr,
			webAppButtonLastRect.left, webAppButtonLastRect.top + heightDelta,
			webAppButtonLastRect.Width(), webAppButtonLastRect.Height(),
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);

		for(int i = numberOfWebAppButtons - 1; i >= 1; i--)
		{
			CButton button(GetDlgItem(IDC_WEB_BUTTON_1 - 1 + i));

			CRect buttonRect;
			button.GetWindowRect(&buttonRect);
			::MapWindowPoints(nullptr, m_hWnd, &buttonRect.TopLeft(), 2);

			if(i == 1)
			{
				firstButtonTop = buttonRect.top + heightDelta;
			}

			button.DeferWindowPos(hDwp, nullptr,
				buttonRect.left, buttonRect.top + heightDelta,
				buttonRect.Width(), buttonRect.Height(),
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
		}
	}

	CEdit editWnd(GetDlgItem(IDC_EDIT));
	editWnd.DeferWindowPos(hDwp, nullptr, 0, 0, clientRect.Width(), firstButtonTop,
		SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);

	::EndDeferWindowPos(hDwp);
}

LRESULT CTextDlg::OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = ::DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	if(result == HTCLIENT)
		result = HTCAPTION;

	return result;
}

void CTextDlg::OnKeyDown(UINT vk, UINT nRepCnt, UINT nFlags)
{
	if(vk == VK_TAB)
	{
		int start, end;
		m_wndEdit.GetSel(start, end);
		int length = (int)m_wndEdit.SendMessage(WM_GETTEXTLENGTH);

		int newStart, newEnd;

		const int newlineSize = sizeof("\r\n") - 1;

		if(start == 0 && end == length) // all text is selected
		{
			newStart = 0;
			newEnd = m_editIndexes.empty() ? length : (m_editIndexes.front() - newlineSize);
		}
		else
		{
			newStart = 0;
			newEnd = length;

			for(size_t i = 0; i < m_editIndexes.size(); i++)
			{
				int from = (i == 0) ? 0 : m_editIndexes[i - 1];
				int to = m_editIndexes[i] - newlineSize;

				if(from == start && to == end)
				{
					newStart = m_editIndexes[i];
					newEnd = (i + 1 < m_editIndexes.size()) ? (m_editIndexes[i + 1] - newlineSize) : length;

					break;
				}
			}
		}

		m_wndEdit.SetSel(newStart, newEnd, TRUE);
	}
	else
	{
		SetMsgHandled(FALSE);
	}
}

void CTextDlg::OnKeyUp(UINT vk, UINT nRepCnt, UINT nFlags)
{
	OnSelectionMaybeChanged();
	SetMsgHandled(FALSE);
}

void CTextDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	OnSelectionMaybeChanged();
	SetMsgHandled(FALSE);
}

void CTextDlg::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if(nChar == 1) // Ctrl+A
	{
		m_wndEdit.SetSelAll(TRUE);
	}
	else
	{
		SetMsgHandled(FALSE);
	}
}

void CTextDlg::InitWebAppButtons()
{
	// This id shouldn't be in use yet, we're going to use it for a newly created button.
	ATLASSERT(!GetDlgItem(IDC_WEB_BUTTON_1));

	int numberOfButtons = static_cast<int>(m_config.m_webButtonInfos.size());
	if(numberOfButtons == 0)
	{
		return;
	}

	m_webButtonIcons.resize(numberOfButtons);

	int buttonSize = ScaleForWindow(m_hWnd, m_config.m_webButtonsIconSize);

	CRect buttonRect{
		0,
		0,
		GetSystemMetricsForWindow(m_hWnd, SM_CXEDGE) * 4 + buttonSize,
		GetSystemMetricsForWindow(m_hWnd, SM_CYEDGE) * 4 + buttonSize
	};

	for(int i = 0; i < numberOfButtons; i++)
	{
		CString buttonText;
		if(m_config.m_webButtonInfos[i].acceleratorKey)
			buttonText.Format(L"&%c", m_config.m_webButtonInfos[i].acceleratorKey);
		else if(i + 1 < 10)
			buttonText.Format(L"&%d", i + 1);
		else if(i + 1 == 10)
			buttonText.Format(L"1&0");
		else
			buttonText.Format(L"%d", i + 1);

		CButton button;
		button.Create(m_hWnd, buttonRect, buttonText,
			WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, IDC_WEB_BUTTON_1 + i);
		button.SetFont(GetFont());

		if(!m_config.m_webButtonInfos[i].name.IsEmpty())
		{
			if(!m_webButtonTooltip)
			{
				m_webButtonTooltip.Create(m_hWnd, NULL, NULL, WS_POPUP | TTS_NOPREFIX, WS_EX_TOPMOST);
			}

			m_webButtonTooltip.AddTool(
				CToolInfo(TTF_SUBCLASS, button, 0, NULL, (PWSTR)m_config.m_webButtonInfos[i].name.GetString()));
		}

		if(!m_config.m_webButtonInfos[i].iconPath.m_strPath.IsEmpty())
		{
			HICON icon;
			if(::SHDefExtractIcon(
				m_config.m_webButtonInfos[i].iconPath.m_strPath,
				m_config.m_webButtonInfos[i].iconIndex,
				0, &icon, nullptr, buttonSize) == S_OK)
			{
				button.ModifyStyle(0, BS_ICON);
				button.SetIcon(icon);
				m_webButtonIcons[i] = icon;
			}
		}
	}
}

void CTextDlg::InitAddToDbButtons()
{
	ATLASSERT(!GetDlgItem(IDC_ADD_TO_DB));
	ATLASSERT(!GetDlgItem(IDC_ADD_TO_MANUAL_DB));
	CRect buttonRect{
		0,
		0,
		ScaleForWindow(m_hWnd, 240),
		ScaleForWindow(m_hWnd, 24)
	};
	CButton codeButton;
	codeButton.Create(
		m_hWnd,
		buttonRect,
		L"Add to Code DB (Ctrl+L)",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_SPLITBUTTON,
		0,
		IDC_ADD_TO_DB);
	codeButton.SetFont(GetFont());

	CButton manualButton;
	manualButton.Create(
		m_hWnd,
		buttonRect,
		L"Add to Manual DB (Alt+&L)",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_SPLITBUTTON,
		0,
		IDC_ADD_TO_MANUAL_DB);
	manualButton.SetFont(GetFont());

	UpdateAddToDbButtonLabels();

	SIZE codeIdeal{};
	SIZE manualIdeal{};
	codeButton.SendMessage(BCM_GETIDEALSIZE, 0, reinterpret_cast<LPARAM>(&codeIdeal));
	manualButton.SendMessage(BCM_GETIDEALSIZE, 0, reinterpret_cast<LPARAM>(&manualIdeal));
	int idealWidth = max(buttonRect.Width(), max(codeIdeal.cx, manualIdeal.cx));
	codeButton.SetWindowPos(NULL, 0, 0, idealWidth, buttonRect.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	manualButton.SetWindowPos(NULL, 0, 0, idealWidth, buttonRect.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CTextDlg::UpdateAddToDbButtonLabels()
{
	auto makeCaption = [](LPCWSTR databaseName, LPCWSTR shortcut,
		const std::vector<ClippingDestinationInfo>& destinations, CString& defaultLabel)
	{
		CString caption;
		if(destinations.empty())
		{
			defaultLabel.Empty();
			caption.Format(L"Add to %s (not configured, %s)", databaseName, shortcut);
		}
		else
		{
			size_t defaultIndex = FindClippingDestinationIndex(destinations, defaultLabel);
			if(defaultIndex >= destinations.size())
				defaultIndex = 0;
			defaultLabel = destinations[defaultIndex].label;
			CString displayLabel = EscapeClippingDestinationLabelForUi(
				destinations[defaultIndex].label);
			caption.Format(L"Add to %s: %s (%s)", databaseName,
				displayLabel.GetString(), shortcut);
		}
		return caption;
	};

	SetDlgItemText(
		IDC_ADD_TO_DB,
		makeCaption(L"Code DB", L"Ctrl+L", m_config.m_codeClippingDestinations,
			m_codeDefaultDestinationLabel));
	SetDlgItemText(
		IDC_ADD_TO_MANUAL_DB,
		makeCaption(L"Manual DB", L"Alt+&L", m_config.m_manualClippingDestinations,
			m_manualDefaultDestinationLabel));
}

void CTextDlg::ShowClippingDestinationMenu(ClippingDestination destination, CWindow button)
{
	// Refresh the list at the moment the arrow is opened. This makes an
	// externally saved Textify.ini visible without closing this popup.
	UserConfig liveConfig;
	auto& currentDestinations = destination == ClippingDestination::code
		? m_config.m_codeClippingDestinations
		: m_config.m_manualClippingDestinations;
	currentDestinations = destination == ClippingDestination::code
		? liveConfig.m_codeClippingDestinations
		: liveConfig.m_manualClippingDestinations;
	CString& defaultLabel = destination == ClippingDestination::code
		? m_codeDefaultDestinationLabel
		: m_manualDefaultDestinationLabel;
	UpdateAddToDbButtonLabels();

	if(currentDestinations.empty())
	{
		SaveCurrentTextToDatabase(destination, CString{}, false);
		return;
	}

	CMenu menu;
	menu.CreatePopupMenu();
	size_t defaultIndex = FindClippingDestinationIndex(currentDestinations, defaultLabel);
	if(defaultIndex >= currentDestinations.size())
		defaultIndex = 0;
	for(size_t i = 0; i < currentDestinations.size(); i++)
	{
		CString menuLabel = EscapeClippingDestinationLabelForUi(
			currentDestinations[i].label);
		UINT flags = MF_STRING;
		if(i == defaultIndex)
		{
			menuLabel += L" (default)";
			flags |= MF_CHECKED;
		}
		menu.AppendMenu(flags, CLIPPING_DESTINATION_MENU_BASE + static_cast<UINT>(i), menuLabel);
	}

	CRect buttonRect;
	button.GetWindowRect(&buttonRect);
	button.SendMessage(BCM_SETDROPDOWNSTATE, TRUE);
	int command = menu.TrackPopupMenu(
		TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
		buttonRect.left,
		buttonRect.bottom,
		m_hWnd);
	button.SendMessage(BCM_SETDROPDOWNSTATE, FALSE);
	if(command >= static_cast<int>(CLIPPING_DESTINATION_MENU_BASE))
	{
		size_t selectedIndex = static_cast<size_t>(command - CLIPPING_DESTINATION_MENU_BASE);
		if(selectedIndex < currentDestinations.size())
			SaveCurrentTextToDatabase(
				destination,
				currentDestinations[selectedIndex].label,
				true);
	}
}

void CTextDlg::AdjustWindowLocationAndSize(CPoint ptEvent, CRect rcAccObject, CString strText, CString strDefaultText)
{
	CEdit editWnd(GetDlgItem(IDC_EDIT));

	// A dirty, partial workaround.
	// http://stackoverflow.com/questions/35673347
	strText.Replace(L"!", L"! ");
	strText.Replace(L"|", L"| ");
	strText.Replace(L"?", L"? ");
	strText.Replace(L"-", L"- ");
	strText.Replace(L"}", L"} ");
	strText.Replace(L"{", L" {");
	strText.Replace(L"[", L" [");
	strText.Replace(L"(", L" (");
	strText.Replace(L"+", L" +");
	strText.Replace(L"%", L" %");
	strText.Replace(L"$", L" $");
	strText.Replace(L"\\", L" \\");

	CSize defTextSize = GetEditControlTextSize(editWnd, strDefaultText);
	CSize defTextSizeClient = TextSizeToEditClientSize(m_hWnd, editWnd, defTextSize);
	CSize firstLineSize = GetEditControlFirstLineSize(editWnd, strText);
	CSize firstLineSizeClient = TextSizeToEditClientSize(m_hWnd, editWnd, firstLineSize);

	m_minSize.cy = defTextSizeClient.cy;

	int nMaxClientWidth = defTextSizeClient.cx > rcAccObject.Width() ? defTextSizeClient.cx : rcAccObject.Width();

	// Keep the first logical line on one visual line. The monitor-boundary
	// handling below still clamps exceptionally long lines to the screen.
	int layoutWidth = max(nMaxClientWidth, firstLineSize.cx);
	CSize textSize = GetEditControlTextSize(editWnd, strText, layoutWidth);
	CSize textSizeClient = TextSizeToEditClientSize(m_hWnd, editWnd, textSize);
	textSizeClient.cx = max(textSizeClient.cx, firstLineSizeClient.cx);

	if(textSizeClient.cx < rcAccObject.Width())
	{
		// Perhaps it will look better if we won't shrink the control,
		// as it will fit perfectly above the control.
		// Let's see if the shrinking is small.

		int nMinClientWidth = ScaleForWindow(m_hWnd, 200);

		if(rcAccObject.Width() <= nMinClientWidth || textSizeClient.cx * 1.5 >= rcAccObject.Width())
		{
			int delta = rcAccObject.Width() - textSizeClient.cx;
			textSizeClient.cx = rcAccObject.Width();
			textSize.cx += delta;

			// Recalculate the height, which might be smaller now.
			//CSize newTextSize = GetEditControlTextSize(editWnd, strText, textSize.cx);
			//textSize.cy = newTextSize.cy;
		}
	}

	CRect rcClient{ rcAccObject.TopLeft(), textSizeClient };
	if(rcAccObject.IsRectEmpty() || !rcClient.PtInRect(ptEvent))
	{
		CPoint ptWindowLocation{ ptEvent };
		ptWindowLocation.Offset(-rcClient.Width() / 2, -rcClient.Height() / 2);
		rcClient.MoveToXY(ptWindowLocation);
	}

	int numberOfWebAppButtons = static_cast<int>(m_config.m_webButtonInfos.size());
	int numberOfWebAppButtonsX = numberOfWebAppButtons;
	int numberOfWebAppButtonsY = 1;
	CSize webAppButtonSize;
	if(numberOfWebAppButtons > 0)
	{
		int numberOfWebAppButtonsPerRow = m_config.m_webButtonsPerRow;
		if(numberOfWebAppButtonsPerRow > 0)
		{
			numberOfWebAppButtonsX = numberOfWebAppButtons > numberOfWebAppButtonsPerRow ? numberOfWebAppButtonsPerRow : numberOfWebAppButtons;
			numberOfWebAppButtonsY = (numberOfWebAppButtons + numberOfWebAppButtonsPerRow - 1) / numberOfWebAppButtonsPerRow;
		}

		CButton webAppButton(GetDlgItem(IDC_WEB_BUTTON_1));
		CRect webAppButtonRect;
		webAppButton.GetWindowRect(webAppButtonRect);
		webAppButtonSize = webAppButtonRect.Size();

		if(rcClient.Width() < webAppButtonSize.cx * numberOfWebAppButtonsX)
			rcClient.right = rcClient.left + webAppButtonSize.cx * numberOfWebAppButtonsX;

		rcClient.bottom += webAppButtonSize.cy * numberOfWebAppButtonsY;

		m_minSize.cx = webAppButtonSize.cx * numberOfWebAppButtonsX;
		m_minSize.cy += webAppButtonSize.cy * numberOfWebAppButtonsY;
	}
	else
	{
		m_minSize.cx = GetSystemMetricsForWindow(m_hWnd, SM_CXICON) +
			GetSystemMetricsForWindow(m_hWnd, SM_CXVSCROLL);
	}

	CButton codeDbButton(GetDlgItem(IDC_ADD_TO_DB));
	CButton manualDbButton(GetDlgItem(IDC_ADD_TO_MANUAL_DB));
	CRect codeDbButtonRect;
	CRect manualDbButtonRect;
	codeDbButton.GetWindowRect(&codeDbButtonRect);
	manualDbButton.GetWindowRect(&manualDbButtonRect);
	CSize codeDbButtonSize = codeDbButtonRect.Size();
	CSize manualDbButtonSize = manualDbButtonRect.Size();
	int databaseButtonsHeight = codeDbButtonSize.cy + manualDbButtonSize.cy;
	rcClient.bottom += databaseButtonsHeight;
	m_minSize.cx = max(m_minSize.cx, max(codeDbButtonSize.cx, manualDbButtonSize.cx));
	m_minSize.cy += databaseButtonsHeight;

	CRect rcWindow{ rcClient };
	WndAdjustWindowRect(m_hWnd, &rcWindow);

	HMONITOR hMonitor = MonitorFromPoint(ptEvent, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitorinfo = { sizeof(MONITORINFO) };
	if(GetMonitorInfo(hMonitor, &monitorinfo))
	{
		CRect rcMonitor{ monitorinfo.rcMonitor };
		CRect rcWindowPrev{ rcWindow };

		if(rcWindow.Width() > rcMonitor.Width() ||
			rcWindow.Height() > rcMonitor.Height())
		{
			if(rcWindow.Height() > rcMonitor.Height())
			{
				rcWindow.top = 0;
				rcWindow.bottom = rcMonitor.Height();
			}

			editWnd.ShowScrollBar(SB_VERT);
			rcWindow.right += GetSystemMetricsForWindow(m_hWnd, SM_CXVSCROLL);
			if(rcWindow.Width() > rcMonitor.Width())
			{
				rcWindow.left = 0;
				rcWindow.right = rcMonitor.Width();
			}
		}

		if(rcWindow.left < rcMonitor.left)
		{
			rcWindow.MoveToX(rcMonitor.left);
		}
		else if(rcWindow.right > rcMonitor.right)
		{
			rcWindow.MoveToX(rcMonitor.right - rcWindow.Width());
		}

		if(rcWindow.top < rcMonitor.top)
		{
			rcWindow.MoveToY(rcMonitor.top);
		}
		else if(rcWindow.bottom > rcMonitor.bottom)
		{
			rcWindow.MoveToY(rcMonitor.bottom - rcWindow.Height());
		}

		if(rcWindowPrev != rcWindow)
		{
			rcClient = rcWindow;
			WndUnadjustWindowRect(m_hWnd, &rcClient);
		}
	}

	if(rcClient.Height() > databaseButtonsHeight)
	{
		rcClient.bottom -= databaseButtonsHeight;
		codeDbButton.SetWindowPos(
			NULL,
			0,
			rcClient.Height(),
			rcClient.Width(),
			codeDbButtonSize.cy,
			SWP_NOZORDER | SWP_NOACTIVATE);
		manualDbButton.SetWindowPos(
			NULL,
			0,
			rcClient.Height() + codeDbButtonSize.cy,
			rcClient.Width(),
			manualDbButtonSize.cy,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
	else
	{
		codeDbButton.ShowWindow(SW_HIDE);
		manualDbButton.ShowWindow(SW_HIDE);
	}

	if(numberOfWebAppButtons > 0)
	{
		if(rcClient.bottom - webAppButtonSize.cy * numberOfWebAppButtonsY > rcClient.top)
		{
			rcClient.bottom -= webAppButtonSize.cy * numberOfWebAppButtonsY;

			CPoint ptButton{ 0, rcClient.Height() };

			for(int i = 0; i < numberOfWebAppButtons; i++)
			{
				if(i > 0 && i % numberOfWebAppButtonsX == 0)
				{
					ptButton.x = 0;
					ptButton.y += webAppButtonSize.cy;
				}

				CButton webAppButton(GetDlgItem(IDC_WEB_BUTTON_1 + i));
				webAppButton.SetWindowPos(NULL, ptButton.x, ptButton.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

				ptButton.x += webAppButtonSize.cx;
			}
		}
		else
		{
			// Hide WebApp buttons.
			for(int i = 0; i < numberOfWebAppButtons; i++)
			{
				CButton webAppButton(GetDlgItem(IDC_WEB_BUTTON_1 + i));
				webAppButton.ShowWindow(SW_HIDE);
			}
		}
	}

	SetWindowPos(NULL, &rcWindow, SWP_NOZORDER | SWP_NOACTIVATE);
	editWnd.SetWindowPos(NULL, 0, 0, rcClient.Width(), rcClient.Height(), SWP_NOZORDER | SWP_NOACTIVATE);

	CRect minSizeRect{ CPoint{}, m_minSize };
	WndAdjustWindowRect(m_hWnd, &minSizeRect);
	m_minSize = minSizeRect.Size();
}

void CTextDlg::OnSelectionMaybeChanged()
{
	if(m_config.m_autoCopySelection)
	{
		int start, end;
		m_wndEdit.GetSel(start, end);
		if(start < end && (start != m_lastSelStart || end != m_lastSelEnd))
		{
			CString str;
			m_wndEdit.GetWindowText(str);

			SetClipboardText(str.Mid(start, end - start));

			m_lastSelStart = start;
			m_lastSelEnd = end;
		}
	}
}

void CTextDlg::MakePinned()
{
	if(m_pinned)
		return;

	CRect rc;
	GetWindowRect(&rc);
	WndUnadjustWindowRect(m_hWnd, &rc);

	CRect minSizeRect{ CPoint{}, m_minSize };
	WndUnadjustWindowRect(m_hWnd, &minSizeRect);

	// Add caption with an exit button.
	SetWindowText(L"Textify");

	ModifyStyle(WS_BORDER, WS_THICKFRAME | DS_MODALFRAME | WS_CAPTION | WS_SYSMENU);

	// Note: this causes WM_SIZE to be sent
	WndAdjustWindowRect(m_hWnd, &rc);
	SetWindowPos(NULL, rc, SWP_NOZORDER | SWP_FRAMECHANGED);

	WndAdjustWindowRect(m_hWnd, &minSizeRect);
	m_minSize = minSizeRect.Size();

	m_editIndexes.clear();

	CEdit editWnd(GetDlgItem(IDC_EDIT));
	editWnd.ShowScrollBar(SB_VERT);

	m_pinned = true;
}

namespace
{
	void AppendStringToTextifyResults(CString& string, CString append, std::vector<int>& indexes)
	{
		// Convert all newlines to CRLF and trim trailing newlines.
		append.Replace(L"\r\n", L"\n");
		append.Replace(L"\r", L"\n");
		append.TrimRight(L"\n");
		append.Replace(L"\n", L"\r\n");

		if(append.IsEmpty())
			return;

		if(!string.IsEmpty())
		{
			string += L"\r\n";
			indexes.push_back(string.GetLength());
		}

		string += append;
	}

	void GetAccessibleInfoFromPointMSAA(POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes)
	{
		outString.Empty();
		outRc = CRect{ pt, CSize{ 0, 0 } };
		outIndexes = std::vector<int>();

		HRESULT hr;

		CComPtr<IAccessible> pAcc;
		CComVariant vtChild;
		hr = AccessibleObjectFromPoint(pt, &pAcc, &vtChild);
		if(FAILED(hr) || !pAcc)
			return;

		// Chromium has a bug in which the correct element is sometimes
		// returned only after a second query. Do it.
		pAcc.Release();
		vtChild.Clear();
		hr = AccessibleObjectFromPoint(pt, &pAcc, &vtChild);
		if(FAILED(hr) || !pAcc)
			return;

		hr = WindowFromAccessibleObject(pAcc, &outWindow.m_hWnd);
		if(FAILED(hr))
			return;

		DWORD processId = 0;
		GetWindowThreadProcessId(outWindow.m_hWnd, &processId);

		while(true)
		{
			CString string;
			std::vector<int> indexes;

			CComBSTR bsName;
			hr = pAcc->get_accName(vtChild, &bsName);
			if(SUCCEEDED(hr) && bsName)
			{
				AppendStringToTextifyResults(string, bsName.m_str, indexes);
			}

			CComBSTR bsValue;
			hr = pAcc->get_accValue(vtChild, &bsValue);
			if(SUCCEEDED(hr) && bsValue && bsValue != bsName)
			{
				AppendStringToTextifyResults(string, bsValue.m_str, indexes);
			}

			CComVariant vtRole;
			hr = pAcc->get_accRole(CComVariant(CHILDID_SELF), &vtRole);
			if(FAILED(hr) || vtRole.lVal != ROLE_SYSTEM_TITLEBAR) // ignore description for the system title bar
			{
				CComBSTR bsDescription;
				hr = pAcc->get_accDescription(vtChild, &bsDescription);
				if(SUCCEEDED(hr) && bsDescription && bsDescription != bsName && bsDescription != bsValue)
				{
					AppendStringToTextifyResults(string, bsDescription.m_str, indexes);
				}
			}

			if(!string.IsEmpty())
			{
				outString = string;
				outIndexes = indexes;
				break;
			}

			if(vtChild.lVal == CHILDID_SELF)
			{
				CComPtr<IDispatch> pDispParent;
				hr = pAcc->get_accParent(&pDispParent);
				if(FAILED(hr) || !pDispParent)
					break;

				CComQIPtr<IAccessible> pAccParent(pDispParent);

				HWND hWnd;
				hr = WindowFromAccessibleObject(pAccParent, &hWnd);
				if(FAILED(hr))
					break;

				DWORD compareProcessId = 0;
				GetWindowThreadProcessId(hWnd, &compareProcessId);
				if(compareProcessId != processId)
					break;

				pAcc.Attach(pAccParent.Detach());
			}
			else
			{
				vtChild.lVal = CHILDID_SELF;
			}
		}

		long pxLeft, pyTop, pcxWidth, pcyHeight;
		hr = pAcc->accLocation(&pxLeft, &pyTop, &pcxWidth, &pcyHeight, vtChild);
		if(SUCCEEDED(hr))
		{
			outRc = CRect{ CPoint{ pxLeft, pyTop }, CSize{ pcxWidth, pcyHeight } };
		}
	}

	bool GetAccessibleInfoFromPointUIA(POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes)
	{
		outString.Empty();
		outRc = CRect{ pt, CSize{ 0, 0 } };
		outIndexes = std::vector<int>();

		HRESULT hr;

		CComPtr<IUIAutomation> uia;
		hr = uia.CoCreateInstance(CLSID_CUIAutomation);
		if(FAILED(hr) || !uia)
			return false;

		CComPtr<IUIAutomationElement> element;
		hr = uia->ElementFromPoint(pt, &element);
		if(FAILED(hr) || !element)
			return true;

		// Chromium has a bug in which the correct element is sometimes
		// returned only after a second query. Do it.
		element.Release();
		hr = uia->ElementFromPoint(pt, &element);
		if(FAILED(hr) || !element)
			return true;

		CComPtr<IUIAutomationCondition> trueCondition;
		hr = uia->CreateTrueCondition(&trueCondition);
		if(FAILED(hr) || !trueCondition)
			return true;

		CComPtr<IUIAutomationTreeWalker> treeWalker;
		hr = uia->CreateTreeWalker(trueCondition, &treeWalker);
		if(FAILED(hr) || !treeWalker)
			return true;

		int processId = 0;
		hr = element->get_CurrentProcessId(&processId);
		if(FAILED(hr))
			return true;

		while(true)
		{
			CString string;
			std::vector<int> indexes;

			CComBSTR bsName;
			hr = element->get_CurrentName(&bsName);
			if(SUCCEEDED(hr) && bsName)
			{
				AppendStringToTextifyResults(string, bsName.m_str, indexes);
			}

			CComVariant varValue;
			hr = element->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &varValue);
			if(SUCCEEDED(hr) && varValue.vt == VT_BSTR && bsName != varValue.bstrVal)
			{
				AppendStringToTextifyResults(string, varValue.bstrVal, indexes);
			}

			if(!string.IsEmpty())
			{
				outString = string;
				outIndexes = indexes;
				break;
			}

			CComPtr<IUIAutomationElement> parentElement;
			hr = treeWalker->GetParentElement(element, &parentElement);
			if(FAILED(hr) || !parentElement)
				break;

			int compareProcessId = 0;
			hr = parentElement->get_CurrentProcessId(&compareProcessId);
			if(FAILED(hr) || compareProcessId != processId)
				break;

			element.Attach(parentElement.Detach());
		}

		hr = element->get_CurrentNativeWindowHandle((UIA_HWND*)&outWindow.m_hWnd);
		if(SUCCEEDED(hr) && !outWindow)
		{
			CComPtr<IUIAutomationElement> parentElement;
			hr = treeWalker->GetParentElement(element, &parentElement);
			if(SUCCEEDED(hr) && parentElement)
			{
				while(true)
				{
					hr = parentElement->get_CurrentNativeWindowHandle((UIA_HWND*)&outWindow.m_hWnd);
					if(FAILED(hr) || outWindow)
						break;

					CComPtr<IUIAutomationElement> nextParentElement;
					hr = treeWalker->GetParentElement(parentElement, &nextParentElement);
					if(FAILED(hr) || !nextParentElement)
						break;

					parentElement.Attach(nextParentElement.Detach());
				}
			}
		}

		CRect boundingRc;
		hr = element->get_CurrentBoundingRectangle(&boundingRc);
		if(SUCCEEDED(hr))
		{
			outRc = boundingRc;
		}

		return true;
	}

	void GetAccessibleInfoFromPoint(TextRetrievalMethod method, POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes)
	{
		if(method == TextRetrievalMethod::msaa)
		{
			GetAccessibleInfoFromPointMSAA(pt, outWindow, outString, outRc, outIndexes);
			return;
		}

		if(method == TextRetrievalMethod::uia)
		{
			GetAccessibleInfoFromPointUIA(pt, outWindow, outString, outRc, outIndexes);
			return;
		}

		// Both MSAA and UIA have downsides specific to them:
		// MSAA: Isn't supported by new programs, e.g. the taskbar in Windows 11.
		// UIA: The returned element is not the most nested, e.g. with links in
		// Chromium or with the Textify text above the "Homepage" link. Also,
		// Firefox on Windows 7 returns an empty result:
		// https://bugzilla.mozilla.org/show_bug.cgi?id=1406295
		// As an attempt to choose the best result, both are used, and the MSAA
		// result is returned if either UIA returns an empty result, or if
		// MSAA's rectangle is smaller, in the assumption that it's a more
		// nested element and a better result.

		if(!GetAccessibleInfoFromPointUIA(pt, outWindow, outString, outRc, outIndexes) ||
			(outString.IsEmpty() && outRc.IsRectNull()))
		{
			GetAccessibleInfoFromPointMSAA(pt, outWindow, outString, outRc, outIndexes);
			return;
		}

		CWindow outWindowMsaa;
		CString outStringMsaa;
		CRect outRcMsaa;
		GetAccessibleInfoFromPointMSAA(pt, outWindowMsaa, outStringMsaa, outRcMsaa, outIndexes);

		if(
			outRcMsaa.left >= outRc.left &&
			outRcMsaa.top >= outRc.top &&
			outRcMsaa.right <= outRc.right &&
			outRcMsaa.bottom <= outRc.bottom && (
				outRcMsaa.left > outRc.left ||
				outRcMsaa.top > outRc.top ||
				outRcMsaa.right < outRc.right ||
				outRcMsaa.bottom < outRc.bottom
				)
			)
		{
			outWindow = outWindowMsaa;
			outString = outStringMsaa;
			outRc = outRcMsaa;
		}
	}

	CSize GetEditControlTextSize(CEdit window, LPCTSTR lpszString, int nMaxWidth /*= INT_MAX*/)
	{
		CFontHandle pEdtFont = window.GetFont();
		if(!pEdtFont)
			return CSize{};

		CClientDC oDC{ window };
		CFontHandle pOldFont = oDC.SelectFont(pEdtFont);

		CRect rc{ 0, 0, nMaxWidth, 0 };
		oDC.DrawTextEx((LPTSTR)lpszString, -1, &rc, DT_CALCRECT | DT_EDITCONTROL | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS | DT_TABSTOP);

		oDC.SelectFont(pOldFont);

		return rc.Size();
	}

	CSize GetEditControlFirstLineSize(CEdit window, LPCTSTR lpszString)
	{
		CString firstLine(lpszString ? lpszString : L"");
		int newline = firstLine.FindOneOf(L"\r\n");
		if(newline >= 0)
			firstLine.Truncate(newline);

		CFontHandle font = window.GetFont();
		if(!font)
			return CSize{};

		CClientDC dc{ window };
		CFontHandle previousFont = dc.SelectFont(font);
		CRect rc{ 0, 0, 0, 0 };
		dc.DrawTextEx(
			firstLine.GetBuffer(),
			firstLine.GetLength(),
			&rc,
			DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX | DT_EXPANDTABS);
		firstLine.ReleaseBuffer();
		dc.SelectFont(previousFont);
		return rc.Size();
	}

	CSize TextSizeToEditClientSize(HWND hWnd, CEdit editWnd, CSize textSize)
	{
		CRect rc{ CPoint{ 0, 0 }, textSize };
		WndAdjustWindowRect(editWnd, &rc);

		UINT nLeftMargin, nRightMargin;
		editWnd.GetMargins(nLeftMargin, nRightMargin);

		CSize editClientSize;

		// Experiments show that this works kinda ok.
		editClientSize.cx = rc.Width() +
			nLeftMargin + nRightMargin +
			2 * GetSystemMetricsForWindow(hWnd, SM_CXBORDER) + 2 * GetSystemMetricsForWindow(hWnd, SM_CXDLGFRAME);

		editClientSize.cy = rc.Height() +
			2 * GetSystemMetricsForWindow(hWnd, SM_CYBORDER)/* +
			2 * GetSystemMetricsForWindow(hWnd, SM_CYDLGFRAME)*/;

		return editClientSize;
	}

	CSize EditClientSizeToTextSize(HWND hWnd, CEdit editWnd, CSize editClientSize)
	{
		UINT nLeftMargin, nRightMargin;
		editWnd.GetMargins(nLeftMargin, nRightMargin);

		editClientSize.cx -=
			nLeftMargin + nRightMargin +
			2 * GetSystemMetricsForWindow(hWnd, SM_CXBORDER) + 2 * GetSystemMetricsForWindow(hWnd, SM_CXDLGFRAME);

		editClientSize.cy -=
			2 * GetSystemMetricsForWindow(hWnd, SM_CYBORDER)/* +
			2 * GetSystemMetricsForWindow(hWnd, SM_CYDLGFRAME)*/;

		CRect rc{ CPoint{ 0, 0 }, editClientSize };
		WndUnadjustWindowRect(editWnd, &rc);

		return rc.Size();
	}
}
