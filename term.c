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

#include <sgtty.h>

struct	sgttyb Old;
struct	sgttyb New;

void	InitTerm(void)
{
	ioctl(0, TIOCGETP, &Old);
	New = Old;
	New.sg_flags |= CBREAK | LITOUT | PASS8 | DECCTQ | RAW;
	New.sg_flags &= ~(ECHO | CRMOD | TILDE | NOHANG | CTLECH);
	ioctl(0, TIOCSETP, &New);
}

void	UnInitTerm(void)
{
	ioctl(0, TIOCSETP, &Old);
}
