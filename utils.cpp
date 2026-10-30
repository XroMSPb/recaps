#include "stdafx.h"

#define BUFSIZE 2048

///////////////////////////////////////////////////////////////////////////////
// Shows a system error message
void ShowError(const WCHAR* message)
{
	WCHAR buffer[BUFSIZE];

	LPWSTR errMessage = NULL;
	DWORD errMessageLength = FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&errMessage,
		0,
		NULL 
	);

	buffer[0] = '\0';
	wcscat_s(buffer, BUFSIZE, message);

	// FormatMessage leaves errMessage untouched when it fails, so only use
	// what it gave us when it actually gave us something
	if (errMessageLength != 0 && errMessage != NULL)
	{
		wcscat_s(buffer, BUFSIZE, L"\n\nError : ");
		wcscat_s(buffer, BUFSIZE, errMessage);
	}

	MessageBox(NULL, buffer, L"Recaps Error", MB_OK | MB_ICONINFORMATION);

	if (errMessage != NULL)
		LocalFree(errMessage);
}


///////////////////////////////////////////////////////////////////////////////
// Prints an error message to the debugger
void PrintDebugString(const char* format, ...)
{
	char buffer[2048];

    va_list args;
    va_start(args, format);
    
    vsprintf_s(buffer, 2048, format, args);
	strcat_s(buffer, 2048, "\n");
    
	OutputDebugStringA(buffer);
}