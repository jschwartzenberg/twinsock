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
#include <string.h>
#include <dos.h>
#include "twinsock.h"
#include "tx.h"

static	int	iErrno = 0;
static	short	idNext = 0;

static	struct	per_task	*pptList = 0;
static	struct	per_socket	*ppsList = 0;
static	struct	tx_queue	*ptxqList = 0;

HWND	hwndManager = 0;
BOOL	bEstablished = 0;

static	void	FireAsyncRequest(struct tx_queue *ptxq);

void _export
RegisterManager(HWND hwnd)
{
	hwndManager = hwnd;
}

void _export
SetInitialised(void)
{
	bEstablished = TRUE;
}


void
CopyDataIn(	void		*pvSource,
		enum arg_type	at,
		void		*pvDest,
		int		nLen)
{
	switch(at)
	{
	case AT_Int16:
		*(short *) pvDest = ntohs(*(short *) pvSource);
		break;

	case AT_Int32:
		*(short *) pvDest = ntohs(*(short *) pvSource);
		break;

	case AT_Char:
		*(char *) pvDest = *(char *) pvSource;
		break;

	case AT_Int16Ptr:
	case AT_Int32Ptr:
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
	case AT_Int16:
		*(short *) pvDest = htons(*(short *) pvSource);
		break;

	case AT_Int32:
		*(short *) pvDest = htons(*(short *) pvSource);
		break;

	case AT_Char:
		*(char *) pvDest = *(char *) pvSource;
		break;

	case AT_Int16Ptr:
	case AT_Int32Ptr:
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
	return GetAnotherTaskInfo(GetCurrentTask());
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

static	struct	per_socket *
NewSocket(struct per_task *ppt, SOCKET s)
{
	struct per_socket *ppsNew;

	ppsNew = (struct per_socket *) malloc(sizeof(struct per_socket));
	ppsNew->s = s;
	ppsNew->iFlags = 0;
	ppsNew->pdIn = 0;
	ppsNew->pdOut = 0;
	ppsNew->htaskOwner = ppt->htask;
	ppsNew->ppsNext = ppsList;
	ppsNew->iEvents = 0;
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
		for (ppd = &ppsParent->pdIn; *ppd; ppd = &(*ppd)->pdNext)
		{
			if ((*ppd)->pchData == (char *) pps)
			{
				pd = *ppd;
				*ppd = pd->pdNext;
				free(pd);
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
					RemoveSocket((struct per_socket *) pd->pchData);
				}
				else
				{
					free(pd->pchData);
				}
				free(pd);
			}
			free(pps);
			return;
		}
	}
}

static	void
DataReceived(SOCKET s, void *pvData, int nLen, enum function_type ft)
{
	struct	data	*pdNew, **ppdList;
	struct	per_socket *pps;
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
				ns = ntohl(*(long *) pvData);
			else
				ns = ntohs(*(short*) pvData);
			SendEarlyClose(ns);
		}
		return;
	}
	for (ppdList = &pps->pdIn; *ppdList; ppdList = &(*ppdList)->pdNext);

	pdNew = (struct data *) malloc(sizeof(struct data));
	pdNew->sin = *(struct sockaddr_in *) pvData;
	pdNew->sin.sin_family = ntohs(pdNew->sin.sin_family);

	nLen -= sizeof(struct sockaddr_in);
	pvData = (char *) pvData + sizeof(struct sockaddr_in);

	pdNew->pdNext = 0;
	pdNew->nUsed = 0;
	*ppdList = pdNew;

	if (pps->iFlags & PSF_ACCEPT)
	{
		pdNew->iLen = 0;
		if (nLen == sizeof(long))
			ns = ntohl(*(long *) pvData);
		else
			ns = ntohs(*(short*) pvData);
		ppt = GetAnotherTaskInfo(pps->htaskOwner);
		pdNew->pchData = (char *) NewSocket(ppt, ns);
		if (pdNew == pps->pdIn)
			Notify(pps, FD_ACCEPT);
	}
	else
	{
		pdNew->iLen = nLen;
		pdNew->pchData = (char *) malloc(nLen);
		memcpy(pdNew->pchData, pvData, nLen);

		if (pdNew == pps->pdIn)
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

static	BOOL
FlushMessages(struct per_task *ppt)
{
	MSG msg;
	BOOL ret;

	if (ppt->lpBlockFunc)
		return ((BOOL far pascal (*)()) ppt->lpBlockFunc)();

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
			ppt->bCancel = TRUE;
	}
	return ret;
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

	for (pppt = &pptList; *pppt; pppt = &((*pppt)->pptNext))
	{
		if (*pppt == ppt)
		{
			*pppt = ppt->pptNext;
			free(ppt);
		}
	}
};

