/*
 * Copyright (c) 1985 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that: (1) source distributions retain this entire copyright
 * notice and comment, and (2) distributions including binaries display
 * the following acknowledgement:  ``This product includes software
 * developed by the University of California, Berkeley and its contributors''
 * in the documentation or other materials provided with the distribution
 * and in all advertising materials mentioning features or use of this
 * software. Neither the name of the University nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)res_mkquery.c	6.12 (Berkeley) 6/1/90";
#endif /* LIBC_SCCS and not lint */

#include <winsock.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "dns.h"
#include "twinsock.h"

/*
 * Form all types of queries.
 * Returns the size of the result or -1.
 */
#pragma argsused
int
res_mkquery(	int	op,
		char	*dname,
		int	class,
		int	type,
		char	*data,
		int	datalen,
		struct rrec *newrr,
		char	*buf,
		int	buflen)
{
	register HEADER *hp;
	register char *cp;
	register int n;
	char *dnptrs[10], **dpp, **lastdnptr;
	extern char *index();

	/*
	 * Initialize header fields.
	 */
	if ((buf == NULL) || (buflen < sizeof(HEADER)))
		return(-1);
	memset(buf, 0, sizeof(HEADER));
	hp = (HEADER *) buf;
	hp->id = htons(++_res.id);
	hp->opcode = op;
	hp->pr = 0;
	hp->rd = 1;
	hp->rcode = NOERROR;
	cp = buf + sizeof(HEADER);
	buflen -= sizeof(HEADER);
	dpp = dnptrs;
	*dpp++ = buf;
	*dpp++ = NULL;
	lastdnptr = dnptrs + sizeof(dnptrs)/sizeof(dnptrs[0]);
	/*
	 * perform opcode specific processing
	 */
	switch (op)
	{
	case QUERY:
		if ((buflen -= QFIXEDSZ) < 0)
			return(-1);
		if ((n = dn_comp((u_char *)dname,
				 (u_char *) cp,
				 buflen,
				 (u_char **) dnptrs,
				 (u_char **) lastdnptr)) < 0)
			return (-1);
		cp += n;
		buflen -= n;
		putshort(type, cp);
		cp += sizeof(u_short);
		putshort(class, cp);
		cp += sizeof(u_short);
		hp->qdcount = htons(1);
		if (op == QUERY || data == NULL)
			break;
		/*
		 * Make an additional record for completion domain.
		 */
		buflen -= RRFIXEDSZ;
		if ((n = dn_comp((u_char *) data,
				 (u_char *) cp,
				 buflen,
				 (u_char **) dnptrs,
				 (u_char **) lastdnptr)) < 0)
			return (-1);
		cp += n;
		buflen -= n;
		putshort(T_NULL, cp);
		cp += sizeof(u_short);
		putshort(class, cp);
		cp += sizeof(u_short);
		putlong(0, cp);
		cp += sizeof(u_long);
		putshort(0, cp);
		cp += sizeof(u_short);
		hp->arcount = htons(1);
		break;

	case IQUERY:
		/*
		 * Initialize answer section
		 */
		if (buflen < 1 + RRFIXEDSZ + datalen)
			return (-1);
		*cp++ = '\0';	/* no domain name */
		putshort(type, cp);
		cp += sizeof(u_short);
		putshort(class, cp);
		cp += sizeof(u_short);
		putlong(0, cp);
		cp += sizeof(u_long);
		putshort(datalen, cp);
		cp += sizeof(u_short);
		if (datalen) {
			memcpy(cp, data, datalen);
			cp += datalen;
		}
		hp->ancount = htons(1);
		break;
	}
	return (cp - buf);
}

#define	host	(*phost)
#define MAXADDRS (MAX_ALTERNATES - 1)
#define	MAXALIASES (MAX_ALTERNATES - 1)

typedef union
{
    long al;
    char ac;
} align;

