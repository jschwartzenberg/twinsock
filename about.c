/*
 *  TwinSock - "Troy's Windows Sockets"
 *
 *  Copyright (C) 1994  Troy Rollo <troy@cbme.unsw.EDU.AU>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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