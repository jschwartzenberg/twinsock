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

#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "twinsock.h"
#include "tx.h"
#include "script.h"
#include "sockinfo.h"

typedef struct __tws_request
{
	short	idRequest;
	long	iSender;
	short	idSenderRequest;
	struct __tws_request *prqNext;
} tws_request;

typedef struct __tws_socket
{
	tws_sockinfo si;
	long	iOwner;
	struct __tws_socket *psckNext;
} tws_socket;

short	iRequestOut = 0;

tws_request *prqList = 0;
tws_request **pprqTail = &prqList;
tws_socket *psckList = 0;
tws_socket **ppsckTail = &psckList;

HINSTANCE	hinst;
static	BOOL	bUseNotify = TRUE;

static void SendInitRequest(void);
void	Shutdown(void);
void	OpenPort(void);
BOOL	ResolvEdit(HWND hwndParent);
extern	int	iPortChanged;
static	int	iBufferSize;

#define	READ_MAX	1024
#define	TIMER_ID_SEND		1
#define	TIMER_ID_RECEIVE	2
#define	TIMER_ID_FLUSH		3
#define	TIMER_ID_BREAK		4
#define TIMER_ID_COMMCHECK	5
#define TIMER_ID_SCRIPT		6

#define	READ_COMPLETED	(WM_USER + 1000)
#define WRITE_COMPLETED	(WM_USER + 1001)

#ifdef __FLAT__
static	HANDLE	idComm;
#else
static	int	idComm;
#endif
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

extern	enum Encoding eLine;
static	int	iInitChar = 0;
static	char	*pchCollect = 0;
static	char	*pchCollEnd = 0;
long	nDiscarded = 0;
long	nBytesRecvd = 0;


#ifdef __FLAT__
char *
GetLastErrorText(void)
{
	char *pchError;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		0,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &pchError,
		0, 0);
	return pchError;
}
#endif

u_short CALLBACK htons (u_short hostshort)
{
	char	*pchValue = (char *) &hostshort;
	char	c;

	c = pchValue[0];
	pchValue[0] = pchValue[1];
	pchValue[1] = c;
	return hostshort;
}

u_long CALLBACK htonl (u_long hostlong)
{
	char	*pchValue = (char *) &hostlong;
	char	c;

	c = pchValue[0];
	pchValue[0] = pchValue[3];
	pchValue[3] = c;
	c = pchValue[1];
	pchValue[1] = pchValue[2];
	pchValue[2] = c;
	return hostlong;
}

u_short CALLBACK ntohs (u_short netshort)
{
	char	*pchValue = (char *) &netshort;
	char	c;

	c = pchValue[0];
	pchValue[0] = pchValue[1];
	pchValue[1] = c;
	return netshort;
}

u_long CALLBACK ntohl (u_long netlong)
{
	char	*pchValue = (char *) &netlong;
	char	c;

	c = pchValue[0];
	pchValue[0] = pchValue[3];
	pchValue[3] = c;
	c = pchValue[1];
	pchValue[1] = pchValue[2];
	pchValue[2] = c;
	return netlong;
}

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

void
SendToScreen(char c)
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
}

static	void
WriteToScreen(char *pch, int iBytes)
{
	while (iBytes--)
		SendToScreen(*pch++);
}

static	void
FillResolverInfo(void)
{
	static	char	achBuffer[256];

	GetTwinSockSetting("Resolver", "Override", "0", achBuffer, sizeof(achBuffer));
	if (*hostinfo.achDomainName &&
	    hostinfo.nHosts &&
	    achBuffer[0] == '0')
		return;

	GetTwinSockSetting("Resolver", "Domain", "", achBuffer, sizeof(achBuffer));
	if (*achBuffer)
		strcpy(hostinfo.achDomainName, achBuffer);
	else if (!hostinfo.achDomainName)
		strcpy(hostinfo.achDomainName, "no-domain-specified");

	GetTwinSockSetting("Resolver", "Server1", "", achBuffer, sizeof(achBuffer));
	if (*achBuffer)
	{
		strcpy(hostinfo.aachHosts[0], achBuffer);
		hostinfo.nHosts = 1;
	}
	else
	{
		if (!hostinfo.nHosts)
		{
			strcpy(hostinfo.aachHosts[0], "127.0.0.1");
			hostinfo.nHosts++;
		}
		return;
	}

	GetTwinSockSetting("Resolver", "Server2", "", achBuffer, sizeof(achBuffer));
	if (*achBuffer)
	{
		strcpy(hostinfo.aachHosts[1], achBuffer);
		hostinfo.nHosts = 2;
	}
	else
	{
		return;
	}

	GetTwinSockSetting("Resolver", "Server3", "", achBuffer, sizeof(achBuffer));
	if (*achBuffer)
	{
		strcpy(hostinfo.aachHosts[2], achBuffer);
		hostinfo.nHosts = 3;
	}
	else
	{
		return;
	}
}