struct hostent *
getanswer(	querybuf *answer,
		int	anslen,
		int	iquery,
		struct	hostent	*phost,	/* The hostent */
		char	*hostbuf,	/* The buffer for host names */
		int	buflen,		/* The size of the host names buffer */
		char	**h_addr_ptrs,	/* The address list */
		char	**host_aliases,	/* The alias list */
		int	*ph_errno	/* Where do we store the error? */
		)
{
	HEADER *hp;
	u_char *cp;
	int n;
	u_char *eom;
	char *bp, **ap;
	int type, class, ancount, qdcount;
	int haveanswer, getclass = C_ANY;
	char **hap;
	int	hbufsize = buflen;

	eom = answer->buf + anslen;
	/*
	 * find first satisfactory answer
	 */
	hp = &answer->hdr;
	ancount = ntohs(hp->ancount);
	qdcount = ntohs(hp->qdcount);
	bp = hostbuf;
	cp = answer->buf + sizeof(HEADER);
	if (qdcount)
	{
		if (iquery)
		{
			if ((n = dn_expand((char *)answer->buf, eom,
			     cp, bp, buflen)) < 0)
			{
				*ph_errno = WSANO_RECOVERY;
				return ((struct hostent *) NULL);
			}
			cp += n + QFIXEDSZ;
			host.h_name = bp;
			n = strlen(bp) + 1;
			bp += n;
			buflen -= n;
		}
		else
		{
			cp += dn_skipname(cp, eom) + QFIXEDSZ;
		}
		while (--qdcount > 0)
			cp += dn_skipname(cp, eom) + QFIXEDSZ;
	}
	else if (iquery)
	{
		if (hp->aa)
			*ph_errno = WSAHOST_NOT_FOUND;
		else
			*ph_errno = WSATRY_AGAIN;
		return ((struct hostent *) NULL);
	}
	ap = host_aliases;
	*ap = NULL;
	host.h_aliases = host_aliases;
	hap = h_addr_ptrs;
	*hap = NULL;
	host.h_addr_list = h_addr_ptrs;
	haveanswer = 0;
	while (--ancount >= 0 && cp < eom)
	{
		if ((n = dn_expand((char *)answer->buf, eom, cp, bp, buflen)) < 0)
			break;
		cp += n;
		type = _getshort(cp);
 		cp += sizeof(u_short);
		class = _getshort(cp);
 		cp += sizeof(u_short) + sizeof(u_long);
		n = _getshort(cp);
		cp += sizeof(u_short);
		if (type == T_CNAME)
		{
			cp += n;
			if (ap >= &host_aliases[MAXALIASES-1])
				continue;
			*ap++ = bp;
			n = strlen(bp) + 1;
			bp += n;
			buflen -= n;
			continue;
		}
		if (iquery && type == T_PTR)
		{
			if ((n = dn_expand((char *)answer->buf, eom,
			    cp, bp, buflen)) < 0)
			{
				cp += n;
				continue;
			}
			cp += n;
			host.h_name = bp;
			return(&host);
		}
		if (iquery || type != T_A)
		{
			cp += n;
			continue;
		}
		if (haveanswer)
		{
			if (n != host.h_length)
			{
				cp += n;
				continue;
			}
			if (class != getclass)
			{
				cp += n;
				continue;
			}
		}
		else
		{
			host.h_length = n;
			getclass = class;
			host.h_addrtype = (class == C_IN) ? AF_INET : AF_UNSPEC;
			if (!iquery)
			{
				host.h_name = bp;
				bp += strlen(bp) + 1;
			}
		}

		bp += sizeof(align) - ((u_long)bp % sizeof(align));

		if (bp + n >= &hostbuf[hbufsize])
			break;
		memcpy(*hap++ = bp, (char *)cp, n);
		bp +=n;
		cp += n;
		haveanswer++;
	}
	if (haveanswer)
	{
		*ap = NULL;
		*hap = NULL;
		return (&host);
	}
	else
	{
		*ph_errno = WSATRY_AGAIN;
		return ((struct hostent *) NULL);
	}
}

