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

extern	HINSTANCE	hinst;
int	iPortChanged = 0;
static	char	achStartPort[256];

static	UINT
GetConfigInt(char const *pchItem, UINT nDefault)
{
	return GetPrivateProfileInt("Config", pchItem, nDefault, "TWINSOCK.INI");
}

void
InitComm(int	idComm)
{
	DCB	dcb;

	dcb.Id = idComm;
	dcb.BaudRate = GetConfigInt("Speed", 19200);
	dcb.ByteSize = GetConfigInt("Databits", 8);
	dcb.Parity = GetConfigInt("Parity", NOPARITY);
	dcb.StopBits = GetConfigInt("StopBits", ONESTOPBIT);
	dcb.RlsTimeout = GetConfigInt("RlsTimeout", 0);
	dcb.CtsTimeout = GetConfigInt("CtsTimeout", 0);
	dcb.DsrTimeout = GetConfigInt("DsrTimeout", 0);
	dcb.fBinary = TRUE;
	dcb.fRtsDisable = GetConfigInt("fRtsDisable", FALSE);
	dcb.fParity = GetConfigInt("fParity", FALSE);
	dcb.fOutxCtsFlow = GetConfigInt("OutxCtsFlow", TRUE);
	dcb.fOutxDsrFlow = GetConfigInt("OutxDsrFlow", FALSE);
	dcb.fDummy = 0;
	dcb.fDtrDisable = GetConfigInt("fDtrDisable", FALSE);
	dcb.fOutX = GetConfigInt("fOutX", TRUE);
	dcb.fInX = GetConfigInt("fInX", FALSE);
	dcb.fPeChar = 0;
	dcb.fNull = 0;
	dcb.fChEvt = 0;
	dcb.fDtrflow = GetConfigInt("fDtrFlow", FALSE);
	dcb.fRtsflow = GetConfigInt("fRtsFlow", FALSE);
	dcb.fDummy2 = 0;
	dcb.XonChar = '\021';
	dcb.XoffChar = '\023';
	dcb.XonLim = 100;
	dcb.XoffLim = 900;
	dcb.PeChar = 0;
	dcb.EofChar = 0;
	dcb.EvtChar = 0;
	dcb.TxDelay = 0;

	SetCommState(&dcb);
}

static	struct
{
	unsigned iValue;
	long	iSpeed;
} aSpeeds[] =
{
	{ CBR_110,	110	},
	{ CBR_300,	300	},
	{ CBR_600,	600	},
	{ CBR_1200,	1200	},
	{ CBR_2400,	2400	},
	{ CBR_4800,	4800	},
	{ CBR_9600,	9600	},
	{ CBR_14400,	14400	},
	{ CBR_19200,	19200	},
	{ CBR_38400,	38400	},
	{ CBR_56000,	56000	},
	{ CBR_128000,	128000	},
	{ CBR_256000,	2256000	},
	{ 0,		-1	},
};

static	char	*apchPorts[] =
{
	"COM1",
	"COM2",
	"COM3",
	"COM4",
	0
};

char	*apchParities[] =
{
	"None",
	"Odd",
	"Even",
	"Mark",
	"Space",
	0
};

char	*apchStopBits[] =
{
	"1",
	"1.5",
	"2",
	0
};

#define	CE_PORT 	101
#define	CE_SPEED	103
#define	CE_DATABITS	105
#define	CE_PARITY	107
#define	CE_STOPBITS	109

static	void
FillCommsDialog(HWND hDlg)
{
	int	i;
	long	iSpeedNow;
	char	achTmp[100];

	GetPrivateProfileString("Config", "Port", "COM1", achTmp, 100, "TWINSOCK.INI");
	SetDlgItemText(hDlg, CE_PORT, achTmp);
	strcpy(achStartPort, achTmp);
	for (i = 0; apchPorts[i]; i++)
		SendDlgItemMessage(hDlg, CE_PORT, CB_ADDSTRING, 0, (LPARAM) apchPorts[i]);
	iSpeedNow = GetPrivateProfileInt("Config", "Speed", 19200, "TWINSOCK.INI");
	for (i = 0; aSpeeds[i].iSpeed != -1; i++)
	{
		sprintf(achTmp, "%ld", aSpeeds[i].iSpeed);
		SendDlgItemMessage(hDlg, CE_SPEED, CB_ADDSTRING, 0, (LPARAM) achTmp);
		if (aSpeeds[i].iSpeed == iSpeedNow ||
		    aSpeeds[i].iValue == iSpeedNow)
		{
			SendDlgItemMessage(hDlg, CE_SPEED, CB_SETCURSEL, i, 0);
		}
	}

	for (i = 5; i <= 8; i++)
	{
		sprintf(achTmp, "%d", i);
		SendDlgItemMessage(hDlg, CE_DATABITS, CB_ADDSTRING, 0, (LPARAM) achTmp);
	}
	i = GetPrivateProfileInt("Config", "DataBits", 8, "TWINSOCK.INI");
	SendDlgItemMessage(hDlg, CE_DATABITS, CB_SETCURSEL, i - 5, 0);

	for (i = 0; apchParities[i]; i++)
		SendDlgItemMessage(hDlg, CE_PARITY, CB_ADDSTRING, 0, (LPARAM) apchParities[i]);
	i = GetPrivateProfileInt("Config", "Parity", 0, "TWINSOCK.INI");
	SendDlgItemMessage(hDlg, CE_PARITY, CB_SETCURSEL, i, 0);

	for (i = 0; apchStopBits[i]; i++)
		SendDlgItemMessage(hDlg, CE_STOPBITS, CB_ADDSTRING, 0, (LPARAM) apchStopBits[i]);
	i = GetPrivateProfileInt("Config", "StopBits", 0, "TWINSOCK.INI");
	SendDlgItemMessage(hDlg, CE_STOPBITS, CB_SETCURSEL, i, 0);
}

