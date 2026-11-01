#include "..\stdafx.h"
#include "..\SelectionCapture.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

CAppModule _Module;

namespace
{
	struct EditorContext
	{
		HANDLE readyEvent;
		HWND window = NULL;
		DWORD threadId = 0;
	};

	LRESULT CALLBACK EditorWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		return ::DefWindowProc(window, message, wParam, lParam);
	}

	bool OpenClipboardForTest()
	{
		for(int attempt = 0; attempt < 50; attempt++)
		{
			if(::OpenClipboard(NULL)) return true;
			::Sleep(10);
		}
		return false;
	}

	void SetClipboardUnicodeText(const wchar_t* text)
	{
		if(!OpenClipboardForTest()) throw std::runtime_error("OpenClipboard failed");
		::EmptyClipboard();
		size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
		HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
		void* destination = ::GlobalLock(memory);
		std::memcpy(destination, text, bytes);
		::GlobalUnlock(memory);
		if(!::SetClipboardData(CF_UNICODETEXT, memory))
		{
			::GlobalFree(memory);
			::CloseClipboard();
			throw std::runtime_error("SetClipboardData failed");
		}
		::CloseClipboard();
	}

	CString GetClipboardUnicodeText()
	{
		CString result;
		if(!OpenClipboardForTest()) return result;
		HANDLE handle = ::GetClipboardData(CF_UNICODETEXT);
		const wchar_t* text = handle ? static_cast<const wchar_t*>(::GlobalLock(handle)) : nullptr;
		if(text)
		{
			result = text;
			::GlobalUnlock(handle);
		}
		::CloseClipboard();
		return result;
	}

	void EditorThread(EditorContext* context)
	{
		context->threadId = ::GetCurrentThreadId();
		WNDCLASSEX windowClass = { sizeof(windowClass) };
		windowClass.lpfnWndProc = EditorWindowProc;
		windowClass.hInstance = ::GetModuleHandle(NULL);
		windowClass.hCursor = ::LoadCursor(NULL, IDC_IBEAM);
		windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		windowClass.lpszClassName = L"SelectionCaptureTestEditor";
		::RegisterClassEx(&windowClass);
		context->window = ::CreateWindowEx(
			WS_EX_TOPMOST,
			windowClass.lpszClassName,
			L"Selection capture test",
			WS_OVERLAPPEDWINDOW | WS_VISIBLE,
			100,
			100,
			500,
			120,
			NULL,
			NULL,
			::GetModuleHandle(NULL),
			NULL);
		HWND editor = ::CreateWindowEx(
			0,
			L"EDIT",
			L"prefix selected text suffix",
			WS_CHILD | WS_VISIBLE | ES_MULTILINE,
			0,
			0,
			480,
			80,
			context->window,
			reinterpret_cast<HMENU>(1),
			::GetModuleHandle(NULL),
			NULL);
		::SetWindowLongPtr(context->window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
		::SetForegroundWindow(context->window);
		::SetFocus(editor);
		::SendMessage(editor, EM_SETSEL, 7, 20);
		context->window = editor;
		::SetEvent(context->readyEvent);

		MSG message;
		while(::GetMessage(&message, NULL, 0, 0) > 0)
		{
			::TranslateMessage(&message);
			::DispatchMessage(&message);
		}
	}
}

int main()
{
	::OleInitialize(NULL);
	EditorContext context{ ::CreateEvent(NULL, TRUE, FALSE, NULL) };
	std::thread editor(EditorThread, &context);
	::WaitForSingleObject(context.readyEvent, 3000);
	::CloseHandle(context.readyEvent);

	int result = 0;
	try
	{
		SetClipboardUnicodeText(L"clipboard sanity check");
		::SendMessage(context.window, EM_SETSEL, 7, 20);
		::SendMessage(context.window, WM_COPY, 0, 0);
		if(GetClipboardUnicodeText() != L"selected text")
			throw std::runtime_error("test editor did not process WM_COPY");

		for(int iteration = 0; iteration < 100; iteration++)
		{
			SetClipboardUnicodeText(L"original clipboard");
			HWND root = ::GetAncestor(context.window, GA_ROOT);
			::SetForegroundWindow(root);
			::SendMessage(context.window, EM_SETSEL, 7, 20);
			CString captured = CaptureCurrentSelection(context.window);
			if(captured != L"selected text")
			{
				DWORD selectionStart = 0;
				DWORD selectionEnd = 0;
				::SendMessage(context.window, EM_GETSEL,
					reinterpret_cast<WPARAM>(&selectionStart),
					reinterpret_cast<LPARAM>(&selectionEnd));
				::SendMessage(context.window, WM_COPY, 0, 0);
				CString directCopy = GetClipboardUnicodeText();
				std::wcerr << L"iteration " << iteration << L", captured instead: ["
					<< captured.GetString() << L"], direct WM_COPY: ["
					<< directCopy.GetString() << L"], selection: "
					<< selectionStart << L".." << selectionEnd << L"\n";
				throw std::runtime_error("selected text was not captured");
			}
			if(GetClipboardUnicodeText() != L"original clipboard")
				throw std::runtime_error("original clipboard was not restored");
		}
		std::cout << "Selection capture repetition test passed.\n";
	}
	catch(const std::exception& error)
	{
		std::cerr << error.what() << "\n";
		result = 1;
	}

	::PostMessage(context.window, WM_CLOSE, 0, 0);
	::PostThreadMessage(context.threadId, WM_QUIT, 0, 0);
	editor.join();
	::OleUninitialize();
	return result;
}
