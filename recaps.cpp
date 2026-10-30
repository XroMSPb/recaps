/******************************************************************************

Recaps - change language and keyboard layout using the CapsLock key.
Copyright (C) 2007 Eli Golovinsky
Copyright (C) 2014 Victor Homyakov

-------------------------------------------------------------------------------

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*******************************************************************************/

#include "stdafx.h"
#include "resource.h"
#include "version.h"
#include "trayicon.h"
#include "fixlayouts.h"
#include "utils.h"

#define HELP_MESSAGE \
    L"Recaps allows you to quickly switch the current\r\n"\
    L"language using the Capslock key.\r\n"\
    L"\r\n"\
    L"Capslock changes the current keyboard language.\r\n"\
    L"Ctrl-Capslock fixes text you typed in the wrong language.\r\n"\
    L"Alt-Capslock is the old Capslock that lets you type in CAPITAL.\r\n"\
    L"\r\n"\
    L"LeftAlt-LeftShift sets the current language to the first in list\r\n"\
    L"RightAlt-RightShift sets the current language to the second in list\r\n"\
    L"\r\n"\
    L"http://www.gooli.org/blog/recaps\r\n\r\n"\
    L"Eli Golovinsky, Israel 2008\r\n"

#define HELP_TITLE \
    L"Recaps " RECAPS_VERSION_STRW L" - Retake your Capslock!"

// Tray icon constants
#define ID_TRAYICON          1
#define APPWM_TRAYICON       (WM_APP + 0)
#define APPWM_NOP            (WM_APP + 1)

// Work the keyboard hook asks us to do. The hook procedure itself must return
// as quickly as it can (see LowLevelHookProc), so all it does is post these.
#define APPWM_SWITCH_LAYOUT      (WM_APP + 2)
#define APPWM_SWITCH_AND_CONVERT (WM_APP + 3)
#define APPWM_SWITCH_TO_NUMBER   (WM_APP + 4)
// posted by whichever thread asked for a switch, so that the timer that
// checks on it is set by the thread that owns the window
#define APPWM_VERIFY_SWITCH      (WM_APP + 5)

//  Our commands
#define ID_ABOUT             2000
#define ID_EXIT              2001
#define ID_AUTOSTART         2002
#define ID_PAUSE             2003
#define ID_IGNORE_APP        2004
// one command per installed language, so they start clear of the rest
#define ID_LANG              2100

// Where Windows keeps the programs that start with it, and where we keep
// the applications to stay away from
#define RUN_KEY     L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE   L"Recaps"
#define IGNORED_KEY L"Software\\Recaps\\Ignored"
#define MAX_IGNORED 64
#define PATH_SEPARATOR L'\\'

// General constants
#define MAXLEN 1024
#define MAX_LAYOUTS 256
#define MUTEX L"recaps-D3E743A3-E0F9-47f5-956A-CD15C6548789"
#define WINDOWCLASS_NAME L"RECAPS"
#define TITLE L"Recaps"

#define CONFIG_KEY           L"Software\\Recaps"
#define KEYBOARD_LAYOUTS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts"

// How often we look at the active window to see which language it is using
#define IDT_LAYOUT_POLL      2
#define IDT_SWITCH_VERIFY    3
#define SWITCH_VERIFY_DELAY  250
#define LAYOUT_POLL_PERIOD   500
#define THEME_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"

// How often we make sure our keyboard hook is still installed
#define IDT_HOOK_WATCHDOG    1
#define HOOK_WATCHDOG_PERIOD 10000

struct KeyboardLayoutInfo {
    WCHAR names[MAX_LAYOUTS][MAXLEN];
    WCHAR klids[MAX_LAYOUTS][KL_NAMELENGTH];
    HKL   hkls[MAX_LAYOUTS];
    UINT  menuIds[MAX_LAYOUTS];
    BOOL  inUse[MAX_LAYOUTS];
    UINT  count;
    UINT  current;
};

KeyboardLayoutInfo g_keyboardInfo = {0};
BOOL g_modalShown = FALSE;
HHOOK g_hHook = NULL;
HWND g_hMessageWindow = NULL;

// Explorer broadcasts this message when the taskbar is created, which is when
// we have to put our tray icon back.
UINT g_uTaskbarCreated = 0;

// Set while a conversion is in progress, so that two of them never fight over
// the clipboard.
volatile LONG g_converting = 0;

// The applications Recaps leaves alone, the executable owning the foreground
// window, and whether that window is one of them. The hook reads the flag.
WCHAR g_ignored[MAX_IGNORED][MAX_PATH] = {0};
UINT g_ignoredCount = 0;
WCHAR g_foregroundApp[MAX_PATH] = L"";
WCHAR g_menuApp[MAX_PATH] = L"";
volatile LONG g_ignoreActive = 0;

// Set while Recaps is paused and the hook is deliberately not installed
BOOL g_paused = FALSE;
HWINEVENTHOOK g_hWinEventHook = NULL;

// The layout the tray icon is currently showing
HKL g_shownLayout = NULL;

// What the last switch asked for, so that we can tell afterwards whether the
// window took any notice of it
HKL g_switchFrom = NULL;
HKL g_switchTo = NULL;

// What GetLastInputInfo reported the last time the watchdog looked
DWORD g_lastCheckedInput = 0;

// Prototypes
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
int OnTrayIcon(HWND hWnd, WPARAM wParam, LPARAM lParam);
int OnCommand(HWND hWnd, WORD wID, HWND hCtl);
BOOL ShowPopupMenu(HWND hWnd);

void RefreshKeyboardLayouts(KeyboardLayoutInfo* info);
void RefreshKeyboardLayoutsIfChanged(KeyboardLayoutInfo* info);
void LoadConfiguration(KeyboardLayoutInfo* info);
void SaveConfiguration(const KeyboardLayoutInfo* info);

HWND RemoteGetFocus();
HKL SwitchLayout();
void SwitchToLayoutNumber(int number);
void UpdateTrayIcon(BOOL force);
void UpdateForegroundApp();
void VerifySwitch();
void StartSwitchAndConvert();
BOOL InstallHook();
void CheckHook();
LRESULT CALLBACK LowLevelHookProc(int nCode, WPARAM wParam, LPARAM lParam);

