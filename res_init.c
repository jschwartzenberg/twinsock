#include <winsock.h>
#include <ddeml.h>
#include <stdlib.h>
#include <string.h>
#include "dns.h"
#include "tx.h"

struct state _res =
{
	RES_TIMEOUT,               	/* retransmition time interval */
	4,                         	/* number of times to retransmit */
	RES_DEFAULT,			/* options flags */
	1,                         	/* number of name servers */
};

void
res_init()
{
	int	i;
	char *pch;
	int	nSearches;
	char	achAddresses[4];
	int	aiAddresses[4];

	memset(_res.nsaddr, 0, 4);
	_res.nscount = 1;

	strncpy(_res.defdname, hostinfo.achDomainName, sizeof(_res.defdname));
	for (i = 0; i < MAXNS && i < hostinfo.nHosts; i++)
	{
		sscanf(hostinfo.aachHosts[i], "%d.%d.%d.%d",
			&aiAddresses[0],
			&aiAddresses[1],
			&aiAddresses[2],
			&aiAddresses[3]);
		achAddresses[0] = aiAddresses[0];
		achAddresses[1] = aiAddresses[1];
		achAddresses[2] = aiAddresses[2];
		achAddresses[3] = aiAddresses[3];
		memcpy(&_res.nsaddr_list[i],
			achAddresses, 4);
	}
	_res.nscount = i ? i : 1;
	if (!_res.defdname[0])
	{
		if ((pch = strchr(hostinfo.achHostName, '.')) != 0)
			strcpy(_res.defdname, pch + 1);
	}
	_res.options |= RES_INIT;
}
