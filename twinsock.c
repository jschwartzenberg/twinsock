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

#include <winsock.h>
#include <stdlib.h>
#include "twinsock.h"
#include "tx.h"

extern	RegisterManager(HWND hwnd);

#define	READ_MAX	1024
#define	TIMER_ID_SEND		1
#define	TIMER_ID_RECEIVE	2
#define	TIMER_ID_FLUSH		3

static	int	idComm;
static	HWND	hwnd;
static	BOOL	bFlushing = FALSE;

extern	void PacketReceiveData(void *pvData, int iLen);

void	SetTransmitTimeout(void)
{
	KillTimer(hwnd, TIMER_ID_SEND);
	SetTimer(hwnd, TIMER_ID_SEND, 3000, 0);
}

void	KillTransmitTimeout(void)
{
	KillTimer(hwnd, TIMER_ID_SEND);
}

void	SetReceiveTimeout(void)
{
	KillTimer(hwnd, TIMER_ID_RECEIVE);
	SetTimer(hwnd, TIMER_ID_RECEIVE, 1500, 0);
}

void	KillReceiveTimeout(void)
{
	KillTimer(hwnd, TIMER_ID_RECEIVE);
}

void	FlushInput(void)
{
	KillTimer(hwnd, TIMER_ID_FLUSH);
	bFlushing = TRUE;
	SetTimer(hwnd, TIMER_ID_FLUSH, 1500, 0);
}

static	void	DoReading(void)
{
	static	char	achBuffer[READ_MAX];
	int	nRead;
	COMSTAT	cs;

	do
	{
		nRead = ReadComm(idComm, achBuffer, READ_MAX);
		if (nRead <= 0)
		{
			GetCommError(idComm, &cs);
			nRead = -nRead;
		}
		if (nRead)
		{
			if (bFlushing)
				FlushInput();
			else
				PacketReceiveData(achBuffer, nRead);
		}
	} while (nRead);
}

int	SendData(void *pvData, int iDataLen)
{
	int	nWritten;
	COMSTAT	cs;
	int	iLen;

	if (bFlushing)
		return iDataLen; /* Lie */
	iLen = iDataLen;
	do
	{
		nWritten = WriteComm(idComm, pvData, iLen);
		if (nWritten < 0)
		{
			GetCommError(idComm, &cs);
			nWritten = -nWritten;
		}
		iLen -= nWritten;
		pvData = (char *) pvData + nWritten;
	} while (iLen);
	return iDataLen;
}

LRESULT	CALLBACK _export
WindowProc(	HWND	hWnd,
		UINT	wMsg,
		WPARAM	wParam,
		LPARAM	lParam)
{

	switch(wMsg)
	{
	case WM_SYSCOMMAND:
		if (wParam == SC_MAXIMIZE ||
		    wParam == SC_RESTORE)
			return 0;
		break;

	case WM_COMMNOTIFY:
		switch(LOWORD(lParam))
		{
		case CN_RECEIVE:
			DoReading();
			break;
		}
		break;

	case WM_TIMER:
		switch(wParam)
		{
		case TIMER_ID_SEND:
			TimeoutReceived(TIMER_ID_SEND);
			break;

		case TIMER_ID_RECEIVE:
			KillTimer(hWnd, TIMER_ID_RECEIVE);
			PacketReceiveData(0, 0);
			break;

		case TIMER_ID_FLUSH:
			KillTimer(hWnd, TIMER_ID_FLUSH);
			bFlushing = FALSE;
			break;
		}
		break;

	case WM_USER:
		PacketTransmitData((void *) lParam, wParam, 0);
		DoReading();
		break;

	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hWnd, wMsg, wParam, lParam);
}

