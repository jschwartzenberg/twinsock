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

#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <sys/socket.h>
#include "twinsock.h"
#include "tx.h"
#include "wserror.h"

#ifdef NEED_H_ERRNO
extern int h_errno;
#endif
#ifdef NO_H_ERRNO
#define h_errno errno
#endif

#ifndef AF_UNSPEC
#define	AF_UNSPEC	0
#endif
#ifndef AF_UNIX
#define AF_UNIX		-1
#endif
#ifndef AF_INET
#define AF_INET		-1
#endif
#ifndef AF_IMPLINK
#define AF_IMPLINK      -1
#endif
#ifndef AF_PUP
#define AF_PUP          -1
#endif
#ifndef AF_CHAOS
#define AF_CHAOS        -1
#endif
#ifndef AF_NS
#define AF_NS          -1
#endif
#ifndef AF_ISO
#define AF_ISO          -1
#endif
#ifndef AF_ECMA
#define AF_ECMA         -1
#endif
#ifndef AF_DATAKIT
#define AF_DATAKIT      -1
#endif
#ifndef AF_CCITT
#define AF_CCITT        -1
#endif
#ifndef AF_SNA
#define AF_SNA          -1
#endif
#ifndef AF_DECnet
#define AF_DECnet       -1
#endif
#ifndef AF_DLI
#define AF_DLI          -1
#endif
#ifndef AF_LAT
#define AF_LAT          -1
#endif
#ifndef AF_HYLINK
#define AF_HYLINK       -1
#endif
#ifndef AF_APPLETALK
#define AF_APPLETALK    -1
#endif
#ifndef AF_NETBIOS
#define AF_NETBIOS    -1
#endif

#define	rangeof(x)	(sizeof(x) / sizeof(x[0]))

static int HostSocketFamily[] =
{
	AF_UNSPEC,	                /* unspecified */
	AF_UNIX,	                /* local to host (pipes, portals) */
	AF_INET,	                /* internetwork: UDP, TCP, etc. */
	AF_IMPLINK,	                /* arpanet imp addresses */
	AF_PUP,	 	                /* pup protocols: e.g. BSP */
	AF_CHAOS,	                /* mit CHAOS protocols */
	AF_NS,	 	                /* NS, IPX and SPX */
	AF_ISO,	 	                /* ISO protocols */
	AF_ECMA,	                /* european computer manufacturers */
	AF_DATAKIT,	                /* datakit protocols */
	AF_CCITT,	                /* CCITT protocols, X.25 etc */
	AF_SNA,	                        /* IBM SNA */
	AF_DECnet,	                /* DECnet */
	AF_DLI,	                        /* Direct data link interface */
	AF_LAT,	                        /* LAT */
	AF_HYLINK,	                /* NSC Hyperchannel */
	AF_APPLETALK,	                /* AppleTalk */
	AF_NETBIOS	                /* NetBios-style addresses */
};

static int HostSocketType[] =
{
	0,
	SOCK_STREAM,
	SOCK_DGRAM,
	SOCK_RAW,
        SOCK_RDM,
	SOCK_SEQPACKET
};

#ifndef IPPROTO_IP
#define IPPROTO_IP 0
#endif
#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP 1
#endif
#ifndef IPPROTO_GGP
#define IPPROTO_GGP 2
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_PUP
#define IPPROTO_PUP 12
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 18
#endif
#ifndef IPPROTO_ND
#define IPPROTO_ND 77
#endif

static int HostSocketProtocol[] =
{
	IPPROTO_IP, IPPROTO_ICMP, IPPROTO_GGP, 3, 4,
	5, IPPROTO_TCP, 7, 8, 9,
	10, 11, IPPROTO_PUP, 13, 14, 15, 16, IPPROTO_UDP, 18, 19,
	20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
	30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
	40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
	50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
	60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
	70, 71, 72, 73, 74, 75, 76, IPPROTO_ND, 78, 79
};


#ifndef SO_ACCEPTCONN
#define	SO_ACCEPTCONN 0
#endif
#ifndef SO_USELOOPBACK
#define	SO_USELOOPBACK 0
#endif

static struct
{
 int iWindows;
 int iHost;
} HostSocketOption[] =
{
	{ 0x0001,	SO_DEBUG	},
	{ 0x0002,	SO_ACCEPTCONN	},
	{ 0x0004,	SO_REUSEADDR	},
	{ 0x0008,	SO_KEEPALIVE	},
	{ 0x0010,	SO_DONTROUTE	},
	{ 0x0020,	SO_BROADCAST	},
	{ 0x0040,	SO_USELOOPBACK	},
	{ 0x0080,	SO_LINGER	},
	{ 0x0100,	SO_OOBINLINE	}
};

static int
GetFamily(int x)
{
	return (x < rangeof(HostSocketFamily)) ? HostSocketFamily[x] : x;
}

static int
RevFamily(int x)
{
	int i;

	for (i = 0; i < rangeof(HostSocketFamily); i++)
	{
		if (HostSocketFamily[i] == x)
			return i;
	}
	return x;
}

