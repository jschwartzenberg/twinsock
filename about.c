/*
 *  TwinSock - "Troy's Windows Sockets"
 *
 *  Copyright (C) 1994  Troy Rollo <troy@cbme.unsw.EDU.AU>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the license in the file LICENSE.TXT included
 *  with the TwinSock distribution.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 */
#include <windows.h>

extern	HINSTANCE hinst;

BOOL	CALLBACK
AboutDlgProc(	HWND	hDlg,
		UINT	wMsg,
		WPARAM	wParam,
		LPARAM	lParam)
{
	if (wMsg == WM_COMMAND &&
	    (wParam == IDOK || wParam == IDCANCEL))
	{
		EndDialog(hDlg, TRUE);
		return TRUE;
	}
	else if (wMsg == WM_INITDIALOG)
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

void
About(HWND hwndParent)
{
	FARPROC	fpDlgProc;

	fpDlgProc = MakeProcInstance((FARPROC) AboutDlgProc, hinst);
	DialogBox(hinst, "ABOUT_DLG", hwndParent, fpDlgProc);
	FreeProcInstance(fpDlgProc);
}