void
DataReceived(void *pvData, int iLen)
{
	static	struct tx_request *ptxr = 0;
	static	struct tx_request txrHeader;
	static	int	nBytes = 0;
	short	nPktLen;
	enum Functions ft;
	int	nCopy;

	while (iLen)
	{
		if (nBytes < 10)
		{
			nCopy = 10 - nBytes;
			if (nCopy > iLen)
				nCopy = iLen;
			memcpy((char *) &txrHeader + nBytes, pvData, nCopy);
			nBytes += nCopy;
			pvData = (char *) pvData + nCopy;
			iLen -= nCopy;
			if (nBytes == 10)
			{
				nPktLen = ntohs(txrHeader.nLen);
				ptxr = (struct tx_request *) malloc(sizeof(struct tx_request) + nPktLen - 1);
				memcpy(ptxr, &txrHeader, 10);
			}
		}
		if (nBytes >= 10)
		{
			nPktLen = ntohs(txrHeader.nLen);
			ft = (enum Functions) ntohs(txrHeader.iType);
			nCopy = nPktLen - nBytes;
			if (nCopy > iLen)
				nCopy = iLen;
			if (nCopy)
			{
				memcpy((char *) ptxr + nBytes, pvData, nCopy);
				nBytes += nCopy;
				pvData = (char *) pvData + nCopy;
				iLen -= nCopy;
			}
			if (nBytes == nPktLen)
			{
				if (ft == FN_Init)
					SetInitialised();
				else
					ResponseReceived(ptxr);
				free(ptxr);
				ptxr = 0;
				nBytes = 0;
			}
		}
	}
}

static	UINT
GetConfigInt(char const *pchItem, UINT nDefault)
{
	return GetPrivateProfileInt("Config", pchItem, nDefault, "TWINSOCK.INI");
}

static void
SendInitRequest(void)
{
	struct	tx_request txr;

	txr.iType = htons(FN_Init);
	txr.nArgs = 0;
	txr.nLen = htons(10);
	txr.id = -1;
	txr.nError = 0;
	PacketTransmitData(&txr, 10, 0);
}

void
Shutdown(void)
{
	PostQuitMessage(0);
}

#pragma argsused
int	far	pascal
WinMain(HINSTANCE	hInstance,
	HINSTANCE	hPrec,
	LPSTR		lpCmdLine,
	int		nShow)
{
	WNDCLASS wc;
	MSG	msg;
	DCB	dcb;
	char	achProfileEntry[256];

	wc.style = 0;
	wc.lpfnWndProc = WindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, "TSICON");
	wc.hCursor = 0;
	wc.hbrBackground = 0;
	wc.lpszMenuName = 0;
	wc.lpszClassName = "TwinSock Communications";
	RegisterClass(&wc);
	hwnd = CreateWindow(	"TwinSock Communications",
				"TwinSock Communications",
				WS_OVERLAPPEDWINDOW |
				 WS_MINIMIZE |
				 WS_VISIBLE,
				CW_USEDEFAULT,
				0,
				CW_USEDEFAULT,
				0,
				0,
				0,
				hInstance,
				0);
	ShowWindow(hwnd, SW_SHOW);
	RegisterManager(hwnd);

	GetPrivateProfileString("Config", "Port", "COM1", achProfileEntry, 256, "TWINSOCK.INI");
	idComm = OpenComm(achProfileEntry, 1024, 1024);
	if (idComm < 0)
		exit(1);
	dcb.Id = idComm;
	dcb.BaudRate = GetConfigInt("Speed", 19200);
	dcb.ByteSize = GetConfigInt("Databits", 8);
	dcb.Parity = GetConfigInt("Parity", NOPARITY);
	dcb.StopBits = GetConfigInt("StopBits", ONESTOPBIT);
	dcb.RlsTimeout = GetConfigInt("RlsTimeout", 0);
	dcb.CtsTimeout = GetConfigInt("CtsTimeout", 0);
	dcb.DsrTimeout = GetConfigInt("DsrTimeout", 0);
	dcb.fBinary = TRUE;
	dcb.fRtsDisable = GetConfigInt("fRtsDisable", TRUE);
	dcb.fParity = GetConfigInt("fParity", FALSE);
	dcb.fOutxCtsFlow = GetConfigInt("OutxCtsFlow", FALSE);
	dcb.fOutxDsrFlow = GetConfigInt("OutxDsrFlow", FALSE);
	dcb.fDummy = 0;
	dcb.fDtrDisable = GetConfigInt("fDtrDisable", TRUE);
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
	EnableCommNotification(idComm, hwnd, 1, 0);
	SendInitRequest();

 	while (GetMessage(&msg, 0, 0, 0))
	{
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}
