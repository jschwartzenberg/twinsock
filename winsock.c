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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dos.h>
#include <ddeml.h>
#include "twinsock.h"
#include "tx.h"
#include "sockinfo.h"
#include "dns.h"

#ifdef __MSDOS__
#define	PORTSWAP(x)	ntohs(x)
#else
#define	PORTSWAP(x)	ntohl(x)
#endif

static	int	iErrno = 0;
static	short	idNext = 0;

static	struct	per_task	*pptList = 0;
static	struct	per_socket	*ppsList = 0;
static	struct	tx_queue	*ptxqList = 0;

static	HINSTANCE hInstance = 0;

static	void	FireAsyncRequest(struct tx_queue *ptxq);
static	void	ContinueDNSQuery(struct per_socket *pps);
static	void	NextDNSServer(	struct per_socket *pps);
static	void	NextDNSTry(	struct per_socket *pps);

#pragma argsused
int CALLBACK
LibMain(HINSTANCE hInst, WORD wOne, WORD wTwo, LPSTR lpstr)
{
	hInstance = hInst;
	return TRUE;
}

#pragma argsused
int CALLBACK
WEP(int iExitType)
{
	return 1;
}

#ifdef __FLAT__
#pragma argsused
BOOL WINAPI
DllEntryPoint(	HINSTANCE hinstDLL,
		DWORD	dwReason,
		LPVOID lpvReserved)
{
	switch(dwReason)
	{
	case DLL_PROCESS_ATTACH:
		return LibMain(hinstDLL, 0, 0, 0);

	case DLL_PROCESS_DETACH:
		return WEP(0);
	}
	return TRUE;
}
#endif

void
CopyDataIn(	void		*pvSource,
		enum arg_type	at,
		void		*pvDest,
		int		nLen)
{
	switch(at)
	{
	case AT_Int16:
	case AT_Int16Ptr:
		*(short *) pvDest = ntohs(*(short *) pvSource);
		break;

	case AT_Int32:
	case AT_Int32Ptr:
		*(long *) pvDest = ntohl(*(long *) pvSource);
		break;

	case AT_Char:
		*(char *) pvDest = *(char *) pvSource;
		break;

	case AT_GenPtr:
	case AT_String:
		memcpy(pvDest, pvSource, nLen);
		break;
	}
}

void
CopyDataOut(	void		*pvDest,
		enum arg_type	at,
		void		*pvSource,
		int		nLen)
{
	switch(at)
	{
	case AT_Int16Ptr:
	case AT_Int16:
		*(short *) pvDest = htons(*(short *) pvSource);
		break;

	case AT_Int32:
	case AT_Int32Ptr:
		*(long *) pvDest = htonl(*(long *) pvSource);
		break;

	case AT_Char:
		*(char *) pvDest = *(char *) pvSource;
		break;

	case AT_GenPtr:
	case AT_String:
		memcpy(pvDest, pvSource, nLen);
		break;
	}
}

static	struct	per_task *
GetAnotherTaskInfo(HTASK htask)
{
	struct per_task *ppt;

	for (ppt = pptList; ppt; ppt = ppt->pptNext)
	{
		if (ppt->htask == htask)
			return ppt;
	}
	iErrno = WSANOTINITIALISED;
	return 0;
}

static	struct	per_task *
GetTaskInfo(void)
{
#ifdef __FLAT__
	return GetAnotherTaskInfo(0);
#else
	return GetAnotherTaskInfo(GetCurrentTask());
#endif
}


static	struct	per_socket *
GetSocketInfo(SOCKET s)
{
	struct per_socket *pps;


	for (pps = ppsList; pps; pps = pps->ppsNext)
	{
		if (pps->s == s)
			return pps;
	}
	iErrno = WSAENOTSOCK;
	return 0;
}

static	void
Notify(struct per_socket *pps,
	int iCode)
{
	if (pps->iEvents & iCode)
		PostMessage(pps->hWnd, pps->wMsg, pps->s, WSAMAKESELECTREPLY(iCode, 0));
}

static	void
NotifyError(	struct per_socket *pps,
		int	iCode,
		int	iErrno)
{
	if (pps->iEvents & iCode)
		PostMessage(pps->hWnd, pps->wMsg, pps->s, WSAMAKESELECTREPLY(iCode, iErrno));
}

static	struct	per_socket *
NewSocket(struct per_task *ppt, SOCKET s)
{
	struct per_socket *ppsNew;

	ppsNew = (struct per_socket *) malloc(sizeof(struct per_socket));
	if (ppsNew == 0)
		return (ppsNew);
	ppsNew->s = s;
	ppsNew->iFlags = 0;
	ppsNew->pdIn = 0;
	ppsNew->pdOut = 0;
	ppsNew->htaskOwner = ppt->htask;
	ppsNew->ppsNext = ppsList;
	ppsNew->iEvents = 0;
	ppsNew->nOutstanding = 0;
	ppsNew->pdnsi = 0;
	ppsList = ppsNew;
	return ppsNew;
	
}

static void SendEarlyClose(SOCKET s);

static	void
RemoveSocket(struct per_socket *pps)
{
	struct	per_socket **ppps, *ppsParent;
	struct	data	**ppd, *pd;

	/* If our parent has noticed we're here, we need to remove ourselves
	 * from the list of sockets awaiting acception.
	 */
	for (ppsParent = ppsList; ppsParent; ppsParent = ppsParent->ppsNext)
	{
		if (!(ppsParent->iFlags & PSF_ACCEPT))
			continue;
		for (ppd = &ppsParent->pdIn; *ppd;)
		{
			if ((*ppd)->pchData == (char *) pps)
			{
				pd = *ppd;
				*ppd = pd->pdNext;
				free(pd);
			}
			else
			{
				ppd = &(*ppd)->pdNext;
			}
		}
	}

	/* Find our own position in the list */
	for (ppps = &ppsList; *ppps; ppps = &(*ppps)->ppsNext)
	{
		if (*ppps == pps)
		{
			*ppps = pps->ppsNext;

			/* If we have unacknowledged children, kill them */
			while (pps->pdIn)
			{
				pd = pps->pdIn;
				pps->pdIn = pd->pdNext;
				if (pps->iFlags & PSF_ACCEPT)
				{
					SendEarlyClose(((struct per_socket *) pd->pchData)->s);
				}
				else
				{
					free(pd->pchData);
				}
				free(pd);
			}
			if (!(((int) pps->s) & 1))
				ReleaseClientSocket(pps->s);
			free(pps);
			return;
		}
	}
}

static	void
FunctionReceived(SOCKET s, void *pvData, int nLen, enum Functions ft)
{
	struct	data	*pdNew, **ppdList;
	struct	per_socket *pps, *ppsNew;
	struct	per_task *ppt;
	int		ns;

	pps = GetSocketInfo(s);
	if (!pps)
	{
		if (ft == FN_Accept)
		{
			nLen -= sizeof(struct sockaddr_in);
			pvData = (char *) pvData + sizeof(struct sockaddr_in);
			if (nLen == sizeof(long))
				ns = (int) ntohl(*(long *) pvData);
			else
				ns = (int) ntohs(*(short*) pvData);
			SendEarlyClose(ns);
		}
		return;
	}
	for (ppdList = &pps->pdIn; *ppdList; ppdList = &(*ppdList)->pdNext);

	pdNew = (struct data *) malloc(sizeof(struct data));
	if (pdNew == 0)
		return; /* We will get out of sync, but such is life */
	pdNew->sin = *(struct sockaddr_in *) pvData;
	pdNew->sin.sin_family = ntohs(pdNew->sin.sin_family);

	nLen -= sizeof(struct sockaddr_in);
	pvData = (char *) pvData + sizeof(struct sockaddr_in);

	pdNew->pdNext = 0;
	pdNew->nUsed = 0;
	*ppdList = pdNew;

	/* Note that the calls to Notify below should *really*
	 * only be made if the data gets put at the head of the
	 * queue, but some telnet implementations miss notifications
	 * and this screws us right up. By putting in additional
	 * notifications we at least have a chance of recovery.
	 */
	if (pps->iFlags & PSF_ACCEPT)
	{
		pdNew->iLen = 0;
		if (nLen == sizeof(long))
			ns = (int) ntohl(*(long *) pvData);
		else
			ns = (int) ntohs(*(short*) pvData);
		ppt = GetAnotherTaskInfo(pps->htaskOwner);
		ppsNew = NewSocket(ppt, ns);
		pdNew->pchData = (char *) ppsNew;
		ppsNew->iFlags |= PSF_CONNECT | PSF_BOUND;
		ppsNew->sinLocal = pps->sinLocal;
		ppsNew->sinRemote = pdNew->sin;
		Notify(pps, FD_ACCEPT);
	}
	else
	{
		pdNew->iLen = nLen;
		pdNew->pchData = (char *) malloc(nLen);
		if (pdNew->pchData == 0)
			pdNew->iLen = 0;	/* Return EOF to the application */
		else
			memcpy(pdNew->pchData, pvData, nLen);
		if (pps->pdnsi)
			ContinueDNSQuery(pps);
		else
			Notify(pps, nLen ? FD_READ : FD_CLOSE);
	}
}

static	BOOL
StartBlocking(struct per_task *ppt)
{
	if (ppt->bBlocking)
	{
		iErrno = WSAEINPROGRESS;
		return FALSE;
	}
	else
	{
		ppt->bBlocking = TRUE;
		ppt->bCancel = FALSE;
		return TRUE;
	}
}

static	void
EndBlocking(struct per_task *ppt)
{
	ppt->bBlocking = FALSE;
}

BOOL	CALLBACK _export
DefBlockFunc(void)
{
	MSG msg;
	BOOL ret;

 	ret = (BOOL) PeekMessage(&msg,0,0,0,PM_REMOVE);
	if (ret)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		/* Some poorly behaved applications quit without
		 * checking the return value of WSACancel. It
		 * *can* fail.
		 */
		if (msg.message == WM_QUIT)
			WSACancelBlockingCall();
	}
	return ret;
}

