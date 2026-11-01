#include "stdafx.h"
#include "ClippingStore.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <cwctype>

namespace
{
	std::filesystem::path ResolveDirectory(const CString& configuredDirectory);
	std::wstring ExtractClippingFileStem(const CString& text);
	std::wstring SanitizeWindowsFileStem(std::wstring value);
	bool IsReservedWindowsFileStem(const std::wstring& value);
	std::wstring CandidateFileName(const std::wstring& preferredStem, unsigned int index);
	std::string ToUtf8(const CString& text);
}

bool SaveNextClipping(
	const CString& configuredDirectory,
	const CString& text,
	CString& savedPath,
	CString& errorMessage)
{
	if(configuredDirectory.IsEmpty())
	{
		errorMessage = L"Configure a clipping destination in Textify.ini first.";
		return false;
	}

	std::filesystem::path directory = ResolveDirectory(configuredDirectory);
	std::error_code createError;
	std::filesystem::create_directories(directory, createError);
	if(createError)
	{
		errorMessage.Format(L"Could not create the clipping directory:\n%s", directory.c_str());
		return false;
	}

	std::string utf8 = ToUtf8(text);
	std::wstring preferredStem = ExtractClippingFileStem(text);
	for(unsigned int index = 0; index < (std::numeric_limits<unsigned int>::max)(); index++)
	{
		std::filesystem::path candidate = directory / CandidateFileName(preferredStem, index);
		if(::GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
			continue;

		HANDLE rawFile = ::CreateFileW(
			candidate.c_str(),
			GENERIC_WRITE,
			FILE_SHARE_READ,
			NULL,
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL,
			NULL);

		if(rawFile == INVALID_HANDLE_VALUE)
		{
			DWORD createFileError = ::GetLastError();
			if(createFileError == ERROR_FILE_EXISTS ||
				createFileError == ERROR_ALREADY_EXISTS ||
				::GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
				continue;
			errorMessage.Format(
				L"Could not create clipping file (Windows error %lu):\n%s",
				createFileError,
				candidate.c_str());
			return false;
		}
		CHandle file(rawFile);

		size_t offset = 0;
		DWORD writeError = ERROR_SUCCESS;
		while(offset < utf8.size())
		{
			DWORD chunkSize = static_cast<DWORD>(std::min<size_t>(
				utf8.size() - offset,
				(std::numeric_limits<DWORD>::max)()));
			DWORD written = 0;
			if(!::WriteFile(file, utf8.data() + offset, chunkSize, &written, NULL) || written == 0)
			{
				writeError = ::GetLastError();
				break;
			}
			offset += written;
		}
		file.Close();

		if(offset != utf8.size())
		{
			// Never delete a path here. Even though this invocation created it,
			// preserving a partial file is safer than risking user data when a
			// filesystem reports an unexpected collision or handle state.
			errorMessage.Format(
				L"Could not completely write clipping file (Windows error %lu). "
				L"The file was left intact:\n%s",
				writeError,
				candidate.c_str());
			return false;
		}

		savedPath = candidate.c_str();
		return true;
	}

	errorMessage = L"No available clipping filename was found.";
	return false;
}

namespace
{
	std::filesystem::path ResolveDirectory(const CString& configuredDirectory)
	{
		DWORD expandedLength = ::ExpandEnvironmentStringsW(configuredDirectory, NULL, 0);
		CString expanded;
		if(expandedLength > 0)
		{
			::ExpandEnvironmentStringsW(
				configuredDirectory,
				expanded.GetBuffer(expandedLength),
				expandedLength);
			expanded.ReleaseBuffer();
		}
		else
		{
			expanded = configuredDirectory;
		}

		std::filesystem::path result(expanded.GetString());
		if(result.is_relative())
		{
			WCHAR modulePath[MAX_PATH];
			DWORD length = ::GetModuleFileNameW(NULL, modulePath, ARRAYSIZE(modulePath));
			std::filesystem::path base(std::wstring(modulePath, length));
			result = base.parent_path() / result;
		}
		return result.lexically_normal();
	}

	std::wstring ExtractClippingFileStem(const CString& text)
	{
		std::wstring content(text.GetString(), text.GetLength());
		size_t lineStart = 0;
		while(lineStart <= content.size())
		{
			size_t lineEnd = content.find_first_of(L"\r\n", lineStart);
			if(lineEnd == std::wstring::npos) lineEnd = content.size();
			std::wstring line = content.substr(lineStart, lineEnd - lineStart);
			size_t firstText = line.find_first_not_of(L" \t");
			if(firstText != std::wstring::npos && line.compare(firstText, 3, L"# /") == 0)
			{
				std::wstring candidate = line.substr(firstText + 3);
				if(candidate.find_first_not_of(L" \t") == std::wstring::npos)
				{
					// Also accept a marker on its own line. Use the immediately
					// following logical line as the name source.
					size_t nextLineStart = lineEnd;
					if(nextLineStart < content.size() && content[nextLineStart] == L'\r') nextLineStart++;
					if(nextLineStart < content.size() && content[nextLineStart] == L'\n') nextLineStart++;
					size_t nextLineEnd = content.find_first_of(L"\r\n", nextLineStart);
					if(nextLineEnd == std::wstring::npos) nextLineEnd = content.size();
					candidate = content.substr(nextLineStart, nextLineEnd - nextLineStart);
				}
				return SanitizeWindowsFileStem(std::move(candidate));
			}

			if(lineEnd == content.size()) break;
			lineStart = lineEnd + 1;
			if(content[lineEnd] == L'\r' && lineStart < content.size() && content[lineStart] == L'\n')
				lineStart++;
		}
		return {};
	}

	std::wstring SanitizeWindowsFileStem(std::wstring value)
	{
		auto isTrimmedWhitespace = [](wchar_t character)
		{
			return character == L' ' || character == L'\t';
		};
		while(!value.empty() && isTrimmedWhitespace(value.front())) value.erase(value.begin());
		while(!value.empty() && isTrimmedWhitespace(value.back())) value.pop_back();

		std::wstring result;
		result.reserve(value.size());
		for(wchar_t character : value)
		{
			if(character < 32 ||
				character == L'<' || character == L'>' || character == L':' ||
				character == L'"' || character == L'/' || character == L'\\' ||
				character == L'|' || character == L'?' || character == L'*')
			{
				continue;
			}
			result.push_back(character);
		}

		while(!result.empty() && (result.back() == L' ' || result.back() == L'.'))
			result.pop_back();
		constexpr size_t kMaximumStemLength = 200;
		if(result.size() > kMaximumStemLength)
		{
			result.resize(kMaximumStemLength);
			while(!result.empty() && (result.back() == L' ' || result.back() == L'.'))
				result.pop_back();
		}
		if(IsReservedWindowsFileStem(result))
			result += L'_';
		return result;
	}

	bool IsReservedWindowsFileStem(const std::wstring& value)
	{
		if(value.empty()) return false;
		std::wstring deviceName = value.substr(0, value.find(L'.'));
		for(wchar_t& character : deviceName)
			character = static_cast<wchar_t>(std::towupper(character));
		if(deviceName == L"CON" || deviceName == L"PRN" ||
			deviceName == L"AUX" || deviceName == L"NUL")
		{
			return true;
		}
		if(deviceName.size() == 4 &&
			(deviceName.compare(0, 3, L"COM") == 0 || deviceName.compare(0, 3, L"LPT") == 0) &&
			deviceName[3] >= L'1' && deviceName[3] <= L'9')
		{
			return true;
		}
		return false;
	}

	std::wstring CandidateFileName(const std::wstring& preferredStem, unsigned int index)
	{
		if(preferredStem.empty())
			return L"clips" + std::to_wstring(static_cast<unsigned long long>(index) + 1) + L".md";
		return preferredStem + (index == 0 ? L"" : std::to_wstring(index)) + L".md";
	}

	std::string ToUtf8(const CString& text)
	{
		if(text.IsEmpty())
			return {};
		int size = ::WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), NULL, 0, NULL, NULL);
		std::string result(size, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), result.data(), size, NULL, NULL);
		return result;
	}
}