static	void
AddChar(char c)
{
	static	BOOL	bStillCollect = FALSE;

	SendToScreen(c);
	if (pchCollect &&
	    pchCollect != pchCollEnd &&
	    c != ':' && c != '#' && c != '*' && c != '$' && c != '@')
	{
		*pchCollect++ = c;
		*pchCollect = 0;
		bStillCollect = TRUE;
		return;
	}
	else if (c == achProtoInit[iInitChar])
	{
		iInitChar++;
		if (iInitChar == strlen(achProtoInit))
		{
			FillResolverInfo();
			if (*hostinfo.achHostName &&
			    !strchr(hostinfo.achHostName, '.'))
			{
				strcat(hostinfo.achHostName, ".");
				strcat(hostinfo.achHostName,
					hostinfo.achDomainName);
			}
			WriteToScreen("\r\nStarting Protocol, host=", 27);
			WriteToScreen(hostinfo.achHostName, strlen(hostinfo.achHostName));
			WriteToScreen("\r\n", 2);
			iInitChar = 0;
			bTerminal = 0;
			InitProtocol();
			SendInitRequest();
		}
	}
	else if (iInitChar == 9 && isdigit(c))
	{
		eLine = (enum Encoding) (c - '0');
	}
	else if (iInitChar == 9 && c == '#')
	{
		pchCollect = hostinfo.achDomainName;
		pchCollEnd = pchCollect + sizeof(hostinfo.achDomainName) - 1;
		bStillCollect = TRUE;
	}
	else if (iInitChar == 9 && c == ':')
	{
		pchCollect = hostinfo.achHostName;
		pchCollEnd = pchCollect + sizeof(hostinfo.achHostName) - 1;
		bStillCollect = TRUE;
	}
	else if (iInitChar == 9 && c == '*' && hostinfo.nHosts < 4)
	{
		pchCollect = hostinfo.aachHosts[hostinfo.nHosts++];
		pchCollEnd = pchCollect + sizeof(hostinfo.aachHosts[0]) - 1;
		bStillCollect = TRUE;
	}
	else if (iInitChar)
	{
		iInitChar = 0;
		eLine = E_6Bit;
	}
	if (!bStillCollect)
		pchCollect = 0;
	CheckScripts(hwnd, c);
}

static	void
ProcessReceivedData(char *pchBuffer,
		    int nRead)
{
	int	i;

	if (bTerminal)
	{
		HideCaret(hwnd);
		for (i = 0; i < nRead; i++)
			AddChar(pchBuffer[i]);
		SetCaretPos(iColumn * cxColumn, iRow * cyRow);
		ShowCaret(hwnd);
	}
	else if (bFlushing)
	{
		nDiscarded += nRead;
		FlushInput();
	}
	else
	{
		nBytesRecvd += nRead;
		PacketReceiveData(pchBuffer, nRead);
	}
}

#ifdef __FLAT__
static	char	achReadBuffer[2048];
static	DWORD	dwRead;
static	DWORD	dwWrite;
static	OVERLAPPED	olRead;
static	OVERLAPPED	olWrite;
static	HANDLE	heventRead = 0;
static	HANDLE	heventWrite = 0;
static	void	PrepareToRead(void);

#pragma argsused
static	void
ReceiveCompleted(void)
{
	ProcessReceivedData(achReadBuffer, (int) dwRead);
	PrepareToRead();
}

static void
PrepareToRead(void)
{
	olRead.Internal = olRead.InternalHigh = olRead.Offset = olRead.OffsetHigh = 0;
	olRead.hEvent = heventRead;
	while (ReadFile(idComm,
			achReadBuffer, sizeof(achReadBuffer),
			&dwRead, &olRead));

	if (GetLastError() != ERROR_IO_PENDING)
	{
		char *pchError = GetLastErrorText();

		MessageBox(hwnd, pchError, "Fatal error reading communications port", MB_OK);
		if (pchError)
			LocalFree(pchError);
	}
}

