#include "VaultMarkerFormatter.h"

#include <algorithm>
#include <cwctype>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{
	struct Line
	{
		size_t start;
		size_t textEnd;
		size_t end;
		size_t firstText;
	};

	struct CommentEnd
	{
		size_t nextLine;
		size_t inlineCode = std::wstring::npos;
		size_t inlineCodeLine = std::wstring::npos;
	};

	std::vector<Line> SplitLines(const std::wstring& text);
	std::wstring_view TrimmedLine(const std::wstring& text, const Line& line);
	bool IsBlank(const Line& line);
	bool StartsWith(std::wstring_view text, std::wstring_view prefix);
	bool IsTagMarker(std::wstring_view text);
	bool IsHashComment(std::wstring_view text);
	bool IsLineComment(std::wstring_view text);
	bool TryConsumeComment(
		const std::wstring& text,
		const std::vector<Line>& lines,
		size_t lineIndex,
		CommentEnd& result);
	bool TryConsumeDelimitedComment(
		const std::wstring& text,
		const std::vector<Line>& lines,
		size_t lineIndex,
		std::wstring_view closingToken,
		CommentEnd& result);
	std::wstring PreferredNewline(const std::wstring& text);
}

std::wstring AddVaultSectionMarkers(const std::wstring& capturedText)
{
	std::vector<Line> lines = SplitLines(capturedText);
	size_t firstLine = 0;
	while(firstLine < lines.size() && IsBlank(lines[firstLine]))
		firstLine++;

	if(firstLine == lines.size())
	{
		std::wstring result = capturedText;
		if(!result.empty() && result.back() != L'\r' && result.back() != L'\n')
			result += PreferredNewline(result);
		result += L"# /";
		return result;
	}

	std::wstring_view firstText = TrimmedLine(capturedText, lines[firstLine]);
	if(StartsWith(firstText, L"//?") || IsTagMarker(firstText))
		return capturedText;

	CommentEnd commentEnd;
	if(!TryConsumeComment(capturedText, lines, firstLine, commentEnd))
	{
		std::wstring result = capturedText;
		result.insert(lines[firstLine].firstText, L"# /");
		return result;
	}

	size_t cursor = commentEnd.nextLine;
	size_t inlineCode = commentEnd.inlineCode;
	size_t inlineCodeLine = commentEnd.inlineCodeLine;
	while(inlineCode == std::wstring::npos && cursor < lines.size())
	{
		size_t afterBlanks = cursor;
		while(afterBlanks < lines.size() && IsBlank(lines[afterBlanks]))
			afterBlanks++;
		if(afterBlanks == lines.size())
		{
			cursor = afterBlanks;
			break;
		}

		CommentEnd nextComment;
		if(!TryConsumeComment(capturedText, lines, afterBlanks, nextComment))
		{
			cursor = afterBlanks;
			break;
		}

		cursor = nextComment.nextLine;
		inlineCode = nextComment.inlineCode;
		inlineCodeLine = nextComment.inlineCodeLine;
	}

	std::wstring result = capturedText;
	size_t remarkPosition = lines[firstLine].firstText;
	if(inlineCode != std::wstring::npos)
	{
		const Line& codeLine = lines[inlineCodeLine];
		std::wstring indentation = capturedText.substr(codeLine.start, codeLine.firstText - codeLine.start);
		result.insert(inlineCode, PreferredNewline(capturedText) + indentation + L"# /");
	}
	else if(cursor < lines.size())
	{
		std::wstring_view codeText = TrimmedLine(capturedText, lines[cursor]);
		if(!IsTagMarker(codeText))
			result.insert(lines[cursor].firstText, L"# /");
	}
	else
	{
		if(!result.empty() && result.back() != L'\r' && result.back() != L'\n')
			result += PreferredNewline(capturedText);
		result += L"# /";
	}

	result.insert(remarkPosition, L"//?");
	return result;
}