///////////////////////////////////////////////////////////////////////////////
// Fills `path` with the executable of the Recaps instance that is already
// running, so that we can name it instead of leaving the user guessing which
// copy holds the mutex - an older build left running in the notification area
// takes the same name as this one. Best effort: the message makes sense
// without it too.
static BOOL GetRunningInstancePath(WCHAR* path, DWORD size) {
    HWND existing = FindWindow(WINDOWCLASS_NAME, NULL);
    if (existing == NULL) {
        return FALSE;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(existing, &processId);
    if (processId == 0) {
        return FALSE;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == NULL) {
        return FALSE;
    }

    BOOL result = QueryFullProcessImageName(process, 0, path, &size);
    CloseHandle(process);
    return result;
}

///////////////////////////////////////////////////////////////////////////////
// Whether Recaps starts with Windows.
//
// This is a value under the Run key rather than a shortcut in the Startup
// folder: the Startup page of the Settings app lists what is under Run, so the
// user finds us where they look for it, and no administrator rights are needed
// for it either.
static BOOL IsAutostartEnabled() {
    HKEY hkey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
        return FALSE;
    }

    BOOL enabled = RegQueryValueEx(hkey, RUN_VALUE, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
    RegCloseKey(hkey);
    return enabled;
}

///////////////////////////////////////////////////////////////////////////////
// Turns starting with Windows on or off
static void SetAutostart(BOOL enable) {
    HKEY hkey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &hkey) != ERROR_SUCCESS) {
        return;
    }

    if (enable) {
        WCHAR path[MAX_PATH];
        WCHAR quoted[MAX_PATH + 4];
        if (GetModuleFileName(NULL, path, _countof(path)) > 0 &&
            swprintf_s(quoted, _countof(quoted), L"\"%s\"", path) > 0) {
            RegSetValueEx(hkey, RUN_VALUE, 0, REG_SZ, (const BYTE*)quoted,
                (DWORD)((wcslen(quoted) + 1) * sizeof(WCHAR)));
        }
    } else {
        RegDeleteValue(hkey, RUN_VALUE);
    }

    RegCloseKey(hkey);
}

///////////////////////////////////////////////////////////////////////////////
// Reads the applications Recaps is to keep its hands off. They are stored one
// registry value per executable name, which makes adding and removing one a
// single call and needs no parsing.
static void LoadIgnoredApps() {
    g_ignoredCount = 0;

    HKEY hkey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, IGNORED_KEY, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
        return;
    }

    for (DWORD index = 0; g_ignoredCount < MAX_IGNORED; index++) {
        DWORD length = _countof(g_ignored[g_ignoredCount]);
        if (RegEnumValue(hkey, index, g_ignored[g_ignoredCount], &length,
                NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
            break;
        }
        if (g_ignored[g_ignoredCount][0] != L'\0') {
            g_ignoredCount++;
        }
    }

    RegCloseKey(hkey);
}

