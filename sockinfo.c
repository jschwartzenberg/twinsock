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

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "sockinfo.h"
#include "tx.h"

tws_sockinfo *psiList = 0;

void
AddSocketEntry(int iClient, int iServer)
{
	tws_sockinfo *psi;

	psi = (tws_sockinfo *) malloc(sizeof(tws_sockinfo));
	psi->iClientSocket = iClient;
	psi->iServerSocket = iServer;
	psi->pdata = 0;
	psi->psiNext = psiList;
	psi->ptxrConnect = 0;
	psiList = psi;
	return;
}

int
GetClientFromServer(int iServer)
{
	tws_sockinfo *psi;

	for (psi = psiList; psi; psi = psi->psiNext)
		if (psi->iServerSocket == iServer)
			return psi->iClientSocket;
	return -1;
}

int
GetServerFromClient(int iClient)
{
	tws_sockinfo *psi;

	for (psi = psiList; psi; psi = psi->psiNext)
		if (psi->iClientSocket == iClient)
			return psi->iServerSocket;
	return -1;
}


tws_sockinfo *
FindSocketEntry(int iServer)
{
	tws_sockinfo *psi;

	for (psi = psiList; psi; psi = psi->psiNext)
		if (psi->iServerSocket == iServer)
			return psi;
	return 0;
}

void
ReleaseSocketEntry(int iClient)
{
	tws_sockinfo **ppsi, *psi;

	for (ppsi = &psiList; *ppsi; ppsi = &(*ppsi)->psiNext)
	{
		if ((*ppsi)->iClientSocket == iClient)
		{
			tws_data *pdata;

			psi = (*ppsi)->psiNext;
			if ((*ppsi)->ptxrConnect)
				free((*ppsi)->ptxrConnect);
			while ((*ppsi)->pdata)
			{
				pdata = (*ppsi)->pdata;
				(*ppsi)->pdata = pdata->pdataNext;
				free(pdata->pchData);
				free(pdata);
			}
			free(*ppsi);
			*ppsi = psi;
			return;
		}
	}
}


void
QueueConnectWait(int iSocket,
		struct tx_request *ptxr_)
{
	tws_sockinfo *psi = FindSocketEntry(iSocket);

	if (psi)
	{
		int	iBytes = ntohs(ptxr_->nLen);
		struct tx_request *ptxr = (struct tx_request *) malloc(iBytes);

		memcpy(ptxr, ptxr_, iBytes);
		psi->ptxrConnect = ptxr;
	}
	else
	{
	}
}


void
QueueSendRequest(	int	iServerSocket,
			char	*pchData,
			int	iBytes,
			int	iFlags,
			struct sockaddr_in *psin)
{
	tws_data *pdata, **ppdata;
	tws_sockinfo *psi = FindSocketEntry(iServerSocket);

	if (!psi)
		return;
	pdata = (tws_data *) malloc(sizeof(tws_data));
	pdata->pchData = (char *) malloc(iBytes);
	memcpy(pdata->pchData, pchData, iBytes);
	pdata->nBytes = iBytes;
	pdata->iLoc = 0;
	pdata->pdataNext = 0;
	pdata->iFlags = iFlags;
	if (psin)
	{
		pdata->sinDest = *psin;
		pdata->bTo = 1;
	}
	else
	{
		pdata->bTo = 0;
	}
	for (ppdata = &psi->pdata;
	     *ppdata;
	     ppdata = &(*ppdata)->pdataNext);
	*ppdata = pdata;
	if (psi->pdata == pdata)
		WriteSocketData(psi);
}
