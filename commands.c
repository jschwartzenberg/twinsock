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

#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <sys/socket.h>
#include "twinsock.h"
#include "tx.h"
#include "wserror.h"

struct
{
	int	iErrnoHost;
	int	iErrnoDos;
} error_mappings[] =
{
	{ 0,		0		},
	{ EINTR,	WSAEINTR	},
	{ EBADF,	WSAEBADF	},
	{ EINVAL,	WSAEINVAL	},
	{ EMFILE,	WSAEMFILE	},
	{ EWOULDBLOCK,	WSAEWOULDBLOCK	},
	{ EINPROGRESS,	WSAEINPROGRESS	},
	{ EALREADY,	WSAEALREADY	},
	{ ENOTSOCK,	WSAENOTSOCK	},
	{ EDESTADDRREQ,	WSAEDESTADDRREQ },
	{ EMSGSIZE,	WSAEMSGSIZE	},
	{ EPROTOTYPE,	WSAEPROTOTYPE	},
	{ ENOPROTOOPT,	WSAENOPROTOOPT	},
	{ EPROTONOSUPPORT, WSAEPROTONOSUPPORT },
	{ ESOCKTNOSUPPORT, WSAESOCKTNOSUPPORT },
	{ EOPNOTSUPP,	WSAEOPNOTSUPP	},
	{ EPFNOSUPPORT,	WSAEPFNOSUPPORT },
	{ EAFNOSUPPORT, WSAEAFNOSUPPORT },
	{ EADDRINUSE,	WSAEADDRINUSE	},
	{ EADDRNOTAVAIL,WSAEADDRNOTAVAIL },
	{ ENETDOWN,	WSAENETDOWN	},
	{ ENETUNREACH,	WSAENETUNREACH	},
	{ ENETRESET,	WSAENETRESET	},
	{ ECONNABORTED,	WSAECONNABORTED	},
	{ ECONNRESET,	WSAECONNRESET	},
	{ ENOBUFS,	WSAENOBUFS	},
	{ EISCONN,	WSAEISCONN	},
	{ ENOTCONN,	WSAENOTCONN	},
	{ ESHUTDOWN,	WSAESHUTDOWN	},
	{ ETOOMANYREFS,	WSAETOOMANYREFS	},
	{ ETIMEDOUT,	WSAETIMEDOUT	},
	{ ECONNREFUSED,	WSAECONNREFUSED	},
	{ ELOOP,	WSAELOOP,	},
	{ ENAMETOOLONG,	WSAENAMETOOLONG	},
	{ EHOSTDOWN,	WSAEHOSTDOWN	},
	{ EHOSTUNREACH,	WSAEHOSTUNREACH	},
	{ -1,		-1		}
}, h_error_mappings[] =
{ 
	{ 0,		0		},
	{ HOST_NOT_FOUND, WSAHOST_NOT_FOUND },
	{ TRY_AGAIN,	WSATRY_AGAIN	},
	{ NO_RECOVERY,	WSANO_RECOVERY	},
	{ NO_DATA,	WSANO_DATA	},
	{ -1,		-1		}
};

/* some functions to allow us to copy integer data
 * to and from unaligned addresses
 */

static short
ToShort(char *pchData)
{
	short	n;

	memcpy(&n, pchData, sizeof(short));
	return n;
}

static long
ToLong(char *pchData)
{
	long n;

	memcpy(&n, pchData, sizeof(long));
	return n;
}

static void
FromShort(char *pchData, short n)
{
	memcpy(pchData, &n, sizeof(short));
}

static void
FromLong(char *pchData, long n)
{
	memcpy(pchData, &n, sizeof(long));
}

static int
GetIntVal(struct func_arg *pfa)
{
	switch(pfa->at)
	{
	case AT_Int16:
	case AT_Int16Ptr:
		return ToShort(pfa->pvData);

	case AT_Int32:
	case AT_Int32Ptr:
		return ToLong(pfa->pvData);
	}
}