static	BOOL
FlushMessages(struct per_task *ppt)
{

	if (ppt->lpBlockFunc)
		return ((BOOL (CALLBACK *)(void)) ppt->lpBlockFunc)();
	else
		return DefBlockFunc();
}

static	void
RemoveTXQ(struct tx_queue *ptxq)
{
	struct	tx_queue **pptxq;

	for (pptxq = &ptxqList; *pptxq; pptxq = &((*pptxq)->ptxqNext))
	{
		if (*pptxq == ptxq)
		{
			*pptxq = ptxq->ptxqNext;
			free(ptxq->ptxr);
			free(ptxq);
			return;
		}
	}
}

static	void
RemoveTask(struct per_task *ppt)
{
	struct per_task **pppt;

	DestroyWindow(ppt->hwndDNS);
	for (pppt = &pptList; *pppt; pppt = &((*pppt)->pptNext))
	{
		if (*pppt == ppt)
		{
			*pppt = ppt->pptNext;
			free(ppt->pClient);
			free(ppt);
			break;
		}
	}
};

#pragma argsused
void
ResponseReceived(	char	*pchData,
			int	iSize,
			long	iFrom)
{
	int		nLen;
	int		id;
	struct	tx_queue *ptxq;
	enum Functions	ft;
	struct tx_request *ptxr = (struct tx_request *) pchData;
	struct	sockaddr_in *psin;

	ft = (enum Functions) ntohs(ptxr->iType);
	id = ntohs(ptxr->id);
	nLen = ntohs(ptxr->nLen);
	if (ft == FN_Data || ft == FN_Accept)
	{
		FunctionReceived(id, ptxr->pchData, nLen - sizeof(short) * 5, ft);
		return;
	}
	for (ptxq = ptxqList; ptxq; ptxq = ptxq->ptxqNext)
	{
		if (ptxq->id == id)
		{
			memcpy(ptxq->ptxr, ptxr, nLen);
			ptxq->bDone = TRUE;
			if (ptxq->pchLocation)
				FireAsyncRequest(ptxq);
			return;
		}
	}
	if (ft == FN_SendTo || ft == FN_Send || ft == FN_Connect || ft == FN_Close)
	{
		int	iOffset = 0;
		int	iLen;
		enum arg_type at;
		long	nValue;
		struct	per_socket *pps;
		int	i;
		int	nCode;

		switch(ft)
		{
		case FN_SendTo:
		case FN_Send:
			nCode = 2;
			break;

		case FN_Connect:
			nCode = 3;
			break;

		case FN_Close:
			nCode = 0;
			break;

		default:
			nCode = -1;
			break;
		}

		for (i = 0; i <= nCode; i++)
		{
			at = (enum arg_type) ntohs(*(short *) (ptxr->pchData + iOffset));
			iOffset += 2;
			iLen = ntohs(*(short *) (ptxr->pchData + iOffset));
			iOffset += 2;
			if (i == 1)
					psin = (struct sockaddr_in *) (ptxr->pchData + iOffset);
			if (i == 0 || i == nCode)
			{
				if (at == AT_Int16)
					nValue = ntohs(*(short *) (ptxr->pchData + iOffset));
				else
					nValue = ntohl(*(long *) (ptxr->pchData + iOffset));
				if (i == 0)
				{
					pps = GetSocketInfo(nValue);
					if (!pps)
						return;
					if (ft == FN_Close)
					{
						RemoveSocket(pps);
					}
				}
				else if (ft == FN_Connect)
				{
					if (nValue < 0)
					{
						pps->iConnectResult = ntohs(ptxr->nError);
						NotifyError(pps, FD_CONNECT, ntohs(ptxr->nError));
					}
					else
					{
						pps->iConnectResult = 0;
						pps->iFlags |= PSF_CONNECT;
						Notify(pps, FD_CONNECT);
						Notify(pps, FD_WRITE);
						if (!pps->iFlags & PSF_BOUND)
						{
							psin->sin_family = ntohs(psin->sin_family);
							pps->sinLocal = *psin;
							pps->iFlags |= PSF_BOUND;
						}
					}
				}
				else
				{
					if (nValue < 0)
					{
						pps->nOutstanding = 0;
						NotifyError(pps, FD_WRITE, ntohs(ptxr->nError));
					}	
					else
					{
						pps->nOutstanding -= nValue;
						if (pps->nOutstanding < MAX_OUTSTANDING)
							Notify(pps, FD_WRITE);
					}
				}
			}
			iOffset += iLen;
		}
	}
}

static	struct tx_queue *
TransmitFunction(struct transmit_function *ptf)
{
	int	i;
	int	nSize = sizeof(short) * 5;
	struct	tx_request *ptxr;
	struct	tx_queue *ptxq, **pptxq;
	int	iOffset;
	struct per_task *ppt = GetTaskInfo();

	for (i = 0; i < ptf->nArgs; i++)
		nSize += ptf->pfaList[i].iLen + sizeof(short) * 2;
	nSize += ptf->pfaResult->iLen + sizeof(short) * 2;
	ptxr = (struct tx_request *) malloc(nSize);
	if (ptxr == 0)
	{
		return 0;
	}
	ptxr->iType = htons((short) ptf->fn);
	ptxr->nArgs = htons((short) ptf->nArgs);
	ptxr->nError = 0;
	ptxr->nLen = htons((short) nSize);
	ptxr->id = htons(idNext);
	for (iOffset = i = 0; i < ptf->nArgs; i++)
	{
		*(short *) (ptxr->pchData + iOffset) = htons((short) ptf->pfaList[i].at);
		iOffset += 2;
		*(short *) (ptxr->pchData + iOffset) = htons((short) ptf->pfaList[i].iLen);
		iOffset += 2;
		CopyDataOut(	ptxr->pchData + iOffset,
				ptf->pfaList[i].at,
				ptf->pfaList[i].pvData,
				ptf->pfaList[i].iLen);
		iOffset += ptf->pfaList[i].iLen;
	}
	*(short *) (ptxr->pchData + iOffset) = htons((short) ptf->pfaResult->at);
	iOffset += 2;
	*(short *) (ptxr->pchData + iOffset) = htons((short) ptf->pfaResult->iLen);
	iOffset += 2;
	CopyDataOut(	ptxr->pchData + iOffset,
			ptf->pfaResult->at,
			ptf->pfaResult->pvData,
			ptf->pfaResult->iLen);

	iOffset += ptf->pfaResult->iLen;

	ptxq = (struct tx_queue *) malloc(sizeof(struct tx_queue));
	if (ptxq == 0)
	{
		free(ptxr);
		return 0;
	}
	ptxq->ptxqNext = 0;
	ptxq->id = idNext;
	ptxq->ptxr = ptxr;
	ptxq->bDone = 0;
	ptxq->hwnd = 0;
	ptxq->pchLocation = 0;
	ptxq->wMsg = 0;
	idNext++;
	for (pptxq = &ptxqList; *pptxq; pptxq = &((*pptxq)->ptxqNext));
	*pptxq = ptxq;
	SendDataTo(ppt->pClient, (char *) ptxr, nSize, 0);
	return ptxq;
};

static void
SendEarlyClose(SOCKET s)
{
	struct	func_arg pfaArgs[1];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	int	nReturn;

	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Close, 1, pfaArgs, pfaReturn);
	RemoveTXQ(TransmitFunction(&tf));
}

static void
SetErrorResult(struct func_arg *pfaResult)
{
	switch (pfaResult->at)
	{
	case AT_Int16:
		*(short *) pfaResult->pvData = -1;
		break;

	case AT_Int32:
		*(long *) pfaResult->pvData = -1;
		break;

	case AT_Char:
		*(char *) pfaResult->pvData = -1;
		break;

	case AT_Int16Ptr:
	case AT_Int32Ptr:
	case AT_String:
	case AT_GenPtr:
		*(void **) pfaResult->pvData = 0;
		break;
	}
}

static	int	TransmitFunctionAndBlock(
			struct per_task *ppt,
			struct transmit_function *ptf)
{
	struct tx_queue *ptxq;
	struct tx_request *ptxr;
	int	i, iOffset;
	BOOL	bError = FALSE;

	if (!StartBlocking(ppt))
	{
		iErrno = WSAEINPROGRESS;
		SetErrorResult(ptf->pfaResult);
		return 0;
	}
	ptxq = TransmitFunction(ptf);
	ptxr = ptxq->ptxr;
	while (!ptxq->bDone)
	{
		FlushMessages(ppt);
		if (ppt->bCancel)
		{
			iErrno = WSAEINTR;
			RemoveTXQ(ptxq);
			SetErrorResult(ptf->pfaResult);
			EndBlocking(ppt);
			return 0;
		}
	}
	if (ptxq->ptxr->nError)
	{
		iErrno = ntohs(ptxq->ptxr->nError);
		SetErrorResult(ptf->pfaResult);
		bError = TRUE;
	}
	else
	{
		for (iOffset = i = 0; i < ptf->nArgs; i++)
		{
			ptf->pfaList[i].at = (enum arg_type) ntohs(*(short *) (ptxr->pchData + iOffset));
			iOffset += 2;
			ptf->pfaList[i].iLen = ntohs(*(short *) (ptxr->pchData + iOffset));
			iOffset += 2;
			if (!ptf->pfaList[i].bConstant)
				CopyDataIn(	ptxr->pchData + iOffset,
						ptf->pfaList[i].at,
						ptf->pfaList[i].pvData,
						ptf->pfaList[i].iLen);
			iOffset += ptf->pfaList[i].iLen;
		}
		ptf->pfaResult->at = (enum arg_type) ntohs(*(short *) (ptxr->pchData + iOffset));
		iOffset += 2;
		ptf->pfaResult->iLen = ntohs(*(short *) (ptxr->pchData + iOffset));
		iOffset += 2;
		CopyDataIn(	ptxr->pchData + iOffset,
				ptf->pfaResult->at,
				ptf->pfaResult->pvData,
				ptf->pfaResult->iLen);

	}
	RemoveTXQ(ptxq);
	EndBlocking(ppt);
	return bError ? 0 : 1;
}