void far _export
ResponseReceived(struct tx_request *ptxr)
{
	int		nLen;
	int		id;
	struct	tx_queue *ptxq;
	enum Functions	ft;

	ft = (enum Functions) ntohs(ptxr->iType);
	id = ntohs(ptxr->id);
	nLen = ntohs(ptxr->nLen);
	if (ft == FN_Data || ft == FN_Accept)
	{
		DataReceived(id, ptxr->pchData, nLen - sizeof(short) * 5, ft);
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
}

static	struct tx_queue *
TransmitFunction(struct transmit_function *ptf)
{
	int	i;
	int	nSize = sizeof(short) * 5;
	struct	tx_request *ptxr;
	struct	tx_queue *ptxq, **pptxq;
	int	iOffset;

	for (i = 0; i < ptf->nArgs; i++)
		nSize += ptf->pfaList[i].iLen + sizeof(short) * 2;
	nSize += ptf->pfaResult->iLen + sizeof(short) * 2;
	ptxr = (struct tx_request *) malloc(nSize);
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
	SendMessage(hwndManager, WM_USER, nSize, (LPARAM) ptxr);
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
			*(short *) (ptxr->pchData + iOffset) = htons((short) ptf->pfaList[i].at);
			iOffset += 2;
			*(short *) (ptxr->pchData + iOffset) = htons((short) ptf->pfaList[i].iLen);
			iOffset += 2;
			if (!ptf->pfaList[i].bConstant)
				CopyDataIn(	ptxr->pchData + iOffset,
						ptf->pfaList[i].at,
						ptf->pfaList[i].pvData,
						ptf->pfaList[i].iLen);
			iOffset += ptf->pfaList[i].iLen;
		}
		*(short *) (ptxr->pchData + iOffset) = htons((short) ptf->pfaResult->at);
		iOffset += 2;
		*(short *) (ptxr->pchData + iOffset) = htons((short) ptf->pfaResult->iLen);
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

	CopyHostEnt(ppt);

	phe = (struct hostent *) pchData;
	memcpy(phe, &ppt->he, sizeof(*phe));

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
		phe->h_addr_list[i] = phe->h_addr_list[i] - pchOld + pchData;
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
	memcpy(pse, &ppt->se, sizeof(*pse));
	pchData += sizeof(struct servent *);

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
	int	nAddrs;

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
	int	iLocation;
	int	i;
	int	nAlii;
	struct	protoent *ppe;

	CopyProtoEnt(ppt);

	ppe = (struct protoent *) pchData;
	memcpy(ppe, &ppt->se, sizeof(*ppe));
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

SOCKET pascal far _export
accept(SOCKET s, struct sockaddr FAR *addr,
                          int FAR *addrlen)
{
	struct per_task *ppt;
	struct per_socket *pps, *ppsNew;
	struct data *pd;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if (!(pps->iFlags & PSF_ACCEPT))
	{
		iErrno = WSAEINVAL;
		return -1;
	}
	if (!pps->pdIn && (pps->iFlags & PSF_NONBLOCK))
	{
		iErrno = WSAEWOULDBLOCK;
		return -1;
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

int pascal far _export
bind(SOCKET s, const struct sockaddr FAR *addr, int namelen)
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
	if (!GetSocketInfo(s))
		return -1;
	psa = (struct sockaddr *) malloc(namelen);
	memcpy(psa, addr, namelen);
	psa->sa_family = htons(psa->sa_family);
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_CARGS(pfaArgs[1],	AT_GenPtr,	psa,		namelen			);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	&namelen,	sizeof(namelen)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Bind, 3, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	free(psa);
	return nReturn;
}

int pascal far _export
closesocket(SOCKET s)
{
	struct per_task *ppt;
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[1];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Close, 1, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	RemoveSocket(pps); /* Assume success. Is this valid? */
	return nReturn;
}

int pascal far _export
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
	psa = (struct sockaddr *) malloc(namelen);
	memcpy(psa, name, namelen);
	psa->sa_family = htons(psa->sa_family);
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_CARGS(pfaArgs[1],	AT_GenPtr,	psa,		namelen			);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	&namelen,	sizeof(namelen)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Connect, 3, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	free(psa);
	if (nReturn != -1)
		pps->iFlags |= PSF_CONNECT;
	return nReturn;
}

int pascal far _export
ioctlsocket(SOCKET s, long cmd, u_long far * arg)
{
	struct per_task *ppt;
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[3];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	switch(cmd)
	{
	case FIONBIO:
		if (*arg)
			pps->iFlags |= PSF_NONBLOCK;
		else
			pps->iFlags &= ~PSF_NONBLOCK;
		break;

	case FIONREAD:
		if (pps->pdIn)
			*arg = pps->pdIn->iLen - pps->pdIn->nUsed;
		else
			*arg = 0;
		break;

	case SIOCATMARK:
		*(BOOL *) arg = 0;
		break;
	}
	return 0;
}

int pascal far _export getpeername (SOCKET s, struct sockaddr FAR *name,
                            int FAR * namelen)
{
	struct per_task *ppt;
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[3];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if (!GetSocketInfo(s))
		return -1;
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_GenPtr,	name,		*namelen		);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	namelen,	sizeof(*namelen)	);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_GetPeerName, 3, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	if (nReturn != -1)
		name->sa_family = ntohs(name->sa_family);
	return nReturn;
}

