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
#include "twinsock.h"

extern	HINSTANCE	hinst;
int	iPortChanged = 0;
static	char	achStartPort[256];

static	UINT
GetConfigInt(char const *pchItem, UINT nDefault)
{
	return GetPrivateProfileInt("Config", pchItem, nDefault, "TWINSOCK.INI");
}

enum LineSpeed { B110, B300, B600, B1200, B2400, B4800, B9600,
		 B14400, B19200, B38400, B56000, B128000, B256000,
		 BBOGUS };
unsigned GetSpeedIndex(enum LineSpeed lsSpeed);

void
#ifdef __FLAT__
InitComm(HANDLE idComm)
#else
InitComm(int	idComm)
#endif
{
	DCB	dcb;
	unsigned speed;
	int	iFlowType;
#ifdef __FLAT__
	COMMTIMEOUTS cto;
#endif

	memset(&dcb, 0, sizeof(dcb));
#ifdef __FLAT__
	dcb.DCBlength = sizeof(dcb);
	dcb.fDtrControl = GetConfigInt("fDtrControl",
				iFlowType ? DTR_CONTROL_HANDSHAKE :
				DTR_CONTROL_ENABLE);
	dcb.fDsrSensitivity = GetConfigInt("fDsrSensitivity", 0);
	dcb.fTXContinueOnXoff = GetConfigInt("fTXContinueOnXoff", TRUE);
	dcb.fErrorChar = 0;
	dcb.fRtsControl = GetConfigInt("fRtsControl",
					iFlowType ? RTS_CONTROL_ENABLE :
					RTS_CONTROL_HANDSHAKE);
	dcb.fAbortOnError = GetConfigInt("fAbortOnError", FALSE);
	dcb.wReserved = 0;
	dcb.ErrorChar = 0;
	dcb.wReserved1 = 0;
#else
	dcb.Id = idComm;
	dcb.RlsTimeout = GetConfigInt("RlsTimeout", 0);
	dcb.CtsTimeout = GetConfigInt("CtsTimeout", 0);
	dcb.DsrTimeout = GetConfigInt("DsrTimeout", 1000);
	dcb.PeChar = 0;
	dcb.fChEvt = 0;
	dcb.fDtrflow = GetConfigInt("fDtrFlow", (iFlowType == 1));
	dcb.fRtsflow = GetConfigInt("fRtsFlow", (iFlowType == 0));
	dcb.TxDelay = 0;
	dcb.fDtrDisable = GetConfigInt("fDtrDisable", FALSE);
	dcb.fRtsDisable = GetConfigInt("fRtsDisable", FALSE);
	dcb.fPeChar = 0;
	dcb.fDummy = 0;
#endif
	iFlowType = GetConfigInt("FlowType", 0);
	speed = GetConfigInt("Speed", B19200);
	dcb.BaudRate = GetSpeedIndex((enum LineSpeed) speed);
	dcb.ByteSize = GetConfigInt("Databits", 8);
	dcb.Parity = GetConfigInt("Parity", NOPARITY);
	dcb.StopBits = GetConfigInt("StopBits", ONESTOPBIT);
	dcb.fBinary = TRUE;
	dcb.fParity = GetConfigInt("fParity", FALSE);
	dcb.fOutxCtsFlow = GetConfigInt("OutxCtsFlow", (iFlowType == 0));
	dcb.fOutxDsrFlow = GetConfigInt("OutxDsrFlow", (iFlowType == 1));
	dcb.fOutX = GetConfigInt("fOutX", FALSE);
	dcb.fInX = GetConfigInt("fInX", FALSE);
	dcb.fNull = 0;
	dcb.fDummy2 = 0;
	dcb.XonChar = '\021';
	dcb.XoffChar = '\023';
	dcb.XonLim = 100;
	dcb.XoffLim = 900;
	dcb.EofChar = 0;
	dcb.EvtChar = 0;

#ifdef __FLAT__
	memset(&cto, 0, sizeof(cto));
	cto.ReadIntervalTimeout = 5;
	SetCommState(idComm, &dcb);
	SetCommTimeouts(idComm, &cto);
#else
	SetCommState(&dcb);
#endif
}

static	struct
{
    unsigned		iValue;
    char	       *strSpeed;
    enum LineSpeed	lsSpeed;
} aSpeeds[] =
{
	{ CBR_110,	"110",	B110	},
	{ CBR_300,	"300",	B300	},
	{ CBR_600,	"600",	B600	},
	{ CBR_1200,	"1200",	B1200	},
	{ CBR_2400,	"2400",	B2400	},
	{ CBR_4800,	"4800",	B4800	},
	{ CBR_9600,	"9600",	B9600	},
	{ CBR_14400,	"14400",B14400	},
	{ CBR_19200,	"19200",B19200	},
	{ CBR_38400,	"38400",B38400	},
	{ CBR_56000,	"57600",B56000	},
	{ CBR_128000,	"128000",B128000},
	{ CBR_256000,	"256000",B256000},
	{ 0,		"BOGUS",BBOGUS	},
};

