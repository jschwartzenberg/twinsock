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
#endif
