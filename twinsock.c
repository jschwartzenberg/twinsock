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

HINSTANCE	hinst;

extern	RegisterManager(HWND hwnd);
static void SendInitRequest(void);
void	Shutdown(void);
void	OpenPort(void);
extern	int	iPortChanged;

#define	READ_MAX	1024
#define	TIMER_ID_SEND		1
#define	TIMER_ID_RECEIVE	2
#define	TIMER_ID_FLUSH		3
#define	TIMER_ID_BREAK		4

static	int	idComm;
static	HWND	hwnd;
static	BOOL	bFlushing = FALSE;
static	BOOL	bTerminal = TRUE;

#define	SCREEN_COLUMNS	80
#define	SCREEN_ROWS	25
static	char	aachScreen[SCREEN_ROWS][SCREEN_COLUMNS];
static	int	iScrollRow = 0;
static	int	iRow = 0, iColumn = 0;
static	int	cyRow, cxColumn;
#define	ROW_INDEX(x)	((x + iScrollRow) % SCREEN_ROWS)

static	char	const	achProtoInit[] = "@$TSStart$@";
static	int	iInitChar = 0;

extern	void PacketReceiveData(void *pvData, int iLen);

void	SetTransmitTimeout(void)
{
	KillTimer(hwnd, TIMER_ID_SEND);
	SetTimer(hwnd, TIMER_ID_SEND, 10000, 0);
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

static	void
AddChar(char c)
{
	RECT	rcClient;
	RECT	rcRedraw;

	if (c == '\r')
	{
		iColumn = 0;
	}
	else if (c == '\b')
	{
		if (iColumn > 0)
			iColumn--;
	}
	else if (c == '\n')
	{
		if (iRow < SCREEN_ROWS - 1)
		{
			iRow++;
		}
		else
		{
			memset(aachScreen[iScrollRow], 0x20, sizeof(aachScreen[iScrollRow]));
			iScrollRow++;
			while (iScrollRow >= SCREEN_ROWS)
				iScrollRow -= SCREEN_ROWS;
			GetClientRect(hwnd, &rcClient);
			ScrollWindow(hwnd, 0, -cyRow, &rcClient, 0);
			UpdateWindow(hwnd);
		}
	}
	else if (c >= 0x20 && c <= 0x7e)
	{
		aachScreen[ROW_INDEX(iRow)][iColumn] = c;
		rcRedraw.top = iRow * cyRow;
		rcRedraw.left = iColumn * cxColumn;
		rcRedraw.bottom = rcRedraw.top + cyRow;
		rcRedraw.right = rcRedraw.left + cxColumn;
		InvalidateRect(hwnd, &rcRedraw, TRUE);
		if (iColumn < SCREEN_COLUMNS - 1)
			iColumn++;
	}
	if (c == achProtoInit[iInitChar])
	{
		iInitChar++;
		if (iInitChar == strlen(achProtoInit))
		{
			iInitChar = 0;
			bTerminal = 0;
			RegisterManager(hwnd);
			SendInitRequest();
		}
	}
	else
	{
		iInitChar = 0;
	}
}

static	void	DoReading(void)
{
	static	char	achBuffer[READ_MAX];
	int	nRead;
	COMSTAT	cs;
	int	i;

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
			if (bTerminal)
			{
				HideCaret(hwnd);
				for (i = 0; i < nRead; i++)
					AddChar(achBuffer[i]);
				SetCaretPos(iColumn * cxColumn, iRow * cyRow);
				ShowCaret(hwnd);
			}
			else if (bFlushing)
			{
				FlushInput();
			}
			else
			{
				PacketReceiveData(achBuffer, nRead);
			}
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

void
PaintScreen(	HWND	hWnd)
{
	int	i, iRow;
	int	iCol;
	TEXTMETRIC tm;
	HFONT	hfontOld;
	HFONT	hfontFixed;
	PAINTSTRUCT ps;
	int	cyHeight;
	int	yPos;

	BeginPaint(hWnd, &ps);
	hfontFixed = (HFONT) GetStockObject(SYSTEM_FIXED_FONT);
	hfontOld = (HFONT) SelectObject(ps.hdc, (HGDIOBJ) hfontFixed);
	GetTextMetrics(ps.hdc, &tm);
	cyHeight = tm.tmHeight + tm.tmExternalLeading;
	for (i = 0; i < SCREEN_ROWS; i++)
	{
		iRow = ROW_INDEX(i);
		TextOut(ps.hdc, 0, i * cyRow, aachScreen[iRow], SCREEN_COLUMNS);
	}
	SelectObject(ps.hdc, (HGDIOBJ) hfontOld);
	EndPaint(hWnd, &ps);
}

LRESULT	CALLBACK _export
WindowProc(	HWND	hWnd,
		UINT	wMsg,
		WPARAM	wParam,
		LPARAM	lParam)
{
	char	c;

	switch(wMsg)
	{
	case WM_COMMAND:
		switch(wParam)
		{
		case 100:
			if (CommsEdit(hWnd))
			{
				if (iPortChanged)
				{
					CloseComm(idComm);
					OpenPort();
				}
				else
				{
					InitComm(idComm);
				}
			}
			break;

		case 101:
			if (bTerminal)
			{
				bTerminal = 0;
				RegisterManager(hwnd);
				SendInitRequest();
			}
			break;

		case 102:
			if (!bTerminal)
			{
				Shutdown();
				SendData("\030\030\030\030\030", 5);
			}
			break;

		case 103:
			PostQuitMessage(0);
			break;

		case 104:
			if (bTerminal)
			{
				SetCommBreak(idComm);
				SetTimer(hwnd, TIMER_ID_BREAK, 1500, 0);
			}
			break;

		case 105:
			if (bTerminal)
				DialNumber(hWnd);
			break;

		case 201:
			About(hWnd);
			break;
		}
		break;

	case WM_COMMNOTIFY:
		switch(LOWORD(lParam))
		{
		case CN_RECEIVE:
			DoReading();
			break;
		}
		break;

	case WM_CHAR:
		if (bTerminal)
		{
			c = wParam;
			SendData(&c, 1);
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

		case TIMER_ID_BREAK:
			ClearCommBreak(idComm);
			KillTimer(hWnd, TIMER_ID_BREAK);
			break;
		}
		break;

	case WM_PAINT:
		PaintScreen(hWnd);
		return 0;
		break;

	case WM_SETFOCUS:
		CreateCaret(hWnd, 0, cxColumn, cyRow);
		SetCaretPos(iColumn * cxColumn, iRow * cyRow);
		ShowCaret(hWnd);
		break;

	case WM_KILLFOCUS:
		DestroyCaret();
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
				{
					if (ptxr->id == -1)
					{
						SetWindowText(hwnd, "TwinSock - Connected");
						CloseWindow(hwnd);
						SetInitialised();
					}
				}
				else
				{
					ResponseReceived(ptxr);
				}
				free(ptxr);
				ptxr = 0;
				nBytes = 0;
			}
		}
	}
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
	bTerminal = 1;
	SetWindowText(hwnd, "TwinSock - No Connection");
	KillTimer(hwnd, TIMER_ID_SEND);
	KillTimer(hwnd, TIMER_ID_RECEIVE);
	KillTimer(hwnd, TIMER_ID_FLUSH);
	ReInitPackets();
}