#else
static	void	DoReading(void)
{
	static	char	achBuffer[READ_MAX];
	static	BOOL	bAlreadyHere = FALSE;
	int	nRead;
	COMSTAT	cs;
	int	i;

	if (bAlreadyHere)
		return;
	bAlreadyHere = TRUE;
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
			ProcessReceivedData(achBuffer, nRead);
		}
	} while (nRead || cs.cbInQue);
	bAlreadyHere = FALSE;
}
#endif

#ifdef __FLAT__

typedef	struct	__tws_outdata
{
	char	*pchWrite;
	int	nWrite;
	struct	__tws_outdata *podNext;	
} tws_outdata;

static	tws_outdata	*podList = 0, **ppodTail = &podList;
static	BOOL	bWaiting = FALSE;

void
AddToDataQueue(void *pvData, int iDataLen)
{
	tws_outdata *podNow = (tws_outdata *) malloc(sizeof(tws_outdata));

	*ppodTail = podNow;
	ppodTail = &podNow->podNext;
	podNow->pchWrite = (char *) malloc(iDataLen);
	memcpy(podNow->pchWrite, pvData, iDataLen);
	podNow->nWrite = iDataLen;
	podNow->podNext = 0;
}

#pragma argsused
void	WINAPI
SendCompleted(	void)
{
	tws_outdata *pod;

	while (podList)
	{
		pod = podList;
		podList = podList->podNext;
		if (!podList)
			ppodTail = &podList;
		olWrite.Internal = olWrite.InternalHigh = olWrite.Offset = olWrite.OffsetHigh = 0;
		olWrite.hEvent = heventWrite;
		if (!WriteFile(idComm, pod->pchWrite, pod->nWrite, &dwWrite, &olWrite))
		{
			if (GetLastError() != ERROR_IO_PENDING)
			{
				char *pchError = GetLastErrorText();

				MessageBox(hwnd, pchError, "Fatal error writing communications port", MB_OK);
				if (pchError)
					LocalFree(pchError);
			}
			free(pod->pchWrite);
			free(pod);
			return;
		}
		free(pod->pchWrite);
		free(pod);
	}
	bWaiting = FALSE;
}

int	SendData(void *pvData, int iDataLen)
{
	if (bFlushing)
		return iDataLen; /* Lie */
	if (bWaiting)
	{
		AddToDataQueue(pvData, iDataLen);
		return iDataLen;
	}
	olWrite.Internal = olWrite.InternalHigh = olWrite.Offset = olWrite.OffsetHigh = 0;
	olWrite.hEvent = heventWrite;
	if (!WriteFile(idComm, pvData, iDataLen, &dwWrite, &olWrite))
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			char *pchError = GetLastErrorText();

			MessageBox(hwnd, pchError, "Fatal error writing communications port", MB_OK);
			if (pchError)
				LocalFree(pchError);
		}
		else
		{
			bWaiting = TRUE;
		}
	}
	return iDataLen;
}

#else
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
		/* If the write would overflow the buffer, pretend we wrote it OK
		 * and return immediately
		 */
		GetCommError(idComm, &cs);
		if (cs.cbOutQue + iLen >= iBufferSize)
			return iDataLen;
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
#endif

#ifdef __FLAT__

static	HANDLE	hThread = 0;
static	DWORD	dwThreadID;