static int
GetType(int x)
{
	return (x < rangeof(HostSocketType)) ? HostSocketType[x] : x;
}

static int
GetProtocol(int x)
{
	return (x < rangeof(HostSocketProtocol)) ? HostSocketProtocol[x] : x;
}

static	int
GetOption(int x)
{
	int	iOption = 0;
	int	i;

	for (i = 0; i < rangeof(HostSocketOption); i++)
	{
		if (x & HostSocketOption[i].iWindows)
			iOption |= HostSocketOption[i].iHost;
	}
	return iOption;
}

extern void PacketTransmitData (void *pvData, int iDataLen, int iStream);

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

static unsigned short
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
FromShort(char *pchData, unsigned short n)
{
	memcpy(pchData, &n, sizeof(short));
}

static void
FromLong(char *pchData, long n)
{
	memcpy(pchData, &n, sizeof(long));
}

static long
GetIntVal(struct func_arg *pfa)
{
	switch(pfa->at)
	{
	case AT_Int16:
	case AT_Int16Ptr:
		return ntohs(ToShort(pfa->pvData));

	case AT_Int32:
	case AT_Int32Ptr:
		return ntohl(ToLong(pfa->pvData));
	}
}

void
SetIntVal(struct func_arg *pfa, long iVal)
{
	switch(pfa->at)
	{
	case AT_Int16:
	case AT_Int16Ptr:
		FromShort(pfa->pvData,htons((short) iVal));
		return;

	case AT_Int32:
	case AT_Int32Ptr:
		FromLong(pfa->pvData, htonl((short) iVal));
		return;
	}
};

