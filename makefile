all:	twinsock.exe

winsock.dll: winsock.c
	bcc -WD -ml -v -w- -lc -lm -ls winsock.c

winsock.lib : winsock.dll
	implib winsock.lib winsock.dll

packet.obj: packet.c
	bcc -WE -ml -v -w- -c packet.c

showprot.obj: showprot.c
	bcc -WE -ml -v -w- -c showprot.c

twinsock.obj: twinsock.c
	bcc -WE -ml -v -w- -c twinsock.c

about.obj: about.c
	bcc -WE -ml -v -w- -c about.c

comms.obj: comms.c
	bcc -WE -ml -v -w- -c comms.c

twinsock.exe: twinsock.obj packet.obj about.obj comms.obj showprot.obj winsock.lib
	bcc -WE -lc -lm -ls -ml -v twinsock.obj packet.obj about.obj comms.obj showprot.obj winsock.lib
	rc twinsock
