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

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <ctype.h>
#include <netinet/in.h>
#include <errno.h>
#ifdef NEED_SELECT_H
#include <sys/select.h>
#endif
#include "twinsock.h"
#include "tx.h"
#include "sockinfo.h"

extern	enum Encoding eLine;

extern	void	PacketTransmitData(void *pvData, int iDataLen, int iStream);

#define	BUFFER_SIZE	1024

static	fd_set	fdsActive;
static	fd_set	fdsListener;
static	fd_set	fdsWriting;
static	int	nLargestFD;
static	char	achBuffer[BUFFER_SIZE];
static	int	bFlushing = 0;
void	FlushInput(void);

int	GetNextExpiry(struct timeval *ptvValue);
void	CheckTimers(void);
void	WriteSocketData(tws_sockinfo *psi);
extern	char	*sys_errlist[];

static	int	nHosts = 0;
static	char	achDomain[256] = "";
static	char	aachHosts[4][30];

#define	TIMER_ID_SEND		0
#define	TIMER_ID_RECEIVE	1
#define	TIMER_ID_FLUSH		2

int	BumpLargestFD(int iValue)
{
	if (iValue > nLargestFD)
		nLargestFD = iValue;
	FD_SET(iValue, &fdsActive);
};

int	SetListener(int iValue)
{
	FD_SET(iValue, &fdsListener);
}

int	SetWriter(int iValue)
{
	if (iValue > nLargestFD)
		nLargestFD = iValue;
	FD_SET(iValue, &fdsWriting);
}

int	SetClosed(int iValue)
{
	FD_CLR(iValue, &fdsListener);
	FD_CLR(iValue, &fdsActive);
	FD_CLR(iValue, &fdsWriting);
}

void
read_resolver(void)
{
	FILE	*fp;
	char	achInput[80];
	char	*pchIn, *pchOut;

	fp = fopen("/etc/resolv.conf", "r");
	if (!fp)
	{
		fp = fopen("/usr/etc/resolv.conf", "r");
		if (!fp)
			return;
	}
	while (fgets(achInput, 80, fp))
	{
		if (achInput[strlen(achInput) - 1] == '\n')
			achInput[strlen(achInput) - 1] = 0;
		pchIn = achInput;
		while (isspace(*pchIn))
				pchIn++;
		if (!strncmp(pchIn, "domain", 6))
		{
			pchIn += 6;
			while (isspace(*pchIn))
				pchIn++;
			pchOut = achDomain;
			while (*pchIn && !isspace(*pchIn))
				*pchOut++ = *pchIn++;
			*pchOut = 0;
		}
		else if (!strncmp(pchIn, "nameserver", 10) && nHosts < 4)
		{
			pchIn += 10;
			while (isspace(*pchIn))
				pchIn++;
			pchOut = aachHosts[nHosts];
			while (*pchIn && !isspace(*pchIn))
				*pchOut++ = *pchIn++;
			*pchOut = 0;
			nHosts++;
		}
	}
	fclose(fp);
}

static	char	achHostName[256];