struct sockaddr *
ConvertSA(struct func_arg *pfa, struct sockaddr_in *sin)
{
	u_short tmp;

	memcpy(sin, pfa->pvData, sizeof(*sin));
	tmp = *(u_short *) sin;
	*(u_short *) sin = 0;
	sin->sin_family = GetFamily(ntohs(tmp));
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


int
CompressArg(	struct	tx_request *ptxr,
		struct	func_arg *pfaArgs,
		int	nArgs,
		struct	func_arg *pfaResult,
		int	iArg)
{
	struct	func_arg *pfaNow;
	char	*pchNewData, *pchData;
	int	i, j;
	int	nLen;

	pchData = pchNewData = ptxr->pchData;
	for (i = 0; i <= nArgs; i++)
	{
		if (i < nArgs)
			pfaNow = pfaArgs + i;
		else
			pfaNow = pfaResult;
		for (j = 0; j < pfaNow->iLen + 4; j++)
			pchNewData[j] = pchData[j];
		pchData += pfaNow->iLen + 4;
		if (i == iArg)
		{
			pfaNow->iLen = 0;
			*(short *) (pchNewData + 2) = 0;
		}
		pchNewData += pfaNow->iLen + 4;
	}
	nLen = pchNewData - ptxr->pchData + 10;
	ptxr->nLen = htons((short) nLen);
	return nLen;
}


void
GetPortRange(	short	iInPort,
		short	*piStartPort,
		short	*piEndPort)
{
	char	achBuffer[1024];
	char	achPort[20];
	char	*pchComma;

	sprintf(achPort, "%d", (int) iInPort);
	GetTwinSockSetting("Mappings",
			achPort,
			"",
			achBuffer,
			sizeof(achBuffer));
	if (!*achBuffer || !(pchComma = (char *) strchr(achBuffer, ',')))
	{
		*piStartPort = *piEndPort = iInPort;
		return;
	}
	*pchComma++ = 0;
	*piStartPort = atoi(achBuffer);
	*piEndPort = atoi(pchComma);
}

void
SendRemapMessage(	short	iFrom,
			short	iTo)
{
	char	achBuffer[1024];
	char	achPortDesc[50];
	char	achPort[20];
	struct	tx_request *ptxr;

	sprintf(achPortDesc, "Port %d", (int) iFrom);
	sprintf(achPort, "%d", (int) iFrom);
	GetTwinSockSetting("PortNames",
			achPort,
			achPortDesc,
			achBuffer,
			sizeof(achBuffer));
	strcat(achBuffer, " remapped to ");
	sprintf(achPortDesc, "Port %d", (int) iTo);
	sprintf(achPort, "%d", (int) iTo);
	GetTwinSockSetting("PortNames",
			achPort,
			achPortDesc,
			achBuffer + strlen(achBuffer),
			sizeof(achBuffer) - strlen(achBuffer));
	ptxr = (struct tx_request *) malloc(11 + strlen(achBuffer));
        ptxr->iType = htons(FN_Message);
        ptxr->nArgs = 0;
        ptxr->nLen = htons(11 + strlen(achBuffer));
        ptxr->id = -1;
        ptxr->nError = 0;
	strcpy(ptxr->pchData, achBuffer);
        PacketTransmitData(ptxr, 10 + strlen(achBuffer) + 1, -2);
	free(ptxr);
}

void
ResponseReceived(struct tx_request *ptxr_)
{
	enum Functions ft;
	short	nArgs;
	short	nLen;
	short	id;
	short	iInPort;
	short	iStartPort;
	short	iEndPort;
	short	iPort;
	int	iLen;
	int	iValue;
	int	iSocket;
	int	nOptName;
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
		PacketTransmitData(ptxr, sizeof(short) * 5, -2);
		iErrorSent = 1;
		break;

	case FN_Send:
		SetIntVal(&faResult,
			send(GetIntVal(&pfaArgs[0]),
			     pfaArgs[1].pvData,
			     GetIntVal(&pfaArgs[2]),
			     GetIntVal(&pfaArgs[3])));
		nLen = CompressArg(ptxr, pfaArgs, nArgs, &faResult, 1);
		break;

	case FN_SendTo:
		SetIntVal(&faResult,
			sendto(GetIntVal(&pfaArgs[0]),
			       pfaArgs[1].pvData,
			       GetIntVal(&pfaArgs[2]),
			       GetIntVal(&pfaArgs[3]),
			       ConvertSA(&pfaArgs[4], &sin),
			       GetIntVal(&pfaArgs[5])));
		nLen = CompressArg(ptxr, pfaArgs, nArgs, &faResult, 1);
		break;

	case FN_Bind:
		ConvertSA(&pfaArgs[1], &sin);
		iInPort = ntohs(sin.sin_port);
		GetPortRange(iInPort, &iStartPort, &iEndPort);
		for (iPort = iStartPort; iPort <= iEndPort; iPort++)
		{
			errno = 0;
			sin.sin_port = htons(iPort);
			iValue = bind(GetIntVal(&pfaArgs[0]),
					(struct sockaddr *) &sin,
			     		GetIntVal(&pfaArgs[2]));
			if (!iValue)
			{
				if (iPort != iInPort)
					SendRemapMessage(iInPort, iPort);
				break;
			}
		}
		SetIntVal(&faResult, iValue);
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
		iSocket = socket(GetFamily(GetIntVal(&pfaArgs[0])),
				GetType(GetIntVal(&pfaArgs[1])),
				GetProtocol(GetIntVal(&pfaArgs[2])));
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
					iLen);
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
			iValue = RevFamily(iValue);
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
			iValue = RevFamily(iValue);
			iValue = htons((short) iValue);
			((struct sockaddr *) pfaArgs[1].pvData)->sa_family =
						(short) iValue;
		}
		break;

	case FN_GetSockOpt:
		iLen = GetIntVal(&pfaArgs[4]);
		nOptName = GetOption(GetIntVal(&pfaArgs[2]));
		iValue = getsockopt(	GetIntVal(&pfaArgs[0]),
					GetIntVal(&pfaArgs[1]),
					nOptName,
					(char *) pfaArgs[3].pvData,
					&iLen);
		if (iValue != -1)
		{
			SwapSockOptOut(&pfaArgs[3],
					nOptName);
			SetIntVal(&pfaArgs[4], iLen);
		}
		SetIntVal(&faResult, iValue);
		break;

	case FN_SetSockOpt:
		iLen = GetIntVal(&pfaArgs[4]);
		nOptName = GetOption(GetIntVal(&pfaArgs[2]));
		SwapSockOptIn(&pfaArgs[3],
				nOptName);
		iValue = setsockopt(	GetIntVal(&pfaArgs[0]),
					SOL_SOCKET,
					nOptName,
					(char *) pfaArgs[3].pvData,
					iLen);
		SwapSockOptOut(&pfaArgs[3],
				nOptName);
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
		if (pfaArgs[0].at == AT_Int16)
			iValue = *(short *) pfaArgs[0].pvData;
		else
			iValue = *(long *) pfaArgs[0].pvData;
		pse = getservbyport(iValue,
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
		if (ft >= FN_HostByAddr && ft <= FN_ProtoByName)
			ptxr->nError = htons(MapHError(h_errno));
		else
			ptxr->nError = htons(MapError(errno));
		PacketTransmitData(ptxr, nLen, -2);
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
		enum Functions ft)
{
	struct	tx_request *ptxr;
	int	iDataLen;
	struct	sockaddr_in sa;

	iDataLen = sizeof(struct sockaddr_in) + iLen;
	sa = *psa;
	sa.sin_family = htons(RevFamily(sa.sin_family));
	ptxr = (struct tx_request *) malloc(sizeof(short) * 5 + iDataLen);
	ptxr->nLen = htons(iDataLen + sizeof(short) * 5);
	ptxr->id = htons(iSocket);
	ptxr->nArgs = 0;
	ptxr->nError = 0;
	ptxr->iType = htons(ft);
	memcpy(ptxr->pchData, &sa, sizeof(sa));
	memcpy(ptxr->pchData + sizeof(sa), pvData, iLen);
	PacketTransmitData(ptxr, sizeof(short) * 5 + iDataLen,
		(ft == FN_Data) ? iSocket : -2);
	free(ptxr);
}
