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
#ifdef __MSDOS__
#include <winsock.h>
#include <stdlib.h>
#else
#include <sys/types.h>
#include <netinet/in.h>
#endif
#include "packet.h"
#include "twinsock.h"

#define	MAX_STREAMS	256
#define	WINDOW_SIZE	4

short	nInSeq = 0;
short	nOutSeq = 0;

long	nCRCErrors = 0;
long	nRetransmits = 0;
long	nTimeouts = 0;
long	nInsane = 0;
long	nIncomplete = 0;

enum Encoding eLine = E_6Bit;

extern	int	SendData(void	*pvData, int nBytes);

static unsigned long crc_32_tab[] = { /* CRC polynomial 0xedb88320 */
0x00000000l, 0x77073096l, 0xee0e612cl, 0x990951bal, 0x076dc419l, 0x706af48fl, 0xe963a535l, 0x9e6495a3l,
0x0edb8832l, 0x79dcb8a4l, 0xe0d5e91el, 0x97d2d988l, 0x09b64c2bl, 0x7eb17cbdl, 0xe7b82d07l, 0x90bf1d91l,
0x1db71064l, 0x6ab020f2l, 0xf3b97148l, 0x84be41del, 0x1adad47dl, 0x6ddde4ebl, 0xf4d4b551l, 0x83d385c7l,
0x136c9856l, 0x646ba8c0l, 0xfd62f97al, 0x8a65c9ecl, 0x14015c4fl, 0x63066cd9l, 0xfa0f3d63l, 0x8d080df5l,
0x3b6e20c8l, 0x4c69105el, 0xd56041e4l, 0xa2677172l, 0x3c03e4d1l, 0x4b04d447l, 0xd20d85fdl, 0xa50ab56bl,
0x35b5a8fal, 0x42b2986cl, 0xdbbbc9d6l, 0xacbcf940l, 0x32d86ce3l, 0x45df5c75l, 0xdcd60dcfl, 0xabd13d59l,
0x26d930acl, 0x51de003al, 0xc8d75180l, 0xbfd06116l, 0x21b4f4b5l, 0x56b3c423l, 0xcfba9599l, 0xb8bda50fl,
0x2802b89el, 0x5f058808l, 0xc60cd9b2l, 0xb10be924l, 0x2f6f7c87l, 0x58684c11l, 0xc1611dabl, 0xb6662d3dl,
0x76dc4190l, 0x01db7106l, 0x98d220bcl, 0xefd5102al, 0x71b18589l, 0x06b6b51fl, 0x9fbfe4a5l, 0xe8b8d433l,
0x7807c9a2l, 0x0f00f934l, 0x9609a88el, 0xe10e9818l, 0x7f6a0dbbl, 0x086d3d2dl, 0x91646c97l, 0xe6635c01l,
0x6b6b51f4l, 0x1c6c6162l, 0x856530d8l, 0xf262004el, 0x6c0695edl, 0x1b01a57bl, 0x8208f4c1l, 0xf50fc457l,
0x65b0d9c6l, 0x12b7e950l, 0x8bbeb8eal, 0xfcb9887cl, 0x62dd1ddfl, 0x15da2d49l, 0x8cd37cf3l, 0xfbd44c65l,
0x4db26158l, 0x3ab551cel, 0xa3bc0074l, 0xd4bb30e2l, 0x4adfa541l, 0x3dd895d7l, 0xa4d1c46dl, 0xd3d6f4fbl,
0x4369e96al, 0x346ed9fcl, 0xad678846l, 0xda60b8d0l, 0x44042d73l, 0x33031de5l, 0xaa0a4c5fl, 0xdd0d7cc9l,
0x5005713cl, 0x270241aal, 0xbe0b1010l, 0xc90c2086l, 0x5768b525l, 0x206f85b3l, 0xb966d409l, 0xce61e49fl,
0x5edef90el, 0x29d9c998l, 0xb0d09822l, 0xc7d7a8b4l, 0x59b33d17l, 0x2eb40d81l, 0xb7bd5c3bl, 0xc0ba6cadl,
0xedb88320l, 0x9abfb3b6l, 0x03b6e20cl, 0x74b1d29al, 0xead54739l, 0x9dd277afl, 0x04db2615l, 0x73dc1683l,
0xe3630b12l, 0x94643b84l, 0x0d6d6a3el, 0x7a6a5aa8l, 0xe40ecf0bl, 0x9309ff9dl, 0x0a00ae27l, 0x7d079eb1l,
0xf00f9344l, 0x8708a3d2l, 0x1e01f268l, 0x6906c2fel, 0xf762575dl, 0x806567cbl, 0x196c3671l, 0x6e6b06e7l,
0xfed41b76l, 0x89d32be0l, 0x10da7a5al, 0x67dd4accl, 0xf9b9df6fl, 0x8ebeeff9l, 0x17b7be43l, 0x60b08ed5l,
0xd6d6a3e8l, 0xa1d1937el, 0x38d8c2c4l, 0x4fdff252l, 0xd1bb67f1l, 0xa6bc5767l, 0x3fb506ddl, 0x48b2364bl,
0xd80d2bdal, 0xaf0a1b4cl, 0x36034af6l, 0x41047a60l, 0xdf60efc3l, 0xa867df55l, 0x316e8eefl, 0x4669be79l,
0xcb61b38cl, 0xbc66831al, 0x256fd2a0l, 0x5268e236l, 0xcc0c7795l, 0xbb0b4703l, 0x220216b9l, 0x5505262fl,
0xc5ba3bbel, 0xb2bd0b28l, 0x2bb45a92l, 0x5cb36a04l, 0xc2d7ffa7l, 0xb5d0cf31l, 0x2cd99e8bl, 0x5bdeae1dl,
0x9b64c2b0l, 0xec63f226l, 0x756aa39cl, 0x026d930al, 0x9c0906a9l, 0xeb0e363fl, 0x72076785l, 0x05005713l,
0x95bf4a82l, 0xe2b87a14l, 0x7bb12bael, 0x0cb61b38l, 0x92d28e9bl, 0xe5d5be0dl, 0x7cdcefb7l, 0x0bdbdf21l,
0x86d3d2d4l, 0xf1d4e242l, 0x68ddb3f8l, 0x1fda836el, 0x81be16cdl, 0xf6b9265bl, 0x6fb077e1l, 0x18b74777l,
0x88085ae6l, 0xff0f6a70l, 0x66063bcal, 0x11010b5cl, 0x8f659effl, 0xf862ae69l, 0x616bffd3l, 0x166ccf45l,
0xa00ae278l, 0xd70dd2eel, 0x4e048354l, 0x3903b3c2l, 0xa7672661l, 0xd06016f7l, 0x4969474dl, 0x3e6e77dbl,
0xaed16a4al, 0xd9d65adcl, 0x40df0b66l, 0x37d83bf0l, 0xa9bcae53l, 0xdebb9ec5l, 0x47b2cf7fl, 0x30b5ffe9l,
0xbdbdf21cl, 0xcabac28al, 0x53b39330l, 0x24b4a3a6l, 0xbad03605l, 0xcdd70693l, 0x54de5729l, 0x23d967bfl,
0xb3667a2el, 0xc4614ab8l, 0x5d681b02l, 0x2a6f2b94l, 0xb40bbe37l, 0xc30c8ea1l, 0x5a05df1bl, 0x2d02ef8dl
};

