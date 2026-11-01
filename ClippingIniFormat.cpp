#include "stdafx.h"
#include "ClippingIniFormat.h"

#include <cwctype>
#include <map>

namespace
{
	struct IniDocument
	{
		std::vector<CString> lines;
		CString eol = L"\r\n";
		bool finalEol = false;
	};

	struct IniSection
	{
		size_t start = 0;
		size_t end = 0;
		CString name;
	};

	struct FriendlyArrayBlock
	{
		bool found = false;
		size_t startLine = 0;
		size_t endLine = 0;
		CString contents;
	};

	struct CanonicalCatalog
	{
		bool codeSchemaPresent = false;
		bool manualSchemaPresent = false;
		std::vector<ClippingDestinationInfo> code;
		std::vector<ClippingDestinationInfo> manual;
	};

	IniDocument SplitIniDocument(const CString& text)
	{
		IniDocument document;
		int start = 0;
		bool detectedEol = false;
		while(start < text.GetLength())
		{
			int newline = text.Find(L'\n', start);
			if(newline < 0)
			{
				document.lines.push_back(text.Mid(start));
				return document;
			}

			int lineEnd = newline;
			if(lineEnd > start && text[lineEnd - 1] == L'\r')
			{
				lineEnd--;
				if(!detectedEol)
					document.eol = L"\r\n";
			}
			else if(!detectedEol)
			{
				document.eol = L"\n";
			}
			detectedEol = true;
			document.lines.push_back(text.Mid(start, lineEnd - start));
			start = newline + 1;
		}

		document.finalEol = text.GetLength() > 0 && text[text.GetLength() - 1] == L'\n';
		return document;
	}

	CString JoinIniDocument(const IniDocument& document)
	{
		CString result;
		for(size_t i = 0; i < document.lines.size(); i++)
		{
			result += document.lines[i];
			if(i + 1 < document.lines.size() || document.finalEol)
				result += document.eol;
		}
		return result;
	}

	bool TryGetSectionName(const CString& line, CString& name)
	{
		CString trimmed = line;
		trimmed.Trim();
		if(trimmed.GetLength() < 2 || trimmed[0] != L'[' ||
			trimmed[trimmed.GetLength() - 1] != L']')
		{
			return false;
		}
		name = trimmed.Mid(1, trimmed.GetLength() - 2);
		name.Trim();
		return !name.IsEmpty();
	}

	std::vector<IniSection> GetIniSections(const IniDocument& document)
	{
		std::vector<IniSection> sections;
		for(size_t i = 0; i < document.lines.size(); i++)
		{
			CString name;
			if(!TryGetSectionName(document.lines[i], name))
				continue;
			if(!sections.empty())
				sections.back().end = i;
			sections.push_back({ i, document.lines.size(), name });
		}
		return sections;
	}

	bool TryParseKeyValue(const CString& line, CString& key, CString& value)
	{
		CString trimmed = line;
		trimmed.Trim();
		if(trimmed.IsEmpty() || trimmed[0] == L';' || trimmed[0] == L'#')
			return false;
		int equals = trimmed.Find(L'=');
		if(equals <= 0)
			return false;
		key = trimmed.Left(equals);
		value = trimmed.Mid(equals + 1);
		key.Trim();
		value.Trim();
		return !key.IsEmpty();
	}

	const IniSection* FindSection(
		const std::vector<IniSection>& sections,
		LPCWSTR name)
	{
		for(const auto& section : sections)
		{
			if(section.name.CompareNoCase(name) == 0)
				return &section;
		}
		return nullptr;
	}

	bool TryParseIndexedName(const CString& name, LPCWSTR prefix, int& index)
	{
		int prefixLength = static_cast<int>(wcslen(prefix));
		if(name.GetLength() <= prefixLength ||
			name.Left(prefixLength).CompareNoCase(prefix) != 0)
		{
			return false;
		}
		CString number = name.Mid(prefixLength);
		if(number.GetLength() > 1 && number[0] == L'0')
			return false;
		for(int i = 0; i < number.GetLength(); i++)
		{
			if(number[i] < L'0' || number[i] > L'9')
				return false;
		}
		index = _wtoi(number);
		return index > 0 && index <= 1000;
	}