main(int argc, char **argv)
{
	fd_set	fdsRead, fdsWrite, fdsExcept, fdsDummy;
	int	nRead;
	int	i;
	int	iResult;
	struct	sockaddr_in saSource;
	int	nSourceLen;
	int	s;
	struct	timeval tvZero;
	struct	timeval tv;

	while (argc-- > 1)
	{
		argv++;
		if (**argv =='-')
		{
			while (*++*argv)
			{
				switch(**argv)
				{
				case '8':
					eLine = E_8Bit;
					break;

				case 'n':
					eLine = E_8NoCtrl;
					break;

				case 'N':
					eLine = E_8NoHiCtrl;
					break;

				case 'x':
					eLine = E_8NoX;
					break;

				case 'X':
					eLine = E_8NoHiX;
					break;

				case 'e':
					eLine = E_Explicit;
					break;
				}
			}
		}
	}

	read_resolver();
	fprintf(stderr, "TwinSock Host 2.0\n");
	fprintf(stderr, "Copyright 1994-1995 Troy Rollo\n");
	fprintf(stderr, "This program is free software\n");
	fprintf(stderr, "See the file COPYING for details\n");
	fprintf(stderr, "\nStart your TwinSock client now\n");

	if (isatty(0))
		InitTerm();

	InitProtocol();
	*achHostName = 0;
	gethostname(achHostName, 256);
	fprintf(stdout, "!@$TSStart%d:%s", (int) eLine, achHostName);
	fprintf(stdout, "#%s", achDomain);
	for ( i = 0; i < nHosts; i++)
		fprintf(stdout, "*%s", aachHosts[i]);
	fprintf(stdout, "$@");
	fflush(stdout);

	nLargestFD = 0;
	FD_ZERO(&fdsActive);
	FD_ZERO(&fdsExcept);
	FD_ZERO(&fdsWriting);

	FD_SET(0, &fdsActive);

	while(1)
	{
		fdsRead = fdsActive;
		fdsWrite = fdsWriting;
		iResult = select(nLargestFD + 1,
				&fdsRead,
				&fdsWrite,
				&fdsExcept,
				GetNextExpiry(&tv) ? &tv : 0);
		CheckTimers();
		if (iResult <= 0)
			continue;
		if (FD_ISSET(0, &fdsRead))
		{
			nRead = read(0, achBuffer, BUFFER_SIZE);
			if (nRead > 0)
			{
				if (bFlushing)
					FlushInput();
				else
					PacketReceiveData(achBuffer, nRead);
			}
			else if (nRead == 0 && !isatty(0))
			{
				exit(0);
			}
		}
		for (i = 3; i <= nLargestFD; i++)
		{
			if (FD_ISSET(i, &fdsWrite))
			{
				tws_sockinfo *psi;

				FD_CLR(i, &fdsWriting);
				psi = FindSocketEntry(i);
				if (!psi)
					continue;
				if (psi->ptxrConnect)
					FinishConnect(psi);
				WriteSocketData(psi);
			}
			if (FD_ISSET(i, &fdsRead))
			{
				FD_ZERO(&fdsDummy);
				FD_SET(i, &fdsDummy);
				tvZero.tv_sec = tvZero.tv_usec = 0;
				if (select(i + 1,
						&fdsDummy,
						&fdsWrite,
						&fdsExcept,
						&tvZero) != 1)
					continue; /* Select lied */

				nSourceLen = sizeof(saSource);
				if (FD_ISSET(i, &fdsListener))
				{
					int	iClient;

					s = accept(i,
						&saSource,
						&nSourceLen);
					if (s == -1)
						continue;
					iClient = GetServerSocket();
					AddSocketEntry(iClient, s);
					BumpLargestFD(s);
					s = htonl(iClient);
					SendSocketData(GetClientFromServer(i),
						&s,
						sizeof(s),
						&saSource,
						nSourceLen,
						FN_Accept);
				}
				else
				{
					nRead = recvfrom(i,
							achBuffer,
							BUFFER_SIZE,
							0,
							&saSource,
							&nSourceLen);
					if (nRead < 0 &&
					    errno == ENOTCONN)
					{
						/* Was a datagram socket */
						FD_CLR(i, &fdsActive);
						continue;
					}
					if (nRead >= 0)
						SendSocketData(GetClientFromServer(i),
							achBuffer,
							nRead,
							&saSource,
							nSourceLen,
							FN_Data);
					if (nRead == 0)
						SetClosed(i);
				}
			}
		}
	}
}


void
SetTransmitTimeout(void)
{
	KillTimer(TIMER_ID_SEND);
	SetTimer(TIMER_ID_SEND, 10000);
}

void
KillTransmitTimeout(void)
{
	KillTimer(TIMER_ID_SEND);
}

void	SetReceiveTimeout(void)
{
	KillTimer(TIMER_ID_RECEIVE);
	SetTimer(TIMER_ID_RECEIVE, 1500);
}

void	KillReceiveTimeout(void)
{
	KillTimer(TIMER_ID_RECEIVE);
}

static	struct	timeval atvTimers[3];
static	int	iTimersOn;
static	int	iTimerRunning;
static	int	iInTimer = 0;


int	FireTimer(int iTimer)
{
	switch(iTimer)
	{
	case TIMER_ID_SEND:
		TimeoutReceived();
		break;

	case TIMER_ID_RECEIVE:
		PacketReceiveData(0, 0);
		break;

	case TIMER_ID_FLUSH:
		bFlushing = 0;
		break;
	}
}

int	GetNextExpiry(struct timeval *ptvValue)
{
	struct	timeval	tvNow;
	struct	timezone tzDummy;
	struct	timeval	tvGo;
	int	i;

	if (!iTimersOn)
		return 0;

	tvGo.tv_sec = 0x7fffffff;
	tvGo.tv_usec = 0;
	for (i = 0; i < 3; i++)
	{
		if (!(iTimersOn & (1 << i)))
			continue;
		if (atvTimers[i].tv_sec < tvGo.tv_sec ||
		    (atvTimers[i].tv_sec == tvGo.tv_sec &&
		     atvTimers[i].tv_usec < tvGo.tv_usec))
		{
			tvGo = atvTimers[i];
			iTimerRunning = i;
		}
	}
	gettimeofday(&tvNow, &tzDummy);
	ptvValue->tv_sec = tvGo.tv_sec - tvNow.tv_sec;
	ptvValue->tv_usec = tvGo.tv_usec - tvNow.tv_usec;
	while (ptvValue->tv_usec < 0)
	{
		ptvValue->tv_usec += 1000000l;
		ptvValue->tv_sec--;
	}
	while (ptvValue->tv_usec >= 1000000l)
	{
		ptvValue->tv_usec -= 1000000l;
		ptvValue->tv_sec++;
	}
	if (ptvValue->tv_sec < 0)
	{
		ptvValue->tv_sec = 0;
		ptvValue->tv_usec = 100;
	}
	if (!ptvValue->tv_sec &&
	    ptvValue->tv_usec < 100)
		ptvValue->tv_usec = 100;
	return 1;
}