int pascal far _export getsockname (SOCKET s, struct sockaddr FAR *name,
                            int FAR * namelen)
{
	struct per_task *ppt;
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[3];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if (!GetSocketInfo(s))
		return -1;
	*namelen = sizeof(struct sockaddr_in); /* Just in case */
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_GenPtr,	name,		*namelen		);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	namelen,	sizeof(*namelen)	);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_GetSockName, 3, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	if (nReturn != -1)
		name->sa_family = ntohs(name->sa_family);
	return nReturn;
}

int pascal far _export getsockopt (SOCKET s, int level, int optname,
                           char FAR * optval, int FAR *optlen)
{
	struct per_task *ppt;
	struct per_socket *pps;
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
			*optval = iOptVal;
		}
	}
	return nReturn;
}

u_long pascal far _export htonl (u_long hostlong)
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

u_short pascal far _export htons (u_short hostshort)
{
	char	*pchValue = (char *) &hostshort;
	char	c;

	c = pchValue[0];
	pchValue[0] = pchValue[1];
	pchValue[1] = c;
	return hostshort;
}

unsigned long pascal far _export inet_addr (const char FAR * cp)
{
	unsigned long	iValue;
	char	*pchValue = (char *) &iValue;

	if (!GetTaskInfo())
		return -1;
	pchValue[0] = atoi(cp);
	cp = strchr(cp, '.');
	if (cp)
	{
		cp++;
		pchValue[1] = atoi(cp);
		cp = strchr(cp, '.');
		if (cp)
		{
			cp++;
			pchValue[2] = atoi(cp);
			cp = strchr(cp, '.');
			if (cp)
			{
				cp++;
				pchValue[3] = atoi(cp);
				cp = strchr(cp, '.');
				if (!cp)
				{
					return iValue;
				}
			}
		}
	}
	return -1;
}