	bool StartsWithNoCase(const CString& value, LPCWSTR prefix)
	{
		int prefixLength = static_cast<int>(wcslen(prefix));
		return value.GetLength() >= prefixLength &&
			value.Left(prefixLength).CompareNoCase(prefix) == 0;
	}

	bool HasNumericSuffix(const CString& value, LPCWSTR prefix)
	{
		int prefixLength = static_cast<int>(wcslen(prefix));
		if(!StartsWithNoCase(value, prefix) || value.GetLength() == prefixLength)
			return false;
		for(int i = prefixLength; i < value.GetLength(); i++)
		{
			if(value[i] < L'0' || value[i] > L'9')
				return false;
		}
		return true;
	}

	CString StripOptionalQuotes(CString value)
	{
		value.Trim();
		if(value.GetLength() >= 2 && value[0] == L'"' &&
			value[value.GetLength() - 1] == L'"')
		{
			value = value.Mid(1, value.GetLength() - 2);
		}
		value.Trim();
		return value;
	}

	bool AddUniqueDestination(
		std::vector<ClippingDestinationInfo>& destinations,
		ClippingDestinationInfo destination,
		LPCWSTR listName,
		CString& errorMessage)
	{
		destination.label.Trim();
		destination.directory.Trim();
		if(destination.label.IsEmpty() || destination.directory.IsEmpty())
		{
			errorMessage.Format(L"%s entries require both a label and a path.", listName);
			return false;
		}
		if(destinations.size() >= 1000)
		{
			errorMessage.Format(L"%s supports at most 1000 destinations.", listName);
			return false;
		}
		if(destination.label.GetLength() > 1024)
		{
			errorMessage.Format(L"%s labels cannot exceed 1024 characters.", listName);
			return false;
		}
		if(destination.directory.GetLength() > 2048)
		{
			errorMessage.Format(L"%s paths cannot exceed 2048 characters.", listName);
			return false;
		}
		if(destination.label.Find(L'"') >= 0 || destination.directory.Find(L'"') >= 0)
		{
			errorMessage.Format(L"%s labels and paths cannot contain a double quote (\").", listName);
			return false;
		}
		for(int i = 0; i < destination.label.GetLength(); i++)
		{
			if(destination.label[i] == L'\r' || destination.label[i] == L'\n' ||
				destination.label[i] == L'\0')
			{
				errorMessage.Format(L"%s labels cannot contain a line break.", listName);
				return false;
			}
		}
		for(int i = 0; i < destination.directory.GetLength(); i++)
		{
			if(destination.directory[i] == L'\r' || destination.directory[i] == L'\n' ||
				destination.directory[i] == L'\0')
			{
				errorMessage.Format(L"%s paths cannot contain a line break.", listName);
				return false;
			}
		}
		for(const auto& existing : destinations)
		{
			if(existing.label.CompareNoCase(destination.label) == 0)
			{
				errorMessage.Format(L"%s contains the duplicate label: %s", listName,
					destination.label.GetString());
				return false;
			}
		}
		destinations.push_back(destination);
		return true;
	}

