				TwinSock 1.0
				============
			    Troy's Windows Sockets
			  Copyright 1994 Troy Rollo

What is TwinSock?
-----------------

TwinSock is a free implementation of proxy sockets for Windows.

Other Windows Sockets drivers use a network card, or a well known Internet
over serial lines protocol, such as SLIP, C-SLIP or PPP. These drivers may
access the network card or communications card directly, or via a VxD or DOS
based TCP/IP stack. their uses are limited to cases where either the machine
is directly connected to a network, or the host at the other end of the phone
line supports the same serial line internet protocol.

The other shortcoming of these drivers is that they require an official IP
address to operate, and frequently you will not be able to connect very far
beyond the host you connect directly to.

TwinSock, on the other hand, makes use of the IP address of the host to
provide socket services to the client. When an application running under
Windows requests socket services of TwinSock, TwinSock will transparently
pass these requests on to the TwinSock Host program running on the remote
machine for processing. The result is that you have all the same networking
capabilities as you would if your Windows machine were physically connected
to the network in place of thte host machine.

Licensing
---------

TwinSock is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

Installing
----------

	1. Copy WINSOCK.DLL, TWINSOCK.EXE and TWINSOCK.INI to your Windows
	   directory.

	2. Edit TWINSOCK.INI and make sure all the parameters match up with
	   what you need for your modem.

	3. Copy the following files to a directory on your UNIX host,
	   renaming makefile.unx to Makefile.

		makefile.unx
		tshost.c
		packet.c
		commands.c
		term.c
		packet.h
		twinsock.h
		tx.h
		wserror.h

	4. Type "make" to build the server. If it doesn't compile first
	   off, try to modify it until it does. The files you will probably
	   need to touch are (in decreasing order of probability):

		term.c
		tshost.c
		commands.c

	   You should avoid touching packet.c if possible.

	5. Using your favourite terminal program, start tshost.
	   You should see a message telling you to start your TwinSock
	   client.

	6. Start the TwinSock client.

	7. Start your favourite Windows Sockets applications.

Shutting down
-------------

	1. Stop the TwinSock client.

	2. Start your terminal program.

	3. Type ^X (control-X) five times.

Enhancements and Bug Reports
----------------------------

	Enhancements and bug reports should be directed to:

		twinsock@cbme.unsw.EDU.AU

TODO & BUGS
-----------

	The protocol used over the serial connection converts everything
	to base 64 using the characters A-Z, a-z, '.' and '/' in order to
	get past the most obstinate terminal servers. This is probably
	overkill in almost all cases, and there should be an option to
	fix this.

	Out Of Band data should be handled properly.

	The protocol over the serial line was kept simple to ensure a
	quick release (9 days after the project was started). It could
	benefit from any number of advanced features.

	The internals don't clean up properly if an application exits
	without cleaning up itself.

	There are some potentially annoying bugs, although this version
	is usable with most Windows Sockets applications.

	TwinSock Host should be ported to more hosts, including non
	UNIX hosts.

History
-------

	05-Nov-1994	Project initiated
	14-Nov-1994	Version 1.0 released