void
ReadCommsDialog(HWND hDlg)
{
	int	i;
	char	achTemp[100];

	GetDlgItemText(hDlg, CE_PORT, achTemp, 100);
	WritePrivateProfileString("Config", "Port", achTemp, "TWINSOCK.INI");
	if (stricmp(achTemp, achStartPort))
		iPortChanged = 1;

	i = SendDlgItemMessage(hDlg, CE_SPEED, CB_GETCURSEL, 0, 0);
	i = aSpeeds[i].iValue;
	sprintf(achTemp, "%d", i);
	WritePrivateProfileString("Config", "Speed", achTemp, "TWINSOCK.INI");

	i = SendDlgItemMessage(hDlg, CE_DATABITS, CB_GETCURSEL, 0, 0);
	i += 5;
	sprintf(achTemp, "%d", i);
	WritePrivateProfileString("Config", "Databits", achTemp, "TWINSOCK.INI");

	i = SendDlgItemMessage(hDlg, CE_PARITY, CB_GETCURSEL, 0, 0);
	sprintf(achTemp, "%d", i);
	WritePrivateProfileString("Config", "Parity", achTemp, "TWINSOCK.INI");

	i = SendDlgItemMessage(hDlg, CE_STOPBITS, CB_GETCURSEL, 0, 0);
	sprintf(achTemp, "%d", i);
	WritePrivateProfileString("Config", "StopBits", achTemp, "TWINSOCK.INI");
}

#pragma argsused
BOOL	CALLBACK
CommsDlgProc(	HWND	hDlg,
		UINT	wMsg,
		WPARAM	wParam,
		LPARAM	lParam)
{
	switch(wMsg)
	{
	case WM_INITDIALOG:
		FillCommsDialog(hDlg);
		return TRUE;

	case WM_COMMAND:
		switch(wParam)
		{
		case IDOK:
			ReadCommsDialog(hDlg);
			EndDialog(hDlg, TRUE);
			break;

		case IDCANCEL:
			EndDialog(hDlg, FALSE);
			break;
		}
		break;
	}
	return FALSE;
}

BOOL
CommsEdit(HWND hwndParent)
{
	FARPROC	fpDlgProc;
	BOOL	bStatus;

	fpDlgProc = MakeProcInstance((FARPROC) CommsDlgProc, hinst);
	bStatus = DialogBox(hinst, "COMMS_DLG", hwndParent, fpDlgProc);
	FreeProcInstance(fpDlgProc);
	return bStatus;
}



#define	DL_NUMBER	101
#define	DL_PULSE	102
#define	DL_TONE		103

#pragma argsused
BOOL	CALLBACK
DialDlgProc(	HWND	hDlg,
		UINT	wMsg,
		WPARAM	wParam,
		LPARAM	lParam)
{
	int	iMethod;
	char	achNumber[80];

	switch(wMsg)
	{
	case WM_INITDIALOG:
		iMethod = GetPrivateProfileInt("Config", "DialingMethod", 1, "TWINSOCK.INI");
		SendMessage(GetDlgItem(hDlg, iMethod ? DL_TONE : DL_PULSE), BM_SETCHECK, 1, 0);
		GetPrivateProfileString("Config", "LastNumber", "", achNumber, 80, "TWINSOCK.INI");
		SetDlgItemText(hDlg, DL_NUMBER, achNumber);
		EnableWindow(GetDlgItem(hDlg, IDOK),
			(GetDlgItemText(hDlg, DL_NUMBER, achNumber, 80) != 0));
		return TRUE;

	case WM_COMMAND:
		switch(wParam)
		{
		case IDOK:
			GetDlgItemText(hDlg, DL_NUMBER, achNumber, 80);
			WritePrivateProfileString("Config", "LastNumber", achNumber, "TWINSOCK.INI");
			iMethod = SendMessage(GetDlgItem(hDlg, DL_TONE), BM_GETCHECK, 0, 0);
			WritePrivateProfileString("Config", "DialingMethod", iMethod ? "1" : "0", "TWINSOCK.INI");
			EndDialog(hDlg, TRUE);
			SendData(iMethod ? "ATDT" : "ATDP", 4);
			SendData(achNumber, strlen(achNumber));
			SendData("\r", 1);
			break;

		case IDCANCEL:
			EndDialog(hDlg, FALSE);
			break;

		case DL_NUMBER:
			EnableWindow(GetDlgItem(hDlg, IDOK),
				(GetDlgItemText(hDlg, DL_NUMBER, achNumber, 80) != 0));
			break;

		}
		break;
	}
	return FALSE;
}

BOOL
DialNumber(HWND hwndParent)
{
	FARPROC	fpDlgProc;
	BOOL	bStatus;

	fpDlgProc = MakeProcInstance((FARPROC) DialDlgProc, hinst);
	bStatus = DialogBox(hinst, "DIAL_DLG", hwndParent, fpDlgProc);
	FreeProcInstance(fpDlgProc);
	return bStatus;
}