	bool FindFriendlyArrayBlock(
		const IniDocument& document,
		const IniSection& clippingsSection,
		LPCWSTR keyName,
		FriendlyArrayBlock& block,
		CString& errorMessage)
	{
		for(size_t lineIndex = clippingsSection.start + 1;
			lineIndex < clippingsSection.end;
			lineIndex++)
		{
			CString key;
			CString value;
			if(!TryParseKeyValue(document.lines[lineIndex], key, value) ||
				key.CompareNoCase(keyName) != 0 || value.IsEmpty() || value[0] != L'[')
			{
				continue;
			}

			block.found = true;
			block.startLine = lineIndex;
			bool inQuote = false;
			for(size_t currentLine = lineIndex;
				currentLine < clippingsSection.end;
				currentLine++)
			{
				CString fragment = currentLine == lineIndex
					? value.Mid(1)
					: document.lines[currentLine];
				for(int i = 0; i < fragment.GetLength(); i++)
				{
					WCHAR ch = fragment[i];
					if(!inQuote && (ch == L';' || ch == L'#'))
					{
						// Comments are part of the friendly editor format, but
						// brackets and quotes inside them aren't array syntax.
						block.contents += fragment.Mid(i);
						break;
					}
					if(ch == L'"')
						inQuote = !inQuote;
					if(ch == L']' && !inQuote)
					{
						CString trailing = fragment.Mid(i + 1);
						trailing.TrimLeft();
						if(!trailing.IsEmpty() && trailing[0] != L';' && trailing[0] != L'#')
						{
							errorMessage.Format(
								L"The %s array has unexpected text after its closing ].",
								keyName);
							return false;
						}
						block.endLine = currentLine;
						for(size_t laterLine = currentLine + 1;
							laterLine < clippingsSection.end;
							laterLine++)
						{
							CString laterKey;
							CString laterValue;
							if(TryParseKeyValue(document.lines[laterLine], laterKey, laterValue) &&
								laterKey.CompareNoCase(keyName) == 0 &&
								!laterValue.IsEmpty() && laterValue[0] == L'[')
							{
								errorMessage.Format(L"The [clippings] section contains more than one %s array.", keyName);
								return false;
							}
						}
						return true;
					}
					block.contents.AppendChar(ch);
				}
				if(currentLine + 1 < clippingsSection.end)
					block.contents.AppendChar(L'\n');
			}

			errorMessage.Format(L"The %s array is missing its closing ].", keyName);
			return false;
		}
		return true;
	}

	void SkipArrayWhitespaceAndComments(const CString& text, int& position)
	{
		while(position < text.GetLength())
		{
			WCHAR ch = text[position];
			if(iswspace(ch))
			{
				position++;
				continue;
			}
			if(ch == L';' || ch == L'#')
			{
				while(position < text.GetLength() && text[position] != L'\n')
					position++;
				continue;
			}
			break;
		}
	}

	bool ParseQuotedArrayValue(
		const CString& text,
		int& position,
		CString& value,
		LPCWSTR listName,
		CString& errorMessage)
	{
		if(position >= text.GetLength() || text[position] != L'"')
		{
			errorMessage.Format(L"%s entries must use quoted labels and paths.", listName);
			return false;
		}
		int closingQuote = text.Find(L'"', position + 1);
		if(closingQuote < 0)
		{
			errorMessage.Format(L"%s contains an unterminated quoted value.", listName);
			return false;
		}
		value = text.Mid(position + 1, closingQuote - position - 1);
		position = closingQuote + 1;
		return true;
	}

	bool ParseFriendlyArrayContents(
		const CString& contents,
		LPCWSTR listName,
		std::vector<ClippingDestinationInfo>& destinations,
		CString& errorMessage)
	{
		int position = 0;
		bool firstEntry = true;
		while(true)
		{
			SkipArrayWhitespaceAndComments(contents, position);
			if(position >= contents.GetLength())
				return true;
			if(!firstEntry)
			{
				if(contents[position] != L',')
				{
					errorMessage.Format(L"%s entries must be separated by commas.", listName);
					return false;
				}
				position++;
				SkipArrayWhitespaceAndComments(contents, position);
				if(position >= contents.GetLength())
					return true; // A trailing comma is allowed.
			}

			ClippingDestinationInfo destination;
			if(!ParseQuotedArrayValue(contents, position, destination.label, listName, errorMessage))
				return false;
			while(position < contents.GetLength() && iswspace(contents[position]))
				position++;
			if(position >= contents.GetLength() || contents[position] != L':')
			{
				errorMessage.Format(L"%s requires a colon between each label and path.", listName);
				return false;
			}
			position++;
			while(position < contents.GetLength() && iswspace(contents[position]))
				position++;
			if(!ParseQuotedArrayValue(contents, position, destination.directory, listName, errorMessage))
				return false;
			if(!AddUniqueDestination(destinations, destination, listName, errorMessage))
				return false;
			firstEntry = false;
		}
	}

