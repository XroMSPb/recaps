#pragma once

//  Prototypes
void    AddTrayIcon( HWND hWnd, UINT uID, UINT uCallbackMsg, UINT uIcon, LPCWSTR pszToolTip );
void    RemoveTrayIcon( HWND hWnd, UINT uID);
void    ModifyTrayIcon( HWND hWnd, UINT uID, UINT uIcon, LPCWSTR pszToolTip );
HICON   LoadSmallIcon( HINSTANCE hInstance, UINT uID );
HICON   CreateTextIcon( LPCWSTR text, COLORREF color );
void    SetTrayIconText( HWND hWnd, UINT uID, LPCWSTR text, COLORREF color, LPCWSTR pszToolTip );