static void
CopyHostEnt(struct per_task *ppt)
{
	int	iLocation;
	int	i;
	int	nAddrs;

	ppt->he.h_addrtype = ntohs(*(short *) ppt->achHostEnt);
	ppt->he.h_length = ntohs(*(short *) (ppt->achHostEnt + sizeof(short)));
	nAddrs = ntohs(*(short *) (ppt->achHostEnt + sizeof(short) * 2));
	iLocation = sizeof(short) * 3;
	for (i = 0; i < nAddrs; i++)
	{
		if (i < MAX_ALTERNATES - 1)
			ppt->apchHostAddresses[i] = ppt->achHostEnt + iLocation;
		iLocation += 4;
	}
	if (nAddrs < MAX_ALTERNATES - 1)
		ppt->apchHostAddresses[nAddrs] = 0;
	else
		ppt->apchHostAddresses[MAX_ALTERNATES - 1] = 0;
	ppt->he.h_addr_list = ppt->apchHostAddresses;
	ppt->he.h_name = ppt->achHostEnt + iLocation;
	iLocation += strlen(ppt->achHostEnt + iLocation)+ 1;
	for (i = 0; ppt->achHostEnt[iLocation] && i < MAX_ALTERNATES - 1; i++)
	{
		ppt->apchHostAlii[i] = ppt->achHostEnt + iLocation;
		iLocation += strlen(ppt->achHostEnt + iLocation)+ 1;
	}
	ppt->apchHostAlii[i] = 0;
	ppt->he.h_aliases = ppt->apchHostAlii;
	ppt->he.h_addr_list = ppt->apchHostAddresses;
}

static int
CopyHostEntTo(struct per_task *ppt, char *pchData)
{
	int	i;
	int	nAddrs;
	struct hostent *phe;
	char	*pchOld;
	int	nAlii;

	phe = (struct hostent *) pchData;
	memcpy(phe, &ppt->he, sizeof(ppt->he));

	pchData += sizeof(*phe);

	for (nAddrs = 0; phe->h_addr_list[nAddrs]; nAddrs++);
	for (nAlii = 0; phe->h_aliases[nAlii]; nAlii++);

	memcpy(pchData, phe->h_addr_list, sizeof(char *) * (nAddrs + 1));
	phe->h_addr_list = (char **) pchData;
	pchData += sizeof(char *) * (nAddrs + 1);

	memcpy(pchData, phe->h_aliases, sizeof(char *) * (nAddrs + 1));
	phe->h_aliases = (char **) pchData;
	pchData += sizeof(char *) * (nAddrs + 1);

	pchOld = phe->h_addr_list[0];
	memcpy(pchData, pchOld, 4 * nAddrs);
	for (i = 0; i < nAddrs; i++)
		phe->h_addr_list[i] =  pchData + (phe->h_addr_list[i] - pchOld);
	pchData += 4 * nAddrs;

	strcpy(pchData, phe->h_name);
	phe->h_name = pchData;
	pchData += strlen(pchData) + 1;

	for (i = 0; i < nAlii; i++)
	{
		strcpy(pchData, phe->h_aliases[i]);
		phe->h_aliases[i] = pchData;
		pchData += strlen(pchData) + 1;
	}
	return (pchData - (char *) phe);
}

static void
CopyServEnt(struct per_task *ppt)
{
	int	iLocation;
	int	i;

	/* Note that s_port is needed in network byte order */
        ppt->se.s_port = *(short *) ppt->achServEnt;
	iLocation = sizeof(short);
	ppt->se.s_proto = ppt->achServEnt + iLocation;
	iLocation += strlen(ppt->achServEnt + iLocation) + 1;
	ppt->se.s_name = ppt->achServEnt + iLocation;
	iLocation += strlen(ppt->achServEnt + iLocation) + 1;
	for (i = 0; ppt->achServEnt[iLocation] && i < MAX_ALTERNATES - 1; i++)
	{
		ppt->apchServAlii[i] = ppt->achServEnt + iLocation;
		iLocation += strlen(ppt->achServEnt + iLocation) + 1;
	}
	ppt->apchServAlii[i] = 0;
	ppt->se.s_aliases = ppt->apchServAlii;
}

static int
CopyServEntTo(struct per_task *ppt, char *pchData)
{
	int	i;
	int	nAlii;
	struct	servent *pse;

	CopyServEnt(ppt);

	pse = (struct servent *) pchData;
	memcpy(pse, &ppt->se, sizeof(ppt->se));
	pchData += sizeof(*pse);

	for (nAlii = 0; pse->s_aliases[nAlii]; nAlii++);
	memcpy(pchData, pse->s_aliases, sizeof(char *) * (nAlii + 1));
	pse->s_aliases = (char **) pchData;
	pchData += sizeof(char *) * (nAlii + 1);

	strcpy(pchData, ppt->se.s_name);
	ppt->se.s_name = pchData;
	pchData += strlen(pchData) + 1;

	strcpy(pchData, ppt->se.s_proto);
	ppt->se.s_proto = pchData;
	pchData += strlen(pchData) + 1;

	for (i = 0; i < nAlii; i++)
	{
		strcpy(pchData, pse->s_aliases[i]);
		pse->s_aliases[i] = pchData;
		pchData += strlen(pchData) + 1;
	}
	return (pchData - (char *) pse);
}

static void
CopyProtoEnt(struct per_task *ppt)
{
	int	iLocation;
	int	i;

	ppt->pe.p_proto = ntohs(*(short *) ppt->achProtoEnt);
	iLocation = sizeof(short);
	ppt->pe.p_name = ppt->achProtoEnt + iLocation;
	iLocation += strlen(ppt->achProtoEnt + iLocation) + 1;
	for (i = 0; ppt->achProtoEnt[iLocation] && i < MAX_ALTERNATES - 1; i++)
	{
		ppt->apchProtoAlii[i] = ppt->achProtoEnt + iLocation;
		iLocation += strlen(ppt->achProtoEnt + iLocation) + 1;
	}
	ppt->apchProtoAlii[i] = 0;
	ppt->pe.p_aliases = ppt->apchProtoAlii;
}

static int
CopyProtoEntTo(struct per_task *ppt, char *pchData)
{
	int	i;
	int	nAlii;
	struct	protoent *ppe;

	CopyProtoEnt(ppt);

	ppe = (struct protoent *) pchData;
	memcpy(ppe, &ppt->pe, sizeof(ppt->pe));
	pchData += sizeof(*ppe);

	for (nAlii = 0; ppe->p_aliases[nAlii]; nAlii++);
	memcpy(pchData, ppe->p_aliases, sizeof(char *) * (nAlii + 1));
	ppe->p_aliases = (char **) pchData;
	pchData += sizeof(char *) * (nAlii + 1);

	strcpy(pchData, ppe->p_name);
	ppe->p_name = pchData;
	pchData += strlen(pchData) + 1;

	for (i = 0; i < nAlii; i++)
	{
		strcpy(pchData, ppe->p_aliases[i]);
		ppe->p_aliases[i] = pchData;
		pchData += strlen(pchData) + 1;
	}
	return (pchData - (char *) ppe);
}

SOCKET CALLBACK _export
accept(SOCKET s, struct sockaddr FAR *addr,
                          int FAR *addrlen)
{
	struct per_task *ppt;
	struct per_socket *pps, *ppsNew;
	struct data *pd;

	if ((ppt = GetTaskInfo()) == 0)
		return (INVALID_SOCKET);
	if ((pps = GetSocketInfo(s)) == 0)
		return (INVALID_SOCKET);
	if (!(pps->iFlags & PSF_ACCEPT))
	{
		iErrno = WSAEINVAL;
		return (INVALID_SOCKET);
	}
	if (!pps->pdIn && (pps->iFlags & PSF_NONBLOCK))
	{
		iErrno = WSAEWOULDBLOCK;
		return (INVALID_SOCKET);
	}
	if (!StartBlocking(ppt))
		return (INVALID_SOCKET);
	while (!pps->pdIn)
	{
		FlushMessages(ppt);
		if (ppt->bCancel)
		{
			iErrno = WSAEINTR;
			EndBlocking(ppt);
			return (INVALID_SOCKET);
		}
	}

	pd = pps->pdIn;
	if (addr && addrlen)
	{
		memcpy(addr, &pd->sin, sizeof(pd->sin));
		*addrlen = sizeof(pd->sin);
	}
	ppsNew = (struct per_socket *) pd->pchData;

	pps->pdIn = pd->pdNext;
	free(pd);
	if (pps->pdIn)
		Notify(pps, FD_ACCEPT);

	EndBlocking(ppt);
	return ppsNew->s;
}

int CALLBACK _export
bind(SOCKET s, const struct sockaddr FAR *addr, int namelen)
{
	struct per_task *ppt;
	int	nReturn;
	struct	func_arg pfaArgs[3];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	struct	sockaddr	*psa;
	struct	per_socket 	*pps;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if ((namelen < sizeof(*psa)) || IsBadReadPtr(addr, sizeof(*psa)))
	{
		iErrno = WSAEFAULT;
		return (SOCKET_ERROR);
	}
	namelen = sizeof(*psa);
	psa = (struct sockaddr *) malloc(namelen);
	if (psa == 0)
	{
		iErrno = WSAENOBUFS;
		return (SOCKET_ERROR);
	}
	memcpy(psa, addr, namelen);
	psa->sa_family = htons(psa->sa_family);
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_GenPtr,	psa,		namelen			);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	&namelen,	sizeof(namelen)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Bind, 3, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	if (nReturn != -1)
	{
		psa->sa_family = ntohs(psa->sa_family);
		pps->sinLocal = *(struct sockaddr_in *) psa;
		pps->iFlags |= PSF_BOUND;
	}
	free(psa);
	return nReturn;
}

int CALLBACK _export
closesocket(SOCKET s)
{
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[1];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if (GetTaskInfo() == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Close, 1, pfaArgs, pfaReturn);
	RemoveTXQ(TransmitFunction(&tf));
	return 0;
}