	bool LoadCanonicalCatalog(
		const IniDocument& document,
		bool loadCode,
		bool loadManual,
		CanonicalCatalog& catalog,
		CString& errorMessage)
	{
		std::map<int, ClippingDestinationInfo> codeByIndex;
		std::map<int, ClippingDestinationInfo> manualByIndex;
		auto sections = GetIniSections(document);
		for(const auto& section : sections)
		{
			int index = 0;
			std::map<int, ClippingDestinationInfo>* destinationMap = nullptr;
			LPCWSTR listName = nullptr;
			if(HasNumericSuffix(section.name, L"code_db_"))
			{
				if(!loadCode)
					continue;
				if(!TryParseIndexedName(section.name, L"code_db_", index))
				{
					errorMessage.Format(L"[%s] has an invalid destination index.",
						section.name.GetString());
					return false;
				}
				catalog.codeSchemaPresent = true;
				destinationMap = &codeByIndex;
				listName = L"code_db";
			}
			else if(HasNumericSuffix(section.name, L"manual_db_"))
			{
				if(!loadManual)
					continue;
				if(!TryParseIndexedName(section.name, L"manual_db_", index))
				{
					errorMessage.Format(L"[%s] has an invalid destination index.",
						section.name.GetString());
					return false;
				}
				catalog.manualSchemaPresent = true;
				destinationMap = &manualByIndex;
				listName = L"manual_db";
			}
			else
			{
				continue;
			}

			ClippingDestinationInfo destination;
			bool labelSeen = false;
			bool pathSeen = false;
			for(size_t line = section.start + 1; line < section.end; line++)
			{
				CString key;
				CString value;
				if(!TryParseKeyValue(document.lines[line], key, value))
					continue;
				if(key.CompareNoCase(L"label") == 0)
				{
					if(labelSeen)
					{
						errorMessage.Format(L"[%s] contains more than one label= key.",
							section.name.GetString());
						return false;
					}
					labelSeen = true;
					destination.label = StripOptionalQuotes(value);
				}
				else if(key.CompareNoCase(L"path") == 0)
				{
					if(pathSeen)
					{
						errorMessage.Format(L"[%s] contains more than one path= key.",
							section.name.GetString());
						return false;
					}
					pathSeen = true;
					destination.directory = StripOptionalQuotes(value);
				}
			}
			if(destination.label.IsEmpty() || destination.directory.IsEmpty())
			{
				errorMessage.Format(L"[%s] requires both label= and path=.",
					section.name.GetString());
				return false;
			}
			if(destinationMap->contains(index))
			{
				errorMessage.Format(L"The %s list defines index %d more than once.",
					listName, index);
				return false;
			}
			(*destinationMap)[index] = destination;
		}

		const IniSection* clippings = FindSection(sections, L"clippings");
		if(clippings)
		{
			for(size_t line = clippings->start + 1; line < clippings->end; line++)
			{
				CString key;
				CString value;
				if(!TryParseKeyValue(document.lines[line], key, value))
					continue;
				int index = 0;
				std::map<int, ClippingDestinationInfo>* destinationMap = nullptr;
				LPCWSTR listName = nullptr;
				if(HasNumericSuffix(key, L"code_db_"))
				{
					if(!loadCode)
						continue;
					if(!TryParseIndexedName(key, L"code_db_", index))
					{
						errorMessage.Format(L"%s has an invalid destination index.",
							key.GetString());
						return false;
					}
					catalog.codeSchemaPresent = true;
					destinationMap = &codeByIndex;
					listName = L"code_db";
				}
				else if(HasNumericSuffix(key, L"manual_db_"))
				{
					if(!loadManual)
						continue;
					if(!TryParseIndexedName(key, L"manual_db_", index))
					{
						errorMessage.Format(L"%s has an invalid destination index.",
							key.GetString());
						return false;
					}
					catalog.manualSchemaPresent = true;
					destinationMap = &manualByIndex;
					listName = L"manual_db";
				}
				else
				{
					continue;
				}

				if(destinationMap->contains(index))
					continue;
				ClippingDestinationInfo destination;
				if(!ParseCompactClippingDestination(value, destination))
				{
					errorMessage.Format(L"%s_%d is not a labeled destination.", listName, index);
					return false;
				}
				(*destinationMap)[index] = destination;
			}
		}

		for(const auto& item : codeByIndex)
		{
			if(!AddUniqueDestination(catalog.code, item.second, L"code_db", errorMessage))
				return false;
		}
		for(const auto& item : manualByIndex)
		{
			if(!AddUniqueDestination(catalog.manual, item.second, L"manual_db", errorMessage))
				return false;
		}
		return true;
	}

