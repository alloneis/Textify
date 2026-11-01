#include "..\VaultMarkerFormatter.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	void Expect(const std::wstring& actual, const std::wstring& expected, const char* name)
	{
		if(actual != expected)
		{
			std::wcerr << L"FAILED: " << name << L"\nExpected:\n" << expected
				<< L"\nActual:\n" << actual << L"\n";
			throw std::runtime_error(name);
		}
	}
}

int main()
{
	Expect(
		AddVaultSectionMarkers(
			L"// All capital letters must be replaced\n\n"
			L"// Call function after a delay\n"
			L"setTimeout(function()\n{\n});"),
		L"//?// All capital letters must be replaced\n\n"
			L"// Call function after a delay\n"
			L"# /setTimeout(function()\n{\n});",
		"line comments and internal blank line");

	Expect(
		AddVaultSectionMarkers(L"setTimeout(function() {});"),
		L"# /setTimeout(function() {});",
		"code without comments");

	Expect(
		AddVaultSectionMarkers(L"# explanation\nprint('ok')"),
		L"//?# explanation\n# /print('ok')",
		"hash comment");

	Expect(
		AddVaultSectionMarkers(L"#include <vector>\nint main() {}"),
		L"# /#include <vector>\nint main() {}",
		"preprocessor is code");

	Expect(
		AddVaultSectionMarkers(L"/** docs */\nvoid run();"),
		L"//?/** docs */\n# /void run();",
		"block comment");

	Expect(
		AddVaultSectionMarkers(L"/* docs */ void run();"),
		L"//?/* docs */ \r\n# /void run();",
		"inline code after block comment");

	Expect(
		AddVaultSectionMarkers(L"//?existing\n# /run();"),
		L"//?existing\n# /run();",
		"existing markers are unchanged");

	Expect(
		AddVaultSectionMarkers(L"-- SQL query\nSELECT 1;"),
		L"//?-- SQL query\n# /SELECT 1;",
		"SQL comment");

	std::cout << "Vault marker formatter tests passed.\n";
	return 0;
}
