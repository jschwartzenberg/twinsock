#ifndef __SOCKINFO_H
#define __SOCKINFO_H

/* This file defines the structures mapping sockets numbers.
 * In order to allow the socket() call to succeed immediately,
 * the WINSOCK.DLL assigns its idea of the socket number, sends
 * the request off in the background, and returns immediately.
 *
 * TWINSOCK.EXE then receives it, and stores WINSOCK.DLL's idea
 * of the socket number in iClientSocket, and its own idea in
 * iServerSocket. It then uses iServerSocket in its communications
 * with the tshost.
 *
 * tshost receives the socket number, and stores it in iClientSocket.
 * It then makes the socket() call and places the results in
 * iServerSocket. The socket() call no longer passes its results
 * back. If it fails, subsequent attempts to use the socket will
 * be failed with different values depending on the nature of the
 * call.
 *
 * The host component also maintains a list of data waiting to
 * be sent to the socket, so it can send without blocking.
 * The select() loop then has to let us know when the socket
 * is ready.
 *
 * for accept() calls, the server assigns the client side, so
 * the whole thing works backwards. This has the potential to
 * result in collisions. This is fixed by assigning all sockets
 * with bit 0 set to the server side, and all with bit 0 clear
 * to the client side.
 */

typedef struct __tws_data
{
	char	*pchData;
	int	nBytes;
	int	iLoc;
	struct sockaddr_in sinDest;
	int	iFlags;
	int	bTo;	/* sendto or send? */
	struct __tws_data *pdataNext;
} tws_data;

typedef struct __tws_sockinfo
{
	int	iClientSocket;
	int	iServerSocket;
	tws_data *pdata;
	struct	tx_request *ptxrConnect;
	struct __tws_sockinfo *psiNext;
} tws_sockinfo;

extern	int	GetClientSocket(void);
extern	int	GetServerSocket(void);
extern	void	ReleaseClientSocket(int iSocket);
extern	void	ReleaseServerSocket(int iSocket);
#ifndef __Windows
extern	tws_sockinfo *FindClientSocket(int iClient);
extern	tws_sockinfo *FindServerSocket(int iServer);
extern	tws_sockinfo *FindSocketEntry(int iServer);
#endif

extern	int	HasSocketArg(enum Functions fn);

#endif