char FAR * pascal far _export inet_ntoa (struct in_addr in)
{
	struct per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;

	sprintf(ppt->achAddress, "%d.%d.%d.%d",
			(int) in.S_un.S_un_b.s_b1,
			(int) in.S_un.S_un_b.s_b2,
			(int) in.S_un.S_un_b.s_b3,
			(int) in.S_un.S_un_b.s_b4);
	return ppt->achAddress;
}

int pascal far _export listen (SOCKET s, int backlog)
{
	struct per_task *ppt;
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if (pps->iFlags & PSF_CONNECT)
	{
		iErrno = WSAEISCONN;
		return -1;
	}
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&backlog,	sizeof(backlog)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Listen, 2, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	if (nReturn != -1)
		pps->iFlags |= PSF_ACCEPT;
	return nReturn;
}

u_long pascal far _export ntohl (u_long netlong)
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

u_short pascal far _export ntohs (u_short netshort)
{
	char	*pchValue = (char *) &netshort;
	char	c;

	c = pchValue[0];
	pchValue[0] = pchValue[1];
	pchValue[1] = c;
	return netshort;
}

int pascal far _export recv (SOCKET s, char FAR * buf, int len, int flags)
{
	return recvfrom(s, buf, len, flags, 0, 0);
}

int pascal far _export recvfrom (SOCKET s, char FAR * buf, int len, int flags,
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

	pd = pps->pdIn;
	if (from && fromlen)
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

	EndBlocking(ppt);
	return len;
}

#define	PPS_ERROR	((struct per_socket *) -1)

static	struct	per_socket **
GetPPS(fd_set *fds)
{
	struct per_socket **pps;
	int	i;

	if (!fds || !fds->fd_count)
		return 0;
	pps = (struct per_socket *) malloc(sizeof(struct per_socket *) *
					fds->fd_count);
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


int pascal far _export select (int nfds, fd_set *readfds, fd_set far *writefds,
                fd_set *exceptfds, const struct timeval far *timeout)
{
	struct per_task *ppt;
	struct per_socket **ppsRead, **ppsWrite, **ppsExcept;
	BOOL	bOneOK = FALSE;
	BOOL	bTimedOut = FALSE;
	int	iOld, iNew;
	unsigned	long	tExpire;
	int	i;

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
		free(ppsRead);
		return -1;
	}
	ppsExcept = GetPPS(exceptfds);
	if (ppsExcept == PPS_ERROR)
	{
		free(ppsRead);
		free(ppsWrite);
		return -1;
	}
		
	while (!bOneOK && !bTimedOut && !ppt->bCancel)
	{
		FlushMessages(ppt);
		if (ppsWrite)
			bOneOK = TRUE;
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
		nfds += writefds->fd_count;
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

int pascal far _export send (SOCKET s, const char FAR * buf, int len, int flags)
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
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_CARGS(pfaArgs[1],	AT_GenPtr,	buf,		len			);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	&len,		sizeof(len)		);
	INIT_ARGS(pfaArgs[3],	AT_Int,		&flags,		sizeof(flags)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Send, 4, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	Notify(pps, FD_WRITE);
	return nReturn;
}

int pascal far _export sendto (SOCKET s, const char FAR * buf, int len, int flags,
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
	psa = (struct sockaddr *) malloc(tolen);
	memcpy(psa, to, tolen);
	psa->sa_family = htons(psa->sa_family);
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_CARGS(pfaArgs[1],	AT_GenPtr,	buf,		len			);
	INIT_ARGS(pfaArgs[2],	AT_IntPtr,	&len,		sizeof(len)		);
	INIT_ARGS(pfaArgs[3],	AT_Int,		&flags,		sizeof(flags)		);
	INIT_CARGS(pfaArgs[4],	AT_GenPtr,	to,		tolen			);
	INIT_ARGS(pfaArgs[5],	AT_Int,		&tolen,		sizeof(tolen)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Send, 6, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	free(psa);
	Notify(pps, FD_WRITE);
	return nReturn;
}

int pascal far _export setsockopt (SOCKET s, int level, int optname,
                           const char FAR * optval, int optlen)
{
	struct per_task *ppt;
	struct per_socket *pps;
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
	TransmitFunctionAndBlock(ppt, &tf);
	return nReturn;
}

int pascal far _export shutdown (SOCKET s, int how)
{
	struct per_task *ppt;
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	INIT_ARGS(pfaArgs[0],	AT_Int,		&s,		sizeof(s)		);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&how,		sizeof(how)		);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Shutdown, 2, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	if (nReturn != -1 && (how == 0 || how == 2))
		pps->iFlags |= PSF_SHUTDOWN;
	return nReturn;
}

SOCKET pascal far _export socket (int af, int type, int protocol)
{
	struct per_task *ppt;
	struct per_socket *pps;
	int	nReturn;
	struct	func_arg pfaArgs[3];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	INIT_ARGS(pfaArgs[0],	AT_Int,		&af,		sizeof(af)		);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&type,		sizeof(type)		);
	INIT_ARGS(pfaArgs[2],	AT_Int,		&protocol,	sizeof(protocol)	);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_Socket, 3, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	if (nReturn != -1)
		NewSocket(ppt, nReturn);
	return nReturn;
}

