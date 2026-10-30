#pragma once

struct ClipboardFormat
{
	UINT format;
	HANDLE dataHandle;
};

struct ClipboardData
{
	int count;
	ClipboardFormat* dataArray;
};


// time in milliseconds to allow the target application
// to execute commands simmulated by keystrokes
#define REMOTE_APP_WAIT 20

// Tells this module which window to make the owner of the clipboard. Has to be
// called before any of the clipboard functions below: a clipboard opened
// without an owner window cannot be written to.
void SetClipboardWindow(HWND hwnd);

// The main function that converts the current selected text in the active 
// window from one layout to another.
void ConvertSelectedTextInActiveWindow(HKL hklSource, HKL hklTarget);

// Functions to convert UNICODE strings between keyboard layouts
WCHAR LayoutConvertChar(WCHAR ch, HKL hklSource, HKL hklTarget);
size_t LayoutConvertString(const WCHAR* str, WCHAR* buffer, size_t size, HKL hklSource, HKL hklTarget);
HKL DetectLayoutFromString(const WCHAR* str, int* pmatches);

// Functions to store and restore all of the data in the clipboard
BOOL StoreClipboardData(ClipboardData* formats);
BOOL RestoreClipboardData(ClipboardData* formats);
void FreeClipboardData(ClipboardData* formats);

// Convenience functions for the clipboard
WCHAR* GetClipboardText();
BOOL SetClipboardText(const WCHAR* text);

// Functions that simulate key presses in the current window
BOOL SendKey(BYTE vk, BOOL extended);
BOOL SendKeyCombo(BYTE vkModifier, BYTE vk, BOOL extended);