#define UPDC32(octet, crc) (crc_32_tab[((crc) ^ (octet)) & 0xff] ^ ((crc) >> 8))

static	short
CalcCRC(data, size)
char *data;
int size;
{
	unsigned long crc = 0xffff;

	while (size--)
	{
		crc = UPDC32(*data++, crc);
	}
	crc = ~crc;
	return (crc & 0xffff);
}

struct	packet_queue
{
	int			idPacket;
	int			iPacketLen;
	int			iStream;
	int			iFlags;
	struct	packet		*pkt;
	struct	packet_queue	*ppqNext;
};

#define	PQF_LEADER	0x0001
#define	PQF_TRAILER	0x0002

static	struct	packet_queue *ppqList = 0;
static	int	iInitialised = 0;
static	struct	packet_queue *appqStreams[MAX_STREAMS];
static	struct	packet_queue *ppqSent = 0;
static	int	nSent = 0;
static	struct	packet_queue *ppqReceived = 0;
static	char	aiStreams[MAX_STREAMS / 8];

#define	STREAM_BIT(x)	(1 << (((x) + 1) % 8))
#define	STREAM_BYTE(x)	aiStreams[((x) + 1) / 8]

#define	SET_STREAM(x)	(STREAM_BYTE(x) |= STREAM_BIT(x))
#define	CLR_STREAM(x)	(STREAM_BYTE(x) &= ~STREAM_BIT(x))
#define	GET_STREAM(x)	((STREAM_BYTE(x) & STREAM_BIT(x)) != 0)