struct hostent FAR * pascal far _export gethostbyaddr(const char FAR * addr,
                                              int len, int type)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[3];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	INIT_CARGS(pfaArgs[0],	AT_GenPtr,	addr,		len			);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&len,		sizeof(len)		);
	INIT_ARGS(pfaArgs[2],	AT_Int,		&type,		sizeof(type)		);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achHostEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_HostByAddr, 3, pfaArgs, pfaReturn);
	if (TransmitFunctionAndBlock(ppt, &tf))
	{
		CopyHostEnt(ppt);
		return &ppt->he;
	}
	else
	{
		return 0;
	}
}

struct hostent FAR * pascal far _export gethostbyname(const char FAR * name)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[1];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	INIT_CARGS(pfaArgs[0],	AT_String,	name,		strlen(name) + 1	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achHostEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_HostByName, 1, pfaArgs, pfaReturn);
	if (TransmitFunctionAndBlock(ppt, &tf))
	{
		CopyHostEnt(ppt);
		return &ppt->he;
	}
	else
	{
		return 0;
	}
}

struct servent FAR * pascal far _export getservbyport(int port, const char FAR * proto)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
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

struct servent FAR * pascal far _export getservbyname(const char FAR * name,
                                              const char FAR * proto)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
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

struct protoent FAR * pascal far _export getprotobynumber(int proto)
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

struct protoent FAR * pascal far _export getprotobyname(const char FAR * name)
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

int pascal far _export
gethostname(char *name, int namelen)
{
	struct per_task *ppt;
	int	nReturn;
	struct	func_arg pfaArgs[2];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	INIT_ARGS(pfaArgs[0],	AT_String,	name,		namelen		);
	INIT_ARGS(pfaArgs[1],	AT_Int,		&namelen,	sizeof(namelen)	);
	INIT_ARGS(pfaReturn,	AT_Int,		&nReturn,	sizeof(nReturn)		);
	INIT_TF(tf, FN_GetHostName, 2, pfaArgs, pfaReturn);
	TransmitFunctionAndBlock(ppt, &tf);
	return nReturn;
}

