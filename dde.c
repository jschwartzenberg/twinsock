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

#include <winsock.h>
#include <ddeml.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "tx.h"

#ifndef __DLL__
static	DWORD	iid;
static	FARPROC	lpDdeProc;
static	HSZ	hszService;
static	HSZ	hszTopic;
static	HSZ	hszHostInfo;
#endif

tws_hostinfo hostinfo = { 0 };

struct	DataPacket
{
	int	iBytes;
	char	*pchData;
	struct	DataPacket *pktNext;
};

struct	ClientStruct
{
	HCONV	hconv;
	HSZ	hszItem;
	struct	DataPacket *pktResponse;
	struct ClientStruct *pcsNext;
};

struct	ClientStruct *pcsList = 0;

struct ClientStruct *
GetClientStruct(HCONV hconv)
{
	struct ClientStruct *pcs;

	for (pcs = pcsList; pcs && pcs->hconv != hconv; pcs = pcs->pcsNext);
	return pcs;
}

void
CreateClientStruct(HCONV hconv)
{
	struct ClientStruct *pcs;

	pcs = (struct ClientStruct *) malloc(sizeof(struct ClientStruct));
	pcs->hconv = hconv;
	pcs->pcsNext = pcsList;
	pcs->hszItem = 0;
	pcs->pktResponse = 0;
	pcsList = pcs;
}

#pragma argsused
HDDEDATA CALLBACK _export
DdeProc(	UINT	type,
		UINT	fmt,
		HCONV	hconv,
		HSZ	hsz1,
		HSZ	hsz2,
		HDDEDATA hData,
		DWORD	dwData1,
		DWORD	dwData2)
{
	struct	ClientStruct *pcs;
	struct	DataPacket *pkt;
	int	iSize;
	char	*pchData;

	switch(type)
	{
#ifndef __DLL__
	case XTYP_ADVSTART:
		if (hsz1 != hszTopic)
			return FALSE;
		pcs = GetClientStruct(hconv);
		if (!pcs)
			return FALSE;
		pcs->hszItem = hsz2;
		DdeKeepStringHandle(iid, hsz2);
		return (HDDEDATA) TRUE;

	case XTYP_CONNECT:
		if (hsz1 != hszTopic ||
		    hsz2 != hszService)
			return FALSE;
		return (HDDEDATA) TRUE;

	case XTYP_CONNECT_CONFIRM:
		CreateClientStruct(hconv);
		return 0;

	case XTYP_ADVREQ:
		pcs = GetClientStruct(hconv);
		pkt = pcs->pktResponse;
		if (!pkt)
			return 0;
		pcs->pktResponse = pkt->pktNext;
		pchData = (char *) malloc(pkt->iBytes + sizeof(long));
		*(long *) pchData = pkt->iBytes;
		memcpy(pchData + sizeof(long), pkt->pchData, pkt->iBytes);
		hData = DdeCreateDataHandle(iid,
					pchData,
					pkt->iBytes + sizeof(long),
					0,
					pcs->hszItem,
					CF_PRIVATEFIRST,
					0);
		free(pchData);
		free(pkt->pchData);
		free(pkt);
		if (pcs->pktResponse)
			DdePostAdvise(iid, hszTopic, pcs->hszItem);
		return hData;

	case XTYP_REQUEST:
		hData = DdeCreateDataHandle(iid,
				(char *) &hostinfo,
				sizeof(hostinfo),
				0,
				hszHostInfo,
				CF_PRIVATEFIRST,
				0);
		return hData;

	case XTYP_POKE:
#endif
	case XTYP_ADVDATA:
		iSize = (int) DdeGetData(hData, 0, 0, 0);
		pchData = (char *) malloc(iSize);
		DdeGetData(hData, pchData, iSize, 0);
		iSize = (int) *(long *) pchData;
		ResponseReceived(pchData + sizeof(long), iSize, (long) hconv);
		free(pchData);
		return DDE_FACK;

	default:
		switch(type & XCLASS_MASK)
		{
		case XCLASS_BOOL:
			return FALSE;

		case XCLASS_DATA:
			return 0;

		case XCLASS_FLAGS:
			return DDE_FNOTPROCESSED;

		case XCLASS_NOTIFICATION:
			return 0;
		}
		break;
	}
	return 0;
}

