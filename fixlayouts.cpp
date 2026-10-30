#include "stdafx.h"
#include "fixlayouts.h"
#include "utils.h"

// Cast through ULONG_PTR so that these are well formed on 64-bit builds too
#define HKL_HEBREW  ((HKL)(ULONG_PTR)0x040D040D)
#define HKL_ENGLISH ((HKL)(ULONG_PTR)0x04090409)

// Another application may well have the clipboard open - the clipboard
// history, browsers and chat clients all watch it - so we give it a few tries
#define CLIPBOARD_OPEN_ATTEMPTS 10
#define CLIPBOARD_OPEN_WAIT     20

// How long we wait for the active window to put the copied text on the
// clipboard before giving up on it
#define COPY_ATTEMPTS 10
#define COPY_WAIT     30

// How long we wait for a selection we asked for with a keystroke to appear
#define SELECTION_ATTEMPTS 5
#define SELECTION_WAIT     40

// How long the application gets to read the clipboard after we paste,
// before the previous contents go back on it
#define PASTE_WAIT 400

// The window that owns the clipboard while we work with it
static HWND g_clipboardWindow = NULL;

// Formats that ask the Windows 10 and 11 clipboard history and the cloud
// clipboard to leave the data we put on the clipboard alone
static UINT g_cfExcludeFromMonitor = 0;
static UINT g_cfCanIncludeInHistory = 0;
static UINT g_cfCanUploadToCloud = 0;

///////////////////////////////////////////////////////////////////////////////
// Remembers the window to open the clipboard with and registers the clipboard
// formats we need
void SetClipboardWindow(HWND hwnd)
{
	g_clipboardWindow = hwnd;
	g_cfExcludeFromMonitor  = RegisterClipboardFormat(L"ExcludeClipboardContentFromMonitorProcessing");
	g_cfCanIncludeInHistory = RegisterClipboardFormat(L"CanIncludeInClipboardHistory");
	g_cfCanUploadToCloud    = RegisterClipboardFormat(L"CanUploadToCloudClipboard");
}

///////////////////////////////////////////////////////////////////////////////
// Opens the clipboard, retrying while another application holds it open.
//
// The window we pass in becomes the clipboard owner, and it must not be NULL:
// EmptyClipboard sets the owner to NULL when the clipboard was opened without
// a window, and SetClipboardData then fails.
static BOOL OpenClipboardWithRetry()
{
	for (int i = 0; i < CLIPBOARD_OPEN_ATTEMPTS; i++)
	{
		if (OpenClipboard(g_clipboardWindow))
			return TRUE;
		Sleep(CLIPBOARD_OPEN_WAIT);
	}

	PrintDebugString("Could not open the clipboard, last error %lu", GetLastError());
	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////
// Puts a single DWORD on the clipboard under `format`.
// Must be called with the clipboard open.
static void SetClipboardDword(UINT format, DWORD value)
{
	if (!format)
		return;

	HANDLE handle = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
	if (!handle)
		return;

	DWORD* data = (DWORD*)GlobalLock(handle);
	if (!data)
	{
		GlobalFree(handle);
		return;
	}
	*data = value;
	GlobalUnlock(handle);

	if (!SetClipboardData(format, handle))
		GlobalFree(handle);
}

///////////////////////////////////////////////////////////////////////////////
// Keeps the text we are about to put on the clipboard out of the clipboard
// history and out of the cloud clipboard: it is a temporary value of ours, not
// something the user copied.
// Must be called with the clipboard open.
static void ExcludeFromClipboardHistory()
{
	SetClipboardDword(g_cfExcludeFromMonitor, 1);
	SetClipboardDword(g_cfCanIncludeInHistory, 0);
	SetClipboardDword(g_cfCanUploadToCloud, 0);
}

///////////////////////////////////////////////////////////////////////////////
// Puts unicode text on the clipboard
BOOL SetClipboardText(const WCHAR* text)
{
	size_t size = sizeof(WCHAR) * (wcslen(text) + 1);
	HANDLE handle = GlobalAlloc(GMEM_MOVEABLE, size);
	if (!handle)
		return FALSE;

	WCHAR* clipboardText = (WCHAR*)GlobalLock(handle);
	if (!clipboardText)
	{
		GlobalFree(handle);
		return FALSE;
	}
	memcpy(clipboardText, text, size);
	GlobalUnlock(handle);

	if (!OpenClipboardWithRetry())
	{
		GlobalFree(handle);
		return FALSE;
	}

	BOOL result = FALSE;
	if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, handle))
	{
		// the clipboard owns the handle now
		ExcludeFromClipboardHistory();
		result = TRUE;
	}
	else
	{
		PrintDebugString("Could not put text on the clipboard, last error %lu", GetLastError());
		GlobalFree(handle);
	}

	CloseClipboard();
	return result;
}

