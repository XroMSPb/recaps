// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#if !defined(AFX_STDAFX_H__A9DB83DB_A9FD_11D0_BFD1_444553540000__INCLUDED_)
#define AFX_STDAFX_H__A9DB83DB_A9FD_11D0_BFD1_444553540000__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#include <tchar.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <windows.h>

// WIN32_LEAN_AND_MEAN keeps OLE out of windows.h, so the COM bits that the
// Text Services Framework and UI Automation need are pulled in by hand
#include <objbase.h>
#include <msctf.h>
#include <uiautomation.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <process.h>

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STDAFX_H__A9DB83DB_A9FD_11D0_BFD1_444553540000__INCLUDED_)