void
SetIntVal(struct func_arg *pfa, int iVal)
{
	switch(pfa->at)
	{
	case AT_Int16:
	case AT_Int16Ptr:
		FromShort(pfa->pvData,(short) iVal);
		return;

	case AT_Int32:
	case AT_Int32Ptr:
		FromLong(pfa->pvData, iVal);
		return;
	}
};

struct sockaddr *
ConvertSA(struct func_arg *pfa, struct sockaddr_in *sin)
{
	memcpy(sin, pfa->pvData, sizeof(*sin));
	sin->sin_family = ntohs(sin->sin_family);
	return (struct sockaddr *) sin;
}

int
MapError(int iError)
{
	int	i;

	for (i = 0; error_mappings[i].iErrnoHost != -1; i++)
	{
		if (error_mappings[i].iErrnoHost == iError)
			return error_mappings[i].iErrnoDos;
	}
	return WSAEFAULT;
}

int
MapHError(int iError)
{
	int	i;

	for (i = 0; h_error_mappings[i].iErrnoHost != -1; i++)
	{
		if (h_error_mappings[i].iErrnoHost == iError)
			return h_error_mappings[i].iErrnoDos;
	}
	return WSAEFAULT;
}

int
CopyString(void *pvData, char *pchString, int iMax)
{
	char	*pchData;
	int	iLen;

	pchData = (char *) pvData;
	iLen = strlen(pchString);
	if (iLen + 1 > iMax - 1)
		return 0;
	strcpy(pchData,	pchString);
	return iLen + 1;
}

void
CopyHostEnt(void *pvData, struct hostent *phe)
{
	int	iLocation;
	char	*pchData;
	int	i;

	pchData = (char *) pvData;
	FromShort(pchData, htons(phe->h_addrtype));
	FromShort(pchData + sizeof(short), htons(phe->h_length));
	for (i = 0; phe->h_addr_list[i]; i++);
	FromShort(pchData + sizeof(short) * 2, htons(i));
	iLocation = sizeof(short) * 3;
	for (i = 0; phe->h_addr_list[i]; i++)
	{
		memcpy(pchData + iLocation, phe->h_addr_list[i], 4);
		iLocation += 4;
	}
	iLocation += CopyString(pchData + iLocation,
				phe->h_name,
				MAX_HOST_ENT - iLocation);
	for (i = 0; phe->h_aliases[i]; i++)
	{
		iLocation += CopyString(pchData + iLocation,
					phe->h_aliases[i],
					MAX_HOST_ENT - iLocation);
	}
	pchData[iLocation] = 0;
}

void
CopyNetEnt(void *pvData, struct netent *pne)
{
	int	iLocation;
	char	*pchData;
	int	i;

	pchData = (char *) pvData;
	FromShort(pchData, htons(pne->n_addrtype));
	FromLong(pchData + sizeof(short), pne->n_net);
	iLocation = sizeof(short) + sizeof(long);
	iLocation += CopyString(pchData + iLocation,
				pne->n_name,
				MAX_HOST_ENT - iLocation);
	for (i = 0; pne->n_aliases[i]; i++)
	{
		iLocation += CopyString(pchData + iLocation,
					pne->n_aliases[i],
					MAX_HOST_ENT - iLocation);
	}
	pchData[iLocation] = 0;
}

void
CopyServEnt(void *pvData, struct servent *pse)
{
	int	iLocation;
	char	*pchData;
	int	i;

	pchData = (char *) pvData;
	FromShort(pchData, pse->s_port); /* No htons - already in network order */
	iLocation = sizeof(short);
	iLocation += CopyString(pchData + iLocation,
				pse->s_proto,
				MAX_HOST_ENT - iLocation);
	iLocation += CopyString(pchData + iLocation,
				pse->s_name,
				MAX_HOST_ENT - iLocation);
	for (i = 0; pse->s_aliases[i]; i++)
	{
		iLocation += CopyString(pchData + iLocation,
					pse->s_aliases[i],
					MAX_HOST_ENT - iLocation);
	}
	pchData[iLocation] = 0;
}