///////////////////////////////////////////////////////////////////////////////
// Gets unicode text from the clipboard. 
// You must free the returned string when you do not need it anymore.
WCHAR* GetClipboardText()
{
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
		return NULL;

	if (!OpenClipboardWithRetry())
		return NULL;

	WCHAR* text = NULL;
	HANDLE handle = GetClipboardData(CF_UNICODETEXT);
	if (handle)
	{
		const WCHAR* clipboardText = (const WCHAR*)GlobalLock(handle);
		if (clipboardText)
		{
			// GlobalSize is rounded up, so look for the terminator ourselves
			// and never read past the end of the block
			size_t maxLength = GlobalSize(handle) / sizeof(WCHAR);
			size_t length = 0;
			while (length < maxLength && clipboardText[length] != L'\0')
				length++;

			text = (WCHAR*)malloc(sizeof(WCHAR) * (length + 1));
			if (text)
			{
				memcpy(text, clipboardText, sizeof(WCHAR) * length);
				text[length] = L'\0';
			}
			GlobalUnlock(handle);
		}
	}

	// always close the clipboard: leaving it open would stop every other
	// application from copying anything
	CloseClipboard();
	return text;
}

///////////////////////////////////////////////////////////////////////////////
// Tells whether a clipboard format can be copied aside and put back later.
//
// Only formats whose data is a memory block can. The others are handles the
// application that copied them owns (bitmaps, palettes, metafiles), or are
// rendered on demand by that application, and duplicating the handle would
// hand the clipboard something that is about to be freed.
static BOOL IsRestorableClipboardFormat(UINT format)
{
	switch (format)
	{
	case CF_BITMAP:
	case CF_DSPBITMAP:
	case CF_PALETTE:
	case CF_METAFILEPICT:
	case CF_DSPMETAFILEPICT:
	case CF_ENHMETAFILE:
	case CF_DSPENHMETAFILE:
	case CF_OWNERDISPLAY:
		return FALSE;
	default:
		break;
	}

	if (format >= CF_PRIVATEFIRST && format <= CF_PRIVATELAST)
		return FALSE;
	if (format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST)
		return FALSE;

	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Stores the clipboard data in all the formats we can put back in `formats`.
// You must call FreeClipboardData on `formats` when it is no longer needed.
BOOL StoreClipboardData(ClipboardData* formats)
{
	ZeroMemory(formats, sizeof(*formats));

	if (!OpenClipboardWithRetry())
		return FALSE;

	int maxFormats = CountClipboardFormats();
	if (maxFormats <= 0)
	{
		// the clipboard is empty - there is nothing to remember, but putting
		// it back later still means emptying it
		CloseClipboard();
		return TRUE;
	}

	formats->dataArray = (ClipboardFormat*)malloc(sizeof(ClipboardFormat) * maxFormats);
	if (!formats->dataArray)
	{
		CloseClipboard();
		return FALSE;
	}
	ZeroMemory(formats->dataArray, sizeof(ClipboardFormat) * maxFormats);

	UINT format = EnumClipboardFormats(0);
	while (format != 0 && formats->count < maxFormats)
	{
		if (IsRestorableClipboardFormat(format))
		{
			HANDLE dataHandle = GetClipboardData(format);
			SIZE_T size = dataHandle ? GlobalSize(dataHandle) : 0;
			const void* source = size ? GlobalLock(dataHandle) : NULL;
			if (source)
			{
				HANDLE copy = GlobalAlloc(GMEM_MOVEABLE, size);
				void* dest = copy ? GlobalLock(copy) : NULL;
				if (dest)
				{
					CopyMemory(dest, source, size);
					GlobalUnlock(copy);
					formats->dataArray[formats->count].format = format;
					formats->dataArray[formats->count].dataHandle = copy;
					formats->count++;
				}
				else if (copy)
				{
					GlobalFree(copy);
				}
				GlobalUnlock(dataHandle);
			}
		}

		// next format
		format = EnumClipboardFormats(format);
	}

	CloseClipboard();
	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Restores the data in the clipboard from `formats` that was generated by
// StoreClipboardData. Every handle the clipboard accepts becomes its property
// and is dropped from `formats`.
BOOL RestoreClipboardData(ClipboardData* formats)
{
	if (!OpenClipboardWithRetry())
		return FALSE;

	BOOL result = EmptyClipboard();
	for (int i = 0; i < formats->count; i++)
	{
		if (!formats->dataArray[i].dataHandle)
			continue;

		if (SetClipboardData(formats->dataArray[i].format, formats->dataArray[i].dataHandle))
			formats->dataArray[i].dataHandle = NULL;
		else
			result = FALSE;
	}

	CloseClipboard();
	return result;
}

///////////////////////////////////////////////////////////////////////////////
// Frees `formats` allocated by StoreClipboardData, along with any handle the
// clipboard did not take over
void FreeClipboardData(ClipboardData* formats)
{
	if (formats->dataArray)
	{
		for (int i = 0; i < formats->count; i++)
		{
			if (formats->dataArray[i].dataHandle)
				GlobalFree(formats->dataArray[i].dataHandle);
		}
		free(formats->dataArray);
	}

	ZeroMemory(formats, sizeof(*formats));
}

///////////////////////////////////////////////////////////////////////////////
// Asks UI Automation what the focused control has selected.
//
// This is what EM_GETSEL could never do: UI Automation reaches the control
// through its accessibility interface, so it works across process boundaries
// and in applications whose controls are not windows at all. When the focused
// element does not implement the text pattern we say so and the caller falls
// back to the clipboard.
//
// Returns TRUE when it could find out. *ppText is then the selected text, or
// NULL when nothing is selected; free it with free(). *pIsDocument, when
// asked for, says whether what has the focus is a document rather than a
// field, which decides what selecting everything would mean.
static BOOL GetSelectedTextFromFocus(WCHAR** ppText, BOOL* pIsDocument)
{
	*ppText = NULL;
	if (pIsDocument)
		*pIsDocument = FALSE;

	IUIAutomation* automation = NULL;
	HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
		IID_IUIAutomation, (void**)&automation);
	if (FAILED(hr) || automation == NULL)
		return FALSE;

	BOOL answered = FALSE;
	IUIAutomationElement* focused = NULL;
	if (SUCCEEDED(automation->GetFocusedElement(&focused)) && focused != NULL)
	{
		CONTROLTYPEID controlType = 0;
		if (pIsDocument && SUCCEEDED(focused->get_CurrentControlType(&controlType)))
			*pIsDocument = (controlType == UIA_DocumentControlTypeId);

		IUIAutomationTextPattern* textPattern = NULL;
		if (SUCCEEDED(focused->GetCurrentPatternAs(UIA_TextPatternId,
				IID_IUIAutomationTextPattern, (void**)&textPattern)) && textPattern != NULL)
		{
			IUIAutomationTextRangeArray* ranges = NULL;
			if (SUCCEEDED(textPattern->GetSelection(&ranges)) && ranges != NULL)
			{
				int count = 0;
				ranges->get_Length(&count);
				answered = TRUE;

				IUIAutomationTextRange* range = NULL;
				if (count > 0 && SUCCEEDED(ranges->GetElement(0, &range)) && range != NULL)
				{
					BSTR selected = NULL;
					if (SUCCEEDED(range->GetText(-1, &selected)) && selected != NULL)
					{
						UINT length = SysStringLen(selected);
						if (length > 0)
						{
							size_t size = sizeof(WCHAR) * (length + 1);
							*ppText = (WCHAR*)malloc(size);
							if (*ppText)
								memcpy(*ppText, selected, size);
						}
						SysFreeString(selected);
					}
					range->Release();
				}
				ranges->Release();
			}
			textPattern->Release();
		}
		focused->Release();
	}

	automation->Release();
	return answered;
}

///////////////////////////////////////////////////////////////////////////////
// Waits for a selection we just asked for with a keystroke to appear.
//
// The keystroke has to travel to the other application and be acted upon
// there, which takes a good deal longer than it takes us to ask the question
// again, so asking once and believing the answer finds nothing selected.
static WCHAR* WaitForSelection()
{
	for (int i = 0; i < SELECTION_ATTEMPTS; i++)
	{
		Sleep(SELECTION_WAIT);

		WCHAR* text = NULL;
		GetSelectedTextFromFocus(&text, NULL);
		if (text)
			return text;
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Puts `dummy` on the clipboard, asks the active window to copy its selection
// over it and returns what we got, or NULL when nothing was copied.
//
// Comparing against a string we put there ourselves is how we find out whether
// anything was selected at all. EM_GETSEL, which this used to ask the focused
// control, cannot tell us: the pointers it writes through mean nothing in
// another process.
static WCHAR* CopySelectedText(const WCHAR* dummy)
{
	if (!SetClipboardText(dummy))
		return NULL;

	// copy the selected text by simulating Ctrl-C
	SendKeyCombo(VK_CONTROL, 'C', FALSE);

	for (int i = 0; i < COPY_ATTEMPTS; i++)
	{
		Sleep(COPY_WAIT);

		WCHAR* text = GetClipboardText();
		if (text && wcscmp(text, dummy) != 0)
			return text;

		free(text);
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Converts the text in the active window from one keyboard layout to another
// using the clipboard.
void ConvertSelectedTextInActiveWindow(HKL hklSource, HKL hklTarget)
{
	const WCHAR dummy[] = L"__RECAPS__";

	// store previous clipboard data. if we cannot, we leave the clipboard
	// alone rather than destroy what the user had on it.
	ClipboardData prevClipboardData;
	if (!StoreClipboardData(&prevClipboardData))
	{
		PrintDebugString("Could not store the clipboard, giving up on the conversion");
		return;
	}

	WCHAR* sourceText = NULL;
	BOOL isDocument = FALSE;

	// Ask UI Automation first: it hands us the selected text without going
	// near the clipboard, and it also tells us when nothing is selected.
	BOOL selectionKnown = GetSelectedTextFromFocus(&sourceText, &isDocument);
	if (selectionKnown && sourceText == NULL)
	{
		// Nothing is selected, so there is nothing to convert yet. In a field -
		// a search box, an address bar - taking all of it is what the user
		// means by fixing what they just typed. In a document that would take
		// the entire text, so the line up to the caret is taken instead: a
		// phrase of short words is one line, and taking only the word before
		// the caret would leave the rest of it in the wrong layout.
		if (isDocument)
			SendKeyCombo(VK_SHIFT, VK_HOME, TRUE);
		else
			SendKeyCombo(VK_CONTROL, 'A', FALSE);

		sourceText = WaitForSelection();
	}

	if (sourceText == NULL)
	{
		// the application does not tell us, so go through the clipboard
		sourceText = CopySelectedText(dummy);
		if (!sourceText && !isDocument)
		{
			// nothing was selected: select the whole field and try again. Never
			// in a document, where that would mean the whole text.
			SendKeyCombo(VK_CONTROL, 'A', FALSE);
			sourceText = CopySelectedText(dummy);
		}
	}

	if (sourceText)
	{
		size_t length = wcslen(sourceText);
		WCHAR* targetText = (WCHAR*)malloc(sizeof(WCHAR) * (length + 1));
		if (targetText)
		{
			// if the string only matches one particular layout, use it
			// otherwise use the provided layout
			int matches = 0;
			HKL hklDetected = DetectLayoutFromString(sourceText, &matches);
			if (matches == 1)
				hklSource = hklDetected;

			// convert the text between layouts and put it on the clipboard
			if (LayoutConvertString(sourceText, targetText, length + 1, hklSource, hklTarget) &&
				SetClipboardText(targetText))
			{
				// simulate Ctrl-V to paste the text, replacing the previous text
				SendKeyCombo(VK_CONTROL, 'V', FALSE);

				// Give the application time to actually read the clipboard before
				// the previous contents go back on it. It reads it when it gets
				// round to handling the keystroke rather than when we send it, and
				// putting the old contents back too early is what makes the wrong
				// text land in the document.
				Sleep(PASTE_WAIT);
			}

			free(targetText);
		}

		free(sourceText);
	}

	// always restore the original clipboard data, even when nothing was
	// converted: our dummy string must not be left on the clipboard
	RestoreClipboardData(&prevClipboardData);
	FreeClipboardData(&prevClipboardData);
}

///////////////////////////////////////////////////////////////////////////////
// Converts a character from one keyboard layout to another
WCHAR LayoutConvertChar(WCHAR ch, HKL hklSource, HKL hklTarget)
{
	// special handling for some ambivalent characters in hebrew layout
	if (hklSource == HKL_HEBREW && hklTarget == HKL_ENGLISH)
	{
		switch (ch)
		{
			case L'.':  return L'/';
			case L'/':  return L'q';
			case L'\'': return L'w';
			case L',':  return L'\'';
		}
	}
	else if (hklSource == HKL_ENGLISH && hklTarget == HKL_HEBREW)
	{
		switch (ch)
		{
			case L'/':  return L'.';
			case L'q':  return L'/';
			case L'w':  return L'\'';
			case L'\'': return L',';
		}
	}

	// get the virtual key code and the shift state using the character and the source keyboard layout
	SHORT vkAndShift = VkKeyScanEx(ch, hklSource);
	if (vkAndShift == -1)
		return 0; // no such char in source keyboard layout

	BYTE vk = LOBYTE(vkAndShift);
	BYTE shift = HIBYTE(vkAndShift);

	// convert the shift state returned from VkKeyScanEx to an array that represents the
	// key state usable with ToUnicodeEx that we will be calling next
	BYTE keyState[256] = {0};
	if (shift & 1) keyState[VK_SHIFT] = 0x80;	// turn on high bit
	if (shift & 2) keyState[VK_CONTROL] = 0x80;
	if (shift & 4) keyState[VK_MENU] = 0x80;

	// convert virtual key and key state to a new character using the target keyboard layout
	WCHAR buffer[10] = {0};
	int result = ToUnicodeEx(vk, 0, keyState, buffer, 10, 0, hklTarget);

	// result can be more than 1 if the character in the source layout is represented by 
	// several characters in the target layout, but we ignore this to simplify the function.
	if (result == 1)
		return buffer[0];
	
	// conversion failed for some reason
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Converts a string from one keyboard layout to another
size_t LayoutConvertString(const WCHAR* str, WCHAR* buffer, size_t size, HKL hklSource, HKL hklTarget)
{
	size_t i;
	size_t changed = 0;
	size_t length = wcslen(str);
	for (i = 0; i < length && i < size-1; i++)
	{
		// A character the source layout cannot produce - a line break, a dash
		// pasted in from somewhere, an emoji - is kept as it is. Abandoning
		// the whole string over one of them is why converting a paragraph in
		// a document did nothing at all.
		WCHAR ch = LayoutConvertChar(str[i], hklSource, hklTarget);
		buffer[i] = ch ? ch : str[i];
		if (ch != 0 && ch != str[i])
			changed++;
	}
	buffer[i] = '\0';

	// nothing came out any different, so there is nothing worth pasting
	return changed ? i : 0;
}

///////////////////////////////////////////////////////////////////////////////
// Goes through all the installed keyboard layouts and returns a layout that
// can generate the string. If not matching layout is found, returns NULL.
// If `pmatches` is not NULL it will be set to the number of matched layouts.
HKL DetectLayoutFromString(const WCHAR* str, int* pmatches)
{
	HKL result = NULL;
	UINT layoutCount = GetKeyboardLayoutList(0, NULL);
	HKL* hkls = (HKL*)malloc(sizeof(HKL) * layoutCount);
	if (!hkls)
	{
		if (pmatches)
			*pmatches = 0;
		return NULL;
	}
	layoutCount = GetKeyboardLayoutList(layoutCount, hkls);

	int matches = 0;
	size_t length = wcslen(str);
	for (UINT layout = 0; layout < layoutCount; layout++)
	{
		BOOL validLayout = TRUE;
		for (size_t i = 0; i < length; i++)
		{
			SHORT vk = VkKeyScanEx(str[i], hkls[layout]);
			if (vk == -1)
			{
				validLayout = FALSE;
				break;
			}
		}
		if (validLayout)
		{
			matches++;
			if (!result)
				result = hkls[layout];
		}
	}

	free(hkls);

	if (pmatches)
		*pmatches = matches;

	return result;
}

///////////////////////////////////////////////////////////////////////////////
// Fills in one keyboard event for SendInput.
//
// SendInput rather than keybd_event, which this used to call: keybd_event is
// deprecated, and it sends no scan code, which applications that read the raw
// input instead of the window messages ignore.
static void FillKeyInput(INPUT* input, BYTE vk, BOOL extended, BOOL keyUp)
{
	ZeroMemory(input, sizeof(*input));
	input->type = INPUT_KEYBOARD;
	input->ki.wVk = vk;
	input->ki.wScan = (WORD)MapVirtualKey(vk, MAPVK_VK_TO_VSC);
	input->ki.dwFlags = (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (keyUp ? KEYEVENTF_KEYUP : 0);
}

///////////////////////////////////////////////////////////////////////////////
// Simulates a key press in the active window
BOOL SendKey(BYTE vk, BOOL extended)
{
	INPUT inputs[2];
	FillKeyInput(&inputs[0], vk, extended, FALSE);
	FillKeyInput(&inputs[1], vk, extended, TRUE);

	return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

///////////////////////////////////////////////////////////////////////////////
// Simulates a key combination (such as Ctrl+X) in the active window
BOOL SendKeyCombo(BYTE vkModifier, BYTE vk, BOOL extended)
{
	// Leave the modifier alone when the user is already holding it down, which
	// is the usual case here: the shortcut that got us going contains it.
	BOOL modPressed = (GetAsyncKeyState(vkModifier) & 0x8000) != 0;

	INPUT inputs[4];
	UINT count = 0;
	if (!modPressed)
		FillKeyInput(&inputs[count++], vkModifier, FALSE, FALSE);
	FillKeyInput(&inputs[count++], vk, extended, FALSE);
	FillKeyInput(&inputs[count++], vk, extended, TRUE);
	if (!modPressed)
		FillKeyInput(&inputs[count++], vkModifier, FALSE, TRUE);

	return SendInput(count, inputs, sizeof(INPUT)) == count;
}

///////////////////////////////////////////////////////////////////////////////
// Presses Win+Space, the shortcut Windows acts on by itself.
//
// It is the last resort for windows that answer neither the layout change
// message nor the request made through TSF. Because the system is what acts
// on it, it reaches a little further than talking to the window does. What it
// cannot do is pick a particular layout: it only moves on to the next one,
// which is the same thing when two are installed.
//
// It does not reach a protected window such as the Windows Security credential
// prompt: injected input is dropped there. See VerifySwitch in recaps.cpp.
BOOL SendSystemLayoutShortcut()
{
	INPUT inputs[4];
	FillKeyInput(&inputs[0], VK_LWIN, TRUE, FALSE);
	FillKeyInput(&inputs[1], VK_SPACE, FALSE, FALSE);
	FillKeyInput(&inputs[2], VK_SPACE, FALSE, TRUE);
	FillKeyInput(&inputs[3], VK_LWIN, TRUE, TRUE);

	return SendInput(4, inputs, sizeof(INPUT)) == 4;
}
