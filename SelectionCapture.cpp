#include "stdafx.h"
#include "SelectionCapture.h"

#include <cstring>

namespace
{
	constexpr DWORD kNonControlModifierReleaseTimeoutMs = 350;
	constexpr DWORD kClipboardPollIntervalMs = 10;
	constexpr DWORD kOpenClipboardTimeoutMs = 400;

	struct ClipboardFormatSnapshot
	{
		UINT format;
		std::vector<unsigned char> bytes;
	};

	struct ClipboardSnapshot
	{
		bool captured = false;
		std::vector<ClipboardFormatSnapshot> formats;
	};

	bool AreNonControlCopyModifiersReleased();
	bool OpenClipboardWithRetry();
	bool IsGlobalMemoryClipboardFormat(UINT format);
	ClipboardSnapshot CaptureClipboardSnapshot();
	void RestoreTargetWindow(HWND targetWindow);
	HWND GetTargetFocusedWindow(HWND targetWindow);
	bool SendCopyKeystroke();
	bool TryCopyFromWindow(HWND window, DWORD timeoutMs, CString& text);
	bool WaitForCopiedText(DWORD sequenceBefore, DWORD timeoutMs, CString& text);
	bool TryReadClipboardText(CString& text);
	void RestoreClipboard(const ClipboardSnapshot& snapshot);
}

CString CaptureCurrentSelection(HWND targetWindow)
{
	ClipboardSnapshot originalClipboard = CaptureClipboardSnapshot();
	RestoreTargetWindow(targetWindow);

	// Alt/Shift would turn Ctrl+C into a different shortcut. Ctrl itself does
	// not need to be released: for Ctrl+right-click we can send only C while
	// the user's physical Ctrl key remains down, avoiding a visible pause.
	DWORD releaseDeadline = ::GetTickCount() + kNonControlModifierReleaseTimeoutMs;
	while(!AreNonControlCopyModifiersReleased() && static_cast<LONG>(releaseDeadline - ::GetTickCount()) > 0)
	{
		::Sleep(5);
	}

	CString captured;
	HWND focusedWindow = GetTargetFocusedWindow(targetWindow);
	// Standard edit controls can copy directly without changing focus or
	// synthesizing a key. This also avoids a race where the target becomes
	// foreground between Ctrl-down and C-down and receives a literal "c".
	TryCopyFromWindow(targetWindow, 300, captured);
	if(captured.IsEmpty() && focusedWindow != targetWindow)
		TryCopyFromWindow(focusedWindow, 300, captured);

	DWORD sequenceBefore = 0;
	if(captured.IsEmpty())
	{
		sequenceBefore = ::GetClipboardSequenceNumber();
		if(SendCopyKeystroke())
			WaitForCopiedText(sequenceBefore, 650, captured);
	}

	if(captured.IsEmpty())
	{
		// Some standard controls do not process synthetic Ctrl+C reliably
		// while the mouse is over an existing selection. WM_COPY is harmless
		// for controls that ignore it and provides a reliable fallback for
		// controls that support the standard edit protocol.
		focusedWindow = GetTargetFocusedWindow(targetWindow);
		if(focusedWindow)
			TryCopyFromWindow(focusedWindow, 300, captured);
	}

	if(captured.IsEmpty())
	{
		// Retry once after the target application's input queue has settled.
		// Electron-based editors can update their selection/clipboard one turn
		// later than the mouse hook notification.
		RestoreTargetWindow(targetWindow);
		sequenceBefore = ::GetClipboardSequenceNumber();
		if(SendCopyKeystroke())
			WaitForCopiedText(sequenceBefore, 850, captured);
	}

	RestoreClipboard(originalClipboard);
	return captured;
}