int CALLBACK _export
connect(SOCKET s, const struct sockaddr FAR *name, int namelen)
{
	struct per_task *ppt;
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[3];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	struct	sockaddr	*psa;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if (pps->iFlags & PSF_CONNECT)
	{
		if (pps->iFlags & PSF_CRUSED)
		{
			iErrno = WSAEISCONN;
			return -1;
		}
		pps->iFlags |= PSF_CRUSED;
		if (pps->iConnectResult)
		{
			iErrno = pps->iConnectResult;
			return -1;
		}
		else
		{
			return 0;
		}
	}
	if ((namelen < sizeof(*psa)) || IsBadReadPtr(name, sizeof(*psa)))
	{
		iErrno = WSAEFAULT;
		return (SOCKET_ERROR);
	}
	namelen = sizeof(*psa);
	psa = (struct sockaddr *) malloc(namelen);
	if (psa == 0)
	{
		iErrno = WSAENOBUFS;
		return (SOCKET_ERROR);
	}
	memcpy(psa, name, namelen);
	pps->sinRemote = *(struct sockaddr_in *) psa;
	psa->sa_family = htons(psa->sa_family);
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_GenPtr,	psa,		namelen			);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	&namelen,	sizeof(namelen)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Connect, 3, pfaArgs, pfaReturn);
	if (pps->iFlags & PSF_NONBLOCK)
	{
		pps->iFlags |= PSF_CONNECTING;
		RemoveTXQ(TransmitFunction(&tf));
		iErrno = WSAEWOULDBLOCK;
		nReturn = -1;
	}
	else
	{
		TransmitFunctionAndBlock(ppt, &tf);
		if (nReturn != -1)
		{
			pps->iFlags |= PSF_CONNECT;
			if (!(pps->iFlags & PSF_BOUND))
			{
				psa->sa_family = ntohs(psa->sa_family);
				pps->sinLocal = *(struct sockaddr_in *) psa;
				pps->iFlags |= PSF_BOUND | PSF_CRUSED;
			}
		}
	}
	free(psa);
	return nReturn;
}