#pragma argsused
BOOL
StartDDE(DC_ARGS
	 HINSTANCE	hInst)
{
	char	achItem[9];
#ifdef __DLL__
	HSZ	hszHostInfo;
#endif

#ifndef __DLL__
	lpDdeProc = MakeProcInstance((FARPROC) DdeProc, hInst);
#endif
	DC_CI(iid) = 0;
	if (
#ifdef __DLL__
	    DdeInitialize(&pClient->iid,
			  (PFNCALLBACK) DdeProc,
			  APPCMD_CLIENTONLY,
			  0)
#else
	    DdeInitialize(&iid,
			  (PFNCALLBACK) lpDdeProc,
			  0, 0)
#endif
	   )
	{
#ifndef __DLL__
		FreeProcInstance(lpDdeProc);
#endif
		return FALSE;
	}
	DC_CI(hszService) = DdeCreateStringHandle(DC_CI(iid), "TwinSock", 0);
	DC_CI(hszTopic) = DdeCreateStringHandle(DC_CI(iid), "Connection", 0);

#ifdef __DLL__
	pClient->hconv = DdeConnect(pClient->iid,
				pClient->hszService,
				pClient->hszTopic, 0);
	if (!pClient->hconv)
	{
		DdeUninitialize(pClient->iid);
		return FALSE;
	}
	sprintf(achItem, "%08lx", (long) pClient->hconv);
	pClient->hszItem = DdeCreateStringHandle(pClient->iid, achItem, 0);
	DdeClientTransaction(0, 0, pClient->hconv, pClient->hszItem,
			CF_PRIVATEFIRST,
			XTYP_ADVSTART, 1000, 0);
#else
	DdeNameService(iid, hszService, 0, DNS_REGISTER);
#endif
#ifdef __DLL__
	if (!hostinfo.nHosts)
	{
		HDDEDATA hData;

		hszHostInfo = DdeCreateStringHandle(pClient->iid, "HostInfo", 0);
		hData = DdeClientTransaction(0, 0, pClient->hconv, hszHostInfo,
				CF_PRIVATEFIRST,
				XTYP_REQUEST, 1000, 0);
		if (hData)
		{
			DdeGetData(hData, (char *) &hostinfo, sizeof(hostinfo), 0);
			DdeFreeDataHandle(hData);
		}
	}
#else
	hszHostInfo = DdeCreateStringHandle(iid, "HostInfo", 0);
#endif

	return TRUE;
}

void
StopDDE(DC_ARGS_)
{
#ifdef __DLL__
	DdeDisconnect(pClient->hconv);
	DdeClientTransaction(0, 0, pClient->hconv, pClient->hszItem,
			0, XTYP_ADVSTOP, 1000, 0);
#else
	DdeNameService(iid, hszService, 0, DNS_UNREGISTER);
#endif
	DdeFreeStringHandle(DC_CI(iid), DC_CI(hszService));
	DdeFreeStringHandle(DC_CI(iid), DC_CI(hszTopic));
	DdeUninitialize(DC_CI(iid));
#ifndef __DLL__
	FreeProcInstance(lpDdeProc);
#endif
}

void
SendDataTo(	DC_ARGS
		char	*pchData,
		int	iBytes,
		long	iTo)
{
#ifdef __DLL__
	char	*pchDataNew;

	pchDataNew = (char *) malloc(iBytes + sizeof(long));
	*(long *) pchDataNew = iBytes;
	memcpy(pchDataNew + sizeof(long), pchData, iBytes);
	DdeClientTransaction(pchDataNew, iBytes + sizeof(long),
			pClient->hconv, pClient->hszItem, CF_PRIVATEFIRST,
			XTYP_POKE, TIMEOUT_ASYNC, 0);
	free(pchDataNew);
#else
	struct	ClientStruct *pcs;
	struct	DataPacket **ppkt, *pkt;

	pcs = GetClientStruct((HCONV) iTo);
	pkt = (struct DataPacket *) malloc(sizeof(struct DataPacket));
	pkt->iBytes = iBytes;
	pkt->pchData = (char *) malloc(iBytes);
	memcpy(pkt->pchData, pchData, iBytes);
	pkt->pktNext = 0;
	for (ppkt = &pcs->pktResponse; *ppkt; ppkt = &(*ppkt)->pktNext);
	*ppkt = pkt;
	DdePostAdvise(iid, hszTopic, pcs->hszItem);
#endif
}
