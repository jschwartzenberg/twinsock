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

#include "twinsock.h"
#include <string.h>

/* The following defines the maximum for each type of socket */
#define	MAX_SOCKETS 256
#define	INT_BITS 32
#define	MAX_SOCKINTS (MAX_SOCKETS / INT_BITS)

static	unsigned	long	aiClientSockets[MAX_SOCKINTS];
static	unsigned	long	aiServerSockets[MAX_SOCKINTS];

static	int	iInitialised = 0;

static	void
InitSocks(void)
{
	if (!iInitialised)
	{
		iInitialised = 1;
		memset(aiClientSockets, 0, sizeof(aiClientSockets));
		memset(aiServerSockets, 0, sizeof(aiServerSockets));
	}
}

static int
GetNewSocket(unsigned long *piSockets)
{
	unsigned long iNow;
	int	i, j;

	for (i = 0; i < MAX_SOCKINTS; i++)
	{
		for (j = 0, iNow = 1; j < INT_BITS; j++, iNow <<= 1)
		{
			if (!(piSockets[i] & iNow))
			{
				piSockets[i] |= iNow;
				return i * INT_BITS + j;
			}
		}
	}
	return -1;
}

int
GetClientSocket(void)
{
	int	iSocket;

	InitSocks();
	iSocket = GetNewSocket(aiClientSockets);
	if (iSocket == -1)
		return iSocket;
	else
		return iSocket * 2;
}

int
GetServerSocket(void)
{
	int	iSocket;

	InitSocks();
	iSocket = GetNewSocket(aiServerSockets);
	if (iSocket == -1)
		return iSocket;
	else
		return iSocket * 2 + 1;
}

static void
ReleaseSocket(	unsigned long *piSockets,
		int iSocket)
{
	int	i;
	int	j;

	i = iSocket / INT_BITS;
	j = iSocket % INT_BITS;
	piSockets[i] &= ~(1L << j);
}

void
ReleaseClientSocket(int iSocket)
{
	ReleaseSocket(aiClientSockets, iSocket / 2);
}

void
ReleaseServerSocket(int iSocket)
{
	ReleaseSocket(aiServerSockets, (iSocket - 1) / 2);
}

/* Fortunately all the calls which include sockets in their
 * arguments have them as argument 1. HasSocketArg returns
 * true if the function specified has this argument. If it
 * does, we will need to replace the argument.
 */

int
HasSocketArg(enum Functions fn)
{
	switch(fn)
	{
	case FN_Bind:
	case FN_Close:
	case FN_Connect:
	case FN_IOCtl:
	case FN_GetPeerName:	/* Obsolete */
	case FN_GetSockName:	/* Obsolete */
	case FN_Listen:
	case FN_Select:		/* Obsolete */
	case FN_Send:
	case FN_SendTo:
	case FN_SetSockOpt:
	case FN_Shutdown:
	case FN_Socket:		/* Has the client's idea in it */
		return 1;

	default:
		return 0;
	}
}