int CALLBACK _export
ioctlsocket(SOCKET s, long cmd, u_long far * arg)
{
	struct per_socket *pps;

	if (GetTaskInfo() == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	switch(cmd)
	{
	case FIONBIO:
		if (IsBadReadPtr(arg, sizeof(*arg)))
		{
			iErrno = WSAEFAULT;
			return (SOCKET_ERROR);
		}
		if (*arg)
			pps->iFlags |= PSF_NONBLOCK;
		else
			pps->iFlags &= ~PSF_NONBLOCK;
		break;

	case FIONREAD:
		if (IsBadWritePtr(arg, sizeof(*arg)))
		{
			iErrno = WSAEFAULT;
			return (SOCKET_ERROR);
		}
		if (pps->pdIn)
			*arg = pps->pdIn->iLen - pps->pdIn->nUsed;
		else
			*arg = 0;
		break;

	case SIOCATMARK:
		if (IsBadWritePtr(arg, sizeof(BOOL)))
		{
			iErrno = WSAEFAULT;
			return (SOCKET_ERROR);
		}
		*(BOOL *) arg = 0;
		break;
	}
	return 0;
}

int CALLBACK _export getpeername (SOCKET s, struct sockaddr FAR *name,
                            int FAR * namelen)
{
	struct per_socket *pps;

	if (GetTaskInfo() == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if (!(pps->iFlags & PSF_CONNECT))
	{
		iErrno = WSAENOTCONN;
		return -1;
	}
	*namelen = sizeof(struct sockaddr_in); /* Just in case */
	memcpy(name, &pps->sinRemote, sizeof(struct sockaddr_in));
	return 0;
}

int CALLBACK _export getsockname (SOCKET s, struct sockaddr FAR *name,
                            int FAR * namelen)
{
	struct per_socket *pps;

	if (GetTaskInfo() == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	*namelen = sizeof(struct sockaddr_in); /* Just in case */
	if (pps->iFlags & PSF_BOUND)
	{
		memcpy(name, &pps->sinLocal, sizeof(struct sockaddr_in));
	}
	else
	{
		memset(name, 0, sizeof(struct sockaddr_in));
		((struct sockaddr_in *) name)->sin_family = AF_INET;
	}
	return 0;
}

int CALLBACK _export getsockopt (SOCKET s, int level, int optname,
                           char FAR * optval, int FAR *optlen)
{
	struct per_task *ppt;
	int	nReturn;
	struct	func_arg pfaArgs[5];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	long	iOptVal;
	short	iOptLen;

	iOptLen = sizeof(long);

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if (!GetSocketInfo(s))
		return -1;
	if (level != IPPROTO_TCP && level != (int) SOL_SOCKET)
	{
		iErrno = WSAEINVAL;
		return (SOCKET_ERROR);
	}
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&level,		sizeof(level)		);
	INIT_ARGS(pfaArgs[2],	AT_Int,		&optname,	sizeof(optname)		);
	if (optname == SO_LINGER)
		INIT_ARGS(pfaArgs[3],	AT_GenPtr,	optval,		iOptLen		);
	else
		INIT_ARGS(pfaArgs[3],	AT_Int32Ptr,	&iOptVal,	iOptLen		);
	INIT_ARGS(pfaArgs[4],	AT_IntPtr,	&iOptLen,	sizeof(iOptLen)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_GetSockOpt, 5, pfaArgs, pfaReturn);
	if (TransmitFunctionAndBlock(ppt, &tf))
	{
		if (optname == SO_LINGER)
		{
			*optlen = sizeof(struct linger);
		}
		else
		{
			*optlen = sizeof(int);
			* (int FAR *) optval = (int) iOptVal;
		}
	}
	return nReturn;
}

u_long CALLBACK _export htonl (u_long hostlong)
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

u_short CALLBACK _export htons (u_short hostshort)
{
	char	*pchValue = (char *) &hostshort;
	char	c;

	c = pchValue[0];
	pchValue[0] = pchValue[1];
	pchValue[1] = c;
	return hostshort;
}

unsigned long CALLBACK _export inet_addr (const char FAR * cp)
{
	unsigned long	iValue;
	char	*pchValue = (char *) &iValue;
	int	iTmp;
	int	i;

	if (!GetTaskInfo())
		return (INADDR_NONE);
	
	for (i = 0; i < 4; i++)
	{
		iTmp = 0;
		if (!isdigit(*cp))
			return INADDR_NONE;
		while (isdigit(*cp))
		{
			iTmp *= 10;
			iTmp += *cp - '0';
			cp++;
		}
		if (iTmp > 255 || iTmp < 0)
			return INADDR_NONE;
		if (*cp != (i == 3 ? '\0' : '.'))
			return INADDR_NONE;
		cp++;
		pchValue[i] = iTmp;
	}
	return iValue;
}

char FAR * CALLBACK _export inet_ntoa (struct in_addr in)
{
	struct per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;

	sprintf(ppt->achAddress, "%d.%d.%d.%d",
			(int) in.S_un.S_un_b.s_b1,
			(int) in.S_un.S_un_b.s_b2,
			(int) in.S_un.S_un_b.s_b3,
			(int) in.S_un.S_un_b.s_b4);
	return ppt->achAddress;
}

int CALLBACK _export listen (SOCKET s, int backlog)
{
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if (GetTaskInfo() == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if (pps->iFlags & PSF_CONNECT)
	{
		iErrno = WSAEISCONN;
		return -1;
	}
	if (!(pps->iFlags & PSF_BOUND))
	{
		/* Bind first - we need to know the address */
		struct	sockaddr_in sin;

		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = 0;
		sin.sin_addr.s_addr = INADDR_ANY;
		bind(s, (struct sockaddr *) &sin, sizeof(sin));
	}
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&backlog,	sizeof(backlog)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Listen, 2, pfaArgs, pfaReturn);
	RemoveTXQ(TransmitFunction(&tf));
	pps->iFlags |= PSF_ACCEPT;
	return 0;
}

u_long CALLBACK _export ntohl (u_long netlong)
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

u_short CALLBACK _export ntohs (u_short netshort)
{
	char	*pchValue = (char *) &netshort;
	char	c;

	c = pchValue[0];
	pchValue[0] = pchValue[1];
	pchValue[1] = c;
	return netshort;
}

int CALLBACK _export recv (SOCKET s, char FAR * buf, int len, int flags)
{
	return recvfrom(s, buf, len, flags, 0, 0);
}

#pragma argsused
int CALLBACK _export recvfrom (SOCKET s, char FAR * buf, int len, int flags,
                         struct sockaddr FAR *from, int FAR * fromlen)
{
	struct per_task *ppt;
	struct per_socket *pps;
	struct data *pd;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if (pps->iFlags & PSF_CLOSED)
	{
		/* Haven't you got the message yet? */
		iErrno = WSAENOTCONN;
		return -1;
	}
	if (pps->iFlags & PSF_ACCEPT)
	{
		iErrno = WSAENOTCONN;
		return -1;
	}
	if (pps->iFlags & PSF_SHUTDOWN)
	{
		iErrno = WSAESHUTDOWN;
		return -1;
	}
	if (!pps->pdIn && (pps->iFlags & PSF_NONBLOCK))
	{
		iErrno = WSAEWOULDBLOCK;
		return -1;
	}
	/*
	 * Validate the user buffer
	 */
	if (IsBadWritePtr(buf, len)) /* Make sure we can write the user data buffer */
	{
		iErrno = WSAEFAULT;
		return (SOCKET_ERROR);
	}
	/*
	 * If we have a 'from' buffer, make sure that
	 * we can read and write it. Also make sure that
	 * the buffer passed is large enough to hold a sockaddr.
	 */
	if (from)
	{
		if (!fromlen ||				/* Did the user specify a length */
		    IsBadReadPtr(fromlen, sizeof(*fromlen)) || /* Can we read length ?? */
		    IsBadWritePtr(fromlen, sizeof(*fromlen)) || /* Can we write it ??    */
		    (*fromlen < sizeof(struct sockaddr)) ) /* Can it hold a sockaddr? */
		{
			iErrno = WSAEFAULT;
			return (SOCKET_ERROR);
		}
	}
	if (!StartBlocking(ppt))
		return -1;
	while (!pps->pdIn)
	{
		FlushMessages(ppt);
		if (ppt->bCancel)
		{
			iErrno = WSAEINTR;
			EndBlocking(ppt);
			return -1;
		}
	}
	EndBlocking(ppt);

	pd = pps->pdIn;
	if (from)
	{
		memcpy(from, &pd->sin, sizeof(pd->sin));
		*fromlen = sizeof(pd->sin);
	}
	if (len > pd->iLen - pd->nUsed)
		len = pd->iLen - pd->nUsed;
	memcpy(buf, pd->pchData + pd->nUsed, len);
	pd->nUsed += len;
	if (pd->nUsed == pd->iLen)
	{
		pps->pdIn = pd->pdNext;
		free(pd->pchData);
		free(pd);
		if (pps->pdIn)
			Notify(pps, pps->pdIn->iLen ? FD_READ : FD_CLOSE);
	}
	else
	{
		Notify(pps, FD_READ);
	}

	return len;
}

#define	PPS_ERROR	((struct per_socket **) -1)

static	struct	per_socket **
GetPPS(fd_set *fds)
{
	struct per_socket **pps;
	u_short	i;

	if (!fds || !fds->fd_count)
		return 0;
	pps = (struct per_socket **) malloc(sizeof(struct per_socket *) *
					    fds->fd_count);
	if (pps == 0)
	{
		iErrno = WSAENOBUFS;
		return PPS_ERROR;
	}
	for (i = 0; i < fds->fd_count; i++)
	{
		pps[i] = GetSocketInfo(fds->fd_array[i]);
		if (!pps[i])
		{
			free(pps);
			return PPS_ERROR;
		}
	}
	return pps;
}


int CALLBACK _export select (int nfds, fd_set *readfds, fd_set far *writefds,
                fd_set *exceptfds, const struct timeval far *timeout)
{
	struct per_task *ppt;
	struct per_socket **ppsRead, **ppsWrite, **ppsExcept;
	BOOL	bOneOK = FALSE;
	BOOL	bTimedOut = FALSE;
	unsigned	long	tExpire;
	u_short i;
	u_short	iOld, iNew;

	if (timeout)
	{
		tExpire = GetTickCount();
		tExpire += timeout->tv_usec / 1000 + timeout->tv_sec * 1000;
	}
	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if (!StartBlocking(ppt))
		return -1;

	ppsRead = GetPPS(readfds);
	if (ppsRead == PPS_ERROR)
		return -1;
	ppsWrite = GetPPS(writefds);
	if (ppsWrite == PPS_ERROR)
	{
		if (ppsRead)
			free(ppsRead);
		return -1;
	}
	ppsExcept = GetPPS(exceptfds);
	if (ppsExcept == PPS_ERROR)
	{
		if (ppsRead)
			free(ppsRead);
		if (ppsWrite)
			free(ppsWrite);
		return -1;
	}
		
	while (!bOneOK && !bTimedOut && !ppt->bCancel)
	{
		if (ppsWrite)
		{
			for (i = 0; i < writefds->fd_count; i++)
			{
				if ((!(ppsWrite[i]->iFlags & (PSF_CONNECTING | PSF_MUSTCONN)) ||
				     (ppsWrite[i]->iFlags & PSF_CONNECT)) &&
				    ppsWrite[i]->nOutstanding < MAX_OUTSTANDING)
					bOneOK = TRUE;
			}
		}
		if (ppsRead)
		{
			for (i = 0; i < readfds->fd_count; i++)
			{
				if (ppsRead[i]->pdIn)
					bOneOK = TRUE;
			}
		}
		if (timeout && GetTickCount() >= tExpire)
			bTimedOut = TRUE;
		else if (!bOneOK)
			FlushMessages(ppt);
	}

	nfds = 0;
	if (readfds)
	{
		for (iOld = iNew = 0; iOld < readfds->fd_count; iOld++)
		{
			if (iOld != iNew)
				readfds->fd_array[iNew] =
						readfds->fd_array[iOld];
			if (ppsRead[iOld]->pdIn)
				iNew++;
		}
		readfds->fd_count = iNew;
		nfds += iNew;
	}
	if (writefds)
	{
		for (iOld = iNew = 0; iOld < writefds->fd_count; iOld++)
		{
			if (iOld != iNew)
				writefds->fd_array[iNew] =
						writefds->fd_array[iOld];
			if ((!(ppsWrite[iOld]->iFlags & (PSF_CONNECTING | PSF_MUSTCONN)) ||
				     (ppsWrite[iOld]->iFlags & PSF_CONNECT)) &&
				    ppsWrite[iOld]->nOutstanding < MAX_OUTSTANDING)
				iNew++;
		}
		writefds->fd_count = iNew;
		nfds += iNew;
	}
	if (exceptfds)
		exceptfds->fd_count = 0;

	EndBlocking(ppt);
	if (ppsRead)
		free(ppsRead);
	if (ppsWrite)
		free(ppsWrite);
	if (ppsExcept)
		free(ppsExcept);
	if (ppt->bCancel && !nfds)
	{
		iErrno = WSAEINTR;
		return -1;
	}
	else
	{
		return nfds;
	}
}

/* We don't wait for the result of a send because some telnets don't behave
 * correctly with WSAEINPROGRESS when sending. This results in characters
 * being dropped to hell while we wait for the response from the host.
 * We return success unless the socket is not connected or is closed.
 * This is fairly safe on connected sockets (which is what uses send).
 *
 * We don't do this with sendto because it's really only telnets that suffer
 * from this, although that doesn't stop another application from having the
 * same problem with sendto later on.
 *
 * This causes certain FTP clients to display phenomenal transfer rates.
 * They should be checking the transfer rates *after* closing their sockets.
 */
int CALLBACK _export send (SOCKET s, const char FAR * buf, int len, int flags)
{
	struct per_task *ppt;
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[4];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if ((pps->iFlags & PSF_CONNECTING) &&
	    !(pps->iFlags & PSF_CONNECT))
	{
		iErrno = WSAEWOULDBLOCK;
		return -1;
	}
	if (!(pps->iFlags & PSF_CONNECT))
	{
		iErrno = WSAENOTCONN;
		return -1;
	}
	if (pps->iFlags & PSF_CLOSED)
	{
		iErrno = WSAECONNRESET;
		return -1;
	}
	if (IsBadReadPtr(buf, len))	/* Is the data buffer readable ? */
	{
		iErrno = WSAEFAULT;
		return (SOCKET_ERROR);
	}
	if (pps->nOutstanding >= MAX_OUTSTANDING)
	{
		if (pps->iFlags & PSF_NONBLOCK)
		{
			iErrno = WSAEWOULDBLOCK;
			return -1;
		}
		if (!StartBlocking(ppt))
		{
			iErrno = WSAEINPROGRESS;
			return -1;
		}
		while (pps->nOutstanding >= MAX_OUTSTANDING && !ppt->bCancel)
			FlushMessages(ppt);
		EndBlocking(ppt);
		if (ppt->bCancel)
		{
			ppt->bCancel = FALSE;
			iErrno = WSAEINTR;
			return -1;
		}
	}
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_CARGS(pfaArgs[1],	AT_GenPtr,	buf,		len			);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	&len,		sizeof(len)		);
	INIT_ARGS(pfaArgs[3],	AT_Int,		&flags,		sizeof(flags)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Send, 4, pfaArgs, pfaReturn);
	RemoveTXQ(TransmitFunction(&tf));
	pps->nOutstanding += len;
	if (pps->nOutstanding < MAX_OUTSTANDING)
		Notify(pps, FD_WRITE);
	return len;
}

int CALLBACK _export sendto (SOCKET s, const char FAR * buf, int len, int flags,
                       const struct sockaddr FAR *to, int tolen)
{
	struct per_task *ppt;
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[6];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	struct	sockaddr	*psa;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if (pps->nOutstanding >= MAX_OUTSTANDING)
	{
		if (pps->iFlags & PSF_NONBLOCK)
		{
			iErrno = WSAEWOULDBLOCK;
			return -1;
		}
		if (!StartBlocking(ppt))
		{
			iErrno = WSAEINPROGRESS;
			return -1;
		}
		while (pps->nOutstanding >= MAX_OUTSTANDING && !ppt->bCancel)
			FlushMessages(ppt);
		EndBlocking(ppt);
		if (ppt->bCancel)
		{
			ppt->bCancel = FALSE;
			iErrno = WSAEINTR;
			return -1;
		}
	}
	psa = (struct sockaddr *) malloc(tolen);
	if (psa == 0)
	{
		iErrno = WSAENOBUFS;
		return (SOCKET_ERROR);
	}
	/*
	 * Sanity check the arguments.
	 */
	if ((tolen < sizeof(*psa)) ||
	    IsBadReadPtr(buf, len) ||
	    IsBadReadPtr(to, sizeof(*psa)) )
	{
		free(psa);
		iErrno = WSAEFAULT;
		return (SOCKET_ERROR);
	}
	/*
	 * The WINSOCK spec allows namelen's that are larger
	 * then the size of a sockaddr. The WSAT expects sendto()s
	 * with tolen's > sizeof(struct sockaddr) to succeed.
	 * Make sure that this condition will not cause the
	 * other side to fail (we don't know if the other side
	 * will fail or not with tolen's > sizeof(struct sockaddr).
	 */
	tolen = sizeof(*psa);

	memcpy(psa, to, tolen);
	psa->sa_family = htons(psa->sa_family);
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_CARGS(pfaArgs[1],	AT_GenPtr,	buf,		len			);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	&len,		sizeof(len)		);
	INIT_ARGS(pfaArgs[3],	AT_Int,		&flags,		sizeof(flags)		);
	INIT_CARGS(pfaArgs[4],	AT_GenPtr,	psa,		tolen			);
	INIT_ARGS(pfaArgs[5],	AT_Int,		&tolen,		sizeof(tolen)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);

	INIT_TF(tf, FN_SendTo, 6, pfaArgs, pfaReturn);
	RemoveTXQ(TransmitFunction(&tf));
	pps->nOutstanding += len;
	if (pps->nOutstanding < MAX_OUTSTANDING)
		Notify(pps, FD_WRITE);
	free(psa);
	Notify(pps, FD_WRITE);
	return len;
}