namespace
{
	bool AreNonControlCopyModifiersReleased()
	{
		return (::GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0 &&
			(::GetAsyncKeyState(VK_MENU) & 0x8000) == 0;
	}

	bool OpenClipboardWithRetry()
	{
		DWORD deadline = ::GetTickCount() + kOpenClipboardTimeoutMs;
		do
		{
			if(::OpenClipboard(NULL)) return true;
			::Sleep(10);
		} while(static_cast<LONG>(deadline - ::GetTickCount()) > 0);
		return false;
	}

	bool IsGlobalMemoryClipboardFormat(UINT format)
	{
		switch(format)
		{
		case CF_BITMAP:
		case CF_DSPBITMAP:
		case CF_ENHMETAFILE:
		case CF_DSPENHMETAFILE:
		case CF_METAFILEPICT:
		case CF_DSPMETAFILEPICT:
		case CF_PALETTE:
		case CF_OWNERDISPLAY:
			return false;
		default:
			return true;
		}
	}

	ClipboardSnapshot CaptureClipboardSnapshot()
	{
		ClipboardSnapshot snapshot;
		if(!OpenClipboardWithRetry()) return snapshot;
		snapshot.captured = true;

		UINT format = 0;
		while((format = ::EnumClipboardFormats(format)) != 0)
		{
			if(!IsGlobalMemoryClipboardFormat(format)) continue;
			HANDLE handle = ::GetClipboardData(format);
			if(!handle) continue;
			SIZE_T size = ::GlobalSize(handle);
			if(size == 0) continue;
			const void* data = ::GlobalLock(handle);
			if(!data) continue;

			ClipboardFormatSnapshot item;
			item.format = format;
			item.bytes.resize(size);
			std::memcpy(item.bytes.data(), data, size);
			::GlobalUnlock(handle);
			snapshot.formats.push_back(std::move(item));
		}

		::CloseClipboard();
		return snapshot;
	}

	void RestoreTargetWindow(HWND targetWindow)
	{
		if(!targetWindow || !::IsWindow(targetWindow)) return;
		HWND root = ::GetAncestor(targetWindow, GA_ROOT);
		if(root) targetWindow = root;
		if(::GetForegroundWindow() != targetWindow)
			::SetForegroundWindow(targetWindow);
	}

	HWND GetTargetFocusedWindow(HWND targetWindow)
	{
		if(!targetWindow || !::IsWindow(targetWindow))
			targetWindow = ::GetForegroundWindow();
		if(!targetWindow) return NULL;

		GUITHREADINFO info = { sizeof(info) };
		DWORD threadId = ::GetWindowThreadProcessId(targetWindow, NULL);
		if(threadId && ::GetGUIThreadInfo(threadId, &info) && info.hwndFocus)
			return info.hwndFocus;
		return targetWindow;
	}

	bool SendCopyKeystroke()
	{
		bool controlAlreadyDown = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		INPUT input[4] = {};
		UINT count = 0;

		if(!controlAlreadyDown)
		{
			input[count].type = INPUT_KEYBOARD;
			input[count++].ki.wVk = VK_CONTROL;
		}

		input[count].type = INPUT_KEYBOARD;
		input[count++].ki.wVk = L'C';
		input[count].type = INPUT_KEYBOARD;
		input[count].ki.wVk = L'C';
		input[count++].ki.dwFlags = KEYEVENTF_KEYUP;

		if(!controlAlreadyDown)
		{
			input[count].type = INPUT_KEYBOARD;
			input[count].ki.wVk = VK_CONTROL;
			input[count++].ki.dwFlags = KEYEVENTF_KEYUP;
		}

		return ::SendInput(count, input, sizeof(INPUT)) == count;
	}

	bool TryCopyFromWindow(HWND window, DWORD timeoutMs, CString& text)
	{
		if(!window || !::IsWindow(window)) return false;
		DWORD sequenceBefore = ::GetClipboardSequenceNumber();
		DWORD_PTR ignored = 0;
		if(!::SendMessageTimeout(
			window,
			WM_COPY,
			0,
			0,
			SMTO_ABORTIFHUNG | SMTO_BLOCK,
			200,
			&ignored))
		{
			return false;
		}
		return WaitForCopiedText(sequenceBefore, timeoutMs, text);
	}

	bool WaitForCopiedText(DWORD sequenceBefore, DWORD timeoutMs, CString& text)
	{
		DWORD deadline = ::GetTickCount() + timeoutMs;
		bool clipboardChanged = false;
		do
		{
			if(::GetClipboardSequenceNumber() != sequenceBefore)
				clipboardChanged = true;
			if(clipboardChanged)
			{
				CString candidate;
				if(TryReadClipboardText(candidate) && !candidate.IsEmpty())
				{
					text = candidate;
					return true;
				}
			}
			::Sleep(kClipboardPollIntervalMs);
		} while(static_cast<LONG>(deadline - ::GetTickCount()) > 0);
		return false;
	}

	bool TryReadClipboardText(CString& text)
	{
		if(OpenClipboardWithRetry())
		{
			bool readSucceeded = false;
			UINT format = ::IsClipboardFormatAvailable(CF_UNICODETEXT)
				? CF_UNICODETEXT
				: (::IsClipboardFormatAvailable(CF_TEXT) ? CF_TEXT : 0);
			HANDLE handle = format ? ::GetClipboardData(format) : NULL;
			if(handle)
			{
				const void* clipboardText = ::GlobalLock(handle);
				if(clipboardText)
				{
					if(format == CF_UNICODETEXT)
						text = static_cast<const WCHAR*>(clipboardText);
					else
						text = CString(CStringA(static_cast<const char*>(clipboardText)));
					::GlobalUnlock(handle);
					readSucceeded = true;
				}
			}
			::CloseClipboard();
			return readSucceeded;
		}
		return false;
	}

	void RestoreClipboard(const ClipboardSnapshot& snapshot)
	{
		if(!snapshot.captured || !OpenClipboardWithRetry()) return;
		if(!::EmptyClipboard())
		{
			::CloseClipboard();
			return;
		}

		for(const ClipboardFormatSnapshot& item : snapshot.formats)
		{
			HGLOBAL copy = ::GlobalAlloc(GMEM_MOVEABLE, item.bytes.size());
			if(!copy) continue;
			void* destination = ::GlobalLock(copy);
			if(!destination)
			{
				::GlobalFree(copy);
				continue;
			}
			std::memcpy(destination, item.bytes.data(), item.bytes.size());
			::GlobalUnlock(copy);
			if(!::SetClipboardData(item.format, copy))
				::GlobalFree(copy);
		}
		::CloseClipboard();
	}
}
