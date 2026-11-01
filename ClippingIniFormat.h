#pragma once

struct ClippingDestinationInfo
{
	CString label;
	CString directory;
};

struct FriendlyClippingArrays
{
	bool codePresent = false;
	bool manualPresent = false;
	std::vector<ClippingDestinationInfo> code;
	std::vector<ClippingDestinationInfo> manual;
};

bool ReadIniTextFile(LPCWSTR filePath, CString& text);

// Parses the compact value forms accepted by the legacy numbered-key format,
// for example Python|D:\db or "Python":D:\db.
bool ParseCompactClippingDestination(
	CString value,
	ClippingDestinationInfo& destination);

// Parses only the friendly arrays in the [clippings] section. Absence is not
// an error and is reported through codePresent/manualPresent.
bool ParseFriendlyClippingArrays(
	const CString& iniText,
	FriendlyClippingArrays& arrays,
	CString& errorMessage);

// Converts the on-disk numbered section representation into the friendly
// array representation shown by the More Settings editor.
bool ConvertClippingIniToEditorFormat(
	const CString& storedIniText,
	CString& editorIniText,
	CString& errorMessage);

// Validates the friendly arrays and converts them back to numbered sections
// for reliable on-disk loading by the Win32 profile APIs.
bool ConvertClippingIniToStorageFormat(
	const CString& editorIniText,
	CString& storedIniText,
	CString& errorMessage);