int CALLBACK _export setsockopt (SOCKET s, int level, int optname,
                           const char FAR * optval, int optlen)
{
	int	nReturn;
	struct	func_arg pfaArgs[5];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	long	iOptVal;
	short	iOptLen;

	iOptLen = sizeof(long);

	if ( GetTaskInfo() == 0)
		return -1;
	if (!GetSocketInfo(s))
		return -1;
	/*
	 * Sanity check the arguments.
	 */
	if (IsBadReadPtr(optval, optlen))	/* Is the option value readable? */
	{
		iErrno = WSAEFAULT;
		return (SOCKET_ERROR);
	}
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&level,		sizeof(level)		);
	INIT_ARGS(pfaArgs[2],	AT_Int,		&optname,	sizeof(optname)		);
	if (optname == SO_LINGER)
	{
		INIT_ARGS(pfaArgs[3],	AT_GenPtr,	optval,		iOptLen		);
	}
	else
	{
		iOptVal = *optval;
		INIT_ARGS(pfaArgs[3],	AT_Int32Ptr,	&iOptVal,	iOptLen		);
	}
	INIT_ARGS(pfaArgs[4],	AT_Int,		&iOptLen,	sizeof(iOptLen)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_SetSockOpt, 5, pfaArgs, pfaReturn);
	/* If we get this far, assume the host will grok.
	 * Better to return immediately because applications
	 * won't expect us to block here.
	 */
	RemoveTXQ(TransmitFunction(&tf));
	return 0;
}

int CALLBACK _export shutdown (SOCKET s, int how)
{
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if (GetTaskInfo() == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&how,		sizeof(how)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Shutdown, 2, pfaArgs, pfaReturn);
	RemoveTXQ(TransmitFunction(&tf));
	pps->iFlags |= PSF_SHUTDOWN;
	return 0;
}

SOCKET CALLBACK _export socket (int af, int type, int protocol)
{
	struct per_task *ppt;
	int	nReturn;
	struct	func_arg pfaArgs[4];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	struct	per_socket *pps;
	int	iSocket;

	if (af != AF_INET)
	{
		iErrno = WSAEAFNOSUPPORT;
		return (SOCKET) SOCKET_ERROR;
	}
	if (type != SOCK_STREAM && type != SOCK_DGRAM)
	{
		iErrno = WSAESOCKTNOSUPPORT;
		return (SOCKET) SOCKET_ERROR;
	}
	if (protocol != 0 &&
	    (type == SOCK_STREAM && protocol != 6) &&
	    (type == SOCK_DGRAM && protocol != 17))
	{
		iErrno = WSAEPROTONOSUPPORT;
		return (SOCKET) SOCKET_ERROR;
	}
	if ((ppt = GetTaskInfo()) == 0)
		return (INVALID_SOCKET);
	iSocket = GetClientSocket();
	if (iSocket == -1)
	{
		iErrno = WSAENOBUFS;
		return -1;
	}
	INIT_ARGS(pfaArgs[0],	AT_Int,		&iSocket,	sizeof(iSocket)		);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&af,		sizeof(af)		);
	INIT_ARGS(pfaArgs[2],	AT_Int,		&type,		sizeof(type)		);
	INIT_ARGS(pfaArgs[3],	AT_Int,		&protocol,	sizeof(protocol)	);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Socket, 4, pfaArgs, pfaReturn);
	RemoveTXQ(TransmitFunction(&tf));
	if (nReturn != -1)
	{
		pps = NewSocket(ppt, iSocket);
		switch(type)
		{
		case SOCK_STREAM:
		case SOCK_SEQPACKET:
			pps->iFlags |= PSF_MUSTCONN;
			break;

		case SOCK_DGRAM:
		case SOCK_RAW:
		case SOCK_RDM:
			pps->iFlags &= ~PSF_MUSTCONN;
			break;
		}
	}
	return iSocket;
}

/* DNS Support */

#define	DNS_MAXPACKET	1024

static	void
NextDNSTry(	struct per_socket *pps)
{
	struct	sockaddr_in sin;
	struct	per_task *ppt = GetAnotherTaskInfo(pps->htaskOwner);

	if (pps->pdnsi->iRetry++ == 5)
	{
		NextDNSServer(pps);
		return;
	}
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(NAMESERVER_PORT);
	memcpy(&sin.sin_addr.s_addr,
		pps->pdnsi->aachTryNow[pps->pdnsi->iTryNow - 1],
		4);
	sendto(	pps->s,
		pps->pdnsi->pchQuery,
		pps->pdnsi->iQueryLen,
		0,
		&sin,
		sizeof(sin));
	SetTimer(ppt->hwndDNS, pps->s, 5000, 0);
}

static	void
NextDNSServer(	struct per_socket *pps)
{
	if (pps->pdnsi->iTryNow == pps->pdnsi->nTryNow)
	{
		pps->pdnsi->bComplete = TRUE;
		pps->pdnsi->iError = WSATRY_AGAIN;
		return;
	}
	pps->pdnsi->iTryNow++;
	pps->pdnsi->iRetry = 0;
	NextDNSTry(pps);
}

static	void
StartDNSLevel(	struct	per_socket *pps,
		char	(*ppchAddresses)[4],
		int	nAddresses)
{
	int	i;

	for (i = 0; i < nAddresses && i < MAX_DNS_SERVERS; i++)
		memcpy(pps->pdnsi->aachTryNow[i], ppchAddresses[i], 4);
	pps->pdnsi->nTryNow = i;
	pps->pdnsi->iTryNow = 0;
	NextDNSServer(pps);
}

static	struct per_socket *
SendDNSQuery(	char	const	*pchName,
		BOOL	bNameToAddress)
{
	SOCKET	s;
	struct per_socket *pps;
	dns_info *pdnsi;
	struct	sockaddr_in sin;
	int	iLen;
	int	nReturn;
	struct	func_arg pfaArgs[3];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	res_init();
	/* Create a socket, and bind it to a port. We don't care which
	 * port, so we don't wait for the result of the bind.
	 */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	iLen = sizeof(sin);
	s = socket(AF_INET, SOCK_DGRAM, 0);
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_GenPtr,	&sin,		iLen			);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	&iLen,		sizeof(iLen)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Bind, 3, pfaArgs, pfaReturn);
	RemoveTXQ(TransmitFunction(&tf));
	pps = GetSocketInfo(s);

	pps->pdnsi = pdnsi = (dns_info *) malloc(sizeof(dns_info));
	pdnsi->bNameToAddress = bNameToAddress;
	pdnsi->bVirtualCircuit = FALSE;
	pdnsi->bComplete = FALSE;
	pdnsi->hwndNotify = 0;
	pdnsi->pchLocation = 0;
	pdnsi->wMsg = 0;
	pdnsi->idRequest = idNext++;
	pdnsi->pchQuery = (char *) malloc(DNS_MAXPACKET);
	pdnsi->iQueryLen = res_mkquery(	QUERY,
					pchName,
					C_IN,
					bNameToAddress ? T_A : T_PTR,
					0, 0, 0,
					pdnsi->pchQuery,
					DNS_MAXPACKET);
	pdnsi->idSent = _res.id;
	StartDNSLevel(	pps,
			_res.nsaddr_list,
			_res.nscount);
	return pps;
}

/* We have received a response */
static	void
ContinueDNSQuery(struct per_socket *pps)
{
	struct data *pd = pps->pdIn;
	dns_info *pdnsi = pps->pdnsi;
	int	i;
	int	iError;
	struct	per_task *ppt;
	HEADER *ph = (HEADER *) pd->pchData;
	int	nLen;

	if (!pd || !pdnsi)
		return;
	ppt = GetAnotherTaskInfo(pps->htaskOwner);
	pps->pdIn = pd->pdNext;

	KillTimer(ppt->hwndDNS, pps->s);
	ph = (HEADER *) pd->pchData;
	if (ntohs(ph->id) == pdnsi->idSent)
	{
		if (ph->rcode != NOERROR || ntohs(ph->ancount) == 0)
		{
			switch(ph->rcode)
			{
			case NXDOMAIN:
				iError = WSAHOST_NOT_FOUND;
				break;

			case SERVFAIL:
				iError = WSATRY_AGAIN;
				break;

			case NOERROR:
				iError = WSANO_DATA;
				break;

			case FORMERR:
			case NOTIMP:
			case REFUSED:
			default:
				iError = WSANO_RECOVERY;
				break;
			}
		}
		else
		{
			iError = 0;
			getanswer((querybuf *) pd->pchData,
				pd->iLen,
				pdnsi->bNameToAddress ? 0 : 1,
				&ppt->he,
				ppt->achHostEnt,
				sizeof(ppt->achHostEnt),
				ppt->apchHostAddresses,
				ppt->apchHostAlii,
				&iError);
			if (!iError)
			{
				if (!pdnsi->bNameToAddress)
				{
					memcpy(ppt->achRevAddress, pdnsi->achInput, 4);
					ppt->he.h_addr_list = ppt->apchHostAddresses;
					ppt->he.h_addr_list[0] = ppt->achRevAddress;
					ppt->he.h_addr_list[1] = 0;
					ppt->he.h_addrtype = PF_INET;
					ppt->he.h_length = 4;
				}
				if (pdnsi->hwndNotify)
				{
					nLen = CopyHostEntTo(ppt, pdnsi->pchLocation);
					PostMessage(pdnsi->hwndNotify,
						pdnsi->wMsg,
						pdnsi->idRequest | 0x4000,
						WSAMAKEASYNCREPLY(nLen, 0));
				}
			}
		}
		if (pdnsi->hwndNotify)
		{
			if (iError)
				PostMessage(pdnsi->hwndNotify,
					pdnsi->wMsg,
					pdnsi->idRequest | 0x4000,
					WSAMAKEASYNCREPLY(0, iError));
			free(pdnsi->pchQuery);
			free(pdnsi);
			pps->pdnsi = 0;
			closesocket(pps->s);
		}
		else
		{
			pdnsi->bComplete = TRUE;
			pdnsi->iError = iError;
		}
	}
}

static	char	achDNSBuffer[1024];