int pascal far _export WSAStartup(WORD wVersionRequired, LPWSADATA lpWSAData)
{
	struct	per_task	*pptNew;

	lpWSAData->wVersion = 0x0101;
	lpWSAData->wHighVersion = 0x0101;
	strcpy(lpWSAData->szDescription,
		"TwinSock 1.0 - Proxy sockets system. "
		"Copyright 1994 Troy Rollo. "
		"This library is free software. "
		"See the file \"COPYING.LIB\" from the "
		"distribution for details.");
	if (!hwndManager)
		strcpy(lpWSAData->szSystemStatus, "Not Initialised.");
	else if (bEstablished)
		strcpy(lpWSAData->szSystemStatus, "Ready.");
	else
		strcpy(lpWSAData->szSystemStatus, "Initialising.");
	lpWSAData->iMaxSockets = 256;
	lpWSAData->iMaxUdpDg = 512;
	lpWSAData->lpVendorInfo = 0;
	if (wVersionRequired == 0x0001)
		return WSAVERNOTSUPPORTED;
	if (!bEstablished)
		return WSASYSNOTREADY;
	pptNew = malloc(sizeof(struct per_task));
	pptNew->htask = GetCurrentTask();
	pptNew->pptNext = pptList;
	pptNew->lpBlockFunc = 0;
	pptNew->bCancel = FALSE;
	pptNew->bBlocking = FALSE;
	pptList = pptNew;
	return 0;
}

int pascal far _export WSACleanup(void)
{
	struct	per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if (ppt->bBlocking)
	{
		iErrno = WSAEINPROGRESS;
		RemoveTask(ppt);
		return -1;
	}
	return 0;
}

void pascal far _export WSASetLastError(int iError)
{
	if (!GetTaskInfo())
		return -1;
	iErrno = iError;
}

int pascal far _export WSAGetLastError(void)
{
	return iErrno;
}

BOOL pascal far _export WSAIsBlocking(void)
{
	struct per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	return ppt->bBlocking;
}

int pascal far _export WSAUnhookBlockingHook(void)
{
	struct	per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	ppt->lpBlockFunc = 0;
	return 0;
}

FARPROC pascal far _export WSASetBlockingHook(FARPROC lpBlockFunc)
{
	struct per_task *ppt;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	ppt->lpBlockFunc = lpBlockFunc;
	return -1;
}

int pascal far _export WSACancelBlockingCall(void)
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
		memcpy(ppt->achHostEnt, pchData, MAX_HOST_ENT);
		nLen = CopyHostEntTo(ppt, ptxq->pchLocation);
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

HANDLE pascal far _export WSAAsyncGetServByName(HWND hWnd, u_int wMsg,
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
	INIT_CARGS(pfaArgs[0],	AT_String,	name,		strlen(name) + 1	);
	INIT_CARGS(pfaArgs[1],	AT_String,	proto,		strlen(proto) + 1	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achHostEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ServByName, 2, pfaArgs, pfaReturn);
	txq = TransmitFunction(&tf);
	txq->hwnd = hWnd;
	txq->pchLocation = buf;
	txq->wMsg = wMsg;
	txq->ft = FN_ServByName;
	txq->htask = ppt->htask;
	return (txq->id | 0x4000);
}

HANDLE pascal far _export WSAAsyncGetServByPort(HWND hWnd, u_int wMsg, int port,
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
	INIT_CARGS(pfaArgs[0],	AT_Int,		&port,		sizeof(port)		);
	INIT_CARGS(pfaArgs[1],	AT_String,	proto,		strlen(proto) + 1	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achHostEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ServByPort, 2, pfaArgs, pfaReturn);
	txq = TransmitFunction(&tf);
	txq->hwnd = hWnd;
	txq->pchLocation = buf;
	txq->wMsg = wMsg;
	txq->ft = FN_ServByPort;
	txq->htask = ppt->htask;
	return (txq->id | 0x4000);
}

