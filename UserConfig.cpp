#include "stdafx.h"
#include "UserConfig.h"

#include <set>

namespace
{
	bool ParseClippingDestination(CString value, ClippingDestinationInfo& destination)
	{
		value.Trim();
		if(value.IsEmpty())
			return false;

		CString label;
		CString directory;
		if(value[0] == L'\"')
		{
			int closingQuote = value.Find(L'\"', 1);
			if(closingQuote <= 1)
				return false;

			label = value.Mid(1, closingQuote - 1);
			directory = value.Mid(closingQuote + 1);
			directory.TrimLeft();
			if(!directory.IsEmpty() && (directory[0] == L':' || directory[0] == L'|'))
				directory.Delete(0);
		}
		else
		{
			int separator = value.Find(L'|');
			if(separator < 0)
			{
				// A bare Windows drive path is an unlabeled entry, not the
				// compact label:path form.
				if(value.GetLength() >= 3 &&
					((value[0] >= L'A' && value[0] <= L'Z') ||
						(value[0] >= L'a' && value[0] <= L'z')) &&
					value[1] == L':' &&
					(value[2] == L'\\' || value[2] == L'/'))
				{
					return false;
				}

				// Also accept the compact form from the design sketch:
				// label:D:\path. The first colon separates the label, while
				// the drive-letter colon remains part of the directory.
				separator = value.Find(L':');
			}
			if(separator <= 0)
				return false;

			label = value.Left(separator);
			directory = value.Mid(separator + 1);
		}

		label.Trim();
		directory.Trim();
		if(directory.GetLength() >= 2 && directory[0] == L'\"' &&
			directory[directory.GetLength() - 1] == L'\"')
		{
			directory = directory.Mid(1, directory.GetLength() - 2);
			directory.Trim();
		}

		if(label.IsEmpty() || directory.IsEmpty())
			return false;

		destination.label = label;
		destination.directory = directory;
		return true;
	}

	bool LoadClippingDestinationList(
		LPCWSTR prefix,
		std::vector<ClippingDestinationInfo>& destinations,
		LPCWSTR iniFilePath)
	{
		std::set<int> indexes;
		bool schemaPresent = false;
		CString indexedPrefix;
		indexedPrefix.Format(L"%s_db_", prefix);
		auto collectIndex = [&indexes, &indexedPrefix, &schemaPresent](LPCWSTR name)
		{
			if(_wcsnicmp(name, indexedPrefix, indexedPrefix.GetLength()) != 0)
				return;
			LPCWSTR number = name + indexedPrefix.GetLength();
			if(*number == L'\0')
				return;
			for(LPCWSTR p = number; *p != L'\0'; p++)
			{
				if(*p < L'0' || *p > L'9')
					return;
			}
			schemaPresent = true;
			if(number[0] == L'0' && number[1] != L'\0')
				return;
			int index = _wtoi(number);
			if(index > 0 && index <= 1000)
				indexes.insert(index);
		};

		// Collect numbered flat keys from [clippings]. Enumerating first means
		// hand-edited gaps don't hide later destinations and an explicitly
		// malformed canonical list cannot silently reactivate a legacy path.
		std::vector<WCHAR> sectionValues(65536);
		GetPrivateProfileSection(
			L"clippings",
			sectionValues.data(),
			static_cast<DWORD>(sectionValues.size()),
			iniFilePath);
		for(LPCWSTR entry = sectionValues.data(); *entry != L'\0'; entry += wcslen(entry) + 1)
		{
			CString key(entry);
			int equals = key.Find(L'=');
			if(equals >= 0)
				key.Truncate(equals);
			collectIndex(key);
		}

		// Collect numbered section names such as [code_db_2].
		std::vector<WCHAR> sectionNames(65536);
		GetPrivateProfileSectionNames(
			sectionNames.data(),
			static_cast<DWORD>(sectionNames.size()),
			iniFilePath);
		for(LPCWSTR section = sectionNames.data(); *section != L'\0'; section += wcslen(section) + 1)
			collectIndex(section);

		WCHAR value[2049];
		for(int i : indexes)
		{
			CString key;
			key.Format(L"%s_db_%d", prefix, i);
			GetPrivateProfileString(L"clippings", key, L"", value, ARRAYSIZE(value), iniFilePath);

			// A section form is accepted too, which is convenient when labels
			// or paths need to contain punctuation:
			// [code_db_1] label=Python, path=D:\vault\python
			CString section;
			section.Format(L"%s_db_%d", prefix, i);
			WCHAR label[1025];
			WCHAR directory[2049];
			GetPrivateProfileString(section, L"label", L"", label, ARRAYSIZE(label), iniFilePath);
			GetPrivateProfileString(section, L"path", L"", directory, ARRAYSIZE(directory), iniFilePath);

			ClippingDestinationInfo destination;
			if(*label != L'\0' || *directory != L'\0')
			{
				destination.label = label;
				destination.directory = directory;
				destination.label.Trim();
				destination.directory.Trim();
				if(destination.label.IsEmpty() || destination.directory.IsEmpty())
					continue;
			}
			else if(!ParseCompactClippingDestination(value, destination))
			{
				// Entries without both a label and path aren't shown. Keep
				// scanning so one malformed item doesn't hide later items.
				continue;
			}

			bool duplicateLabel = false;
			for(const auto& existing : destinations)
			{
				if(existing.label.CompareNoCase(destination.label) == 0)
				{
					duplicateLabel = true;
					break;
				}
			}
			if(!duplicateLabel)
				destinations.push_back(destination);
		}

		return schemaPresent;
	}