void
CopyProtoEnt(void *pvData, struct protoent *ppe)
{
	int	iLocation;
	char	*pchData;
	int	i;

	pchData = (char *) pvData;
	FromShort(pchData, htons(ppe->p_proto));
	iLocation = sizeof(short);
	iLocation += CopyString(pchData + iLocation,
				ppe->p_name,
				MAX_HOST_ENT - iLocation);
	for (i = 0; ppe->p_aliases[i]; i++)
	{
		iLocation += CopyString(pchData + iLocation,
					ppe->p_aliases[i],
					MAX_HOST_ENT - iLocation);
	}
	pchData[iLocation] = 0;
}

void
SwapSockOptIn(	struct func_arg *pfa,
		int	iOpt)
{
	int	iValue;
	char	*pchData;

	pchData = (char *) pfa->pvData;
	if (iOpt == SO_LINGER)
	{
		FromShort(pchData, ntohs(ToShort(pchData)));
		FromShort(pchData + sizeof(short),
				ntohs(ToShort(pchData + sizeof(short))));
	}
	else
	{
		FromLong(pchData, ntohl(ToLong(pchData)));
	}
}

void
SwapSockOptOut(	struct func_arg *pfa,
		int	iOpt)
{
	int	iValue;
	char	*pchData;

	pchData = (char *) pfa->pvData;
	if (iOpt == SO_LINGER)
	{
		FromShort(pchData, htons(ToShort(pchData)));
		FromShort(pchData + sizeof(short),
				htons(ToShort(pchData + sizeof(short))));
	}
	else
	{
		FromLong(pchData, htonl(ToLong(pchData)));
	}
}