	CString FindClippingsValue(
		const IniDocument& document,
		const IniSection* clippings,
		std::initializer_list<LPCWSTR> keys)
	{
		if(!clippings)
			return L"";
		for(LPCWSTR wantedKey : keys)
		{
			for(size_t line = clippings->start + 1; line < clippings->end; line++)
			{
				CString key;
				CString value;
				if(TryParseKeyValue(document.lines[line], key, value) &&
					key.CompareNoCase(wantedKey) == 0 &&
					(value.IsEmpty() || value[0] != L'['))
				{
					return StripOptionalQuotes(value);
				}
			}
		}
		return L"";
	}

	bool BuildCatalogForEditor(
		const IniDocument& document,
		std::vector<ClippingDestinationInfo>& code,
		std::vector<ClippingDestinationInfo>& manual,
		CString& errorMessage)
	{
		CString text = JoinIniDocument(document);
		FriendlyClippingArrays friendly;
		if(!ParseFriendlyClippingArrays(text, friendly, errorMessage))
			return false;

		CanonicalCatalog canonical;
		if(!LoadCanonicalCatalog(
			document,
			!friendly.codePresent,
			!friendly.manualPresent,
			canonical,
			errorMessage))
			return false;

		auto sections = GetIniSections(document);
		const IniSection* clippings = FindSection(sections, L"clippings");
		if(friendly.codePresent)
			code = friendly.code;
		else if(canonical.codeSchemaPresent)
			code = canonical.code;
		else
		{
			CString legacy = FindClippingsValue(
				document, clippings, { L"code_directory", L"code_db", L"directory" });
			if(!legacy.IsEmpty())
				code.push_back({ L"Default", legacy });
		}

		if(friendly.manualPresent)
			manual = friendly.manual;
		else if(canonical.manualSchemaPresent)
			manual = canonical.manual;
		else
		{
			CString legacy = FindClippingsValue(
				document, clippings, { L"manual_directory", L"manual_db" });
			if(!legacy.IsEmpty())
				manual.push_back({ L"Default", legacy });
		}
		return true;
	}

	bool IsClippingDefinitionKey(const CString& key)
	{
		if(key.CompareNoCase(L"directory") == 0 ||
			key.CompareNoCase(L"code_directory") == 0 ||
			key.CompareNoCase(L"manual_directory") == 0 ||
			key.CompareNoCase(L"code_db") == 0 ||
			key.CompareNoCase(L"manual_db") == 0)
		{
			return true;
		}
		return HasNumericSuffix(key, L"code_db_") ||
			HasNumericSuffix(key, L"manual_db_");
	}

	bool RemoveStoredClippingDefinitions(
		IniDocument& document,
		CString& errorMessage)
	{
		std::vector<bool> remove(document.lines.size(), false);
		auto sections = GetIniSections(document);
		for(const auto& section : sections)
		{
			if(HasNumericSuffix(section.name, L"code_db_") ||
				HasNumericSuffix(section.name, L"manual_db_"))
			{
				for(size_t line = section.start; line < section.end; line++)
					remove[line] = true;
			}
		}

		const IniSection* clippings = FindSection(sections, L"clippings");
		if(clippings)
		{
			FriendlyArrayBlock codeBlock;
			FriendlyArrayBlock manualBlock;
			if(!FindFriendlyArrayBlock(document, *clippings, L"code_db", codeBlock, errorMessage) ||
				!FindFriendlyArrayBlock(document, *clippings, L"manual_db", manualBlock, errorMessage))
			{
				return false;
			}
			auto markBlock = [&remove](const FriendlyArrayBlock& block)
			{
				if(!block.found)
					return;
				for(size_t line = block.startLine; line <= block.endLine; line++)
					remove[line] = true;
			};
			markBlock(codeBlock);
			markBlock(manualBlock);

			for(size_t line = clippings->start + 1; line < clippings->end; line++)
			{
				if(remove[line])
					continue;
				CString key;
				CString value;
				if(TryParseKeyValue(document.lines[line], key, value) &&
					IsClippingDefinitionKey(key))
				{
					remove[line] = true;
				}
			}
		}

		std::vector<CString> keptLines;
		keptLines.reserve(document.lines.size());
		for(size_t line = 0; line < document.lines.size(); line++)
		{
			if(!remove[line])
				keptLines.push_back(document.lines[line]);
		}
		document.lines = std::move(keptLines);
		return true;
	}