	void AddLegacyClippingDestination(
		LPCWSTR label,
		std::initializer_list<LPCWSTR> keys,
		std::vector<ClippingDestinationInfo>& destinations,
		LPCWSTR iniFilePath)
	{
		if(!destinations.empty())
			return;

		WCHAR directory[2049];
		for(LPCWSTR key : keys)
		{
			GetPrivateProfileString(L"clippings", key, L"", directory, ARRAYSIZE(directory), iniFilePath);
			if(*directory != L'\0')
			{
				ClippingDestinationInfo destination;
				destination.label = label;
				destination.directory = directory;
				destination.directory.Trim();
				if(!destination.directory.IsEmpty() && destination.directory[0] == L'[')
					return;
				if(!destination.directory.IsEmpty())
					destinations.push_back(destination);
				return;
			}
		}
	}

	// Receives a path with an optional index, e.g.:
	// C:\path\to\awesome.exe,101
	// Removes the index from the string and returns it.
	int ExtractIconIndex(PWSTR pathWithOptionalIndex)
	{
		size_t len = wcslen(pathWithOptionalIndex);
		for(size_t i = len - 1; i > 0; i--)
		{
			WCHAR p = pathWithOptionalIndex[i];
			if(p == ',')
			{
				if(i == len - 1)
					break;

				pathWithOptionalIndex[i] = L'\0';
				return _wtoi(pathWithOptionalIndex + i + 1);
			}

			if(p < L'0' && p > L'9')
				break;
		}

		return 0;
	}

	CPath RelativeToAbsolutePathExpanded(PWSTR relativePath)
	{
		CPath moduleFileNamePath;

		GetModuleFileName(NULL, moduleFileNamePath.m_strPath.GetBuffer(MAX_PATH), MAX_PATH);
		moduleFileNamePath.m_strPath.ReleaseBuffer();
		moduleFileNamePath.RemoveFileSpec();

		CString relativePathExpanded;
		DWORD relativePathExpandedSize = ExpandEnvironmentStrings(relativePath, nullptr, 0);
		ExpandEnvironmentStrings(relativePath, relativePathExpanded.GetBuffer(relativePathExpandedSize), relativePathExpandedSize);
		relativePathExpanded.ReleaseBuffer();

		CPath result;
		result.Combine(moduleFileNamePath, relativePathExpanded);

		return result;
	}
}

CPath UserConfig::GetIniFilePath()
{
	CPath iniFilePath;
	GetModuleFileName(NULL, iniFilePath.m_strPath.GetBuffer(MAX_PATH), MAX_PATH);
	iniFilePath.m_strPath.ReleaseBuffer();
	iniFilePath.RemoveFileSpec();
	iniFilePath.Append(L"Textify.ini");
	return iniFilePath;
}

