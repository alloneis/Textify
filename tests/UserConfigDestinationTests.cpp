#include "..\stdafx.h"
#include "..\UserConfig.h"

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

	class ScopedIniFixture
	{
	public:
		ScopedIniFixture()
		{
			CPath iniFilePath = UserConfig::GetIniFilePath();
			m_path = iniFilePath.m_strPath.GetString();
			m_hadOriginal = std::filesystem::exists(m_path);
			if(m_hadOriginal)
				m_originalBytes = ReadBytes(m_path);
		}

		~ScopedIniFixture()
		{
			try
			{
				if(m_hadOriginal)
					WriteBytes(m_originalBytes);
				else
					std::filesystem::remove(m_path);
				FlushProfileCache();
			}
			catch(...)
			{
				// A test cleanup failure must not mask the original assertion.
			}
		}

		void Write(const std::string& contents)
		{
			WriteBytes(contents);
			FlushProfileCache();
		}

		const std::filesystem::path& Path() const
		{
			return m_path;
		}

	private:
		static std::string ReadBytes(const std::filesystem::path& path)
		{
			std::ifstream stream(path, std::ios::binary);
			if(!stream)
				throw std::runtime_error("could not back up the test-output Textify.ini");
			return std::string(
				std::istreambuf_iterator<char>(stream),
				std::istreambuf_iterator<char>());
		}

		void WriteBytes(const std::string& contents) const
		{
			std::ofstream stream(m_path, std::ios::binary | std::ios::trunc);
			if(!stream)
				throw std::runtime_error("could not write the isolated Textify.ini fixture");
			stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
			if(!stream)
				throw std::runtime_error("could not finish writing the isolated Textify.ini fixture");
		}

		void FlushProfileCache() const
		{
			::WritePrivateProfileStringW(nullptr, nullptr, nullptr, m_path.c_str());
		}

		std::filesystem::path m_path;
		bool m_hadOriginal = false;
		std::string m_originalBytes;
	};

	void ExpectDestination(
		const std::vector<ClippingDestinationInfo>& destinations,
		size_t index,
		LPCWSTR expectedLabel,
		LPCWSTR expectedDirectory,
		const char* context)
	{
		if(index >= destinations.size())
			throw std::runtime_error(std::string(context) + ": destination is missing");
		if(destinations[index].label.Compare(expectedLabel) != 0)
			throw std::runtime_error(std::string(context) + ": label differs");
		if(destinations[index].directory.Compare(expectedDirectory) != 0)
			throw std::runtime_error(std::string(context) + ": directory differs");
	}

	void TestNumberedSectionForm(ScopedIniFixture& fixture)
	{
		fixture.Write(
			"[code_db_1]\r\n"
			"label=Python\r\n"
			"path=C:\\vault\\python\r\n"
			"[code_db_2]\r\n"
			"label=C++\r\n"
			"path=D:\\vault\\cpp\r\n"
			"[manual_db_1]\r\n"
			"label=Notes\r\n"
			"path=E:\\vault\\notes\r\n");

		UserConfig config;
		Expect(config.m_codeClippingDestinations.size() == 2,
			"numbered code sections were not all loaded");
		ExpectDestination(config.m_codeClippingDestinations, 0,
			L"Python", L"C:\\vault\\python", "numbered code section 1");
		ExpectDestination(config.m_codeClippingDestinations, 1,
			L"C++", L"D:\\vault\\cpp", "numbered code section 2");
		Expect(config.m_manualClippingDestinations.size() == 1,
			"numbered manual section was not loaded");
		ExpectDestination(config.m_manualClippingDestinations, 0,
			L"Notes", L"E:\\vault\\notes", "numbered manual section 1");
	}

	void TestFlatCompactForms(ScopedIniFixture& fixture)
	{
		fixture.Write(
			"[clippings]\r\n"
			"code_db_1=\"Python\":C:\\db\\python\r\n"
			"code_db_2=C++|D:\\db\\cpp\r\n"
			"manual_db_1=Notes:E:\\db\\notes\r\n");

		UserConfig config;
		Expect(config.m_codeClippingDestinations.size() == 2,
			"flat compact code entries were not all loaded");
		ExpectDestination(config.m_codeClippingDestinations, 0,
			L"Python", L"C:\\db\\python", "quoted compact code entry");
		ExpectDestination(config.m_codeClippingDestinations, 1,
			L"C++", L"D:\\db\\cpp", "pipe compact code entry");
		Expect(config.m_manualClippingDestinations.size() == 1,
			"flat compact manual entry was not loaded");
		ExpectDestination(config.m_manualClippingDestinations, 0,
			L"Notes", L"E:\\db\\notes", "colon compact manual entry");
	}

	void TestGapsAndNumericOrdering(ScopedIniFixture& fixture)
	{
		fixture.Write(
			"[clippings]\r\n"
			"code_db_10=Ten|C:\\db\\ten\r\n"
			"code_db_2=Two|C:\\db\\two\r\n"
			"[code_db_7]\r\n"
			"label=Seven\r\n"
			"path=C:\\db\\seven\r\n");

		UserConfig config;
		Expect(config.m_codeClippingDestinations.size() == 3,
			"gapped destination indexes hid an entry");
		ExpectDestination(config.m_codeClippingDestinations, 0,
			L"Two", L"C:\\db\\two", "numeric order index 2");
		ExpectDestination(config.m_codeClippingDestinations, 1,
			L"Seven", L"C:\\db\\seven", "numeric order index 7");
		ExpectDestination(config.m_codeClippingDestinations, 2,
			L"Ten", L"C:\\db\\ten", "numeric order index 10");
	}

	void TestLabelsAreRequired(ScopedIniFixture& fixture)
	{
		fixture.Write(
			"[clippings]\r\n"
			"directory=C:\\legacy-must-stay-disabled\r\n"
			"code_db_1=C:\\unlabelled-path\r\n"
			"code_db_2=|C:\\empty-label\r\n"
			"code_db_3=OnlyLabel|\r\n"
			"code_db_5=Valid|C:\\valid\r\n"
			"[code_db_4]\r\n"
			"path=C:\\section-without-label\r\n"
			"[code_db_6]\r\n"
			"label=SectionWithoutPath\r\n");

		UserConfig config;
		Expect(config.m_codeClippingDestinations.size() == 1,
			"an unlabeled or incomplete destination was accepted");
		ExpectDestination(config.m_codeClippingDestinations, 0,
			L"Valid", L"C:\\valid", "only labeled destination");
	}

	void TestDuplicateLabelsAreCaseInsensitive(ScopedIniFixture& fixture)
	{
		fixture.Write(
			"[clippings]\r\n"
			"code_db_1=Python|C:\\first\r\n"
			"code_db_2=python|C:\\second\r\n"
			"code_db_3=PYTHON|C:\\third\r\n"
			"code_db_4=C++|C:\\cpp\r\n");

		UserConfig config;
		Expect(config.m_codeClippingDestinations.size() == 2,
			"case-insensitive duplicate labels were not collapsed");
		ExpectDestination(config.m_codeClippingDestinations, 0,
			L"Python", L"C:\\first", "first duplicate label wins");
		ExpectDestination(config.m_codeClippingDestinations, 1,
			L"C++", L"C:\\cpp", "nonduplicate label remains");
	}

	void TestCanonicalMalformedOrEmptySuppressesLegacy(ScopedIniFixture& fixture)
	{
		fixture.Write(
			"[clippings]\r\n"
			"directory=C:\\legacy\r\n"
			"code_db_1=malformed\r\n");
		UserConfig malformedConfig;
		Expect(malformedConfig.m_codeClippingDestinations.empty(),
			"malformed canonical list unexpectedly activated the legacy path");

		fixture.Write(
			"[clippings]\r\n"
			"directory=C:\\legacy\r\n"
			"code_db_1=\r\n");
		UserConfig emptyFlatConfig;
		Expect(emptyFlatConfig.m_codeClippingDestinations.empty(),
			"empty canonical key unexpectedly activated the legacy path");

		fixture.Write(
			"[clippings]\r\n"
			"directory=C:\\legacy\r\n"
			"[code_db_1]\r\n");
		UserConfig emptySectionConfig;
		Expect(emptySectionConfig.m_codeClippingDestinations.empty(),
			"empty canonical section unexpectedly activated the legacy path");
	}

	void TestLegacyFallback(ScopedIniFixture& fixture)
	{
		fixture.Write(
			"[clippings]\r\n"
			"directory=C:\\old-code\r\n"
			"manual_db=D:\\old-manual\r\n");

		UserConfig config;
		Expect(config.m_codeClippingDestinations.size() == 1,
			"old code directory did not fall back to one destination");
		ExpectDestination(config.m_codeClippingDestinations, 0,
			L"Default", L"C:\\old-code", "old code fallback");
		Expect(config.m_manualClippingDestinations.size() == 1,
			"old manual_db did not fall back to one destination");
		ExpectDestination(config.m_manualClippingDestinations, 0,
			L"Default", L"D:\\old-manual", "old manual fallback");

		fixture.Write(
			"[clippings]\r\n"
			"code_directory=C:\\preferred-code\r\n"
			"code_db=C:\\middle-code\r\n"
			"directory=C:\\old-code\r\n"
			"manual_directory=D:\\preferred-manual\r\n"
			"manual_db=D:\\old-manual\r\n");

		UserConfig preferredConfig;
		ExpectDestination(preferredConfig.m_codeClippingDestinations, 0,
			L"Default", L"C:\\preferred-code", "preferred code legacy key");
		ExpectDestination(preferredConfig.m_manualClippingDestinations, 0,
			L"Default", L"D:\\preferred-manual", "preferred manual legacy key");
	}

	void TestCodeAndManualListsAreIndependent(ScopedIniFixture& fixture)
	{
		fixture.Write(
			"[clippings]\r\n"
			"directory=C:\\code-legacy-must-stay-disabled\r\n"
			"manual_directory=D:\\manual-legacy\r\n"
			"code_db_1=malformed\r\n");

		UserConfig codeCanonicalConfig;
		Expect(codeCanonicalConfig.m_codeClippingDestinations.empty(),
			"code canonical state did not independently suppress code fallback");
		Expect(codeCanonicalConfig.m_manualClippingDestinations.size() == 1,
			"code canonical state incorrectly suppressed manual fallback");
		ExpectDestination(codeCanonicalConfig.m_manualClippingDestinations, 0,
			L"Default", L"D:\\manual-legacy", "independent manual fallback");

		fixture.Write(
			"[clippings]\r\n"
			"directory=C:\\code-legacy\r\n"
			"manual_directory=D:\\manual-legacy-must-stay-disabled\r\n"
			"manual_db_1=malformed\r\n");

		UserConfig manualCanonicalConfig;
		Expect(manualCanonicalConfig.m_manualClippingDestinations.empty(),
			"manual canonical state did not independently suppress manual fallback");
		Expect(manualCanonicalConfig.m_codeClippingDestinations.size() == 1,
			"manual canonical state incorrectly suppressed code fallback");
		ExpectDestination(manualCanonicalConfig.m_codeClippingDestinations, 0,
			L"Default", L"C:\\code-legacy", "independent code fallback");
	}

	void TestFriendlyArrayFormatAndRoundTrip(ScopedIniFixture& fixture)
	{
		fixture.Write(
			"; keep this comment\r\n"
			"[mouse]\r\n"
			"key=2\r\n"
			"[clippings]\r\n"
			"code_db=[\r\n"
			"\t\"..\\database\\textify\":\"D:\\Users\\Administrator\\Desktop\\database\\database\\textify\",\r\n"
			" \t\"python\":\"D:\\\"\r\n"
			"]\r\n"
			"manual_db=[\r\n"
			"\t\r\n"
			"]\r\n");

		UserConfig config;
		Expect(config.m_codeClippingDestinations.size() == 2,
			"friendly code_db array did not load");
		ExpectDestination(config.m_codeClippingDestinations, 0,
			L"..\\database\\textify",
			L"D:\\Users\\Administrator\\Desktop\\database\\database\\textify",
			"friendly first code destination");
		ExpectDestination(config.m_codeClippingDestinations, 1,
			L"python", L"D:\\", "friendly drive-root destination");
		Expect(config.m_manualClippingDestinations.empty(),
			"empty friendly manual_db array was not empty");

		CString friendlyText;
		Expect(ReadIniTextFile(fixture.Path().c_str(), friendlyText),
			"could not read friendly fixture");
		CString storedText;
		CString error;
		Expect(ConvertClippingIniToStorageFormat(friendlyText, storedText, error),
			"friendly-to-storage conversion failed");
		Expect(storedText.Find(L"[code_db_1]") >= 0 &&
			storedText.Find(L"label=..\\database\\textify") >= 0 &&
			storedText.Find(L"path=D:\\") >= 0,
			"storage conversion did not create numbered sections");
		Expect(storedText.Find(L"code_db=[") < 0 && storedText.Find(L"manual_db=[") < 0,
			"storage conversion left friendly arrays on disk");
		Expect(storedText.Find(L"; keep this comment") >= 0 &&
			storedText.Find(L"[mouse]") >= 0 && storedText.Find(L"key=2") >= 0,
			"storage conversion lost unrelated INI content");

		CString editorText;
		Expect(ConvertClippingIniToEditorFormat(storedText, editorText, error),
			"storage-to-editor conversion failed");
		Expect(editorText.Find(L"code_db=[") >= 0 &&
			editorText.Find(L"\"python\":\"D:\\\"") >= 0 &&
			editorText.Find(L"manual_db=[") >= 0,
			"editor conversion did not recreate friendly arrays");
		Expect(editorText.Find(L"[code_db_1]") < 0,
			"editor conversion exposed internal numbered sections");
	}

	void TestFriendlyArrayValidation()
	{
		CString stored;
		CString error;
		CString duplicate =
			L"[clippings]\r\n"
			L"code_db=[\r\n"
			L"\t\"Python\":\"C:\\one\",\r\n"
			L"\t\"python\":\"C:\\two\"\r\n"
			L"]\r\n"
			L"manual_db=[]\r\n";
		Expect(!ConvertClippingIniToStorageFormat(duplicate, stored, error) &&
			error.Find(L"duplicate") >= 0,
			"duplicate friendly labels were not rejected");

		CString missingManual =
			L"[clippings]\r\n"
			L"code_db=[]\r\n";
		Expect(!ConvertClippingIniToStorageFormat(missingManual, stored, error),
			"missing manual_db array was accepted");
	}
}

int main()
{
	try
	{
		ScopedIniFixture fixture;
		Expect(fixture.Path().filename() == L"Textify.ini" &&
			fixture.Path().parent_path().filename() == L"bin-user-config",
			"refusing to run outside the isolated test-output directory");

		TestNumberedSectionForm(fixture);
		TestFlatCompactForms(fixture);
		TestGapsAndNumericOrdering(fixture);
		TestDuplicateLabelsAreCaseInsensitive(fixture);
		TestCanonicalMalformedOrEmptySuppressesLegacy(fixture);
		TestLegacyFallback(fixture);
		TestCodeAndManualListsAreIndependent(fixture);
		TestLabelsAreRequired(fixture);
		TestFriendlyArrayFormatAndRoundTrip(fixture);
		TestFriendlyArrayValidation();

		std::cout << "UserConfig clipping destination tests passed.\n";
		return 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << error.what() << "\n";
		return 1;
	}
}
