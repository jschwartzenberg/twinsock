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

enum packet_type
{
	PT_Data,
	PT_Ack,
	PT_Nak,
	PT_Shutdown
};

#define	PACKET_MAX 512

struct	packet
{
	short	iPacketID;
	short	iPacketLen;
	short	nCRC;
	short	nType;
	char	achData[PACKET_MAX];
};