struct hostent FAR * CALLBACK _export gethostbyaddr(const char FAR * addr,
                                              int len, int type)
{
	struct per_task *ppt;
	struct per_socket *pps;
	struct	hostent *phe;
	int	iError;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	if (!StartBlocking(ppt))
		return 0;
	/*
	 * Sanity check the arguments.
	 */
	if ((type != PF_INET) ||			/* WINSOCK spec says this must be PF_INET */
	    (len != 4) ||				/* WINSOCK spec says must be 4 for PF_INET */
	    IsBadReadPtr(addr, len))			/* Can we read the host address */
	{
		iErrno = WSAEFAULT;
		EndBlocking(ppt);
		return 0;
	}
	sprintf(achDNSBuffer, "%d.%d.%d.%d.in-addr.arpa",
			(int) (unsigned char) addr[3],
			(int) (unsigned char) addr[2],
			(int) (unsigned char) addr[1],
			(int) (unsigned char) addr[0]);
	pps = SendDNSQuery(achDNSBuffer, FALSE);
	memcpy(pps->pdnsi->achInput, addr, 4);
	while (!pps->pdnsi->bComplete && !ppt->bCancel)
		FlushMessages(ppt);
	if (pps->pdnsi->bComplete)
	{
		iError = pps->pdnsi->iError;
		if (pps->pdnsi->iError)
			phe = 0;
		else
			phe = &ppt->he;
	}
	else
	{
		iError = WSAEINTR;
		phe = 0;
	}
	free(pps->pdnsi->pchQuery);
	free(pps->pdnsi);
	pps->pdnsi = 0;
	closesocket(pps->s);
	EndBlocking(ppt);
	iErrno = iError;
	return phe;
}

struct hostent FAR * CALLBACK _export gethostbyname(const char FAR * name)
{
	struct per_task *ppt;
	struct per_socket *pps;
	struct	hostent *phe;
	int	iError;
	int	iLen;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	if (!StartBlocking(ppt))
		return 0;
	/*
	 * Sanity check the argument.
	 */
	if (IsBadReadPtr(name, 1))	/* Make sure we can read at least 1 byte */
	{
		iErrno = WSAEFAULT;
		EndBlocking(ppt);
		return 0;
	}
	strcpy(achDNSBuffer, name);
	if (!strchr(achDNSBuffer, '.'))
	{
		strcat(achDNSBuffer, ".");
		strcat(achDNSBuffer, hostinfo.achDomainName);
	}
	iLen = strlen(achDNSBuffer);
	if (iLen && achDNSBuffer[iLen - 1] == '.')
		achDNSBuffer[iLen - 1] = 0;
	pps = SendDNSQuery(achDNSBuffer, TRUE);
	while (!pps->pdnsi->bComplete && !ppt->bCancel)
		FlushMessages(ppt);
	if (pps->pdnsi->bComplete)
	{
		iError = pps->pdnsi->iError;
		if (pps->pdnsi->iError)
			phe = 0;
		else
			phe = &ppt->he;
	}
	else
	{
		iError = WSAEINTR;
		phe = 0;
	}
	free(pps->pdnsi->pchQuery);
	free(pps->pdnsi);
	pps->pdnsi = 0;
	closesocket(pps->s);
	EndBlocking(ppt);
	iErrno = iError;
	return phe;
}

struct servent FAR * CALLBACK _export getservbyport(int port, const char FAR * proto)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	PORTSWAP(port);
	if (!proto)
		proto = "";
	/*
	 * Sanity check the argumets.
	 */
	else if (IsBadReadPtr(proto, 1))	/* Make sure we can read at least 1 byte */
	{
    		iErrno = WSAEFAULT;
    		return 0;
	}
	INIT_ARGS(pfaArgs[0],	AT_Int,		&port,		sizeof(port)		);
	INIT_CARGS(pfaArgs[1],	AT_String,	proto,		strlen(proto) + 1	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achServEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ServByPort, 2, pfaArgs, pfaReturn);
	if (TransmitFunctionAndBlock(ppt, &tf))
	{
		CopyServEnt(ppt);
		return &ppt->se;
	}
	else
	{
		return 0;
	}
}

struct servent FAR * CALLBACK _export getservbyname(const char FAR * name,
                                              const char FAR * proto)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	if (!proto)
		proto = "";
	INIT_CARGS(pfaArgs[0],	AT_String,	name,		strlen(name) + 1	);
	INIT_CARGS(pfaArgs[1],	AT_String,	proto,		strlen(proto) + 1	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achServEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ServByName, 2, pfaArgs, pfaReturn);
	if (TransmitFunctionAndBlock(ppt, &tf))
	{
		CopyServEnt(ppt);
		return &ppt->se;
	}
	else
	{
		return 0;
	}
}

struct protoent FAR * CALLBACK _export getprotobynumber(int proto)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[1];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	INIT_ARGS(pfaArgs[0],	AT_Int,		&proto,		sizeof(proto)	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achProtoEnt,MAX_HOST_ENT	);
	INIT_TF(tf, FN_ProtoByNumber, 1, pfaArgs, pfaReturn);
	if (TransmitFunctionAndBlock(ppt, &tf))
	{
		CopyProtoEnt(ppt);
		return &ppt->pe;
	}
	else
	{
		return 0;
	}
}

struct protoent FAR * CALLBACK _export getprotobyname(const char FAR * name)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[1];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	INIT_CARGS(pfaArgs[0],	AT_String,	name,		strlen(name) + 1	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achProtoEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ProtoByName, 1, pfaArgs, pfaReturn);
	if (TransmitFunctionAndBlock(ppt, &tf))
	{
		CopyProtoEnt(ppt);
		return &ppt->pe;
	}
	else
	{
		return 0;
	}
}

int CALLBACK _export
gethostname(char *name, int namelen)
{
	struct per_task *ppt;
	int	nReturn;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	int	nChars;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;

	nChars = strlen(hostinfo.achHostName);
	if (nChars)
	{
		if (namelen <= nChars)
		{
			iErrno = WSAEFAULT;
			return -1;
		}
		memcpy(name, hostinfo.achHostName, nChars);
		name[nChars] = 0;
		return 0;
	}

	/* Fallback - we didn't get it from tshost. This should never happen.
	 * If tshost doesn't transmit it, it is too old and won't work with
	 * this version of TwinSock anyway.
	 */
	INIT_ARGS(pfaArgs[0],	AT_String,	name,		namelen		);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&namelen,	sizeof(namelen)	);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_GetHostName, 2, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	return nReturn;
}

LRESULT	CALLBACK _export
TimerWindowProc(HWND	hWnd,
		UINT	wMsg,
		WPARAM	wParam,
		LPARAM	lParam)
{
	if (wMsg == WM_TIMER)
	{
		struct per_socket *pps;

		KillTimer(hWnd, wParam);
		pps = GetSocketInfo(wParam);
		NextDNSTry(pps);
		return 0;
	}
	return DefWindowProc(hWnd, wMsg, wParam, lParam);
}

int CALLBACK _export WSAStartup(WORD wVersionRequired, LPWSADATA lpWSAData)
{
	struct	per_task	*pptNew;
	tws_client	*pClient = (tws_client *) malloc(sizeof(tws_client));
	BOOL	bOK;
	WNDCLASS wc;

	lpWSAData->wVersion = 0x0101;
	lpWSAData->wHighVersion = 0x0101;
	strcpy(lpWSAData->szDescription,
		"TwinSock 2.0");
	bOK = StartDDE(pClient, hInstance);
	if (bOK)
	{
		strcpy(lpWSAData->szSystemStatus, "Ready");
	}
	else
	{
		free(pClient);
		strcpy(lpWSAData->szSystemStatus, "Not Ready");
	}
	lpWSAData->iMaxSockets = 256;
	lpWSAData->iMaxUdpDg = 512;
	lpWSAData->lpVendorInfo = 0;
	if (wVersionRequired == 0x0001)
		return WSAVERNOTSUPPORTED;
	if (!bOK)
		return WSASYSNOTREADY;
	pptNew = malloc(sizeof(struct per_task));
	if (pptNew == 0)
		return (WSAENOBUFS);

	memset(&wc, 0, sizeof(wc));
	wc.style = 0;
	wc.lpfnWndProc = TimerWindowProc;
	wc.cbWndExtra = sizeof(struct per_task *);
	wc.hInstance = hInstance;
	wc.lpszClassName = "TwinSock Timer Window";
	RegisterClass(&wc);

	pptNew->hwndDNS = CreateWindow(	"TwinSock Timer Window",
					"",
					WS_OVERLAPPEDWINDOW,
					0,
					0,
					100,
					100,
					0,
					0,
					hInstance,
					0
				);
	if (!pptNew->hwndDNS)
	{
		free(pptNew);
		return 0;
	}

#ifdef __FLAT__
	pptNew->htask = 0;
#else
	pptNew->htask = GetCurrentTask();
#endif
	pptNew->pptNext = pptList;
	pptNew->lpBlockFunc = DefBlockFunc;
	pptNew->bCancel = FALSE;
	pptNew->bBlocking = FALSE;
	pptNew->pClient = pClient;
	pptList = pptNew;
	return 0;
}

int CALLBACK _export WSACleanup(void)
{
	struct	per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if (ppt->bBlocking)
	{
		iErrno = WSAEINPROGRESS;
		return -1;
	}
	StopDDE(ppt->pClient);
	RemoveTask(ppt);
	return 0;
}

void CALLBACK _export WSASetLastError(int iError)
{
	if (!GetTaskInfo())
		return;
	iErrno = iError;
}

int CALLBACK _export WSAGetLastError(void)
{
	return iErrno;
}

BOOL CALLBACK _export WSAIsBlocking(void)
{
	struct per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	return ppt->bBlocking;
}

int CALLBACK _export WSAUnhookBlockingHook(void)
{
	struct	per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	ppt->lpBlockFunc = DefBlockFunc;
	return 0;
}

FARPROC CALLBACK _export WSASetBlockingHook(FARPROC lpBlockFunc)
{
	struct per_task *ppt;
	FARPROC oldfunc;
	
	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	oldfunc = ppt->lpBlockFunc;
	ppt->lpBlockFunc = lpBlockFunc;

	return (oldfunc);	/* Return the previous value */
}