HANDLE pascal far _export WSAAsyncGetProtoByName(HWND hWnd, u_int wMsg,
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
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achHostEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ProtoByName, 1, pfaArgs, pfaReturn);
	txq = TransmitFunction(&tf);
	txq->hwnd = hWnd;
	txq->pchLocation = buf;
	txq->wMsg = wMsg;
	txq->ft = FN_ProtoByName;
	txq->htask = ppt->htask;
	return (txq->id | 0x4000);
}

HANDLE pascal far _export WSAAsyncGetProtoByNumber(HWND hWnd, u_int wMsg,
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
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achHostEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_ProtoByNumber, 1, pfaArgs, pfaReturn);
	txq = TransmitFunction(&tf);
	txq->hwnd = hWnd;
	txq->pchLocation = buf;
	txq->wMsg = wMsg;
	txq->ft = FN_ProtoByNumber;
	txq->htask = ppt->htask;
	return (txq->id | 0x4000);
}

HANDLE pascal far _export WSAAsyncGetHostByName(HWND hWnd, u_int wMsg,
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
	INIT_CARGS(pfaArgs[0],	AT_GenPtr,	name,		strlen(name) + 1	);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achHostEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_HostByName, 1, pfaArgs, pfaReturn);
	txq = TransmitFunction(&tf);
	txq->hwnd = hWnd;
	txq->pchLocation = buf;
	txq->wMsg = wMsg;
	txq->ft = FN_HostByName;
	txq->htask = ppt->htask;
	return (txq->id | 0x4000);
}

HANDLE pascal far _export WSAAsyncGetHostByAddr(HWND hWnd, u_int wMsg,
                                        const char FAR * addr, int len, int type,
                                        char FAR * buf, int buflen)
{
	struct per_task *ppt;
	struct	func_arg pfaArgs[3];
	struct	func_arg pfaReturn;
	struct	transmit_function tf;
	struct	tx_queue *txq;

	if ((ppt = GetTaskInfo()) == 0)
		return 0;
	INIT_CARGS(pfaArgs[0],	AT_GenPtr,	addr,		len			);
	INIT_CARGS(pfaArgs[1],	AT_Int,		&len,		sizeof(len)		);
	INIT_CARGS(pfaArgs[2],	AT_Int,		&type,		sizeof(type)		);
	INIT_ARGS(pfaReturn,	AT_GenPtr,	ppt->achHostEnt,MAX_HOST_ENT		);
	INIT_TF(tf, FN_HostByAddr, 3, pfaArgs, pfaReturn);
	txq = TransmitFunction(&tf);
	txq->hwnd = hWnd;
	txq->pchLocation = buf;
	txq->wMsg = wMsg;
	txq->ft = FN_HostByAddr;
	txq->htask = ppt->htask;
	return (txq->id | 0x4000);
}

int pascal far _export WSACancelAsyncRequest(HANDLE hAsyncTaskHandle)
{
	struct	tx_queue *ptxq;

	if (!GetTaskInfo())
		return -1;

	for (ptxq = ptxqList; ptxq; ptxq = ptxq->ptxqNext)
	{
		if ((HANDLE) (ptxq->id | 0x4000) == hAsyncTaskHandle)
		{
			RemoveTXQ(ptxq);
			return 0;
		}
	}
	iErrno = WSAEINVAL;
	return -1;
}

int pascal far _export WSAAsyncSelect(SOCKET s, HWND hWnd, u_int wMsg,
                               long lEvent)
{
	struct	per_task *ppt;
	struct	per_socket *pps;

	if ((ppt = GetTaskInfo()) == 0)
		return -1;
	if ((pps = GetSocketInfo(s)) == 0)
		return -1;
	if (lEvent)
		pps->iFlags |= PSF_NONBLOCK;
	pps->hWnd = hWnd;
	pps->wMsg = wMsg;
	pps->iEvents = lEvent;
	Notify(pps, FD_WRITE);
	return 0;
}

int FAR PASCAL _export _WSAFDIsSet(SOCKET s, fd_set FAR *pfds)
{
	int	i;

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