#pragma argsused
DWORD
CommsReader(LPDWORD lpdwParam)
{
	DWORD	dwEvents;
	HANDLE	ahEvents[2];

	ahEvents[0] = heventRead;
	ahEvents[1] = heventWrite;
	while (1)
	{
		switch(WaitForMultipleObjects(2, ahEvents, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
			GetOverlappedResult(idComm, &olRead, &dwRead, TRUE);
			ResetEvent(heventRead);
			PostMessage(hwnd, READ_COMPLETED, 0, 0);
			break;

		case WAIT_OBJECT_0 + 1:
			GetOverlappedResult(idComm, &olWrite, &dwWrite, TRUE);
			ResetEvent(heventWrite);
			PostMessage(hwnd, WRITE_COMPLETED, 0, 0);
			break;
		}
	}
}

void
StopCommThread(void)
{
	if (hThread)
	{
		CloseHandle(hThread);
		hThread = 0;
	}
}

void
StartCommThread(void)
{
	StopCommThread();
	if (!heventRead)
		heventRead = CreateEvent(0, TRUE, 0, 0);
	if (!heventWrite)
		heventWrite = CreateEvent(0, TRUE, 0, 0);
	hThread = CreateThread( 0,
				8192,
				(LPTHREAD_START_ROUTINE) CommsReader,
				0,
				0,
				&dwThreadID);
}
#endif

void
PaintScreen(	HWND	hWnd)
{
	int	i, iRow;
	int	iCol;
	TEXTMETRIC tm;
	HFONT	hfontOld;
	HFONT	hfontFixed;
	PAINTSTRUCT ps;
	int	yPos;

	BeginPaint(hWnd, &ps);
	hfontFixed = (HFONT) GetStockObject(SYSTEM_FIXED_FONT);
	hfontOld = (HFONT) SelectObject(ps.hdc, (HGDIOBJ) hfontFixed);
	GetTextMetrics(ps.hdc, &tm);
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
	case WM_SYSCOMMAND:
		CheckScriptSysCommands(hWnd, wParam, lParam);
		break;

	case WM_COMMAND:
		if (CheckScriptCommands(hWnd, wParam, &bTerminal))
			break;
		switch(wParam)
		{
		case 100:
			if (CommsEdit(hWnd))
			{
				if (iPortChanged)
				{
#ifdef __FLAT__
					StopCommThread();
					CloseHandle(idComm);
#else
					CloseComm(idComm);
#endif
					OpenPort();
				}
				else
				{
					InitComm(idComm);
				}
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

		case 106:
			if (!bTerminal)
				ShowProtoInfo(hWnd);
			break;

		case 107:
			if (bTerminal)
				ResolvEdit(hWnd);
			break;

		case 201:
			About(hWnd);
			break;

		case 301:
			WinHelp(hWnd, "TWINSOCK.HLP", HELP_CONTENTS, 0);
			break;
		}
		break;

#ifdef __FLAT__
	case READ_COMPLETED:
		ReceiveCompleted();
		break;

	case WRITE_COMPLETED:
		SendCompleted();
		break;
#else
	case WM_COMMNOTIFY:
		switch(LOWORD(lParam))
		{
		case CN_RECEIVE:
			DoReading();
			break;
		}
		break;
#endif

	case WM_CHAR:
		PerhapsKillScript(hWnd);
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
			TimeoutReceived();
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

#ifndef __FLAT__
 		case TIMER_ID_COMMCHECK:
 			DoReading();
			break;
#endif

		case TIMER_ID_SCRIPT:
			ScriptTimeOut(hWnd);
			break;
		}
		break;

	case WM_PAINT:
		PaintScreen(hWnd);
		return 0;

	case WM_SETFOCUS:
		CreateCaret(hWnd, 0, cxColumn, cyRow);
		SetCaretPos(iColumn * cxColumn, iRow * cyRow);
		ShowCaret(hWnd);
		break;

	case WM_KILLFOCUS:
		DestroyCaret();
		break;

	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hWnd, wMsg, wParam, lParam);
}

/* Process data received from a client */
#pragma argsused
void
ResponseReceived(	char	*pchData,
			int	iSize,
			long	iFrom)
{
	struct tx_request *ptxr = (struct tx_request *) pchData;
	tws_request *prq;
	tws_socket *psck, **ppsck;
	enum Functions ft;
	int	i;
	char	*c;

	prq = (tws_request *) malloc(sizeof(tws_request));
	prq->prqNext = 0;
	prq->idRequest = iRequestOut++;;
	prq->iSender = iFrom;
	prq->idSenderRequest = ntohs(ptxr->id);
	*pprqTail = prq;
	pprqTail = &prq->prqNext;
	ptxr->id = htons(prq->idRequest);
	ft = (enum Functions) ntohs(ptxr->iType);
	if (HasSocketArg(ft))
	{
		/* If this is a socket call, we need to create an entry
		 * for the new socket
		 */
		if (ft == FN_Socket)
		{
			c = ptxr->pchData;
			if (ntohs(*(short *) (c + 2)) == 2)
				i = ntohs(*(short *) (c + 4));
			else
				i = (short) ntohl(*(long *) (c + 4));
			if (i != -1)
			{
				psck = (tws_socket *) malloc(sizeof(tws_socket));
				psck->iOwner = iFrom;
				psck->si.iClientSocket = i;
				psck->si.iServerSocket = GetClientSocket();
				psck->psckNext = 0;
				*ppsckTail = psck;
				ppsckTail = &psck->psckNext;
			}
		}

		/* Now change the client's idea of the socket number into
		 * our idea of the socket number.
		 */
		c = ptxr->pchData;
		if (ntohs(*(short *) (c + 2)) == 2)
			i = ntohs(*(short *) (c + 4));
		else
			i = (short) ntohl(*(long *) (c + 4));
		for (psck = psckList;
		     psck->si.iClientSocket != i || psck->iOwner != iFrom;
		     psck = psck->psckNext);
		i = psck->si.iServerSocket;
		if (ntohs(*(short *) (c + 2)) == 2)
			*(short *) (c + 4) = htons(i);
		else
			*(long *) (c + 4) = htonl(i);

	}
	PacketTransmitData(pchData, iSize, 0);
#ifndef __FLAT__
	DoReading();
#endif
}