static	char ach6bit[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789./";

void	PoppedPacket(struct packet_queue *ppqPopped);
void	PutInQueue(struct packet_queue *ppq, int iStream);

void	InsertInQueue(struct packet_queue **pppqNext, struct packet_queue *ppq)
{
	for (; *pppqNext; pppqNext = &((*pppqNext)->ppqNext));
	*pppqNext = ppq;
}

void	FlushQueue(struct packet_queue **pppq)
{
	struct	packet_queue *ppqPop;

	while ((ppqPop = *pppq) != 0)
	{
		*pppq = ppqPop->ppqNext;
		free(ppqPop->pkt);
		free(ppqPop);
	}
}


int	TransmitData(void *pvData, int iDataLen)
{
	char	*pchDataIn;
	char	*pchDataOut;
	char	c;
	int	iIn, iOut;
	int	nBits;
	int	nBitsLeft;
	int	nBitsNow;
	int	nDataOut;
	char	cNow, cTmp;

	pchDataIn = (char *) pvData;
	pchDataOut = (char *) malloc(iDataLen * 2 + 1); /* Worst case */
	nBits = nBitsLeft = 0;
	cNow = 0;
	switch(eLine)
	{
	case E_6Bit:
		nDataOut = iDataLen * 4 / 3 + 1;
		if (iDataLen % 6)
			nDataOut++;
		pchDataOut[0] = '@';	/* Signals the receiving end to realign to bit 0 */
		for (iIn = 0, iOut = 1; iOut < nDataOut;)
		{
			if (nBitsLeft)
			{
				cTmp = c & ((1 << nBitsLeft) - 1);
				nBitsNow = 6 - nBits;
				if (nBitsLeft < nBitsNow)
					nBitsNow = nBitsLeft;
				cNow <<= nBitsNow;
				cTmp >>= nBitsLeft - nBitsNow;
				cTmp &= ((1 << nBitsNow) - 1);
				cNow |= cTmp;
				nBits += nBitsNow;
				nBitsLeft -= nBitsNow;
				if (nBits == 6)
				{
					pchDataOut[iOut++] = ach6bit[cNow];
					cNow = 0;
					nBits = 0;
				}
			}
			else
			{
				if (iIn < iDataLen)
					c = pchDataIn[iIn++];
				else
					c = 0;
				nBitsLeft = 8;
			}
		}
		break;

	case E_8Bit:
	case E_8NoX:
	case E_8NoCtrl:
		for (iIn = 0, iOut = 0; iIn < iDataLen; iIn++)
		{
			c = pchDataIn[iIn];
			if (c == '@')
			{
				strcpy(pchDataOut + iOut, "@ ");
				iOut += 2;
			}
			else if (c == '\030')
			{
				strcpy(pchDataOut + iOut, "@X");
				iOut += 2;
			}
			else if (eLine == E_8NoX && c == '\023')
			{
				strcpy(pchDataOut + iOut, "@S");
				iOut += 2;
			}
			else if (eLine == E_8NoX && c == '\021')
			{
				strcpy(pchDataOut + iOut, "@Q");
				iOut += 2;
			}
			else if (eLine == E_8NoCtrl &&
				 c >= 0 && c < '\040')
			{
				pchDataOut[iOut++] = '@';
				pchDataOut[iOut++] = c + '@';
			}
			else
			{
				pchDataOut[iOut++] = c;
			}
		}
		nDataOut = iOut;
		break;
	}
	nDataOut =  SendData(pchDataOut, nDataOut);
	free(pchDataOut);
	return nDataOut;
}

static	void
TransmitPacket(struct packet_queue *ppqList)
{
	TransmitData(ppqList->pkt, ppqList->iPacketLen);
	SetTransmitTimeout();
}

void
InsertIntoIncoming(struct packet *pkt, int iLen)
{
	struct packet_queue *ppq;
	struct packet_queue **pppq;
	struct packet *pktNew;
	int	iPacketID;

	iPacketID = ntohs(pkt->iPacketID);

	for (	pppq = &ppqReceived;
		*pppq && (*pppq)->idPacket < iPacketID;
		pppq = &(*pppq)->ppqNext);
	if ((*pppq) && (*pppq)->idPacket == iPacketID)
		return;
	pktNew = (struct packet *) malloc(sizeof(struct packet));
	*pktNew = *pkt;

	ppq = (struct packet_queue *) malloc(sizeof(struct packet_queue));
	ppq->ppqNext = *pppq;
	ppq->idPacket = iPacketID;
	ppq->pkt = pktNew;
	ppq->iPacketLen = iLen;
	*pppq = ppq;

	while (ppqReceived && ppqReceived->idPacket == nInSeq)
	{
		nInSeq++;
		DataReceived(ppqReceived->pkt->achData,
			     ppqReceived->iPacketLen - sizeof(short) * 4);
		ppq = ppqReceived;
		ppqReceived = ppq->ppqNext;
		free(ppq->pkt);
		free(ppq);
	}
}



void	TimeoutReceived(void)
{
	struct	packet_queue *ppq;

	nTimeouts++;
	for (ppq = ppqSent; ppq; ppq = ppq->ppqNext)
	{
		nRetransmits++;
		TransmitPacket(ppq);
	}
}

void	InitHead()
{
	struct packet *pkt;
	short	nCRC;

	ppqList->idPacket = nOutSeq;
	pkt = ppqList->pkt;
	pkt->iPacketID = htons(nOutSeq);
	nCRC = CalcCRC(pkt, ppqList->iPacketLen);
	pkt->nCRC = htons(nCRC);
	nOutSeq++;
}

void	TransmitHead(void)
{
	struct packet_queue *ppq;

	if (nSent < WINDOW_SIZE)
	{
		nSent++;
		ppq = ppqList;
		ppqList = ppq->ppqNext;
		ppq->ppqNext = 0;
		InsertInQueue(&ppqSent, ppq);
		PoppedPacket(ppq);
		TransmitPacket(ppq);
	}
}

static	void
AckReceived(int id)
{
	struct packet_queue **pppq, *ppq, *ppqTemp;

	for (pppq = &ppqSent;
	     *pppq && (*pppq)->idPacket != id;
	     pppq = &(*pppq)->ppqNext);
	ppq = *pppq;
	if (ppq)
	{
		while (ppqSent != ppq)
		{
			ppqTemp = ppqSent;
			ppqSent = ppqTemp->ppqNext;
			ppqTemp->ppqNext = 0;
			InsertInQueue(&ppqSent, ppqTemp);
			TransmitPacket(ppqTemp);
			nRetransmits++;
		}
		ppqTemp = ppqSent;
		ppqSent = ppqTemp->ppqNext;
		free(ppqTemp->pkt);
		free(ppqTemp);
		nSent--;
		if (ppqList)
			TransmitHead();
		if (!ppqSent)
			KillTransmitTimeout();
	}
}


void	FlushStream(int	iStream)
{
	FlushQueue(appqStreams + iStream + 1);
}

void	PutInQueue(struct packet_queue *ppq, int iStream)
{
	if (iStream == -2) /* Urgent */
	{
		InsertInQueue(&ppqList, ppq);
	}
	else if (GET_STREAM(iStream) && 
		 (appqStreams[iStream + 1] ||
		  (ppq->iFlags & PQF_LEADER)))
	{
		InsertInQueue(appqStreams + iStream + 1, ppq);
	}
	else
	{
		SET_STREAM(iStream);
		InsertInQueue(&ppqList, ppq);
	}
	if (ppqList == ppq)
	{
		InitHead();
		TransmitHead();
	}
}

void	PoppedPacket(struct packet_queue *ppqPopped)
{
	int	iStream, iFlags;
	struct	packet_queue *ppqPop;

	iStream = ppqPopped->iStream;
	iFlags = ppqPopped->iFlags;

	if (iStream != -2 && (iFlags & PQF_TRAILER))
	{
		if (appqStreams[iStream + 1])
		{
			do
			{
				ppqPop = appqStreams[iStream + 1];
				appqStreams[iStream + 1] = ppqPop->ppqNext;
				ppqPop->ppqNext = 0;
				InsertInQueue(&ppqList, ppqPop);
			} while (!(ppqPop->iFlags & PQF_TRAILER));
		}
		else
		{
			CLR_STREAM(iStream);
		}
	}

	if (ppqList)
	{
		InitHead();
		TransmitHead();
	}
}

void	SendPacket(void	*pvData, int iDataLen, int iStream, int iFlags)
{
	struct	packet	*pkt;
	struct	packet_queue *ppq;
	short	nCRC;

	if (!iInitialised)
	{
		iInitialised = 1;
		memset(aiStreams, 0, sizeof(aiStreams));
		memset(appqStreams, 0, sizeof(appqStreams));
	}

	pkt = (struct packet *) malloc(sizeof(struct packet));
	ppq = (struct packet_queue *) malloc(sizeof(struct packet_queue));

	ppq->iPacketLen = iDataLen + sizeof(short) * 4;
	ppq->iStream = iStream;
	ppq->pkt = pkt;
	ppq->ppqNext = 0;
	ppq->iFlags = iFlags;

	pkt->iPacketLen = htons(iDataLen);
	pkt->nCRC = 0;
	pkt->nType = htons((short) PT_Data);
	memcpy(pkt->achData, pvData, iDataLen);

	PutInQueue(ppq, iStream);
}

void	TransmitAck(short id)
{
	struct packet pkt;
	int	nCRC;

	pkt.iPacketLen = 0;
	pkt.nCRC = 0;
	pkt.nType = htons((short) PT_Ack);
	pkt.iPacketID = htons(id);
	nCRC = CalcCRC(&pkt, sizeof(short) * 4);
	pkt.nCRC = htons(nCRC);
	TransmitData(&pkt, sizeof(short) * 4);
}

void	ProcessData(void *pvData, int nDataLen)
{
	static	struct	packet	pkt;
	static	int	nBytes = 0;
	int		nToCopy;
	int		nData;
	enum packet_type pt;
	short		iLen;
	short		nCRC;
	short		id;
	short		iLocation;

	if (!pvData)	/* Receive timeout */
	{
		nIncomplete++;
		nBytes = 0;
		return;
	}

	while (nDataLen)
	{
		if (nBytes < sizeof(short) * 4)
		{
			nToCopy = sizeof(short) * 4 - nBytes;
			if (nToCopy > nDataLen)
				nToCopy = nDataLen;
			memcpy((char *) &pkt + nBytes, pvData, nToCopy);
			pvData = (char *) pvData + nToCopy;
			nDataLen -= nToCopy;
			nBytes += nToCopy;
		}
		if (nBytes < sizeof(short) * 4)
			break;
		pt = (enum packet_type) ntohs(pkt.nType);
		iLen = ntohs(pkt.iPacketLen);
		nCRC = ntohs(pkt.nCRC);
		id = ntohs(pkt.iPacketID);
		if (iLen > PACKET_MAX || iLen < 0) /* Sanity check */
		{
			nInsane++;
			nBytes = 0;
			nDataLen = 0;
			KillReceiveTimeout();
			if (ppqList)
				KillTransmitTimeout();
			FlushInput();
			if (ppqList)
				SetTransmitTimeout();
			return;
		}
		if (nBytes < sizeof(short) * 4 + iLen)
		{
			nToCopy = sizeof(short) * 4 + iLen - nBytes;
			if (nDataLen < nToCopy)
				nToCopy = nDataLen;
			memcpy((char *) &pkt + nBytes, pvData, nToCopy);
			pvData = (char *) pvData + nToCopy;
			nDataLen -= nToCopy;
			nBytes += nToCopy;
		}
		if (nBytes == sizeof(short) * 4 + iLen)
		{
			nBytes = 0;
			pkt.nCRC = 0;
			if (CalcCRC(&pkt, iLen + sizeof(short) * 4) == nCRC)
			{
				switch(pt)
				{
				case PT_Data:
					iLocation = nInSeq - id;
					TransmitAck(id);
					if (iLocation <= 0)
						InsertIntoIncoming(&pkt, iLen + sizeof(short) * 4);
					break;

				case PT_Nak:
					if (ppqList &&
					    id == ppqList->idPacket)
						TransmitHead();
					break;

				case PT_Ack:
					AckReceived(id);
					break;

				case PT_Shutdown:
					Shutdown(0);
					break;
				}
			}
			else
			{
				/* If we flush input we should also
				 * reset any tranmit timeout, otherwise
				 * we may resend the packet while flushing,
				 * and flush the Ack. We also kill any
				 * receive timeout because we have already
				 * "flushed" any existing input.
				 */
				nCRCErrors++;
				KillReceiveTimeout();
				if (ppqList)
					KillTransmitTimeout();
				FlushInput();
				if (ppqList)
					SetTransmitTimeout();
				return;
			}
		}
	}
	if (nBytes)
		SetReceiveTimeout();
}

void	PacketReceiveData(void *pvData, int nDataLen)
{
	static	int	nBits = 0;
	static	char	c = 0;
	static	int	nCtlX = 0;
	char	cIn;
	char	cTmp;
	int	nBitsLeft = 0;
	int	iOut = 0;
	int	nBitsNow;
	char	*pchDataOut;
	char	*pchDataIn;

	if (!pvData)
	{
		ProcessData(0, nDataLen);
		nBits = nBitsLeft = 0;
		return;
	}

	KillReceiveTimeout();

	pchDataIn = (char *) pvData;
	pchDataOut = (char *) malloc(nDataLen);
	switch(eLine)
	{
	case E_6Bit:
		while (nDataLen || nBitsLeft)
		{
			if (nBitsLeft)
			{
				nBitsNow = 8 - nBits;
				if (nBitsLeft < nBitsNow)
					nBitsNow = nBitsLeft;
				c <<= nBitsNow;
				cTmp = cIn >> (nBitsLeft - nBitsNow);
				cTmp &= ((1 << nBitsNow) - 1);
				c |= cTmp;
				nBits += nBitsNow;
				nBitsLeft -= nBitsNow;
				if (nBits == 8)
				{
					pchDataOut[iOut++] = c;
					nBits = 0;
				}
			}
			else
			{
				cIn = *pchDataIn++;
				nDataLen--;
				if (cIn == '\030') /* ^X */
				{
					nCtlX++;
					if (nCtlX >= 5)
						Shutdown();
					continue;
				}
				else
				{
					nCtlX = 0;
				}
				if (cIn == '@')
				{
					cIn = c = 0;
					nBitsLeft = 0;
					nBits = 0;
				}
				else
				{
					if (cIn >= 'A' && cIn <= 'Z')
						cIn -= 'A';
					else if (cIn >= 'a' && cIn <= 'z')
						cIn = cIn - 'a' + 26;
					else if (cIn >= '0' && cIn <= '9')
						cIn = cIn - '0' + 52;
					else if (cIn == '.')
						cIn = 62;
					else if (cIn == '/')
						cIn = 63;
					else
						continue;
					nBitsLeft = 6;
				}
			}
		}
		break;

	case E_8Bit:
	case E_8NoX:
	case E_8NoCtrl:
		while (nDataLen--)
		{
			cIn = *pchDataIn;
			if (cIn == '\030') /* ^X */
			{
				nCtlX++;
				if (nCtlX >= 5)
					Shutdown();
				continue;
			}
			else
			{
				nCtlX = 0;
			}
			if (nBits == 1)
			{
				if (cIn == ' ')
					pchDataOut[iOut++] = '@';
				else
					pchDataOut[iOut++] = cIn - '@';
				nBits = 0;
			}
			else if (nBits == 2)
			{
				pchDataOut[iOut++] = cIn | 0x80;
			}
			else if (nBits == 3)
			{
				pchDataOut[iOut++] = (cIn - '@') | 0x80;
			}
			else if (cIn == '@')
			{
				nBits = 1;
			}
			else
			{
				pchDataOut[iOut++] = *pchDataIn;
			}
			pchDataIn++;
		}
		break;
	}
	ProcessData(pchDataOut, iOut);
	free(pchDataOut);
}

void	PacketTransmitData(void *pvData, int iDataLen, int iStream)
{
	int	iFlags = PQF_LEADER;

	while (iDataLen > PACKET_MAX)
	{
		SendPacket(pvData, PACKET_MAX, iStream, iFlags);
		pvData = (char *) pvData + PACKET_MAX;
		iDataLen -= PACKET_MAX;
		iFlags = 0;
	}
	iFlags |= PQF_TRAILER;
	SendPacket(pvData, iDataLen, iStream, iFlags);
}


void
ReInitPackets(void)
{
	int	i;

	nInSeq = nOutSeq = 0;
	for (i = 0; i < MAX_STREAMS; i++)
		FlushQueue(appqStreams + i);
	FlushQueue(&ppqList);
	memset(aiStreams, 0, sizeof(aiStreams));
}
