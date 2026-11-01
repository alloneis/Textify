#include "..\stdafx.h"
#include "..\ClippingIniFormat.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

CAppModule _Module;

namespace
{
	void Expect(bool condition, const char* message)
	{
		if(!condition)
			throw std::runtime_error(message);
	}

	void ExpectContains(const CString& text, LPCWSTR expected, const char* context)
	{
		if(text.Find(expected) < 0)
			throw std::runtime_error(std::string(context) + ": expected text is missing");
	}

	void ExpectNotContains(const CString& text, LPCWSTR unexpected, const char* context)
	{
		if(text.Find(unexpected) >= 0)
			throw std::runtime_error(std::string(context) + ": obsolete text remains");
	}

	void ExpectDestination(
		const std::vector<ClippingDestinationInfo>& destinations,
		size_t index,
		LPCWSTR label,
		LPCWSTR path,
		const char* context)
	{
		if(index >= destinations.size())
			throw std::runtime_error(std::string(context) + ": destination is missing");
		if(destinations[index].label.Compare(label) != 0)
			throw std::runtime_error(std::string(context) + ": label differs");
		if(destinations[index].directory.Compare(path) != 0)
			throw std::runtime_error(std::string(context) + ": path differs");
	}

	void ExpectCrLfOnly(const CString& text, const char* context)
	{
		for(int i = 0; i < text.GetLength(); i++)
		{
			if(text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r'))
				throw std::runtime_error(std::string(context) + ": introduced a bare LF");
		}
	}

	class ScopedUtf16Fixture
	{
	public:
		ScopedUtf16Fixture()
		{
			WCHAR modulePath[MAX_PATH];
			DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
			Expect(length > 0 && length < ARRAYSIZE(modulePath),
				"could not locate the converter test executable");
			m_path = std::filesystem::path(modulePath).parent_path() /
				L"ClippingIniFormatUtf16Fixture.ini";
			Expect(m_path.parent_path().filename() == L"bin-clipping-ini",
				"refusing to place the UTF-16LE fixture outside test output");
			Expect(!std::filesystem::exists(m_path),
				"refusing to overwrite an existing UTF-16LE fixture");
		}

		~ScopedUtf16Fixture()
		{
			std::error_code ignored;
			std::filesystem::remove(m_path, ignored);
		}

		void Write(const std::wstring& text) const
		{
			static_assert(sizeof(wchar_t) == 2);
			std::ofstream stream(m_path, std::ios::binary | std::ios::trunc);
			Expect(static_cast<bool>(stream), "could not create the UTF-16LE fixture");
			const unsigned char bom[] = { 0xFF, 0xFE };
			stream.write(reinterpret_cast<const char*>(bom), sizeof(bom));
			stream.write(
				reinterpret_cast<const char*>(text.data()),
				static_cast<std::streamsize>(text.size() * sizeof(wchar_t)));
			Expect(static_cast<bool>(stream), "could not finish the UTF-16LE fixture");
		}

		const std::filesystem::path& Path() const
		{
			return m_path;
		}

	private:
		std::filesystem::path m_path;
	};

	void TestCanonicalFriendlyRoundTrip()
	{
		CString originalStorage =
			L"; preserved leading comment\r\n"
			L"[config]\r\n"
			L"font_size=25\r\n"
			L"\r\n"
			L"[clippings]\r\n"
			L"; preserved clippings comment\r\n"
			L"keep=yes\r\n"
			L"directory=C:\\obsolete-legacy\r\n"
			L"\r\n"
			L"[code_db_4]\r\n"
			L"label=Python\r\n"
			L"path=C:\\vault\\python\r\n"
			L"\r\n"
			L"[code_db_9]\r\n"
			L"label=C++\r\n"
			L"path=D:\\vault\\cpp\r\n"
			L"\r\n"
			L"[manual_db_2]\r\n"
			L"label=Notes\r\n"
			L"path=E:\\vault\\notes\r\n"
			L"\r\n"
			L"[other]\r\n"
			L"value=untouched\r\n"
			L"; preserved trailing comment\r\n";

		CString editorFirst;
		CString normalizedStorage;
		CString editorSecond;
		CString error;
		Expect(ConvertClippingIniToEditorFormat(originalStorage, editorFirst, error),
			"canonical-to-editor conversion failed");
		Expect(error.IsEmpty(), "canonical-to-editor conversion returned an error");
		ExpectContains(editorFirst, L"code_db=[\r\n", "friendly code array");
		ExpectContains(editorFirst,
			L"\t\"Python\":\"C:\\vault\\python\",\r\n",
			"friendly Python entry");
		ExpectContains(editorFirst,
			L"\t\"C++\":\"D:\\vault\\cpp\"\r\n",
			"friendly C++ entry");
		ExpectContains(editorFirst,
			L"manual_db=[\r\n\t\"Notes\":\"E:\\vault\\notes\"\r\n]",
			"friendly manual array");
		ExpectContains(editorFirst, L"; preserved leading comment", "leading comment preservation");
		ExpectContains(editorFirst, L"; preserved clippings comment", "clippings comment preservation");
		ExpectContains(editorFirst, L"keep=yes", "unrelated clippings key preservation");
		ExpectContains(editorFirst,
			L"[other]\r\nvalue=untouched\r\n; preserved trailing comment",
			"unrelated section preservation");
		ExpectNotContains(editorFirst, L"directory=C:\\obsolete-legacy", "legacy removal");
		ExpectNotContains(editorFirst, L"[code_db_4]", "canonical code removal");
		ExpectNotContains(editorFirst, L"[manual_db_2]", "canonical manual removal");
		ExpectCrLfOnly(editorFirst, "editor CRLF preservation");

		Expect(ConvertClippingIniToStorageFormat(editorFirst, normalizedStorage, error),
			"editor-to-canonical conversion failed");
		ExpectContains(normalizedStorage,
			L"[code_db_1]\r\nlabel=Python\r\npath=C:\\vault\\python",
			"normalized first code section");
		ExpectContains(normalizedStorage,
			L"[code_db_2]\r\nlabel=C++\r\npath=D:\\vault\\cpp",
			"normalized second code section");
		ExpectContains(normalizedStorage,
			L"[manual_db_1]\r\nlabel=Notes\r\npath=E:\\vault\\notes",
			"normalized manual section");
		ExpectContains(normalizedStorage, L"keep=yes", "storage unrelated key preservation");
		ExpectContains(normalizedStorage, L"[other]\r\nvalue=untouched",
			"storage unrelated section preservation");
		ExpectNotContains(normalizedStorage, L"code_db=[", "friendly code removal on save");
		ExpectNotContains(normalizedStorage, L"manual_db=[", "friendly manual removal on save");
		ExpectCrLfOnly(normalizedStorage, "storage CRLF preservation");

		Expect(ConvertClippingIniToEditorFormat(normalizedStorage, editorSecond, error),
			"normalized storage-to-editor conversion failed");
		if(editorFirst.Compare(editorSecond) != 0)
		{
			std::wcerr << L"--- first editor text ---\n"
				<< editorFirst.GetString()
				<< L"\n--- second editor text ---\n"
				<< editorSecond.GetString()
				<< L"\n--- end editor diff ---\n";
		}
		Expect(editorFirst.Compare(editorSecond) == 0,
			"canonical/friendly roundtrip is not editor-format stable");
	}

	void TestExactCurrentFriendlySample()
	{
		CString editor =
			L"[clippings]\r\n"
			L"code_db=[\r\n"
			L"\t\"../textify\":\"D:\\Users\\Administrator\\Desktop\\database\\database\\textify\",\r\n"
			L" \t\"python\":\"D:\\\"\r\n"
			L"]\r\n"
			L"manual_db=[\r\n"
			L"\t\r\n"
			L"]\r\n";

		FriendlyClippingArrays arrays;
		CString error;
		Expect(ParseFriendlyClippingArrays(editor, arrays, error),
			"exact current friendly sample did not parse");
		Expect(arrays.codePresent && arrays.manualPresent,
			"exact current sample did not detect both arrays");
		Expect(arrays.code.size() == 2 && arrays.manual.empty(),
			"exact current sample returned the wrong catalog sizes");
		ExpectDestination(arrays.code, 0,
			L"../textify",
			L"D:\\Users\\Administrator\\Desktop\\database\\database\\textify",
			"current sample first code destination");
		ExpectDestination(arrays.code, 1, L"python", L"D:\\",
			"current sample second code destination");

		CString stored;
		Expect(ConvertClippingIniToStorageFormat(editor, stored, error),
			"exact current friendly sample did not convert to storage");
		ExpectContains(stored,
			L"[code_db_1]\r\nlabel=../textify\r\npath=D:\\Users\\Administrator\\Desktop\\database\\database\\textify",
			"current sample first canonical destination");
		ExpectContains(stored,
			L"[code_db_2]\r\nlabel=python\r\npath=D:\\",
			"current sample second canonical destination");
		ExpectNotContains(stored, L"[manual_db_1]", "empty current manual catalog");
	}

	void TestValidationRejectsBadFriendlyArrays()
	{
		const std::vector<std::pair<CString, const char*>> invalid = {
			{ L"[clippings]\r\ncode_db=[]\r\n", "missing manual array" },
			{ L"[clippings]\r\ncode_db=[\r\n\"A\":\"C:\\a\"\r\nmanual_db=[]\r\n",
				"missing closing bracket" },
			{ L"[clippings]\r\ncode_db=[\r\nA:\"C:\\a\"\r\n]\r\nmanual_db=[]\r\n",
				"unquoted label" },
			{ L"[clippings]\r\ncode_db=[\r\n\"A\" \"C:\\a\"\r\n]\r\nmanual_db=[]\r\n",
				"missing colon" },
			{ L"[clippings]\r\ncode_db=[\r\n\"A\":\"C:\\one\",\r\n\"a\":\"C:\\two\"\r\n]\r\nmanual_db=[]\r\n",
				"case-insensitive duplicate" },
			{ L"[clippings]\r\ncode_db=[\r\n\"\":\"C:\\a\"\r\n]\r\nmanual_db=[]\r\n",
				"empty label" },
			{ L"[clippings]\r\ncode_db=[\r\n\"A\":\"\"\r\n]\r\nmanual_db=[]\r\n",
				"empty path" },
			{ L"[clippings]\r\ncode_db=[\r\n\"A\":\"C:\\a\" trailing\r\n]\r\nmanual_db=[]\r\n",
				"unexpected trailing text" }
		};

		for(const auto& item : invalid)
		{
			CString stored;
			CString error;
			if(ConvertClippingIniToStorageFormat(item.first, stored, error))
				throw std::runtime_error(std::string("validation accepted ") + item.second);
			if(error.IsEmpty())
				throw std::runtime_error(std::string("validation returned no message for ") + item.second);
		}
	}

	void TestFriendlyArraysOverrideOldDefinitions()
	{
		CString editor =
			L"[clippings]\r\n"
			L"directory=C:\\legacy-code\r\n"
			L"manual_directory=D:\\legacy-manual\r\n"
			L"code_db_1=CompactCode|C:\\compact-code\r\n"
			L"manual_db_1=CompactManual|D:\\compact-manual\r\n"
			L"code_db=[\r\n"
			L"\t\"FriendlyCode\":\"C:\\friendly-code\"\r\n"
			L"]\r\n"
			L"manual_db=[\r\n"
			L"\t\"FriendlyManual\":\"D:\\friendly-manual\"\r\n"
			L"]\r\n"
			L"\r\n"
			L"[code_db_2]\r\n"
			L"label=SectionCode\r\n"
			L"path=C:\\section-code\r\n"
			L"\r\n"
			L"[manual_db_2]\r\n"
			L"label=SectionManual\r\n"
			L"path=D:\\section-manual\r\n";

		CString stored;
		CString error;
		Expect(ConvertClippingIniToStorageFormat(editor, stored, error),
			"friendly precedence input could not be saved");
		ExpectContains(stored,
			L"[code_db_1]\r\nlabel=FriendlyCode\r\npath=C:\\friendly-code",
			"friendly code precedence");
		ExpectContains(stored,
			L"[manual_db_1]\r\nlabel=FriendlyManual\r\npath=D:\\friendly-manual",
			"friendly manual precedence");
		ExpectNotContains(stored, L"legacy-code", "legacy code precedence");
		ExpectNotContains(stored, L"compact-code", "compact code precedence");
		ExpectNotContains(stored, L"SectionCode", "section code precedence");
		ExpectNotContains(stored, L"legacy-manual", "legacy manual precedence");
		ExpectNotContains(stored, L"compact-manual", "compact manual precedence");
		ExpectNotContains(stored, L"SectionManual", "section manual precedence");
	}

	void TestEmptyFriendlyArraysEraseOldDefinitions()
	{
		CString editor =
			L"[clippings]\r\n"
			L"directory=C:\\legacy-code\r\n"
			L"manual_directory=D:\\legacy-manual\r\n"
			L"code_db_1=CompactCode|C:\\compact-code\r\n"
			L"manual_db_1=CompactManual|D:\\compact-manual\r\n"
			L"code_db=[\r\n"
			L"]\r\n"
			L"manual_db=[\r\n"
			L"]\r\n"
			L"\r\n"
			L"[code_db_2]\r\n"
			L"label=SectionCode\r\n"
			L"path=C:\\section-code\r\n"
			L"\r\n"
			L"[manual_db_2]\r\n"
			L"label=SectionManual\r\n"
			L"path=D:\\section-manual\r\n"
			L"\r\n"
			L"[other]\r\n"
			L"keep=yes\r\n";

		CString stored;
		CString error;
		Expect(ConvertClippingIniToStorageFormat(editor, stored, error),
			"empty friendly arrays could not be saved");
		ExpectNotContains(stored, L"directory=", "empty array legacy removal");
		ExpectNotContains(stored, L"code_db_", "empty array code definition removal");
		ExpectNotContains(stored, L"manual_db_", "empty array manual definition removal");
		ExpectContains(stored, L"[other]\r\nkeep=yes", "empty array unrelated preservation");
	}

	void TestUtf16LeFriendlyInput()
	{
		const std::wstring expected =
			L"; UTF-16LE fixture\r\n"
			L"[clippings]\r\n"
			L"code_db=[\r\n"
			L"\t\"中文代码\":\"D:\\资料\\代码\"\r\n"
			L"]\r\n"
			L"manual_db=[\r\n"
			L"\t\"手册\":\"E:\\资料\\手册\"\r\n"
			L"]\r\n";
		ScopedUtf16Fixture fixture;
		fixture.Write(expected);

		CString readText;
		Expect(ReadIniTextFile(fixture.Path().c_str(), readText),
			"could not read the UTF-16LE friendly fixture");
		Expect(readText.Compare(expected.c_str()) == 0,
			"UTF-16LE friendly text changed while reading");
		CString stored;
		CString error;
		Expect(ConvertClippingIniToStorageFormat(readText, stored, error),
			"UTF-16LE friendly text could not be converted");
		ExpectContains(stored, L"label=中文代码\r\npath=D:\\资料\\代码",
			"UTF-16LE code destination");
		ExpectContains(stored, L"label=手册\r\npath=E:\\资料\\手册",
			"UTF-16LE manual destination");
		ExpectCrLfOnly(stored, "UTF-16LE CRLF preservation");
	}

	void TestCommentsAndCanonicalAmbiguity()
	{
		CString commented =
			L"[clippings]\r\n"
			L"code_db=[\r\n"
			L"\t# choose ] only after this comment\r\n"
			L"\t\"Python\":\"D:\\python\" ; a quote \" and ] in a comment\r\n"
			L"] # closing comment\r\n"
			L"manual_db=[]\r\n";
		CString stored;
		CString error;
		Expect(ConvertClippingIniToStorageFormat(commented, stored, error),
			"comment text was treated as friendly-array syntax");
		ExpectContains(stored,
			L"[code_db_1]\r\nlabel=Python\r\npath=D:\\python",
			"comment-safe friendly entry");

		const std::vector<CString> ambiguous = {
			L"[code_db_1]\r\nlabel=A\r\npath=C:\\a\r\n"
			L"[code_db_1]\r\nlabel=B\r\npath=C:\\b\r\n",
			L"[code_db_1]\r\nlabel=A\r\nlabel=B\r\npath=C:\\a\r\n",
			L"[code_db_01]\r\nlabel=A\r\npath=C:\\a\r\n"
		};
		for(const auto& input : ambiguous)
		{
			CString editor;
			error.Empty();
			Expect(!ConvertClippingIniToEditorFormat(input, editor, error),
				"ambiguous canonical configuration was accepted");
			Expect(!error.IsEmpty(),
				"ambiguous canonical configuration returned no error");
		}

		CString trailingJunk =
			L"[clippings]\r\ncode_db=[] junk\r\nmanual_db=[]\r\n";
		Expect(!ConvertClippingIniToStorageFormat(trailingJunk, stored, error),
			"text after a closing array bracket was accepted");

		CString duplicateClippings =
			L"[clippings]\r\ncode_db=[]\r\nmanual_db=[]\r\n"
			L"[clippings]\r\nkeep=yes\r\n";
		Expect(!ConvertClippingIniToStorageFormat(duplicateClippings, stored, error),
			"duplicate clippings sections were accepted");

		CString unrelatedPrefixedNames =
			L"[clippings]\r\ncode_db=[]\r\nmanual_db=[]\r\n"
			L"code_db_notes=keep-this-key\r\n"
			L"[code_db_backup]\r\nvalue=keep-this-section\r\n";
		Expect(ConvertClippingIniToStorageFormat(
			unrelatedPrefixedNames, stored, error),
			"unrelated prefixed names prevented saving");
		ExpectContains(stored, L"code_db_notes=keep-this-key",
			"unrelated prefixed key preservation");
		ExpectContains(stored, L"[code_db_backup]\r\nvalue=keep-this-section",
			"unrelated prefixed section preservation");
	}

	void TestRuntimeCatalogLimits()
	{
		auto expectRejected = [](const CString& editor, const char* context)
		{
			CString stored;
			CString error;
			Expect(!ConvertClippingIniToStorageFormat(editor, stored, error), context);
			Expect(!error.IsEmpty(), "catalog limit failure returned no message");
		};

		CString longLabel(L'A', 1025);
		CString editor;
		editor.Format(
			L"[clippings]\r\ncode_db=[\r\n\"%s\":\"C:\\db\"\r\n]\r\nmanual_db=[]\r\n",
			longLabel.GetString());
		expectRejected(editor, "a label that runtime would truncate was accepted");

		CString longPath(L'P', 2049);
		editor.Format(
			L"[clippings]\r\ncode_db=[\r\n\"A\":\"%s\"\r\n]\r\nmanual_db=[]\r\n",
			longPath.GetString());
		expectRejected(editor, "a path that runtime would truncate was accepted");

		editor = L"[clippings]\r\ncode_db=[\r\n";
		for(int i = 0; i < 1001; i++)
		{
			CString entry;
			entry.Format(L"\"item%d\":\"C:\\db%d\"%s\r\n",
				i, i, i < 1000 ? L"," : L"");
			editor += entry;
		}
		editor += L"]\r\nmanual_db=[]\r\n";
		expectRejected(editor, "more than 1000 destinations were accepted");
	}
}

int main()
{
	try
	{
		TestExactCurrentFriendlySample();
		TestValidationRejectsBadFriendlyArrays();
		TestFriendlyArraysOverrideOldDefinitions();
		TestEmptyFriendlyArraysEraseOldDefinitions();
		TestUtf16LeFriendlyInput();
		TestCommentsAndCanonicalAmbiguity();
		TestRuntimeCatalogLimits();
		TestCanonicalFriendlyRoundTrip();
		std::cout << "Clipping INI conversion tests passed.\n";
		return 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << error.what() << "\n";
		return 1;
	}
}
