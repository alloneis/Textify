#include "stdafx.h"
#include "SettingsDlg.h"
#include "UserConfig.h"
#include "ClippingIniFormat.h"

BOOL CSettingsDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	// Center the dialog on the screen.
	CenterWindow();

	// Init resizing.
	DlgResize_Init();

	// Init font.
	CStatic configTextCtrl(GetDlgItem(IDC_CONFIG_TEXT));
	CFontHandle font(configTextCtrl.GetFont());
	CLogFont fontAttributes(font);
	wcscpy_s(fontAttributes.lfFaceName, L"Consolas");
	m_fontConfigText = fontAttributes.CreateFontIndirect();
	configTextCtrl.SetFont(m_fontConfigText);

	// Load strings.
	CString str;
	str.LoadString(IDS_SETTINGSDLG_TITLE);
	SetWindowText(str);
	str.LoadString(IDS_SETTINGSDLG_OK);
	SetDlgItemText(IDOK, str);
	str.LoadString(IDS_SETTINGSDLG_CANCEL);
	SetDlgItemText(IDCANCEL, str);

	// Load ini file.
	CEdit configText = CEdit(GetDlgItem(IDC_CONFIG_TEXT));
	CString storedIniText;
	if(!LoadIniFile(storedIniText))
	{
		configText.SetReadOnly(TRUE);
		CButton(GetDlgItem(IDOK)).EnableWindow(FALSE);
		MessageBox(L"Could not read Textify.ini. The editor is read-only so the existing file cannot be overwritten.",
			L"Textify - Settings", MB_ICONERROR);
		m_wndEdit.SubclassWindow(configText);
		return TRUE;
	}
	m_loadedStoredIniText = storedIniText;

	FriendlyClippingArrays storedFriendlyArrays;
	CString friendlyParseError;
	bool friendlyArraysValid = ParseFriendlyClippingArrays(
		storedIniText, storedFriendlyArrays, friendlyParseError);
	bool storageNeedsNormalization = friendlyArraysValid &&
		(storedFriendlyArrays.codePresent || storedFriendlyArrays.manualPresent);
	CString editorIniText;
	CString conversionError;
	bool conversionSucceeded = ConvertClippingIniToEditorFormat(
		storedIniText, editorIniText, conversionError);
	if(!conversionSucceeded)
	{
		editorIniText = storedIniText;
		configText.SetReadOnly(TRUE);
		MessageBox(conversionError, L"Textify - Clipping configuration", MB_ICONERROR);
	}
	configText.SetWindowText(editorIniText);
	configText.SetLimitText(0);

	// Other initialization stuff.
	m_wndEdit.SubclassWindow(configText);
	m_editorAvailable = conversionSucceeded;
	CButton(GetDlgItem(IDOK)).EnableWindow(
		m_editorAvailable && storageNeedsNormalization);

	return TRUE;
}

void CSettingsDlg::OnDestroy()
{
}

void CSettingsDlg::OnConfigTextChange(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if(m_editorAvailable)
	{
		m_editorChanged = true;
		CButton(GetDlgItem(IDOK)).EnableWindow(TRUE);
	}
}

void CSettingsDlg::OnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	if(!m_editorAvailable)
		return;
	CString currentStoredIniText;
	if(!LoadIniFile(currentStoredIniText) ||
		currentStoredIniText.Compare(m_loadedStoredIniText) != 0)
	{
		MessageBox(
			L"Textify.ini changed after this editor was opened. Reopen More Settings so your save doesn't overwrite the newer file.",
			L"Textify - Settings",
			MB_ICONWARNING);
		return;
	}

	CEdit configText = CEdit(GetDlgItem(IDC_CONFIG_TEXT));
	CString str;
	configText.GetWindowText(str);
	CString storedIniText;
	CString conversionError;
	if(!ConvertClippingIniToStorageFormat(str, storedIniText, conversionError))
	{
		MessageBox(conversionError, L"Textify - Clipping configuration", MB_ICONERROR);
		return;
	}
	if(!SaveIniFile(storedIniText))
	{
		MessageBox(L"Could not save Textify.ini.", L"Textify - Settings", MB_ICONERROR);
		return;
	}

	EndDialog(nID);
}

void CSettingsDlg::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CString title;
	title.LoadString(IDS_SETTINGSDLG_WARNING_DISCARD_TITLE);

	CString text;
	text.LoadString(IDS_SETTINGSDLG_WARNING_DISCARD_TEXT);

	if(m_editorChanged &&
		MessageBox(text, title, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
	{
		return;
	}

	EndDialog(nID);
}

UINT CSettingsDlg::OnGetDlgCode(LPMSG lpMsg)
{
	// Prevent selecting all text when focused.
	// https://devblogs.microsoft.com/oldnewthing/20031114-00/?p=41823
	return m_wndEdit.DefWindowProc() & ~DLGC_HASSETSEL;
}

// Private functions

CPath CSettingsDlg::GetIniFilePath()
{
	return UserConfig::GetIniFilePath();
}

bool CSettingsDlg::LoadIniFile(CString& result)
{
	return ReadIniTextFile(GetIniFilePath(), result);
}

bool CSettingsDlg::SaveIniFile(CString newFileContents)
{
	CPath iniFilePath = GetIniFilePath();
	CPath iniDirectory = iniFilePath;
	iniDirectory.RemoveFileSpec();
	WCHAR temporaryPath[MAX_PATH];
	if(!GetTempFileName(iniDirectory, L"tfi", 0, temporaryPath))
		return false;

	HANDLE file = CreateFile(
		temporaryPath,
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if(file == INVALID_HANDLE_VALUE)
	{
		DeleteFile(temporaryPath);
		return false;
	}

	auto writeAll = [file](const void* data, DWORD bytes)
	{
		const BYTE* cursor = static_cast<const BYTE*>(data);
		while(bytes > 0)
		{
			DWORD written = 0;
			if(!WriteFile(file, cursor, bytes, &written, nullptr) || written == 0)
				return false;
			cursor += written;
			bytes -= written;
		}
		return true;
	};

	const BYTE bom[] = { 0xFF, 0xFE };
	ULONGLONG contentBytes64 =
		static_cast<ULONGLONG>(newFileContents.GetLength()) * sizeof(WCHAR);
	bool succeeded = contentBytes64 <= MAXDWORD &&
		writeAll(bom, sizeof(bom)) &&
		writeAll(newFileContents.GetString(), static_cast<DWORD>(contentBytes64)) &&
		FlushFileBuffers(file) != FALSE;
	CloseHandle(file);

	if(!succeeded || !MoveFileEx(
		temporaryPath,
		iniFilePath,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFile(temporaryPath);
		return false;
	}
	return true;
}