void
OpenPort(void)
{
	char	achProfileEntry[256];
	char	achMsgBuf[512];
	char	*pchError;

	do
	{
		iPortChanged = 0;
		GetPrivateProfileString("Config", "Port", "COM1", achProfileEntry, 256, "TWINSOCK.INI");
		idComm = OpenComm(achProfileEntry, 16384, 16384);
		if (idComm < 0)
		{
		switch(idComm)
		{
			case IE_BADID:
				pchError = "No such device";
				break;

			case IE_BAUDRATE:
				pchError = "Speed not supported";
				break;

			case IE_BYTESIZE:
				pchError = "Byte size not supported";
				break;

			case IE_DEFAULT:
				pchError = "Bad default parameters for port";
				break;

			case IE_HARDWARE:
				pchError = "Port in use";
				break;

			case IE_MEMORY:
				pchError = "Out of memory";
				break;

			case IE_NOPEN:
				pchError = "Device not open";
				break;

			case IE_OPEN:
				pchError = "Device is already open";
				break;

			default:
				pchError = "Error Unknown";
				break;
			}
			sprintf(achMsgBuf,
				"Unable to open port \"%s\": %s",
				achProfileEntry,
				pchError);		
			if (MessageBox(0, achMsgBuf, "TwinSock", MB_OKCANCEL) == IDCANCEL)
				exit(1);
			if (!CommsEdit(hwnd))
				exit(1);
		}
	} while (idComm < 0);

	InitComm(idComm);
	EnableCommNotification(idComm, hwnd, 1, 0);
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
	char	*pchError;
	TEXTMETRIC	tm;
	HDC	hdc;
	HWND	hwndDesktop;
	HFONT	hfontOld;
	HFONT	hfontFixed;

	hinst = hInstance;
	hwndDesktop = GetDesktopWindow();
	hdc = GetDC(hwndDesktop);
	hfontFixed = (HFONT) GetStockObject(SYSTEM_FIXED_FONT);
	hfontOld = (HFONT) SelectObject(hdc, (HGDIOBJ) hfontFixed);
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, (HGDIOBJ) hfontOld);
	ReleaseDC(hwnd, hdc);
	cxColumn = tm.tmAveCharWidth;
	cyRow = tm.tmHeight + tm.tmExternalLeading;

	memset(aachScreen, 0x20, sizeof(aachScreen));
	wc.style = 0;
	wc.lpfnWndProc = WindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, "TSICON");
	wc.hCursor = 0;
	wc.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName = "TS_MENU";
	wc.lpszClassName = "TwinSock Communications";
	RegisterClass(&wc);
	hwnd = CreateWindow(	"TwinSock Communications",
				"TwinSock - No Connection",
				WS_OVERLAPPED |
				 WS_CAPTION |
				 WS_SYSMENU |
				 WS_MINIMIZEBOX |
				 WS_VISIBLE,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				cxColumn * SCREEN_COLUMNS + 2,
				cyRow * SCREEN_ROWS + 4 +
				  GetSystemMetrics(SM_CYCAPTION) +
				  GetSystemMetrics(SM_CYMENU),
				0,
				0,
				hInstance,
				0);

	OpenPort();

 	while (GetMessage(&msg, 0, 0, 0))
	{
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}