long
AdjustDataIn(	struct tx_request *ptxr,
		enum Functions ft)
{
	tws_socket *psck;
	short	idRequest;
	long	iOwner;
	int	i;
	BOOL	bLong;
	char	*c;

	idRequest = ntohs(ptxr->id);
	for (psck = psckList;
	     psck && psck->si.iServerSocket != idRequest;
	     psck = psck->psckNext);
	if (!psck)
		return 0;
	iOwner = psck->iOwner;
	ptxr->id = htons(psck->si.iClientSocket);

	if (ft == FN_Accept)
	{
		if (ntohs(ptxr->nLen) ==
				sizeof(struct sockaddr_in) +
				 sizeof(short) * 6)
			i = ntohs(*(short *) (ptxr->pchData +
				sizeof(struct sockaddr_in)));
		else
			i = (int) ntohl(*(long *) (ptxr->pchData +
					sizeof(struct sockaddr_in)));

		psck = (tws_socket *) malloc(sizeof(tws_socket));
		psck->iOwner = iOwner;
		/* Because this one is allocated by tshost, and is unique over the
		 * range of all applications using TwinSock, we can use it both ways
		 * as is
		 */
		psck->si.iServerSocket = i;
		psck->si.iClientSocket = i;
		psck->psckNext = 0;
		*ppsckTail = psck;
		ppsckTail = &psck->psckNext;
	}
	return iOwner;
}

long
AdjustRequestIn(struct tx_request *ptxr,
		enum Functions ft)
{
	short	idRequest;
	long	iSender;
	tws_request **pprq, *prqTemp;
	tws_socket **ppsck, *psck;
	int	i;
	char	*c;

	idRequest = ntohs(ptxr->id);
	for (pprq = &prqList;
	     *pprq && (*pprq)->idRequest != idRequest;
	     pprq = &(*pprq)->prqNext);
	if (!*pprq)
		return 0;
	iSender = (*pprq)->iSender;
	ptxr->id = htons((*pprq)->idSenderRequest);
	prqTemp = (*pprq)->prqNext;
	free(*pprq);
	*pprq = prqTemp;
	if (!prqTemp)
		pprqTail = pprq;
	if (HasSocketArg(ft))
	{
		c = ptxr->pchData;
		if (ntohs(*(short *) (c + 2)) == 2)
			i = ntohs(*(short *) (c + 4));
		else
			i = (short) ntohl(*(long *) (c + 4));
		for (psck = psckList;
		     psck && psck->si.iServerSocket != i;
		     psck = psck->psckNext);
		if (psck)
		{
			i = psck->si.iClientSocket;
			if (ntohs(*(short *) (c + 2)) == 2)
				*(short *) (c + 4) = htons(i);
			else
				*(long *) (c + 4) = htonl(i);
		}

		/* If this is a close call, remove our entry describing the
		 * socket.
		 */
		if (ft == FN_Close)
		{
			c = ptxr->pchData;
			if (ntohs(*(short *) (c + 2)) == 2)
				i = ntohs(*(short *) (c + 4));
			else
				i = (short) ntohl(*(long *) (c + 4));
			for (ppsck = &psckList;
			     *ppsck &&
			      ((*ppsck)->si.iClientSocket != i || (*ppsck)->iOwner != iSender);
			     ppsck = &(*ppsck)->psckNext);
			if (*ppsck)
			{
				if (!((*ppsck)->si.iServerSocket & 1))
					ReleaseClientSocket((*ppsck)->si.iServerSocket);
				psck = (*ppsck)->psckNext;
				free(*ppsck);
				*ppsck = psck;
				if (!psck)
					ppsckTail = ppsck;
			}
		}
	}