///////////////////////////////////////////////////////////////////////////////
// Tells whether `app`, an executable name, is one of those we leave alone
static BOOL IsAppIgnored(const WCHAR* app) {
    for (UINT i = 0; i < g_ignoredCount; i++) {
        if (_wcsicmp(g_ignored[i], app) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

///////////////////////////////////////////////////////////////////////////////
// Adds `app` to the list or takes it off again, and saves it
static void ToggleIgnoredApp(const WCHAR* app) {
    HKEY hkey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, IGNORED_KEY, 0, NULL, 0,
            KEY_WRITE, NULL, &hkey, NULL) != ERROR_SUCCESS) {
        return;
    }

    if (IsAppIgnored(app)) {
        RegDeleteValue(hkey, app);
    } else {
        DWORD one = 1;
        RegSetValueEx(hkey, app, 0, REG_DWORD, (const BYTE*)(&one), sizeof(one));
    }

    RegCloseKey(hkey);
    LoadIgnoredApps();
}

///////////////////////////////////////////////////////////////////////////////
// Tells whether a window is part of the shell itself - the taskbar, a second
// taskbar on another monitor, or the flyout that holds the hidden notification
// icons.
//
// Clicking our icon brings one of those to the front. That is the user
// reaching for our menu, not a change of what they were working in, so it must
// not count as the application the menu offers to ignore. A window belonging
// to explorer.exe that is NOT one of these is a real folder window, and
// ignoring that one is a perfectly reasonable thing to ask for.
static BOOL IsShellWindow(HWND hwnd) {
    static const WCHAR* shellClasses[] = {
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"NotifyIconOverflowWindow",
        L"TopLevelWindowForOverflowXamlIsland",
        L"Xaml_WindowedPopupClass",
    };

    WCHAR className[64];
    if (GetClassName(hwnd, className, _countof(className)) == 0) {
        return FALSE;
    }

    for (int i = 0; i < _countof(shellClasses); i++) {
        if (_wcsicmp(className, shellClasses[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

///////////////////////////////////////////////////////////////////////////////
// Works out which application owns the foreground window and whether we are
// meant to leave it alone.
//
// The hook procedure only reads the answer: opening a process to ask for its
// name takes far longer than a low level hook is allowed to spend. When we
// cannot tell - while our own menu is up, for one - the previous answer
// stands, which is also what lets the menu offer the application the user was
// just working in.
void UpdateForegroundApp() {
    WCHAR path[MAX_PATH] = L"";

    HWND foreground = GetForegroundWindow();
    DWORD processId = 0;
    if (foreground != NULL) {
        GetWindowThreadProcessId(foreground, &processId);
    }

    if (processId != 0 && processId != GetCurrentProcessId()) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (process != NULL) {
            DWORD size = _countof(path);
            if (!QueryFullProcessImageName(process, 0, path, &size)) {
                path[0] = L'\0';
            }
            CloseHandle(process);
        }
    }

    if (path[0] != L'\0') {
        const WCHAR* name = wcsrchr(path, PATH_SEPARATOR);
        wcscpy_s(g_foregroundApp, _countof(g_foregroundApp), name ? name + 1 : path);

        // the menu offers the last application that was not the shell
        if (!IsShellWindow(foreground)) {
            wcscpy_s(g_menuApp, _countof(g_menuApp), g_foregroundApp);
        }
    }

    InterlockedExchange(&g_ignoreActive,
        (g_foregroundApp[0] != L'\0' && IsAppIgnored(g_foregroundApp)) ? 1 : 0);
}

///////////////////////////////////////////////////////////////////////////////
// Called by Windows when a different window comes to the front. Without it we
// would only notice on the next tick of the timer, and a CapsLock pressed in
// the first moments after switching to an ignored application would still be
// swallowed.
static void CALLBACK ForegroundChanged(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
        LONG idObject, LONG idChild, DWORD threadId, DWORD time) {
    UNREFERENCED_PARAMETER(hook);
    UNREFERENCED_PARAMETER(event);
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(idObject);
    UNREFERENCED_PARAMETER(idChild);
    UNREFERENCED_PARAMETER(threadId);
    UNREFERENCED_PARAMETER(time);

    UpdateForegroundApp();
    UpdateTrayIcon(FALSE);
}

///////////////////////////////////////////////////////////////////////////////
// Stops and starts Recaps without quitting it, for the times when CapsLock has
// to be CapsLock: a game that binds it, or an application whose own shortcut
// it gets in the way of.
static void SetPaused(BOOL paused) {
    g_paused = paused;

    if (paused) {
        if (g_hHook != NULL) {
            UnhookWindowsHookEx(g_hHook);
            g_hHook = NULL;
        }
    } else {
        InstallHook();
    }

    UpdateTrayIcon(TRUE);
}

///////////////////////////////////////////////////////////////////////////////
// Program's entry point
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Prevent from two copies of Recaps from running at the same time.
    // CreateMutex only sets ERROR_ALREADY_EXISTS when the mutex was there
    // before us, so clear the last error first and nothing that ran before
    // us can be mistaken for a second copy.
    SetLastError(ERROR_SUCCESS);
    HANDLE mutex = CreateMutex(NULL, TRUE, MUTEX);
    if (mutex == NULL) {
        ShowError(L"Recaps could not create its mutex.");
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        WCHAR message[MAXLEN];
        WCHAR path[MAX_PATH] = {0};
        if (GetRunningInstancePath(path, _countof(path))) {
            swprintf_s(message, _countof(message),
                L"Recaps is already running.\n\nThe copy that is running is:\n%s"
                L"\n\nIts icon is in the notification area - right click it for its menu.",
                path);
        } else {
            wcscpy_s(message, _countof(message),
                L"Recaps is already running.\n\n"
                L"Its icon is in the notification area - right click it for its menu.");
        }
        MessageBox(NULL, message, TITLE, MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 1;
    }

    // TSF and UI Automation are COM. This thread pumps messages, so it is an
    // apartment; the conversion thread sets itself up separately.
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // Create a hidden window to listen to events. It has to be a top level
    // window rather than a message-only one: broadcast messages, and the
    // TaskbarCreated message we need in particular, are never delivered to
    // message-only windows.
    WNDCLASSEX wclx = {0};
    wclx.cbSize         = sizeof( wclx );
    wclx.lpfnWndProc    = &WindowProc;
    wclx.hInstance      = hInstance;
    wclx.lpszClassName  = WINDOWCLASS_NAME;
    RegisterClassEx(&wclx);
    g_hMessageWindow = CreateWindowEx(WS_EX_TOOLWINDOW, WINDOWCLASS_NAME, TITLE,
        WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (g_hMessageWindow == NULL) {
        ShowError(L"Recaps could not create its window.");
        CloseHandle(mutex);
        return 1;
    }

    g_uTaskbarCreated = RegisterWindowMessage(L"TaskbarCreated");

    // Initialize
    SetClipboardWindow(g_hMessageWindow);
    AddTrayIcon(g_hMessageWindow, ID_TRAYICON, APPWM_TRAYICON, IDI_MAINFRAME, TITLE);
    RefreshKeyboardLayouts(&g_keyboardInfo);
    LoadIgnoredApps();
    UpdateForegroundApp();
    UpdateTrayIcon(TRUE);

    // Notice straight away when another application comes to the front, so
    // that the ignore list takes effect on the very first keystroke in it
    g_hWinEventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, ForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // Set hook to capture CapsLock
    if (!InstallHook()) {
        ShowError(L"Recaps could not install its keyboard hook.");
    }
    SetTimer(g_hMessageWindow, IDT_HOOK_WATCHDOG, HOOK_WATCHDOG_PERIOD, NULL);
    SetTimer(g_hMessageWindow, IDT_LAYOUT_POLL, LAYOUT_POLL_PERIOD, NULL);

    // Handle messages
    MSG msg;
    BOOL result;
    while ((result = GetMessage(&msg, NULL, 0, 0)) != 0) {
        if (result == -1) {
            break; // the queue is broken, there is nothing left to wait for
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Clean up
    KillTimer(g_hMessageWindow, IDT_HOOK_WATCHDOG);
    KillTimer(g_hMessageWindow, IDT_LAYOUT_POLL);
    KillTimer(g_hMessageWindow, IDT_SWITCH_VERIFY);
    if (g_hWinEventHook != NULL) {
        UnhookWinEvent(g_hWinEventHook);
        g_hWinEventHook = NULL;
    }
    if (g_hHook != NULL) {
        UnhookWindowsHookEx(g_hHook);
        g_hHook = NULL;
    }
    SaveConfiguration(&g_keyboardInfo);
    RemoveTrayIcon(g_hMessageWindow, ID_TRAYICON);
    DestroyWindow(g_hMessageWindow);
    g_hMessageWindow = NULL;
    UnregisterClass(WINDOWCLASS_NAME, hInstance);
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Handles events at the window (both hot key and from the tray icon)
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Explorer was restarted and took our tray icon with it
    if (uMsg == g_uTaskbarCreated && g_uTaskbarCreated != 0) {
        AddTrayIcon(hWnd, ID_TRAYICON, APPWM_TRAYICON, IDI_MAINFRAME, TITLE);
        UpdateTrayIcon(TRUE);
        return 0;
    }

    switch (uMsg) {
    case APPWM_TRAYICON:
        return OnTrayIcon(hWnd, wParam, lParam);

    // The messages the keyboard hook posts. Doing this work here instead of in
    // the hook procedure keeps the hook fast enough for Windows not to drop it,
    // and lets it talk to a foreground window that may be busy.
    case APPWM_SWITCH_LAYOUT:
        RefreshKeyboardLayoutsIfChanged(&g_keyboardInfo);
        SwitchLayout();
        return 0;

    case APPWM_SWITCH_AND_CONVERT:
        RefreshKeyboardLayoutsIfChanged(&g_keyboardInfo);
        StartSwitchAndConvert();
        return 0;

    case APPWM_SWITCH_TO_NUMBER:
        RefreshKeyboardLayoutsIfChanged(&g_keyboardInfo);
        SwitchToLayoutNumber((int)wParam);
        return 0;

    // SetTimer wants the window to belong to the calling thread, and the
    // conversion thread switches layouts too, so the timer is set here
    case APPWM_VERIFY_SWITCH:
        SetTimer(hWnd, IDT_SWITCH_VERIFY, SWITCH_VERIFY_DELAY, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == IDT_HOOK_WATCHDOG) {
            CheckHook();
            return 0;
        }
        if (wParam == IDT_SWITCH_VERIFY) {
            KillTimer(hWnd, IDT_SWITCH_VERIFY);
            VerifySwitch();
            return 0;
        }
        if (wParam == IDT_LAYOUT_POLL) {
            // the event hook above is the quick way; this is the backstop,
            // and it is also what notices a layout changed with Win+Space
            UpdateForegroundApp();
            UpdateTrayIcon(FALSE);
            return 0;
        }
        break;

    // The user added, removed or renamed a keyboard layout
    case WM_SETTINGCHANGE:
    case WM_INPUTLANGCHANGE:
        RefreshKeyboardLayouts(&g_keyboardInfo);
        // the taskbar may have gone from light to dark as well
        UpdateTrayIcon(TRUE);
        return 0;

    case WM_COMMAND:
        return OnCommand(hWnd, LOWORD(wParam), (HWND)lParam);

    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////////
// Create and display a popup menu when the user right-clicks on the icon
int OnTrayIcon(HWND hWnd, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(wParam);

    if (g_modalShown == TRUE) {
        return 0;
    }

    switch (lParam) {
    case WM_RBUTTONUP:
        // Show the context menu
        ShowPopupMenu(hWnd);
        return 0;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Handles user commands from the menu
int OnCommand(HWND hWnd, WORD wID, HWND hCtl) {
    UNREFERENCED_PARAMETER(hCtl);

    //  Have a look at the command and act accordingly
    if (wID == ID_EXIT) {
        PostMessage( hWnd, WM_CLOSE, 0, 0 );
    } else if (wID == ID_ABOUT) {
        MessageBox(NULL, HELP_MESSAGE, HELP_TITLE, MB_OK | MB_ICONINFORMATION);
    } else if (wID == ID_PAUSE) {
        SetPaused(!g_paused);
    } else if (wID == ID_AUTOSTART) {
        SetAutostart(!IsAutostartEnabled());
    } else if (wID == ID_IGNORE_APP) {
        if (g_menuApp[0] != L'\0') {
            ToggleIgnoredApp(g_menuApp);
            UpdateForegroundApp();
        }
    } else {
        for (UINT i = 0; i < g_keyboardInfo.count; i++) {
            if (g_keyboardInfo.menuIds[i] == wID) {
                g_keyboardInfo.inUse[i] = ! (g_keyboardInfo.inUse[i]);
                break;
            }
        }
        SaveConfiguration(&g_keyboardInfo);
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Create and display a popup menu when the user right-clicks on the icon
BOOL ShowPopupMenu(HWND hWnd) {
    int i = 0;

    RefreshKeyboardLayoutsIfChanged(&g_keyboardInfo);

    // Create the menu
    HMENU hPop = CreatePopupMenu();
    InsertMenu(hPop, i++, MF_BYPOSITION | MF_STRING, ID_ABOUT, L"About");
    InsertMenu(hPop, i++, MF_SEPARATOR, 0, NULL);

    InsertMenu(hPop, i++, MF_BYPOSITION | MF_STRING | (g_paused ? MF_CHECKED : 0),
        ID_PAUSE, L"Paused");
    InsertMenu(hPop, i++, MF_BYPOSITION | MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0),
        ID_AUTOSTART, L"Start with Windows");

    // An entry for whichever application was in front when the menu opened,
    // so that keeping Recaps out of it is one click and needs no dialog and
    // no typing of executable names.
    if (g_menuApp[0] != L'\0') {
        WCHAR caption[MAX_PATH + 32];
        _snwprintf_s(caption, _countof(caption), _TRUNCATE, L"Ignore %s", g_menuApp);
        InsertMenu(hPop, i++, MF_BYPOSITION | MF_STRING | (IsAppIgnored(g_menuApp) ? MF_CHECKED : 0),
            ID_IGNORE_APP, caption);
    }

    InsertMenu(hPop, i++, MF_SEPARATOR, 0, NULL);

    // Add items for the languages
    for (UINT layout = 0; layout < g_keyboardInfo.count; layout++) {
        UINT flags = MF_BYPOSITION | MF_STRING;
        if (g_keyboardInfo.inUse[layout]) {
            flags |= MF_CHECKED;
        }
        InsertMenu(hPop, i++, flags, ID_LANG+layout, g_keyboardInfo.names[layout]);
        g_keyboardInfo.menuIds[layout] = ID_LANG+layout;
    }

    InsertMenu(hPop, i++, MF_SEPARATOR, 0, NULL);
    InsertMenu(hPop, i++, MF_BYPOSITION | MF_STRING, ID_EXIT, L"Exit");

    // Show the menu

    // See http://support.microsoft.com/kb/135788 for the reasons 
    // for the SetForegroundWindow and Post Message trick.
    POINT curpos;
    GetCursorPos(&curpos);
    SetForegroundWindow(hWnd);
    g_modalShown = TRUE;
    BOOL cmd = (WORD)TrackPopupMenu(
        hPop, TPM_LEFTALIGN|TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,
        curpos.x, curpos.y, 0, hWnd, NULL
        );
    PostMessage(hWnd, WM_NULL, 0, 0);
    g_modalShown = FALSE;

    // Send a command message to the window to handle the menu item the user chose
    if (cmd) {
        SendMessage(hWnd, WM_COMMAND, cmd, 0);
    }

    DestroyMenu(hPop);

    return cmd;
}

///////////////////////////////////////////////////////////////////////////////
// Finds the KLID - the eight hex digit name a keyboard layout goes by in the
// registry, such as "00010419" - of the layout with the given "Layout Id".
static BOOL FindKlidByLayoutId(WORD layoutId, WCHAR* klid, size_t size) {
    HKEY hLayouts;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, KEYBOARD_LAYOUTS_KEY, 0, KEY_READ, &hLayouts) != ERROR_SUCCESS) {
        return FALSE;
    }

    BOOL found = FALSE;
    for (DWORD index = 0; !found; index++) {
        WCHAR subKeyName[64];
        DWORD subKeyLength = _countof(subKeyName);
        if (RegEnumKeyEx(hLayouts, index, subKeyName, &subKeyLength, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
            break;
        }

        HKEY hLayout;
        if (RegOpenKeyEx(hLayouts, subKeyName, 0, KEY_READ, &hLayout) != ERROR_SUCCESS) {
            continue;
        }

        WCHAR value[16] = {0};
        DWORD type = 0;
        DWORD length = sizeof(value) - sizeof(WCHAR);
        if (RegQueryValueEx(hLayout, L"Layout Id", NULL, &type, (BYTE*)value, &length) == ERROR_SUCCESS &&
            type == REG_SZ &&
            (WORD)wcstoul(value, NULL, 16) == layoutId) {
            found = wcscpy_s(klid, size, subKeyName) == 0;
        }
        RegCloseKey(hLayout);
    }

    RegCloseKey(hLayouts);
    return found;
}

///////////////////////////////////////////////////////////////////////////////
// Builds the KLID of an HKL.
//
// The low word of an HKL is the language id. The high word identifies the
// keyboard layout: zero for the default layout of that language, a small
// number for one of the layouts that ship with Windows, and 0xFxxx for a
// layout that is only identified by a "Layout Id" value in the registry.
static void HklToKlid(HKL hkl, WCHAR* klid, size_t size) {
    WORD language = LOWORD((ULONG_PTR)hkl);
    WORD layout = HIWORD((ULONG_PTR)hkl);

    if ((layout & 0xF000) == 0xF000 && FindKlidByLayoutId((WORD)(layout & 0x0FFF), klid, size)) {
        return;
    }

    if (swprintf_s(klid, size, L"%04X%04X", layout, language) < 0) {
        klid[0] = L'\0';
    }
}

///////////////////////////////////////////////////////////////////////////////
// Reads the name a keyboard layout has of its own, such as
// "Russian - Typewriter", from the registry.
static BOOL GetLayoutText(const WCHAR* klid, WCHAR* text, size_t size) {
    if (klid[0] == L'\0') {
        return FALSE;
    }

    WCHAR keyName[MAXLEN];
    if (swprintf_s(keyName, MAXLEN, L"%s\\%s", KEYBOARD_LAYOUTS_KEY, klid) < 0) {
        return FALSE;
    }

    HKEY hkey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyName, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
        return FALSE;
    }

    // Zero the buffer and leave room for a terminator: RegQueryValueEx does not
    // promise that the string it hands back is terminated.
    ZeroMemory(text, size * sizeof(WCHAR));
    DWORD type = 0;
    DWORD length = (DWORD)((size - 1) * sizeof(WCHAR));
    BOOL result = RegQueryValueEx(hkey, L"Layout Text", NULL, &type, (BYTE*)text, &length) == ERROR_SUCCESS &&
                  type == REG_SZ && text[0] != L'\0';
    RegCloseKey(hkey);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
// Fills in the names we show in the menu. Two layouts of the same language -
// "Russian" and "Russian - Typewriter", say - share one language name, so the
// name of the layout itself is appended whenever a language has more than one
// layout installed and the entries would be indistinguishable otherwise.
static void BuildLayoutNames(KeyboardLayoutInfo* info) {
    for (UINT i = 0; i < info->count; i++) {
        WORD language = LOWORD((ULONG_PTR)info->hkls[i]);
        LCID locale = MAKELCID(language, SORT_DEFAULT);
        if (GetLocaleInfo(locale, LOCALE_SLANGUAGE, info->names[i], MAXLEN) == 0) {
            swprintf_s(info->names[i], MAXLEN, L"%s", info->klids[i]);
        }

        BOOL ambiguous = FALSE;
        for (UINT j = 0; j < info->count; j++) {
            if (j != i && LOWORD((ULONG_PTR)info->hkls[j]) == language) {
                ambiguous = TRUE;
                break;
            }
        }
        if (!ambiguous) {
            continue;
        }

        WCHAR layoutText[MAXLEN];
        if (GetLayoutText(info->klids[i], layoutText, MAXLEN)) {
            WCHAR combined[MAXLEN];
            if (swprintf_s(combined, MAXLEN, L"%s (%s)", info->names[i], layoutText) > 0) {
                wcscpy_s(info->names[i], MAXLEN, combined);
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// Fills ``info`` with the currently installed keyboard layouts and with the
// configuration saved for them.
// Based on http://blogs.msdn.com/michkap/archive/2004/12/05/275231.aspx.
void RefreshKeyboardLayouts(KeyboardLayoutInfo* info) {
    UINT current = info->current;

    memset(info, 0, sizeof(KeyboardLayoutInfo));
    info->current = current;
    info->count = GetKeyboardLayoutList(MAX_LAYOUTS, info->hkls);

    for (UINT i = 0; i < info->count; i++) {
        HklToKlid(info->hkls[i], info->klids[i], KL_NAMELENGTH);
        info->inUse[i] = TRUE;
    }

    BuildLayoutNames(info);
    LoadConfiguration(info);
}

///////////////////////////////////////////////////////////////////////////////
// Re-reads the installed keyboard layouts if the user added or removed one
// since we last looked. Windows has no notification for this that we can rely
// on, and the list is cheap to ask for, so we just compare it with ours.
void RefreshKeyboardLayoutsIfChanged(KeyboardLayoutInfo* info) {
    HKL hkls[MAX_LAYOUTS] = {0};
    UINT count = GetKeyboardLayoutList(MAX_LAYOUTS, hkls);

    if (count == info->count && memcmp(hkls, info->hkls, count * sizeof(HKL)) == 0) {
        return;
    }

    PrintDebugString("The installed keyboard layouts changed, re-reading them");
    RefreshKeyboardLayouts(info);
}

///////////////////////////////////////////////////////////////////////////////
// Load currently active keyboard layouts from the registry
void LoadConfiguration(KeyboardLayoutInfo* info) {
    HKEY hkey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, CONFIG_KEY, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
        return;
    }

    // Load current isUse value for each language
    for (UINT i = 0; i < info->count; i++) {
        DWORD data = 0;
        DWORD type = 0;
        DWORD length = sizeof(data);
        LONG result = RegQueryValueEx(hkey, info->klids[i], 0, &type, (BYTE*)(&data), &length);
        if (result != ERROR_SUCCESS || type != REG_DWORD) {
            // Settings used to be keyed by the localized name of the language,
            // which changed along with the Windows display language and could
            // not tell two layouts of one language apart. Fall back to the old
            // name so that a configuration written by an earlier version of
            // Recaps still counts.
            type = 0;
            length = sizeof(data);
            result = RegQueryValueEx(hkey, info->names[i], 0, &type, (BYTE*)(&data), &length);
        }
        if (result == ERROR_SUCCESS && type == REG_DWORD) {
            info->inUse[i] = (data != 0);
        }
    }

    RegCloseKey(hkey);
}

///////////////////////////////////////////////////////////////////////////////
// Saves currently active keyboard layouts to the registry
void SaveConfiguration(const KeyboardLayoutInfo* info) {
    HKEY hkey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, CONFIG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) != ERROR_SUCCESS) {
        return;
    }

    // Save current isUse value for each language
    for (UINT i = 0; i < info->count; i++) {
        if (info->klids[i][0] == L'\0') {
            continue;
        }
        DWORD data = info->inUse[i] ? 1 : 0;
        RegSetValueEx(hkey, info->klids[i], 0, REG_DWORD, (CONST BYTE*)(&data), sizeof(DWORD));
    }

    RegCloseKey(hkey);
}

///////////////////////////////////////////////////////////////////////////////
// Finds out which window has the focus.
//
// GUITHREADINFO rather than AttachThreadInput, which this used to use:
// attaching our input queue to the queue of another process blocks while that
// process is busy and affects how focus behaves in both of them.
HWND RemoteGetFocus() {
    HWND foreground = GetForegroundWindow();
    if (foreground == NULL) {
        return NULL;
    }

    GUITHREADINFO gui = {0};
    gui.cbSize = sizeof(gui);
    DWORD threadId = GetWindowThreadProcessId(foreground, NULL);
    if (GetGUIThreadInfo(threadId, &gui) && gui.hwndFocus != NULL) {
        return gui.hwndFocus;
    }

    return foreground;
}

///////////////////////////////////////////////////////////////////////////////
// Returns the current layout in the active window
HKL GetCurrentLayout() {
    HWND hwnd = RemoteGetFocus();
    DWORD threadId = GetWindowThreadProcessId(hwnd, NULL);
    return GetKeyboardLayout(threadId);
}

///////////////////////////////////////////////////////////////////////////////
// Picks the colour to draw the language with: white on the dark taskbar, black
// on the light one.
static COLORREF GetTrayTextColor() {
    DWORD lightTheme = 0;
    DWORD type = 0;
    DWORD length = sizeof(lightTheme);

    HKEY hkey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, THEME_KEY, 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        if (RegQueryValueEx(hkey, L"SystemUsesLightTheme", NULL, &type, (BYTE*)(&lightTheme), &length) != ERROR_SUCCESS ||
            type != REG_DWORD) {
            lightTheme = 0;
        }
        RegCloseKey(hkey);
    }

    return lightTheme ? RGB(0, 0, 0) : RGB(255, 255, 255);
}

///////////////////////////////////////////////////////////////////////////////
// Shows the language of the active window on the tray icon, so that the icon
// says something rather than being a logo that never changes.
//
// This runs on a timer because the layout also changes without us - with
// Win+Space, or from the language bar - and there is no notification we could
// subscribe to for another window.
void UpdateTrayIcon(BOOL force) {
    HKL layout = GetCurrentLayout();
    if (layout == NULL || (!force && layout == g_shownLayout)) {
        return;
    }
    g_shownLayout = layout;

    LANGID language = LOWORD((ULONG_PTR)layout);
    LCID locale = MAKELCID(language, SORT_DEFAULT);

    WCHAR abbreviation[16];
    if (GetLocaleInfo(locale, LOCALE_SISO639LANGNAME, abbreviation, _countof(abbreviation)) == 0) {
        wcscpy_s(abbreviation, _countof(abbreviation), L"?");
    }
    CharUpperBuff(abbreviation, (DWORD)wcslen(abbreviation));

    // the full name of the layout goes into the tooltip
    WCHAR tooltip[128];
    const WCHAR* name = NULL;
    for (UINT i = 0; i < g_keyboardInfo.count; i++) {
        if (g_keyboardInfo.hkls[i] == layout) {
            name = g_keyboardInfo.names[i];
            break;
        }
    }
    _snwprintf_s(tooltip, _countof(tooltip), _TRUNCATE, L"Recaps%s%s%s",
        name != NULL ? L" - " : L"",
        name != NULL ? name : L"",
        g_paused ? L" (paused)" : L"");

    // grey while paused, so that the icon says at a glance that CapsLock is
    // CapsLock again
    COLORREF color = g_paused ? RGB(128, 128, 128) : GetTrayTextColor();
    SetTrayIconText(g_hMessageWindow, ID_TRAYICON, abbreviation, color, tooltip);
}

///////////////////////////////////////////////////////////////////////////////
// Switches the input of the whole desktop to `hkl` through the Text Services
// Framework.
//
// WM_INPUTLANGCHANGEREQUEST only reaches windows that run a classic message
// loop, so it does nothing in UWP and WinUI applications - Settings, the Store,
// anything built on XAML - while TSF is how Windows itself switches the input
// profile. The profile is activated for the whole desktop, which is what
// Windows 10 and 11 do by default: one input language for everything rather
// than one per window.
static BOOL ActivateLayoutViaTsf(HKL hkl) {
    ITfInputProcessorProfiles* profiles = NULL;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, (void**)&profiles);
    if (FAILED(hr) || profiles == NULL) {
        return FALSE;
    }

    ITfInputProcessorProfileMgr* profileMgr = NULL;
    hr = profiles->QueryInterface(IID_ITfInputProcessorProfileMgr, (void**)&profileMgr);
    if (FAILED(hr) || profileMgr == NULL) {
        profiles->Release();
        return FALSE;
    }

    LANGID language = LOWORD((ULONG_PTR)hkl);

    // Move the input language first. ActivateProfile picks which layout a
    // language uses; on its own it is documented to fail when the language
    // asked for is not the current one, and the flag that stops it failing
    // only marks the profile for later rather than switching now.
    profiles->ChangeCurrentLanguage(language);
    hr = profileMgr->ActivateProfile(TF_PROFILETYPE_KEYBOARDLAYOUT, language,
        CLSID_NULL, GUID_NULL, hkl, TF_IPPMF_FORSESSION);
    if (FAILED(hr)) {
        // Without this flag the call is documented to fail when the language
        // being asked for is not the one that is currently active, which is
        // exactly the case every time we switch.
        hr = profileMgr->ActivateProfile(TF_PROFILETYPE_KEYBOARDLAYOUT, language,
            CLSID_NULL, GUID_NULL, hkl,
            TF_IPPMF_FORSESSION | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
    }
    profileMgr->Release();
    profiles->Release();

    if (FAILED(hr)) {
        PrintDebugString("ActivateProfile failed with 0x%08lX", hr);
        return FALSE;
    }
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Activates the selected language
HKL ChangeInputLanguage(UINT newLanguage) {
    HWND hwnd = RemoteGetFocus();
    if (hwnd == NULL) {
        return NULL;
    }

    g_keyboardInfo.current = newLanguage;
    HKL hkl = g_keyboardInfo.hkls[g_keyboardInfo.current];

    // Ask through TSF first, which is what reaches modern applications, and
    // post the message as well for the windows that only understand that.
    // Both ask for the same layout, so asking twice costs nothing.
    g_switchFrom = GetCurrentLayout();
    g_switchTo = hkl;
    ActivateLayoutViaTsf(hkl);
    PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)hkl);

    // and look in a moment at whether any of that was listened to
    PostMessage(g_hMessageWindow, APPWM_VERIFY_SWITCH, 0, 0);
#ifdef _DEBUG
    PrintDebugString("Language set to %S", g_keyboardInfo.names[g_keyboardInfo.current]);
#endif
    return hkl;
}

///////////////////////////////////////////////////////////////////////////////
// Looks at whether the layout we asked for actually took, and reaches for the
// system shortcut when nothing moved at all.
//
// Some windows act on neither the message nor on what TSF was asked for - the
// XAML windows the shell is built out of - and for those the shortcut Windows
// handles itself is the only thing left to try.
//
// It does NOT rescue the Windows Security credential prompt, which is what
// this was written for. That window was measured: it ignores the layout
// change message, it ignores an injected Win+Space that works everywhere
// else, and the keystrokes typed into it never reach a low level hook at all.
// Input into a password prompt is protected, so nothing here can run for it.
// This fallback is kept for the windows that are merely stubborn rather than
// protected, and it has not been shown to rescue one of those either.
//
// The fallback only fires when the layout is still exactly the one we started
// from, so a switch that did work is never disturbed, and it happens at most
// once per keystroke.
void VerifySwitch() {
    HKL target = g_switchTo;
    HKL source = g_switchFrom;
    g_switchTo = NULL;
    g_switchFrom = NULL;

    if (target == NULL || source == NULL || target == source) {
        return;
    }
    if (g_converting) {
        return; // a conversion is busy sending keystrokes of its own
    }

    HKL current = GetCurrentLayout();
    if (current == target || current != source) {
        return; // it worked, or something else moved it in the meantime
    }

    PrintDebugString("The window ignored the layout change, using the system shortcut");
    SendSystemLayoutShortcut();
}

///////////////////////////////////////////////////////////////////////////////
// Switches the current language
HKL SwitchLayout() {
    if (g_keyboardInfo.count == 0) {
        return NULL;
    }

    HKL currentLayout = GetCurrentLayout();

    // Find the current keyboard layout's index
    UINT currentLanguageIndex = 0;
    for (UINT i = 0; i < g_keyboardInfo.count; i++) {
        if (g_keyboardInfo.hkls[i] == currentLayout) {
            currentLanguageIndex = i;
            break;
        }
    }

    // Find the next active layout
    BOOL found = FALSE;
    UINT newLanguage = currentLanguageIndex;
    for (UINT i = 0; i < g_keyboardInfo.count; i++) {
        newLanguage = (newLanguage + 1) % g_keyboardInfo.count;
        if (g_keyboardInfo.inUse[newLanguage]) {
            found = TRUE;
            break;
        }
    }

    return found ? ChangeInputLanguage(newLanguage) : NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Switch to active layout with desired number if it is not selected yet.
void SwitchToLayoutNumber(int number) {
    HKL currentLayout = GetCurrentLayout();
    BOOL found = FALSE;
    UINT languageIndex = 0;
    for (UINT i = 0; i < g_keyboardInfo.count; i++) {
        if (!g_keyboardInfo.inUse[i]) {
            continue;
        }
        if (number-- == 0) {
            if (g_keyboardInfo.hkls[i] != currentLayout) {
                found = TRUE;
                languageIndex = i;
            }
            break;
        }
    }

    if (found) {
        ChangeInputLanguage(languageIndex);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Converts current selection or the entire text to the current keyboard layout
void SwitchAndConvert(void*) {
    // This thread has no message loop, so it joins the multi threaded
    // apartment, which is what UI Automation wants from a client anyway.
    HRESULT comInitialized = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    HKL sourceLayout = GetCurrentLayout();
    HKL targetLayout = SwitchLayout();
    if (targetLayout != NULL) {
        ConvertSelectedTextInActiveWindow(sourceLayout, targetLayout);
    }

    if (SUCCEEDED(comInitialized)) {
        CoUninitialize();
    }
    InterlockedExchange(&g_converting, 0);
}

///////////////////////////////////////////////////////////////////////////////
// Starts a conversion in another thread, since it simulates the keystrokes
// that copy and paste the text and has to wait for the other application to
// react to them. Only one conversion runs at a time: two of them would fight
// over the clipboard.
void StartSwitchAndConvert() {
    if (InterlockedCompareExchange(&g_converting, 1, 0) != 0) {
        return;
    }
    if (_beginthread(SwitchAndConvert, 0, NULL) == (uintptr_t)(-1)) {
        InterlockedExchange(&g_converting, 0);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Installs the low level keyboard hook
BOOL InstallHook() {
    if (g_hHook != NULL) {
        return TRUE;
    }

    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelHookProc, GetModuleHandle(NULL), 0);
    if (g_hHook == NULL) {
        PrintDebugString("SetWindowsHookEx failed with error %lu", GetLastError());
        return FALSE;
    }
    return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Makes sure our keyboard hook is still there.
//
// Windows removes a low level keyboard hook whose procedure takes longer than
// LowLevelHooksTimeout to return and tells nobody about it, and there is no API
// to ask whether a hook is still installed. So we put it back from time to
// time: a hook that is still alive only moves to the front of the hook chain,
// and no keystroke is lost, since the new hook is in place before the old one
// is removed. We skip the whole thing while nobody is using the machine, so
// that we never keep it from going idle.
void CheckHook() {
    if (g_paused) {
        return; // the hook is meant to be gone
    }

    if (g_hHook != NULL) {
        LASTINPUTINFO lastInput = {0};
        lastInput.cbSize = sizeof(lastInput);
        if (GetLastInputInfo(&lastInput)) {
            if (lastInput.dwTime == g_lastCheckedInput) {
                return;
            }
            g_lastCheckedInput = lastInput.dwTime;
        }
    }

    HHOOK previous = g_hHook;
    HHOOK fresh = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelHookProc, GetModuleHandle(NULL), 0);
    if (fresh == NULL) {
        PrintDebugString("Reinstalling the keyboard hook failed with error %lu", GetLastError());
        return;
    }

    g_hHook = fresh;
    if (previous != NULL) {
        UnhookWindowsHookEx(previous);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Checks key state
BOOL IsKeyPressed(int nVirtKey) {
    // GetAsyncKeyState, not GetKeyState: inside a low level hook we are after
    // the physical state of the key, not the state our own message queue has
    // caught up with.
    return (GetAsyncKeyState(nVirtKey) & 0x8000) != 0;
}

BOOL g_lMenuKeyPressed = FALSE;
BOOL g_lShiftKeyPressed = FALSE;
BOOL g_rMenuKeyPressed = FALSE;
BOOL g_rShiftKeyPressed = FALSE;

// Set when we swallowed a CapsLock key down, so that we know to swallow the
// matching key up as well
BOOL g_capsDownSwallowed = FALSE;

///////////////////////////////////////////////////////////////////////////////
// Remembers that a key other than Alt and Shift went by while one of them was
// held down, which is what tells an Alt-Shift layout switch apart from an
// Alt-Shift-something shortcut
static void MarkOtherKeyPressed() {
    if (IsKeyPressed(VK_LMENU)) {
        g_lMenuKeyPressed = TRUE;
    }
    if (IsKeyPressed(VK_LSHIFT)) {
        g_lShiftKeyPressed = TRUE;
    }
    if (IsKeyPressed(VK_RMENU)) {
        g_rMenuKeyPressed = TRUE;
    }
    if (IsKeyPressed(VK_RSHIFT)) {
        g_rShiftKeyPressed = TRUE;
    }
}

///////////////////////////////////////////////////////////////////////////////
// A LowLevelHookProc implementation that captures the CapsLock key.
//
// Everything in here has to be quick: Windows gives a low level hook procedure
// only LowLevelHooksTimeout milliseconds to return and silently removes the
// hook when it is slower, so the work itself is posted to our own window.
LRESULT CALLBACK LowLevelHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) return CallNextHookEx(g_hHook, nCode, wParam, lParam);

    KBDLLHOOKSTRUCT* data = (KBDLLHOOKSTRUCT*)lParam;

    // ignore injected keystrokes
    if ((data->flags & LLKHF_INJECTED) == 0) {
        //WM_KEYDOWN WM_KEYUP WM_SYSKEYDOWN WM_SYSKEYUP
        if (wParam == WM_KEYDOWN) {
            switch (data->vkCode) {
            case VK_CAPITAL:
                if (g_ignoreActive) {
                    // this application is on the ignore list, so CapsLock
                    // is simply CapsLock here
                    break;
                }
                if (IsKeyPressed(VK_CONTROL)) {
                    // Handle Ctrl-CapsLock - switch current layout and convert
                    // the text in the current field
                    PostMessage(g_hMessageWindow, APPWM_SWITCH_AND_CONVERT, 0, 0);
                } else {
                    // Handle CapsLock - only switch current layout
                    PostMessage(g_hMessageWindow, APPWM_SWITCH_LAYOUT, 0, 0);
                }
                g_capsDownSwallowed = TRUE;
                return 1; // prevent windows from handling the keystroke
            default:
                break;
            }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            switch (data->vkCode) {
            case VK_CAPITAL:
                MarkOtherKeyPressed();
                if (g_capsDownSwallowed) {
                    // we swallowed the key down, so swallow the key up too -
                    // otherwise applications see a release without a press
                    g_capsDownSwallowed = FALSE;
                    return 1;
                }
                break;
            case VK_LMENU:
                if (!g_ignoreActive && !g_lMenuKeyPressed && IsKeyPressed(VK_LMENU) && IsKeyPressed(VK_LSHIFT) && !IsKeyPressed(VK_CONTROL)) {
                    PostMessage(g_hMessageWindow, APPWM_SWITCH_TO_NUMBER, 0, 0);
                }
                g_lMenuKeyPressed = FALSE;
                break;
            case VK_LSHIFT:
                if (!g_ignoreActive && !g_lShiftKeyPressed && IsKeyPressed(VK_LMENU) && IsKeyPressed(VK_LSHIFT) && !IsKeyPressed(VK_CONTROL)) {
                    PostMessage(g_hMessageWindow, APPWM_SWITCH_TO_NUMBER, 0, 0);
                }
                g_lShiftKeyPressed = FALSE;
                break;
            case VK_RMENU:
                if (!g_ignoreActive && !g_rMenuKeyPressed && IsKeyPressed(VK_RMENU) && IsKeyPressed(VK_RSHIFT) && !IsKeyPressed(VK_CONTROL)) {
                    PostMessage(g_hMessageWindow, APPWM_SWITCH_TO_NUMBER, 1, 0);
                }
                g_rMenuKeyPressed = FALSE;
                break;
            case VK_RSHIFT:
                if (!g_ignoreActive && !g_rShiftKeyPressed && IsKeyPressed(VK_RMENU) && IsKeyPressed(VK_RSHIFT) && !IsKeyPressed(VK_CONTROL)) {
                    PostMessage(g_hMessageWindow, APPWM_SWITCH_TO_NUMBER, 1, 0);
                }
                g_rShiftKeyPressed = FALSE;
                break;
            default:
                MarkOtherKeyPressed();
                break;
            }
        }
    }

    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}