	std::vector<CString> FormatFriendlyArrays(
		const std::vector<ClippingDestinationInfo>& code,
		const std::vector<ClippingDestinationInfo>& manual)
	{
		std::vector<CString> lines;
		auto appendArray = [&lines](LPCWSTR name,
			const std::vector<ClippingDestinationInfo>& destinations)
		{
			CString opening;
			opening.Format(L"%s=[", name);
			lines.push_back(opening);
			for(size_t i = 0; i < destinations.size(); i++)
			{
				CString entry;
				entry.Format(L"\t\"%s\":\"%s\"%s",
					destinations[i].label.GetString(),
					destinations[i].directory.GetString(),
					i + 1 < destinations.size() ? L"," : L"");
				lines.push_back(entry);
			}
			lines.push_back(L"]");
		};
		appendArray(L"code_db", code);
		appendArray(L"manual_db", manual);
		return lines;
	}

	void AppendCanonicalSections(
		IniDocument& document,
		LPCWSTR prefix,
		const std::vector<ClippingDestinationInfo>& destinations)
	{
		for(size_t i = 0; i < destinations.size(); i++)
		{
			// Don't add a separator before the first generated section. Such a
			// separator would survive when the generated sections are hidden in
			// the settings editor, adding one blank line on every round trip.
			if(i > 0 && !document.lines.empty() && !document.lines.back().IsEmpty())
				document.lines.push_back(L"");
			CString section;
			section.Format(L"[%s_%zu]", prefix, i + 1);
			document.lines.push_back(section);
			CString label;
			label.Format(L"label=%s", destinations[i].label.GetString());
			document.lines.push_back(label);
			CString path;
			path.Format(L"path=%s", destinations[i].directory.GetString());
			document.lines.push_back(path);
		}
	}
}

bool ReadIniTextFile(LPCWSTR filePath, CString& text)
{
	text.Empty();
	CAtlFile file;
	HRESULT hr = file.Create(filePath, GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING);
	if(FAILED(hr))
		return false;

	ULONGLONG size64 = 0;
	if(FAILED(file.GetSize(size64)) || size64 > 16 * 1024 * 1024)
		return false;
	DWORD size = static_cast<DWORD>(size64);
	std::vector<BYTE> bytes(size);
	if(size > 0 && FAILED(file.Read(bytes.data(), size)))
		return false;
	if(size == 0)
		return true;

	if(size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
	{
		if((size - 2) % 2 != 0)
			return false;
		int characters = static_cast<int>((size - 2) / 2);
		WCHAR* buffer = text.GetBuffer(characters);
		for(int i = 0; i < characters; i++)
			buffer[i] = static_cast<WCHAR>(bytes[2 + i * 2] | (bytes[3 + i * 2] << 8));
		text.ReleaseBuffer(characters);
		return true;
	}

	int offset = size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF
		? 3
		: 0;
	int inputLength = static_cast<int>(size) - offset;
	UINT codePage = CP_UTF8;
	DWORD flags = MB_ERR_INVALID_CHARS;
	int characters = MultiByteToWideChar(
		codePage, flags, reinterpret_cast<LPCCH>(bytes.data() + offset), inputLength, nullptr, 0);
	if(characters == 0)
	{
		codePage = CP_ACP;
		flags = 0;
		characters = MultiByteToWideChar(
			codePage, flags, reinterpret_cast<LPCCH>(bytes.data() + offset), inputLength, nullptr, 0);
	}
	if(characters <= 0)
		return false;
	WCHAR* buffer = text.GetBuffer(characters);
	MultiByteToWideChar(
		codePage, flags, reinterpret_cast<LPCCH>(bytes.data() + offset), inputLength,
		buffer, characters);
	text.ReleaseBuffer(characters);
	return true;
}

bool ParseCompactClippingDestination(
	CString value,
	ClippingDestinationInfo& destination)
{
	value.Trim();
	if(value.IsEmpty())
		return false;

	CString label;
	CString directory;
	if(value[0] == L'"')
	{
		int closingQuote = value.Find(L'"', 1);
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
			if(value.GetLength() >= 3 &&
				((value[0] >= L'A' && value[0] <= L'Z') ||
					(value[0] >= L'a' && value[0] <= L'z')) &&
				value[1] == L':' && (value[2] == L'\\' || value[2] == L'/'))
			{
				return false;
			}
			separator = value.Find(L':');
		}
		if(separator <= 0)
			return false;
		label = value.Left(separator);
		directory = value.Mid(separator + 1);
	}

	label.Trim();
	directory = StripOptionalQuotes(directory);
	if(label.IsEmpty() || directory.IsEmpty())
		return false;
	destination.label = label;
	destination.directory = directory;
	return true;
}

