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

enum	arg_type
{
	AT_Int16 = 1,
	AT_Int32,
	AT_Int16Ptr,
	AT_Int32Ptr,
	AT_Char,
	AT_String,
	AT_GenPtr,
#ifdef __MSDOS__
	AT_Int = AT_Int16,
	AT_IntPtr = AT_Int16
#else
#ifdef apollo
#define	AT_Int AT_Int32
#define	AT_IntPtr AT_Int32
#else
	AT_Int = AT_Int32,
	AT_IntPtr = AT_Int
#endif
#endif
};

enum Encoding
{
	E_6Bit = 0,
	E_8Bit,
	E_8NoCtrl,
	E_8NoX
};

enum	Functions
{
	FN_Init = 0,
	FN_Accept,
	FN_Bind,
	FN_Close,
	FN_Connect,
	FN_IOCtl,
	FN_GetPeerName,
	FN_GetSockName,
	FN_GetSockOpt,
	FN_Listen,
	FN_Select,
	FN_Send,
	FN_SendTo,
	FN_SetSockOpt,
	FN_Shutdown,
	FN_Socket,
	FN_Data,
	FN_GetHostName,
        FN_HostByAddr,
        FN_HostByName,
        FN_ServByPort,
        FN_ServByName,
        FN_ProtoByNumber,
        FN_ProtoByName
};

struct	func_arg
{
	enum arg_type	at;
	void		*pvData;
	int		iLen;
#ifdef	_Windows
	BOOL		bConstant;
#endif
};

struct	transmit_function
{
	enum	Functions	fn;
	int			nArgs;
	struct	func_arg	*pfaList;
	struct	func_arg	*pfaResult;
};

#define	MAX_HOST_ENT	1024
#define	MAX_ALTERNATES	20

#ifdef _Windows
struct	data
{
	int	iLen;
	int	nUsed;
	struct	sockaddr_in sin;
	char	*pchData;
	struct	data *pdNext;
};

struct	per_task
{
	HTASK			htask;
	char			achAddress[16];
	struct	per_task	*pptNext;
	int			iErrno;
	FARPROC			lpBlockFunc;
	BOOL			bCancel;
	BOOL			bBlocking;
	struct	hostent		he;
	struct	servent		se;
	struct	protoent	pe;
	char			achHostEnt[MAX_HOST_ENT];
	char			*apchHostAlii[MAX_ALTERNATES];
	char			*apchHostAddresses[MAX_ALTERNATES];
	char			achServEnt[MAX_HOST_ENT];
	char			*apchServAlii[MAX_ALTERNATES];
	char			achProtoEnt[MAX_HOST_ENT];
	char			*apchProtoAlii[MAX_ALTERNATES];
};

struct	per_socket
{
	SOCKET			s;
	unsigned short		iFlags;
	struct	data		*pdIn;
	struct	data		*pdOut;
	HTASK			htaskOwner;
	struct	per_socket	*ppsNext;
	long			iEvents;
	HWND			hWnd;
	unsigned		wMsg;			
};

#define	PSF_ACCEPT	0x0001
#define	PSF_CONNECT	0x0002
#define	PSF_SHUTDOWN	0x0004
#define	PSF_NONBLOCK	0x0008
#define	PSF_CLOSED	0x0010

#define	INIT_ARGS(args, type, data, size) \
		( args.at = type, \
		  args.pvData = (void *) data, \
		  args.iLen = size, \
		  args.bConstant = FALSE )

#define INIT_CARGS(args, type, data, size) \
		( args.at = type, \
		  args.pvData = (void *) data, \
		  args.iLen = size, \
		  args.bConstant = TRUE )

#define	INIT_TF(tf, func, count, args, retval) \
		( tf.fn = func, \
		  tf.nArgs = count, \
		  tf.pfaList = args, \
		  tf.pfaResult = &retval )

#endif
