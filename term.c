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

#ifndef NO_SGTTY_H
#include <sgtty.h>
#else
#include <sys/ioctl.h>
#endif
#ifdef NEED_TTOLD_H
#include <sys/ttold.h>
#endif

#if !defined(CBREAK) && defined(O_CBREAK)
#define CBREAK O_CBREAK
#endif
#if !defined(LITOUT) && defined(O_LITOUT)
#define LITOUT O_LITOUT
#endif
#if !defined(PASS8) && defined(O_PASS8)
#define PASS8 O_PASS8
#endif
#if !defined(DECCTQ) && defined(O_DECCTQ)
#define DECCTQ O_DECCTQ
#endif
#if !defined(RAW) && defined(O_RAW)
#define RAW O_RAW
#endif
#if !defined(ECHO) && defined(O_ECHO)
#define ECHO O_ECHO
#endif
#if !defined(CRMOD) && defined(O_CRMOD)
#define CRMOD O_CRMOD
#endif
#if !defined(TILDE) && defined(O_TILDE)
#define TILDE O_TILDE
#endif
#if !defined(NOHANG) && defined(O_NOHANG)
#define NOHANG O_NOHANG
#endif
#if !defined(CTLECH) && defined(O_CTLECH)
#define CTLECH O_CTLECH
#endif

struct	sgttyb Old;
struct	sgttyb New;

void	InitTerm(void)
{
	ioctl(0, TIOCGETP, &Old);
	New = Old;
	New.sg_flags |= CBREAK | RAW;
#ifdef PASS8
	New.sg_flags |= PASS8;
#endif
#ifdef DECCTQ
	New.sg_flags |= DECCTQ;
#endif
#ifdef LITOUT
	New.sg_flags |= LITOUT;
#endif
	New.sg_flags &= ~(ECHO | CRMOD | TILDE | NOHANG | CTLECH);
	ioctl(0, TIOCSETP, &New);
}

void	UnInitTerm(void)
{
	ioctl(0, TIOCSETP, &Old);
}
