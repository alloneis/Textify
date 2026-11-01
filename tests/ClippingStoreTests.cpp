#include "..\stdafx.h"
#include "..\ClippingStore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

CAppModule _Module;

namespace
{
	void Expect(bool condition, const char* message)
	{
		if(!condition) throw std::runtime_error(message);
	}

	std::string ReadBytes(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		return std::string(
			std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>());
	}
}

int main()
{
	std::filesystem::path directory = std::filesystem::temp_directory_path() /
		(L"textify-clipping-test-" + std::to_wstring(::GetCurrentProcessId()));
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory);

	try
	{
		{
			std::ofstream existing(directory / L"clips1.md", std::ios::binary);
			existing << "sentinel-one";
		}

		CString savedPath;
		CString error;
		Expect(SaveNextClipping(directory.c_str(), L"second file", savedPath, error),
			"save after clips1 conflict failed");
		Expect(std::filesystem::path(savedPath.GetString()).filename() == L"clips2.md",
			"clips1 conflict did not advance to clips2");
		Expect(ReadBytes(directory / L"clips1.md") == "sentinel-one",
			"existing clips1 content was changed");
		Expect(ReadBytes(directory / L"clips2.md") == "second file",
			"clips2 content is incorrect");

		std::filesystem::create_directory(directory / L"clips3.md");
		savedPath.Empty();
		error.Empty();
		Expect(SaveNextClipping(directory.c_str(), L"fourth file", savedPath, error),
			"save after directory conflict failed");
		Expect(std::filesystem::path(savedPath.GetString()).filename() == L"clips4.md",
			"directory conflict did not advance to clips4");
		Expect(ReadBytes(directory / L"clips4.md") == "fourth file",
			"clips4 content is incorrect");

		savedPath.Empty();
		error.Empty();
		CString namedText = L"//? reset a thread\r\n# /function reset(tid) {\r\n\tthreadState.delete(tid);\r\n}";
		Expect(SaveNextClipping(directory.c_str(), namedText, savedPath, error),
			"marker-based filename save failed");
		Expect(std::filesystem::path(savedPath.GetString()).filename() == L"function reset(tid) {.md",
			"content after # / was not used as filename");

		savedPath.Empty();
		error.Empty();
		Expect(SaveNextClipping(directory.c_str(), namedText, savedPath, error),
			"marker-based collision save failed");
		Expect(std::filesystem::path(savedPath.GetString()).filename() == L"function reset(tid) {1.md",
			"marker-based collision did not advance the index");

		savedPath.Empty();
		error.Empty();
		CString illegalNameText = L"# /foo<bar>:baz?/qux*|\\\"end. ";
		Expect(SaveNextClipping(directory.c_str(), illegalNameText, savedPath, error),
			"illegal filename character save failed");
		Expect(std::filesystem::path(savedPath.GetString()).filename() == L"foobarbazquxend.md",
			"Windows-illegal filename characters were not removed");

		savedPath.Empty();
		error.Empty();
		CString markerOnOwnLine = L"# /\nProcess.setExceptionHandler(function(details) {\n}";
		Expect(SaveNextClipping(directory.c_str(), markerOnOwnLine, savedPath, error),
			"standalone marker filename save failed");
		Expect(std::filesystem::path(savedPath.GetString()).filename() ==
			L"Process.setExceptionHandler(function(details) {.md",
			"line following standalone # / was not used as filename");

		std::filesystem::remove_all(directory);
		std::cout << "Clipping store collision tests passed.\n";
		return 0;
	}
	catch(const std::exception& error)
	{
		std::filesystem::remove_all(directory);
		std::cerr << error.what() << "\n";
		return 1;
	}
}