bool ParseFriendlyClippingArrays(
	const CString& iniText,
	FriendlyClippingArrays& arrays,
	CString& errorMessage)
{
	arrays = {};
	errorMessage.Empty();
	IniDocument document = SplitIniDocument(iniText);
	auto sections = GetIniSections(document);
	const IniSection* clippings = nullptr;
	for(const auto& section : sections)
	{
		if(section.name.CompareNoCase(L"clippings") != 0)
			continue;
		if(clippings)
		{
			errorMessage = L"Textify.ini contains more than one [clippings] section.";
			return false;
		}
		clippings = &section;
	}
	if(!clippings)
		return true;

	FriendlyArrayBlock codeBlock;
	FriendlyArrayBlock manualBlock;
	bool codeValid = FindFriendlyArrayBlock(
		document, *clippings, L"code_db", codeBlock, errorMessage);
	arrays.codePresent = codeBlock.found;
	if(!codeValid)
		return false;
	bool manualValid = FindFriendlyArrayBlock(
		document, *clippings, L"manual_db", manualBlock, errorMessage);
	arrays.manualPresent = manualBlock.found;
	if(!manualValid)
		return false;
	if(codeBlock.found && !ParseFriendlyArrayContents(
		codeBlock.contents, L"code_db", arrays.code, errorMessage))
	{
		return false;
	}
	if(manualBlock.found && !ParseFriendlyArrayContents(
		manualBlock.contents, L"manual_db", arrays.manual, errorMessage))
	{
		return false;
	}
	return true;
}

bool ConvertClippingIniToEditorFormat(
	const CString& storedIniText,
	CString& editorIniText,
	CString& errorMessage)
{
	IniDocument document = SplitIniDocument(storedIniText);
	std::vector<ClippingDestinationInfo> code;
	std::vector<ClippingDestinationInfo> manual;
	if(!BuildCatalogForEditor(document, code, manual, errorMessage) ||
		!RemoveStoredClippingDefinitions(document, errorMessage))
	{
		return false;
	}

	auto sections = GetIniSections(document);
	const IniSection* clippings = FindSection(sections, L"clippings");
	std::vector<CString> friendlyLines = FormatFriendlyArrays(code, manual);
	if(clippings)
	{
		document.lines.insert(
			document.lines.begin() + clippings->start + 1,
			friendlyLines.begin(),
			friendlyLines.end());
	}
	else
	{
		if(!document.lines.empty() && !document.lines.back().IsEmpty())
			document.lines.push_back(L"");
		document.lines.push_back(L"[clippings]");
		document.lines.insert(document.lines.end(), friendlyLines.begin(), friendlyLines.end());
	}
	document.finalEol = true;
	editorIniText = JoinIniDocument(document);
	return true;
}

bool ConvertClippingIniToStorageFormat(
	const CString& editorIniText,
	CString& storedIniText,
	CString& errorMessage)
{
	FriendlyClippingArrays arrays;
	if(!ParseFriendlyClippingArrays(editorIniText, arrays, errorMessage))
		return false;
	if(!arrays.codePresent || !arrays.manualPresent)
	{
		errorMessage = L"The [clippings] section must contain both code_db=[...] and manual_db=[...].";
		return false;
	}

	IniDocument document = SplitIniDocument(editorIniText);
	if(!RemoveStoredClippingDefinitions(document, errorMessage))
		return false;
	AppendCanonicalSections(document, L"code_db", arrays.code);
	AppendCanonicalSections(document, L"manual_db", arrays.manual);
	document.finalEol = true;
	storedIniText = JoinIniDocument(document);
	return true;
}