namespace
{
	std::vector<Line> SplitLines(const std::wstring& text)
	{
		std::vector<Line> lines;
		size_t start = 0;
		while(start < text.size())
		{
			size_t textEnd = start;
			while(textEnd < text.size() && text[textEnd] != L'\r' && text[textEnd] != L'\n')
				textEnd++;

			size_t end = textEnd;
			if(end < text.size() && text[end] == L'\r') end++;
			if(end < text.size() && text[end] == L'\n') end++;

			size_t firstText = start;
			while(firstText < textEnd && std::iswspace(text[firstText]))
				firstText++;
			lines.push_back({ start, textEnd, end, firstText });
			start = end;
		}

		if(text.empty() || (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')))
			lines.push_back({ text.size(), text.size(), text.size(), text.size() });
		return lines;
	}

	std::wstring_view TrimmedLine(const std::wstring& text, const Line& line)
	{
		return std::wstring_view(text).substr(line.firstText, line.textEnd - line.firstText);
	}

	bool IsBlank(const Line& line)
	{
		return line.firstText == line.textEnd;
	}

	bool StartsWith(std::wstring_view text, std::wstring_view prefix)
	{
		return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
	}

	bool IsTagMarker(std::wstring_view text)
	{
		if(text.empty() || text[0] != L'#') return false;
		size_t position = 1;
		while(position < text.size() && (text[position] == L' ' || text[position] == L'\t'))
			position++;
		return position < text.size() && text[position] == L'/';
	}

	bool IsHashComment(std::wstring_view text)
	{
		if(text.empty() || text[0] != L'#' || IsTagMarker(text)) return false;
		size_t position = 1;
		while(position < text.size() && std::iswspace(text[position])) position++;
		if(position == text.size() || text[position] == L'!') return true;

		size_t wordEnd = position;
		while(wordEnd < text.size() && std::iswalpha(text[wordEnd])) wordEnd++;
		std::wstring word(text.substr(position, wordEnd - position));
		std::transform(word.begin(), word.end(), word.begin(), std::towlower);
		static const std::unordered_set<std::wstring> PreprocessorDirectives = {
			L"define", L"elif", L"else", L"endif", L"endregion", L"error",
			L"if", L"ifdef", L"ifndef", L"import", L"include", L"line",
			L"pragma", L"region", L"undef", L"using", L"warning"
		};
		return PreprocessorDirectives.find(word) == PreprocessorDirectives.end();
	}

	bool IsLineComment(std::wstring_view text)
	{
		if(StartsWith(text, L"//")) return !StartsWith(text, L"//?");
		if(IsHashComment(text)) return true;
		if(StartsWith(text, L"--") &&
			(text.size() == 2 || text[2] == L' ' || text[2] == L'\t' || text[2] == L'-')) return true;
		if(StartsWith(text, L"::")) return true;
		if(!text.empty() && (text[0] == L';' || text[0] == L'%' || text[0] == L'\'')) return true;
		if(text.size() >= 3 && std::towlower(text[0]) == L'r' &&
			std::towlower(text[1]) == L'e' && std::towlower(text[2]) == L'm' &&
			(text.size() == 3 || std::iswspace(text[3]))) return true;
		return false;
	}

	bool TryConsumeComment(
		const std::wstring& text,
		const std::vector<Line>& lines,
		size_t lineIndex,
		CommentEnd& result)
	{
		std::wstring_view lineText = TrimmedLine(text, lines[lineIndex]);
		if(IsLineComment(lineText))
		{
			result = { lineIndex + 1 };
			return true;
		}
		if(StartsWith(lineText, L"/*"))
			return TryConsumeDelimitedComment(text, lines, lineIndex, L"*/", result);
		if(StartsWith(lineText, L"<!--"))
			return TryConsumeDelimitedComment(text, lines, lineIndex, L"-->", result);
		return false;
	}

	bool TryConsumeDelimitedComment(
		const std::wstring& text,
		const std::vector<Line>& lines,
		size_t lineIndex,
		std::wstring_view closingToken,
		CommentEnd& result)
	{
		for(size_t current = lineIndex; current < lines.size(); current++)
		{
			std::wstring_view lineText(text.data() + lines[current].start, lines[current].textEnd - lines[current].start);
			size_t closing = lineText.find(closingToken);
			if(closing == std::wstring_view::npos) continue;

			size_t tail = lines[current].start + closing + closingToken.size();
			while(tail < lines[current].textEnd && std::iswspace(text[tail])) tail++;
			result.nextLine = current + 1;
			if(tail < lines[current].textEnd)
			{
				result.inlineCode = tail;
				result.inlineCodeLine = current;
			}
			return true;
		}

		// An unterminated block is more likely incomplete code than a safe
		// metadata header, so leave it to the normal tag-only path.
		return false;
	}

	std::wstring PreferredNewline(const std::wstring& text)
	{
		if(text.find(L"\r\n") != std::wstring::npos) return L"\r\n";
		if(text.find(L'\n') != std::wstring::npos) return L"\n";
		if(text.find(L'\r') != std::wstring::npos) return L"\r";
		return L"\r\n";
	}
}
