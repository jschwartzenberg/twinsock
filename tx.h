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

struct	tx_request
{
	short	iType;
	short	nArgs;
	short	nLen;
	short	id;
	short	nError;
	char	pchData[1];
};

#ifdef _Windows
#if defined(__FLAT__) && !defined(HTASK)
#define HTASK int
#endif

struct	tx_queue
{
	struct	tx_request *ptxr;
	short	id;
	struct tx_queue *ptxqNext;
	BOOL	bDone;
	char	*pchLocation;
	HWND	hwnd;
	u_int	wMsg;
	enum Functions ft;
	HTASK	htask;
};

#ifdef __DLL__
typedef struct __tws_client
{
	DWORD	iid;
	HSZ	hszService;
	HSZ	hszTopic;
	HSZ	hszItem;
	HCONV	hconv;
} tws_client;
#define DC_ARGS tws_client *pClient,
#define DC_ARGS_ tws_client *pClient
#define	DC_CI(x)	pClient->x
#else
#define DC_ARGS
#define	DC_ARGS_	void
#define	DC_CI(x)	x
#endif

typedef struct	__tws_hostinfo
{
	short	nHosts;
	char	achHostName[256];
	char	achDomainName[256];
	char	aachHosts[4][30];
} tws_hostinfo;

void	ResponseReceived(char *pchData, int iSize, long iFrom);
void	SendDataTo(DC_ARGS char *pchData, int iBytes, long iTo);
BOOL	StartDDE(DC_ARGS HINSTANCE hInst);
void	StopDDE(DC_ARGS_);

extern	tws_hostinfo hostinfo;
#endif

