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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "twinsock.h"

extern	HINSTANCE	hinst;

#define RESOLV_DOMAIN	101
#define	RESOLV_NS1	111
#define	RESOLV_NS2	121
#define	RESOLV_NS3	131
#define	RESOLV_OVERRIDE	141

static	void
SetNSItem(	char	*pchBuffer,
		HWND	hDlg,
		int	iItem)
{
	char	*pchDot;
	int	i;

	for (i = 0; pchBuffer && i < 4; i++)
	{
		if (pchBuffer)
		{
			pchDot = strchr(pchBuffer, '.');
			if (pchDot)
				*pchDot++ = 0;
			SetDlgItemText(hDlg, iItem + i, pchBuffer);
			pchBuffer = pchDot;
		}
	}
}

static	void
FillResolvDialog(HWND hDlg)
{
	static	char	achBuffer[256];

	GetTwinSockSetting("Resolver", "Domain", "", achBuffer, sizeof(achBuffer));
	SetDlgItemText(hDlg, RESOLV_DOMAIN, achBuffer);
	GetTwinSockSetting("Resolver", "Server1", "", achBuffer, sizeof(achBuffer));
	SetNSItem(achBuffer, hDlg, RESOLV_NS1);
	GetTwinSockSetting("Resolver", "Server2", "", achBuffer, sizeof(achBuffer));
	SetNSItem(achBuffer, hDlg, RESOLV_NS2);
	GetTwinSockSetting("Resolver", "Server3", "", achBuffer, sizeof(achBuffer));
	SetNSItem(achBuffer, hDlg, RESOLV_NS3);
	GetTwinSockSetting("Resolver", "Override", "0", achBuffer, sizeof(achBuffer));
	SendDlgItemMessage(hDlg, RESOLV_OVERRIDE, BM_SETCHECK, (*achBuffer == '1'), 0);
}

static	void
VerifyResolvOK(HWND hDlg)
{
	int	i, j;
	int	idCtl;
	BOOL	bOneEmpty = FALSE;
	BOOL	bOK = TRUE;
	BOOL	bOverride;
	char	*c;
	int	nValue;
	static	char	achBuffer[256];

	bOverride = SendDlgItemMessage(hDlg, RESOLV_OVERRIDE, BM_GETCHECK, 0, 0);
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 4; j++)
		{
			idCtl = RESOLV_NS1 + 10 * i + j;
			GetDlgItemText(hDlg, idCtl, achBuffer, sizeof(achBuffer));
			if (strlen(achBuffer) > 3)
				bOK = FALSE;
			for (c = achBuffer; *c; c++)
				if (!isdigit(*c))
					bOK = FALSE;
			if (*achBuffer == 0)
			{
				if (j == 0)
					bOneEmpty = TRUE;
				else if (!bOneEmpty)
					bOK = FALSE;
			}
			else if (bOneEmpty)
			{
				bOK = FALSE;
			}
			else
			{
				nValue = atoi(achBuffer);
				if (nValue > 255)
					bOK = FALSE;
			}
		}
		if (!i && bOverride && bOneEmpty)
			bOK = FALSE;
	}
	GetDlgItemText(hDlg, RESOLV_DOMAIN, achBuffer, sizeof(achBuffer));
	if (bOverride && !*achBuffer)
		bOK = FALSE;
	EnableWindow(GetDlgItem(hDlg, IDOK), bOK);
}

static	char *achResNames[3] =
{
	"Server1", "Server2", "Server3"
};

static	void
ReadResolvDialog(HWND hDlg)
{
	int	i, j;
	int	idCtl;
	BOOL	bOneEmpty = FALSE;
	BOOL	bOK = TRUE;
	BOOL	bOverride;
	char	*c;
	int	nValue;
	static	char	achBuffer[256];
	char	achSmallBuf[20];

	bOverride = SendDlgItemMessage(hDlg, RESOLV_OVERRIDE, BM_GETCHECK, 0, 0);
	WritePrivateProfileString("Resolver", "Override", bOverride ? "1" : "0", "TWINSOCK.INI");
	for (i = 0; i < 3; i++)
	{
		achBuffer[0] = 0;
		for (j = 0; j < 4; j++)
		{
			idCtl = RESOLV_NS1 + 10 * i + j;
			GetDlgItemText(hDlg, idCtl, achSmallBuf, sizeof(achSmallBuf));
			if (*achSmallBuf)
			{
				if (j > 0)
					strcat(achBuffer, ".");
				strcat(achBuffer, achSmallBuf);
			}
		}
		WritePrivateProfileString("Resolver", achResNames[i], achBuffer, "TWINSOCK.INI");
	}
	GetDlgItemText(hDlg, RESOLV_DOMAIN, achBuffer, sizeof(achBuffer));
	WritePrivateProfileString("Resolver", "Domain", achBuffer, "TWINSOCK.INI");
}


#pragma argsused
BOOL	CALLBACK
ResolvDlgProc(	HWND	hDlg,
		UINT	wMsg,
		WPARAM	wParam,
		LPARAM	lParam)
{
	switch(wMsg)
	{
	case WM_INITDIALOG:
		FillResolvDialog(hDlg);
		return TRUE;

	case WM_COMMAND:
		switch(wParam)
		{
		case IDOK:
			ReadResolvDialog(hDlg);
			EndDialog(hDlg, TRUE);
			break;

		case IDCANCEL:
			EndDialog(hDlg, FALSE);
			break;

		default:
			VerifyResolvOK(hDlg);
			break;
		}
		break;
	}
	return FALSE;
}

BOOL
ResolvEdit(HWND hwndParent)
{
	FARPROC	fpDlgProc;
	BOOL	bStatus;

	fpDlgProc = MakeProcInstance((FARPROC) ResolvDlgProc, hinst);
	bStatus = DialogBox(hinst, "NS_DLG", hwndParent, fpDlgProc);
	FreeProcInstance(fpDlgProc);
	return bStatus;
}