int CALLBACK _export WSACancelBlockingCall(void)
{
	struct	per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if (ppt->bBlocking)
	{
		ppt->bCancel = TRUE;
		return 0;
	}
	else
	{
		iErrno = WSAEINVAL;
		return -1;
	}
}

static	void
FireAsyncRequest(struct tx_queue *ptxq)
{
	int	nLen;
	char	*pchData;
	struct	per_task *ppt;

	if (ptxq->ptxr->nError)
	{
		PostMessage(ptxq->hwnd, ptxq->wMsg, ptxq->id | 0x4000,
			WSAMAKEASYNCREPLY(0, ntohs(ptxq->ptxr->nError)));
		RemoveTXQ(ptxq);
		return;
	}

	ppt = GetAnotherTaskInfo(ptxq->htask);

	if (!ppt)
	{
		/* How **Rude** */
		RemoveTXQ(ptxq);
		return;
	}

	pchData = ptxq->ptxr->pchData +
		ntohs(ptxq->ptxr->nLen) -
		MAX_HOST_ENT -
		sizeof(short) * 5;

	switch(ptxq->ft)
	{
	case FN_HostByName:
	case FN_HostByAddr:
		/* It doesn't come here anymore */
		break;

	case FN_ServByName:
	case FN_ServByPort:
		memcpy(ppt->achServEnt, pchData, MAX_HOST_ENT);
		nLen = CopyServEntTo(ppt, ptxq->pchLocation);
		break;

	case FN_ProtoByName:
	case FN_ProtoByNumber:
		memcpy(ppt->achProtoEnt, pchData, MAX_HOST_ENT);
		nLen = CopyProtoEntTo(ppt, ptxq->pchLocation);
		break;
	}

	PostMessage(ptxq->hwnd, ptxq->wMsg, ptxq->id | 0x4000,
		WSAMAKEASYNCREPLY(nLen, 0));
	RemoveTXQ(ptxq);
}

#pragma argsused
HANDLE CALLBACK _export WSAAsyncGetServByName(HWND hWnd, u_int wMsg,
                                        const char FAR * name,
					const char FAR * proto,
                                        char FAR * buf, int buflen)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	struct	tx_queue *txq;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	if (!proto)
		proto = "";
	INIT_CARGS(pfaArgs[0],	AT_String,	name,		strlen(name) + 1	);
	INIT_CARGS(pfaArgs[1],	AT_String,	proto,		strlen(proto) + 1	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achServEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ServByName, 2, pfaArgs, pfaReturn);
	txq = TransmitFunction(&tf);
	txq->hwnd = hWnd;
	txq->pchLocation = buf;
	txq->wMsg = wMsg;
	txq->ft = FN_ServByName;
	txq->htask = ppt->htask;
	return (HANDLE) (txq->id | 0x4000);
}

#pragma argsused
HANDLE CALLBACK _export WSAAsyncGetServByPort(HWND hWnd, u_int wMsg, int port,
                                        const char FAR * proto, char FAR * buf,
                                        int buflen)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	struct	tx_queue *txq;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	port = PORTSWAP(port);
	if (!proto)
		proto = "";
	INIT_CARGS(pfaArgs[0],	AT_Int,		&port,		sizeof(port)		);
	INIT_CARGS(pfaArgs[1],	AT_String,	proto,		strlen(proto) + 1	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achServEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ServByPort, 2, pfaArgs, pfaReturn);
	txq = TransmitFunction(&tf);
	txq->hwnd = hWnd;
	txq->pchLocation = buf;
	txq->wMsg = wMsg;
	txq->ft = FN_ServByPort;
	txq->htask = ppt->htask;
	return (HANDLE) (txq->id | 0x4000);
}

#pragma argsused
HANDLE CALLBACK _export WSAAsyncGetProtoByName(HWND hWnd, u_int wMsg,
                                         const char FAR * name, char FAR * buf,
                                         int buflen)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[1];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	struct	tx_queue *txq;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	INIT_CARGS(pfaArgs[0],	AT_String,	name,		strlen(name) + 1	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achProtoEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ProtoByName, 1, pfaArgs, pfaReturn);
	txq = TransmitFunction(&tf);
	txq->hwnd = hWnd;
	txq->pchLocation = buf;
	txq->wMsg = wMsg;
	txq->ft = FN_ProtoByName;
	txq->htask = ppt->htask;
	return (HANDLE) (txq->id | 0x4000);
}

#pragma argsused
HANDLE CALLBACK _export WSAAsyncGetProtoByNumber(HWND hWnd, u_int wMsg,
                                           int number, char FAR * buf,
                                           int buflen)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[1];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	struct	tx_queue *txq;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	INIT_CARGS(pfaArgs[0],	AT_Int,		&number,	sizeof(number)		);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achProtoEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ProtoByNumber, 1, pfaArgs, pfaReturn);
	txq = TransmitFunction(&tf);
	txq->hwnd = hWnd;
	txq->pchLocation = buf;
	txq->wMsg = wMsg;
	txq->ft = FN_ProtoByNumber;
	txq->htask = ppt->htask;
	return (HANDLE) (txq->id | 0x4000);
}

#pragma argsused
HANDLE CALLBACK _export WSAAsyncGetHostByName(HWND hWnd, u_int wMsg,
                                        const char FAR * name, char FAR * buf,
                                        int buflen)
{
	struct per_task *ppt;
	struct per_socket *pps;
	struct	hostent *phe;
	int	iError;
	int	iLen;
	dns_info *pdnsi;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	/*
	 * Sanity check the argument.
	 */
	if (IsBadReadPtr(name, 1))	/* Make sure we can read at least 1 byte */
	{
		iErrno = WSAEFAULT;
		return 0;
	}
	strcpy(achDNSBuffer, name);
	if (!strchr(achDNSBuffer, '.'))
	{
		strcat(achDNSBuffer, ".");
		strcat(achDNSBuffer, hostinfo.achDomainName);
	}
	iLen = strlen(achDNSBuffer);
	if (iLen && achDNSBuffer[iLen - 1] == '.')
		achDNSBuffer[iLen - 1] = 0;
	pps = SendDNSQuery(achDNSBuffer, TRUE);
	pdnsi = pps->pdnsi;
	pdnsi->hwndNotify = hWnd;
	pdnsi->pchLocation = buf;
	pdnsi->wMsg = wMsg;
	return (HANDLE) (pdnsi->idRequest | 0x4000);
}

#pragma argsused
HANDLE CALLBACK _export WSAAsyncGetHostByAddr(HWND hWnd, u_int wMsg,
                                        const char FAR * addr, int len, int type,
                                        char FAR * buf, int buflen)
{
	struct per_task *ppt;
	struct per_socket *pps;
	struct	hostent *phe;
	int	iError;
	dns_info *pdnsi;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	/*
	 * Sanity check the arguments.
	 */
	if ((type != PF_INET) ||			/* WINSOCK spec says this must be PF_INET */
	    (len != 4) ||				/* WINSOCK spec says must be 4 for PF_INET */
	    IsBadReadPtr(addr, len))			/* Can we read the host address */
	{
		iErrno = WSAEFAULT;
		return 0;
	}
	sprintf(achDNSBuffer, "%d.%d.%d.%d.in-addr.arpa",
			(int) (unsigned char) addr[3],
			(int) (unsigned char) addr[2],
			(int) (unsigned char) addr[1],
			(int) (unsigned char) addr[0]);
	pps = SendDNSQuery(achDNSBuffer, FALSE);
	pdnsi = pps->pdnsi;
	memcpy(pdnsi->achInput, addr, 4);
	pdnsi->hwndNotify = hWnd;
	pdnsi->pchLocation = buf;
	pdnsi->wMsg = wMsg;
	return (HANDLE) (pdnsi->idRequest | 0x4000);
}

int CALLBACK _export WSACancelAsyncRequest(HANDLE hAsyncTaskHandle)
{
	struct	tx_queue *ptxq;
	struct	per_socket *pps;
	struct	per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;

	for (ptxq = ptxqList; ptxq; ptxq = ptxq->ptxqNext)
	{
		if ((HANDLE) (ptxq->id | 0x4000) == hAsyncTaskHandle)
		{
			RemoveTXQ(ptxq);
			return 0;
		}
	}
	for (pps = ppsList; pps; pps++)
	{
		if (pps->htaskOwner == ppt->htask &&
		    pps->pdnsi &&
		    (HANDLE) (pps->pdnsi->idRequest | 0x4000) == hAsyncTaskHandle)
		{
			KillTimer(ppt->hwndDNS, pps->s);
			free(pps->pdnsi->pchQuery);
			free(pps->pdnsi);
			pps->pdnsi = 0;
			closesocket(pps->s);
			return 0;
		}
	}
	iErrno = WSAEINVAL;
	return -1;
}

int CALLBACK _export WSAAsyncSelect(SOCKET s, HWND hWnd, u_int wMsg,
                               long lEvent)
{
	struct	per_socket *pps;

	if (GetTaskInfo() == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if (lEvent)
		pps->iFlags |= PSF_NONBLOCK;
	pps->hWnd = hWnd;
	pps->wMsg = wMsg;
	pps->iEvents = lEvent;
	if (!(pps->iFlags & PSF_MUSTCONN) ||
	    (pps->iFlags & PSF_CONNECT))
		Notify(pps, FD_WRITE);
	if (pps->pdIn)
		Notify(pps, (pps->iFlags & PSF_ACCEPT) ?
				FD_ACCEPT :
				(pps->pdIn->iLen ?
					FD_READ :
					FD_CLOSE));
	return 0;
}

int CALLBACK _export __WSAFDIsSet(SOCKET s, fd_set FAR *pfds)
{
	u_short	i;

	if (!GetTaskInfo())
		return -1;
	if (!GetSocketInfo(s))
		return -1;
	for (i = 0; i < pfds->fd_count; i++)
	{
		if (pfds->fd_array[i] == s)
			return TRUE;
	}
	return FALSE;
}