/*
 * Returns the appropriate port speed index value
 * necessary to setup the COM port
 */
unsigned GetSpeedIndex(enum LineSpeed lsSpeed)
{
	int i;
    
	for (i = 0; aSpeeds[i].lsSpeed != BBOGUS; ++i)
	{
		if (lsSpeed == aSpeeds[i].lsSpeed)
		break;
	}
	return (aSpeeds[i].iValue);
}

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
#define	CE_CTSRTS	121
#define	CE_DTRDSR	122
#define	CE_NOFLOW	123
#define	CE_BUFFER	130

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
	iSpeedNow = GetPrivateProfileInt("Config", "Speed", (int) B19200, "TWINSOCK.INI");
	for (i = 0; aSpeeds[i].lsSpeed != BBOGUS; ++i)
	{
		sprintf(achTmp, "%s", aSpeeds[i].strSpeed);	
		SendDlgItemMessage(hDlg, CE_SPEED, CB_ADDSTRING, 0, (LPARAM) achTmp);
		if (aSpeeds[i].lsSpeed == iSpeedNow)
			SendDlgItemMessage(hDlg, CE_SPEED, CB_SETCURSEL, i, 0);
	}

	for (i = 5; i <= 8; i++)
	{
		sprintf(achTmp, "%d", i);
		SendDlgItemMessage(hDlg, CE_DATABITS, CB_ADDSTRING, 0, (LPARAM) achTmp);
	}
	i = GetConfigInt("DataBits", 8);
	SendDlgItemMessage(hDlg, CE_DATABITS, CB_SETCURSEL, i - 5, 0);

	for (i = 0; apchParities[i]; i++)
		SendDlgItemMessage(hDlg, CE_PARITY, CB_ADDSTRING, 0, (LPARAM) apchParities[i]);
	i = GetConfigInt("Parity", 0);
	SendDlgItemMessage(hDlg, CE_PARITY, CB_SETCURSEL, i, 0);

	for (i = 0; apchStopBits[i]; i++)
		SendDlgItemMessage(hDlg, CE_STOPBITS, CB_ADDSTRING, 0, (LPARAM) apchStopBits[i]);
	i = GetConfigInt("StopBits", 0);
	SendDlgItemMessage(hDlg, CE_STOPBITS, CB_SETCURSEL, i, 0);

	i = GetConfigInt("FlowType", 0);
	SendDlgItemMessage(hDlg, CE_CTSRTS + i, BM_SETCHECK, 1, 0);

	i = GetConfigInt("BufferSize", 16384);
	sprintf(achTmp, "%d", i);
	SetDlgItemText(hDlg, CE_BUFFER, achTmp);
}

void
ReadCommsDialog(HWND hDlg)
{
	LRESULT	i;
	char	achTemp[100];

	GetDlgItemText(hDlg, CE_PORT, achTemp, 100);
	WritePrivateProfileString("Config", "Port", achTemp, "TWINSOCK.INI");
	if (stricmp(achTemp, achStartPort))
		iPortChanged = 1;

	i = SendDlgItemMessage(hDlg, CE_SPEED, CB_GETCURSEL, 0, 0);
	i = aSpeeds[i].lsSpeed;
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

	for (i = 0; i < 3; i++)
		if (SendDlgItemMessage(hDlg, CE_CTSRTS + i, BM_GETCHECK, 0, 0))
			break;
	sprintf(achTemp, "%d", i);
	WritePrivateProfileString("Config", "FlowType", achTemp, "TWINSOCK.INI");

	GetDlgItemText(hDlg, CE_BUFFER, achTemp, sizeof(achTemp));
	if (atoi(achTemp) <= 0)
		strcpy(achTemp, "16384");
	WritePrivateProfileString("Config", "BufferSize", achTemp, "TWINSOCK.INI");
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

		case 3:
			WinHelp(hDlg, "TWINSOCK.HLP", HELP_CONTENTS, 0);
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
	char	achNumber[84];
	int	iMethod;

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
			GetDlgItemText(hDlg, DL_NUMBER, achNumber + 4, 76);
			WritePrivateProfileString("Config", "LastNumber", achNumber + 4, "TWINSOCK.INI");
			iMethod = SendMessage(GetDlgItem(hDlg, DL_TONE), BM_GETCHECK, 0, 0);
			WritePrivateProfileString("Config", "DialingMethod", iMethod ? "1" : "0", "TWINSOCK.INI");
			EndDialog(hDlg, TRUE);
			strncpy(achNumber, iMethod ? "ATDT" : "ATDP", 4);
			strcat(achNumber, "\r");
			SendData(achNumber, strlen(achNumber));
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