UserConfig::UserConfig()
{
	LoadFromIniFile();
}

bool UserConfig::LoadFromIniFile()
{
	CPath iniFilePath = GetIniFilePath();
	m_webButtonInfos.clear();
	m_excludedPrograms.clear();
	m_codeClippingDestinations.clear();
	m_manualClippingDestinations.clear();

	WCHAR szBuffer[1025];

	m_mouseHotKey.key = GetPrivateProfileInt(L"mouse", L"key", 0, iniFilePath);
	m_mouseHotKey.ctrl = GetPrivateProfileInt(L"mouse", L"ctrl", 0, iniFilePath);
	m_mouseHotKey.alt = GetPrivateProfileInt(L"mouse", L"alt", 0, iniFilePath);
	m_mouseHotKey.shift = GetPrivateProfileInt(L"mouse", L"shift", 0, iniFilePath);

	m_keybdHotKey.key = GetPrivateProfileInt(L"keyboard", L"key", 0, iniFilePath);
	m_keybdHotKey.ctrl = GetPrivateProfileInt(L"keyboard", L"ctrl", 0, iniFilePath);
	m_keybdHotKey.alt = GetPrivateProfileInt(L"keyboard", L"alt", 0, iniFilePath);
	m_keybdHotKey.shift = GetPrivateProfileInt(L"keyboard", L"shift", 0, iniFilePath);

	m_uiLanguage = static_cast<LANGID>(GetPrivateProfileInt(L"config", L"ui_language", 0, iniFilePath));
	m_checkForUpdates = GetPrivateProfileInt(L"config", L"check_for_updates", 1, iniFilePath);
	m_autoCopySelection = GetPrivateProfileInt(L"config", L"auto_copy_selection", 0, iniFilePath);
	m_hideWndOnStartup = GetPrivateProfileInt(L"config", L"hide_wnd_on_startup", 0, iniFilePath);
	m_hideTrayIcon = GetPrivateProfileInt(L"config", L"hide_tray_icon", 0, iniFilePath);
	m_textBoxNonResiable = GetPrivateProfileInt(L"config", L"text_box_non_resizable", 0, iniFilePath);
	GetPrivateProfileString(L"config", L"font_name", L"", m_fontName.GetBuffer(LF_FACESIZE), LF_FACESIZE, iniFilePath);
	m_fontName.ReleaseBuffer();
	m_fontSize = GetPrivateProfileInt(L"config", L"font_size", 0, iniFilePath);
	m_unicodeSpacesToAscii = GetPrivateProfileInt(L"config", L"unicode_spaces_to_ascii", 0, iniFilePath);

	m_textRetrievalMethod = TextRetrievalMethod::defaultMethod;
	GetPrivateProfileString(L"config", L"text_retrieval_method", L"", szBuffer, ARRAYSIZE(szBuffer), iniFilePath);
	if(_wcsicmp(szBuffer, L"msaa") == 0)
	{
		m_textRetrievalMethod = TextRetrievalMethod::msaa;
	}
	else if(_wcsicmp(szBuffer, L"uia") == 0)
	{
		m_textRetrievalMethod = TextRetrievalMethod::uia;
	}

	m_webButtonsIconSize = GetPrivateProfileInt(L"web_buttons", L"icon_size", 16, iniFilePath);
	m_webButtonsPerRow = GetPrivateProfileInt(L"web_buttons", L"buttons_per_row", 8, iniFilePath);
	FriendlyClippingArrays friendlyArrays;
	CString friendlyArrayError;
	CString rawIniText;
	bool rawIniRead = ReadIniTextFile(iniFilePath, rawIniText);
	bool friendlyArraysParsed = rawIniRead &&
		ParseFriendlyClippingArrays(rawIniText, friendlyArrays, friendlyArrayError);
	// A malformed friendly list must never reactivate an old fallback path.
	// Fail both clipping catalogs closed until the INI is fixed.
	bool hasCodeDestinationList = rawIniRead && !friendlyArraysParsed
		? true
		: friendlyArrays.codePresent;
	bool hasManualDestinationList = rawIniRead && !friendlyArraysParsed
		? true
		: friendlyArrays.manualPresent;
	if(friendlyArraysParsed && hasCodeDestinationList)
		m_codeClippingDestinations = friendlyArrays.code;
	if(friendlyArraysParsed && hasManualDestinationList)
		m_manualClippingDestinations = friendlyArrays.manual;
	if(!hasCodeDestinationList)
	{
		hasCodeDestinationList = LoadClippingDestinationList(
			L"code", m_codeClippingDestinations, iniFilePath);
	}
	if(!hasManualDestinationList)
	{
		hasManualDestinationList = LoadClippingDestinationList(
			L"manual", m_manualClippingDestinations, iniFilePath);
	}
	if(!hasCodeDestinationList)
	{
		AddLegacyClippingDestination(
			L"Default",
			{ L"code_directory", L"code_db", L"directory" },
			m_codeClippingDestinations,
			iniFilePath);
	}
	if(!hasManualDestinationList)
	{
		AddLegacyClippingDestination(
			L"Default",
			{ L"manual_directory", L"manual_db" },
			m_manualClippingDestinations,
			iniFilePath);
	}

	for(int i = 1; ; i++)
	{
		CString iniSectionName;
		iniSectionName.Format(L"web_button_%d", i);

		GetPrivateProfileString(iniSectionName, L"command", L"", szBuffer, ARRAYSIZE(szBuffer), iniFilePath);
		if(*szBuffer == L'\0')
			break;

		WebButtonInfo webButtonInfo;
		webButtonInfo.command = szBuffer;

		GetPrivateProfileString(iniSectionName, L"name", L"", szBuffer, ARRAYSIZE(szBuffer), iniFilePath);
		webButtonInfo.name = szBuffer;

		GetPrivateProfileString(iniSectionName, L"icon", L"", szBuffer, ARRAYSIZE(szBuffer), iniFilePath);
		if(*szBuffer != L'\0')
		{
			webButtonInfo.iconIndex = ExtractIconIndex(szBuffer);
			webButtonInfo.iconPath = RelativeToAbsolutePathExpanded(szBuffer);
		}

		GetPrivateProfileString(iniSectionName, L"key", L"", szBuffer, ARRAYSIZE(szBuffer), iniFilePath);
		webButtonInfo.acceleratorKey = *szBuffer;

		webButtonInfo.width = GetPrivateProfileInt(iniSectionName, L"width", 0, iniFilePath);
		webButtonInfo.height = GetPrivateProfileInt(iniSectionName, L"height", 0, iniFilePath);

		m_webButtonInfos.push_back(webButtonInfo);
	}

	for(int i = 1; ; i++)
	{
		CString iniKeyName;
		iniKeyName.Format(L"%d", i);

		GetPrivateProfileString(L"exclude", iniKeyName, L"", szBuffer, ARRAYSIZE(szBuffer), iniFilePath);
		if(*szBuffer == L'\0')
			break;

		m_excludedPrograms.push_back(szBuffer);
	}

	return true;
}

bool UserConfig::SaveToIniFile() const
{
	CPath iniFilePath = GetIniFilePath();
	if(!iniFilePath)
		return false;

	bool succeeded = true;

	CString str;

	str.Format(L"%d", m_mouseHotKey.key);
	if(!WritePrivateProfileString(L"mouse", L"key", str, iniFilePath))
		succeeded = false;

	str.Format(L"%d", m_mouseHotKey.ctrl ? 1 : 0);
	if(!WritePrivateProfileString(L"mouse", L"ctrl", str, iniFilePath))
		succeeded = false;

	str.Format(L"%d", m_mouseHotKey.alt ? 1 : 0);
	if(!WritePrivateProfileString(L"mouse", L"alt", str, iniFilePath))
		succeeded = false;

	str.Format(L"%d", m_mouseHotKey.shift ? 1 : 0);
	if(!WritePrivateProfileString(L"mouse", L"shift", str, iniFilePath))
		succeeded = false;

	// Don't write the other configuration, as there's no GUI for it.

	return succeeded;
}
