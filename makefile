all:	twinsock.exe

winsock.dll: winsock.c
	bcc -WD -ml -v -lc -lm -ls winsock.c

winsock.lib : winsock.dll
	implib winsock.lib winsock.dll

packet.obj: packet.c
	bcc -WE -ml -v -c packet.c

getentry.obj: getentry.c
	bcc -WE -ml -v -c getentry.c

showprot.obj: showprot.c
	bcc -WE -ml -v -c showprot.c

twinsock.obj: twinsock.c
	bcc -WE -ml -v -c twinsock.c

about.obj: about.c
	bcc -WE -ml -v -c about.c

comms.obj: comms.c
	bcc -WE -ml -v -c comms.c

twinsock.exe: twinsock.obj packet.obj about.obj comms.obj showprot.obj getentry.obj winsock.lib
	bcc -WE -lc -lm -ls -ml -v twinsock.obj packet.obj about.obj comms.obj showprot.obj getentry.obj winsock.lib
	rc twinsock