void	CheckTimers(void)
{
	struct	timeval	tvNow;
	struct	timezone tzDummy;
	int	i;

	gettimeofday(&tvNow, &tzDummy);
	for (i = 0; i < 3; i++)
	{
		if (!(iTimersOn & (1 << i)))
			continue;
		if (atvTimers[i].tv_sec < tvNow.tv_sec ||
		    (atvTimers[i].tv_sec == tvNow.tv_sec &&
		     atvTimers[i].tv_usec < tvNow.tv_usec))
		{
			KillTimer(i);
			FireTimer(i);
		}
	}
}

int	SetTimer(int idTimer, int iTime)
{
	struct	timeval		tvNow;
	struct	timezone	tzDummy;

	gettimeofday(&tvNow, &tzDummy);
	atvTimers[idTimer] = tvNow;
	atvTimers[idTimer].tv_usec += (long) iTime % 1000l * 1000l;
	atvTimers[idTimer].tv_sec += iTime / 1000;
	while (atvTimers[idTimer].tv_usec > 1000000l)
	{
		atvTimers[idTimer].tv_sec++;
		atvTimers[idTimer].tv_usec -= 1000000l;
	}
	iTimersOn |= (1 << idTimer);
}

int	KillTimer(int idTimer)
{
	iTimersOn &= ~(1 << idTimer);
}

void	Shutdown(void)
{
	if (isatty(0))
		UnInitTerm();
	fprintf(stderr, "\nTwinSock Host Finished\n");
	exit(0);
}

int
SendData(void *pvData, int iDataLen)
{
	int	iLen;
	int	nWritten;

	if (bFlushing)
		return iDataLen; /* Lie */
	iLen = iDataLen;

	while (iLen > 0)
	{
		nWritten = write(1, pvData, iDataLen);
		if (nWritten > 0)
		{
			pvData = (char *) pvData + nWritten;
			iLen -= nWritten;
		}
	}
	return iDataLen;
}

void
FlushInput(void)
{
	bFlushing = 1;
	SetTimer(TIMER_ID_FLUSH, 1500);
}

static void
SendInitResponse(void)
{
	struct	tx_request txr;

	txr.iType = htons(FN_Init);
	txr.nArgs = 0;
	txr.nLen = htons(10);
	txr.id = -1;
	txr.nError = 0;
	PacketTransmitData(&txr, 10, -2);
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
				ptxr = (struct tx_request *) malloc(sizeof(struct tx_request) + nPktLen - 10);
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
					SendInitResponse();
				else
					ResponseReceived(ptxr);
				free(ptxr);
				ptxr = 0;
				nBytes = 0;
			}
		}
	}
}

void
WriteSocketData(tws_sockinfo *psi)
{
	int	iResult;

	if (!psi->pdata)
		return;
	if (psi->pdata->bTo)
	{
		iResult = sendto(psi->iServerSocket,
				psi->pdata->pchData + psi->pdata->iLoc,
				psi->pdata->nBytes - psi->pdata->iLoc,
				psi->pdata->iFlags,
				(struct sockaddr *) &psi->pdata->sinDest,
				sizeof(struct sockaddr_in));
	}
	else
	{
		iResult = sendto(psi->iServerSocket,
				psi->pdata->pchData + psi->pdata->iLoc,
				psi->pdata->nBytes - psi->pdata->iLoc,
				psi->pdata->iFlags);
	}
	if (iResult >= 0)
	{
		tws_data *pdata = psi->pdata;

		pdata->iLoc += iResult;
		if (pdata->iLoc == pdata->nBytes)
		{
			psi->pdata = pdata->pdataNext;
			free(pdata->pchData);
			free(pdata);
		}
	}
	else
	{
		if (errno != EWOULDBLOCK &&
		    errno != EINTR &&
		    errno != EAGAIN)
			return; /* The socket is dead */
	}
	if (psi->pdata)
		FD_SET(psi->iServerSocket, &fdsWriting);
}