void
ResponseReceived(struct tx_request *ptxr_)
{
	enum Functions ft;
	short	nArgs;
	short	nLen;
	short	id;
	int	iLen;
	int	iValue;
	int	iSocket;
	int	nOptVal;
	short	nError;
	struct	tx_request *ptxr;
	struct	func_arg *pfaArgs;
	struct	func_arg faResult;
	struct	sockaddr_in sin;
	char	*pchData;
	int	i;
	int	iErrorSent;
	int	iOffset;
	struct	hostent *phe;
	struct	netent *pne;
	struct	servent *pse;
	struct	protoent *ppe;

	nLen = ntohs(ptxr_->nLen);
	ptxr = (struct tx_request *) malloc(nLen);
	memcpy(ptxr, ptxr_, nLen);
	ft = (enum Functions) ntohs(ptxr->iType);
	nArgs = ntohs(ptxr->nArgs);

	pfaArgs = (struct func_arg *) malloc(sizeof(struct func_arg) * nArgs);
	pchData = ptxr->pchData;
	for (i = 0; i < nArgs; i++)
	{
		pfaArgs[i].at = (enum arg_type) ntohs(ToShort(pchData));
		pchData += sizeof(short);
		pfaArgs[i].iLen = ntohs(ToShort(pchData));
		pchData += sizeof(short);
		pfaArgs[i].pvData = pchData;
		pchData += pfaArgs[i].iLen;
	}
	faResult.at = (enum arg_type) ntohs(ToShort(pchData));
	pchData += sizeof(short);
	faResult.iLen = ntohs(ToShort(pchData));
	pchData += sizeof(short);
	faResult.pvData = pchData;

	iErrorSent = 0;
	errno = 0;
	h_errno = 0;

	switch(ft)
	{
	case FN_IOCtl:
	case FN_Accept:
	case FN_Select:
	case FN_Data:
		ptxr->nError = htons(WSAEOPNOTSUPP);
		ptxr->nLen = htons(sizeof(short) * 5);
		PacketTransmitData(ptxr, sizeof(short) * 5, -1);
		iErrorSent = 1;
		break;

	case FN_Send:
		SetIntVal(&faResult,
			send(GetIntVal(&pfaArgs[0]),
			     pfaArgs[1].pvData,
			     GetIntVal(&pfaArgs[2]),
			     GetIntVal(&pfaArgs[3])));
		break;

	case FN_SendTo:
		SetIntVal(&faResult,
			sendto(GetIntVal(&pfaArgs[0]),
			       pfaArgs[1].pvData,
			       GetIntVal(&pfaArgs[2]),
			       GetIntVal(&pfaArgs[3]),
			       ConvertSA(&pfaArgs[4], &sin),
			       GetIntVal(&pfaArgs[5])));
		break;

	case FN_Bind:
		SetIntVal(&faResult,
			bind(GetIntVal(&pfaArgs[0]),
			     ConvertSA(&pfaArgs[1], &sin),
			     GetIntVal(&pfaArgs[2])));
		break;

	case FN_Connect:
		iSocket = GetIntVal(&pfaArgs[0]);
		iValue = connect(iSocket,
				ConvertSA(&pfaArgs[1], &sin),
				GetIntVal(&pfaArgs[2]));
		SetIntVal(&faResult, iValue);
		if (iValue != -1)
			BumpLargestFD(iSocket);
		break;

	case FN_Close:
		SetIntVal(&faResult,
			close(GetIntVal(&pfaArgs[0])));
		FlushStream(GetIntVal(&pfaArgs[0]));
		SetClosed(GetIntVal(&pfaArgs[0]));
		break;

	case FN_Shutdown:
		SetIntVal(&faResult,
			shutdown(GetIntVal(&pfaArgs[0]),
				 GetIntVal(&pfaArgs[1])));
		if (GetIntVal(&pfaArgs[1]) != 1)
			SetClosed(GetIntVal(&pfaArgs[0]));
		break;

	case FN_Listen:
		iSocket = GetIntVal(&pfaArgs[0]);
		iValue = listen(iSocket,
				GetIntVal(&pfaArgs[1]));
		SetIntVal(&faResult, iValue);
		if (iValue != -1)
		{
			BumpLargestFD(iSocket);
			SetListener(iSocket);
		}
		break;

	case FN_Socket:
		iSocket = socket(GetIntVal(&pfaArgs[0]),
				GetIntVal(&pfaArgs[1]),
				GetIntVal(&pfaArgs[2]));
		SetIntVal(&faResult,iSocket);
		if (iSocket != -1)
		{
			BumpLargestFD(iSocket);
			nOptVal = 1;
			iLen = sizeof(nOptVal);
			setsockopt(iSocket,
					SOL_SOCKET,
					SO_OOBINLINE,
					(char *) &nOptVal,
					&iLen);
			errno = 0;
		}
		break;

	case FN_GetPeerName:
		iLen = GetIntVal(&pfaArgs[2]);
		iValue = getpeername(GetIntVal(&pfaArgs[0]),
				     (struct sockaddr *) pfaArgs[1].pvData,
				     &iLen);
		if (iValue != -1)
		{
			SetIntVal(&faResult, iValue);
			SetIntVal(&pfaArgs[2], iValue);
			iValue = ((struct sockaddr *) pfaArgs[1].pvData)->
						sa_family;
			iValue = htons((short) iValue);
			((struct sockaddr *) pfaArgs[1].pvData)->sa_family =
						(short) iValue;
		}
		break;

	case FN_GetSockName:
		iLen = GetIntVal(&pfaArgs[2]);
		iValue = getsockname(GetIntVal(&pfaArgs[0]),
				     (struct sockaddr *) pfaArgs[1].pvData,
				     &iLen);
		if (iValue != -1)
		{
			SetIntVal(&faResult, iValue);
			SetIntVal(&pfaArgs[2], iValue);
			iValue = ((struct sockaddr *) pfaArgs[1].pvData)->
						sa_family;
			iValue = htons((short) iValue);
			((struct sockaddr *) pfaArgs[1].pvData)->sa_family =
						(short) iValue;
		}
		break;

	case FN_GetSockOpt:
		iLen = GetIntVal(&pfaArgs[4]);
		iValue = getsockopt(	GetIntVal(&pfaArgs[0]),
					GetIntVal(&pfaArgs[1]),
					GetIntVal(&pfaArgs[2]),
					(char *) pfaArgs[3].pvData,
					&iLen);
		if (iValue != -1)
		{
			SwapSockOptOut(&pfaArgs[3],
					GetIntVal(&pfaArgs[2]));
			SetIntVal(&pfaArgs[4], iLen);
		}
		SetIntVal(&faResult, iValue);
		break;

	case FN_SetSockOpt:
		iLen = GetIntVal(&pfaArgs[4]);
		SwapSockOptIn(&pfaArgs[3],
				GetIntVal(&pfaArgs[2]));
		iValue = setsockopt(	GetIntVal(&pfaArgs[0]),
					SOL_SOCKET,
					GetIntVal(&pfaArgs[2]),
					(char *) pfaArgs[3].pvData,
					&iLen);
		SwapSockOptOut(&pfaArgs[3],
				GetIntVal(&pfaArgs[2]));
		SetIntVal(&faResult, iValue);
		break;

	case FN_GetHostName:
		SetIntVal(&faResult,
			gethostname((char *) pfaArgs[0].pvData,
					GetIntVal(&pfaArgs[1])));
		break;

	case FN_HostByAddr:
		phe = gethostbyaddr((char *) pfaArgs[0].pvData,
				    GetIntVal(&pfaArgs[1]),
				    GetIntVal(&pfaArgs[2]));
		if (phe)
		{
			h_errno = 0;
			CopyHostEnt(faResult.pvData, phe);
		}
		break;

	case FN_HostByName:
		phe = gethostbyname((char *) pfaArgs[0].pvData);
		if (phe)
		{
			h_errno = 0;
			CopyHostEnt(faResult.pvData, phe);
		}
		else if (!h_errno)
		{
			h_errno = TRY_AGAIN;
		}
		break;

	case FN_ServByPort:
		pse = getservbyport(GetIntVal(&pfaArgs[0]),
				    (char *) pfaArgs[1].pvData);
		if (pse)
		{
			h_errno = 0;
			CopyServEnt(faResult.pvData, pse);
		}
		else
		{
			h_errno = NO_DATA;
		}
		break;

	case FN_ServByName:
		pse = getservbyname((char *) pfaArgs[0].pvData,
				    (char *) pfaArgs[1].pvData);
		if (pse)
		{
			h_errno = 0;
			CopyServEnt(faResult.pvData, pse);
		}
		else
		{
			h_errno = NO_DATA;
		}
		break;

	case FN_ProtoByNumber:
		ppe = getprotobynumber(GetIntVal(&pfaArgs[0]));
		if (ppe)
		{
			h_errno = 0;
			CopyProtoEnt(faResult.pvData, ppe);
		}
		else
		{
			h_errno = NO_DATA;
		}
		break;

	case FN_ProtoByName:
		ppe = getprotobyname((char *) pfaArgs[0].pvData);
		if (ppe)
		{
			h_errno = 0;
			CopyProtoEnt(faResult.pvData, ppe);
		}
		else
		{
			h_errno = NO_DATA;
		}
		break;
	}
	if (!iErrorSent)
	{
		if (ft >= FN_HostByAddr || ft <= FN_ProtoByName)
			ptxr->nError = htons(MapHError(h_errno));
		else
			ptxr->nError = htons(MapError(errno));
		PacketTransmitData(ptxr, nLen, -1);
	}
	free(ptxr);
	free(pfaArgs);
}

void
SendSocketData(int iSocket,
		void	*pvData,
		int	iLen,
		struct sockaddr_in *psa,
		int	iAddrLen,
		enum function_type ft)
{
	struct	tx_request *ptxr;
	int	iDataLen;
	struct	sockaddr_in sa;

	iDataLen = sizeof(struct sockaddr_in) + iLen;
	sa = *psa;
	sa.sin_family = htons(sa.sin_family);
	ptxr = (struct tx_request *) malloc(sizeof(short) * 5 + iDataLen);
	ptxr->nLen = htons(iDataLen + sizeof(short) * 5);
	ptxr->id = htons(iSocket);
	ptxr->nArgs = 0;
	ptxr->nError = 0;
	ptxr->iType = htons(ft);
	memcpy(ptxr->pchData, &sa, sizeof(sa));
	memcpy(ptxr->pchData + sizeof(sa), pvData, iLen);
	PacketTransmitData(ptxr, sizeof(short) * 5 + iDataLen, iSocket);
	free(ptxr);
}