	return iSender;
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
	int	i;
	long	iSender;

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
						if (hostinfo.achHostName)
						{
							char	achBuffer[160];

							strcpy(achBuffer, "TwinSock - ");
							strcat(achBuffer, hostinfo.achHostName);
							SetWindowText(hwnd, achBuffer);
						}
						else
						{
							SetWindowText(hwnd, "TwinSock - Connected");
						}
						CloseWindow(hwnd);
						StartDDE(hinst);
					}
				}
				else if (ft == FN_Message)
				{
					WriteToScreen("\r\n", 2);
					WriteToScreen(ptxr->pchData, nPktLen - 10);
				}
				else
				{
					if (ft == FN_Data ||
					    ft == FN_Accept)
						iSender = AdjustDataIn(ptxr, ft);
					else
						iSender = AdjustRequestIn(ptxr, ft);
					if (iSender)
						SendDataTo((char *) ptxr, nBytes, iSender);
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
	KillTimer(hwnd, TIMER_ID_SCRIPT);
	ReInitPackets();
}

void
OpenPort(void)
{
	char	achProfileEntry[256];
	char	achMsgBuf[512];
	char	*pchError = 0;

	do
	{
		iPortChanged = 0;
		GetPrivateProfileString("Config", "Port", "COM1", achProfileEntry, 256, "TWINSOCK.INI");
		iBufferSize = GetPrivateProfileInt("Config", "BufferSize", 16384, "TWINSOCK.INI");
#ifdef __FLAT__
		idComm = CreateFile(achProfileEntry,
				GENERIC_READ | GENERIC_WRITE,
				0, 0, OPEN_EXISTING,
				FILE_FLAG_OVERLAPPED, 0);
		if (idComm != INVALID_HANDLE_VALUE)
		{
			SetupComm(idComm, iBufferSize, iBufferSize);
		}
		else
		{
			pchError = GetLastErrorText();
#else
		idComm = OpenComm(achProfileEntry, iBufferSize, iBufferSize);
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
#endif
			sprintf(achMsgBuf,
				"Unable to open port \"%s\": %s",
				achProfileEntry,
				pchError);		
			if (MessageBox(0, achMsgBuf, "TwinSock", MB_OKCANCEL) == IDCANCEL)
				exit(1);
			if (!CommsEdit(hwnd))
				exit(1);
		}
#ifdef __FLAT__
		if (pchError)
		{
			LocalFree(pchError);
			pchError = 0;
		}
	} while (idComm == INVALID_HANDLE_VALUE);
#else
	} while (idComm < 0);
#endif

	InitComm(idComm);
#ifdef __FLAT__
	StartCommThread();
	PrepareToRead();
#else
	bUseNotify = GetPrivateProfileInt("Config", "UseNotify", 1, "TWINSOCK.INI");
	if (bUseNotify)
	{
		EnableCommNotification(idComm, hwnd, 10, 0);
	 	SetTimer(hwnd, TIMER_ID_COMMCHECK, 1000, 0);
	}
#endif
}

static	BOOL
MyPeekMessage(	MSG	*pmsg,
		BOOL	*pbHadMessage)
{
	*pbHadMessage = PeekMessage(pmsg, 0, 0, 0, PM_REMOVE);
#ifndef __FLAT__
	DoReading();
#endif
	return !*pbHadMessage || pmsg->message != WM_QUIT;
}

#pragma argsused
int	CALLBACK
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
	BOOL	bHadMessage;
	BOOL	bTempNotify;

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
	ConfigureScripts(hInstance, hwnd, lpCmdLine);

 	while ((bTempNotify = bUseNotify) ? GetMessage(&msg, 0, 0, 0) : MyPeekMessage(&msg, &bHadMessage))
	{
		if ((bTempNotify || bHadMessage) &&
		    !CustomTranslateAccelerator(hwnd, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	TerminateScripts();
	StopDDE();
	return 0;
}

