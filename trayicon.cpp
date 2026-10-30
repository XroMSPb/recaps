#include "stdafx.h"
#include "trayicon.h"

void AddTrayIcon(HWND hWnd, UINT uID, UINT uCallbackMsg, UINT uIcon, LPCWSTR pszToolTip)
{
    NOTIFYICONDATA  nid     = {0};
    nid.cbSize              = sizeof(nid);
    nid.hWnd                = hWnd;
    nid.uID                 = uID;
    nid.uFlags              = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage    = uCallbackMsg;
    nid.hIcon               = LoadSmallIcon(GetModuleHandle(NULL), uIcon);
    wcscpy_s(nid.szTip, _countof(nid.szTip), pszToolTip);
    Shell_NotifyIcon(NIM_ADD, &nid);

    // the shell keeps a copy of the icon, so ours can go
    if (nid.hIcon)
        DestroyIcon(nid.hIcon);
}

void ModifyTrayIcon( HWND hWnd, UINT uID, UINT uIcon, LPCWSTR pszToolTip )
{
    NOTIFYICONDATA  nid = {0};
    nid.cbSize  = sizeof(nid);
    nid.hWnd    = hWnd;
    nid.uID     = uID;

    if (uIcon != (UINT)-1) 
    {
        nid.hIcon = LoadSmallIcon(GetModuleHandle( NULL ), uIcon);
        nid.uFlags |= NIF_ICON;
    }

    if (pszToolTip) 
    {
        wcscpy_s(nid.szTip, _countof(nid.szTip), pszToolTip);
        nid.uFlags |= NIF_TIP;
    }

    if (uIcon != (UINT)-1 || pszToolTip)
        Shell_NotifyIcon(NIM_MODIFY, &nid);

    if (nid.hIcon)
        DestroyIcon(nid.hIcon);
}

void RemoveTrayIcon(HWND hWnd, UINT uID)
{
    NOTIFYICONDATA nid = {0};
    nid.cbSize  = sizeof( nid );
    nid.hWnd    = hWnd;
    nid.uID     = uID;
    Shell_NotifyIcon( NIM_DELETE, &nid );
}

HICON LoadSmallIcon(HINSTANCE hInstance, UINT uID)
{
    // ask for the size the shell wants a small icon to be rather than a fixed
    // 16x16, which is wrong on a high DPI display
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0)
        size = 16;

    return (HICON)LoadImage(hInstance, MAKEINTRESOURCE(uID), IMAGE_ICON, size, size, 0);
}

///////////////////////////////////////////////////////////////////////////////
// Draws a small icon that says `text`, so that the notification area can show
// which language is active instead of a logo that never changes.
//
// The icon is built the classic way, from a colour bitmap and an AND mask: a
// mask bit of one leaves the pixel transparent, a zero lets the colour through.
// Drawing the same text into both, black on black for the mask, puts the
// letters in the colour and leaves the rest of the square see-through.
// Antialiasing is off on purpose - the mask is monochrome and could not
// describe half transparent pixels, so smoothed edges would come out as fringe.
HICON CreateTextIcon(LPCWSTR text, COLORREF color)
{
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0)
        size = 16;

    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    RECT rect = {0, 0, size, size};

    HFONT font = CreateFont(-(size * 7 / 10), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(memDC, font);
    SetBkMode(memDC, TRANSPARENT);

    // the mask: white (one, transparent) everywhere, black (zero) under the text
    HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, mask);
    FillRect(memDC, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetTextColor(memDC, RGB(0, 0, 0));
    DrawText(memDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(memDC, oldBitmap);

    // the colours: black everywhere, the text in the colour we were given
    HBITMAP colorBitmap = CreateCompatibleBitmap(screenDC, size, size);
    oldBitmap = (HBITMAP)SelectObject(memDC, colorBitmap);
    FillRect(memDC, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetTextColor(memDC, color);
    DrawText(memDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(memDC, oldBitmap);

    SelectObject(memDC, oldFont);

    ICONINFO iconInfo = {0};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = mask;
    iconInfo.hbmColor = colorBitmap;
    HICON icon = CreateIconIndirect(&iconInfo);

    DeleteObject(colorBitmap);
    DeleteObject(mask);
    DeleteObject(font);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);

    return icon;
}

///////////////////////////////////////////////////////////////////////////////
// Replaces the tray icon with one that shows `text` and updates its tooltip
void SetTrayIconText(HWND hWnd, UINT uID, LPCWSTR text, COLORREF color, LPCWSTR pszToolTip)
{
    NOTIFYICONDATA nid = {0};
    nid.cbSize  = sizeof(nid);
    nid.hWnd    = hWnd;
    nid.uID     = uID;
    nid.uFlags  = NIF_ICON;
    nid.hIcon   = CreateTextIcon(text, color);

    if (pszToolTip)
    {
        wcscpy_s(nid.szTip, _countof(nid.szTip), pszToolTip);
        nid.uFlags |= NIF_TIP;
    }

    if (nid.hIcon)
    {
        Shell_NotifyIcon(NIM_MODIFY, &nid);
        DestroyIcon(nid.hIcon);
    }
}
