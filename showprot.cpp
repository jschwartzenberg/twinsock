/*
 *  TwinSock - "Troy's Windows Sockets"
 *
 *  Copyright (C) 1994-1995  Troy Rollo <troy@cbme.unsw.EDU.AU>
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
#include <stdio.h>

extern	short	nInSeq;
extern	short	nOutSeq;

extern	long	nCRCErrors;
extern	long	nRetransmits;
extern	long	nTimeouts;
extern	long	nInsane;
extern	long	nIncomplete;

extern	long	nBytesRecvd;
extern	long	nDiscarded;

extern	HINSTANCE hinst;

void	SetValue(HWND hDlg,
		 int idControl,
		 long iValue)
{
	char	achTemp[40];

	sprintf(achTemp, "%ld", iValue);
	SetDlgItemText(hDlg, idControl, achTemp);
}
		 

BOOL	CALLBACK
ProtoDlgProc(	HWND	hDlg,
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
		SetValue(hDlg, 100, nOutSeq);
		SetValue(hDlg, 101, nInSeq);
		SetValue(hDlg, 102, nCRCErrors);
		SetValue(hDlg, 103, nRetransmits);
		SetValue(hDlg, 104, nTimeouts);
		SetValue(hDlg, 105, nInsane);
		SetValue(hDlg, 106, nIncomplete);
		SetValue(hDlg, 107, nBytesRecvd);
		SetValue(hDlg, 108, nDiscarded);
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

void
ShowProtoInfo(HWND hwndParent)
{
	FARPROC	fpDlgProc;

	fpDlgProc = MakeProcInstance((FARPROC) ProtoDlgProc, hinst);
	DialogBox(hinst, "PROTO_DLG", hwndParent, fpDlgProc);
	FreeProcInstance(fpDlgProc);
}
