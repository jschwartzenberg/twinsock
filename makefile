all:	twinsock.exe

winsock.dll: winsock.c
	bcc -WD -ml -v -w- -lc -lC winsock.c

winsock.lib : winsock.dll
	implib winsock.lib winsock.dll

packet.obj: packet.c
	bcc -WE -ml -v -w- -c packet.c

twinsock.obj: twinsock.c
	bcc -WE -ml -v -w- -c twinsock.c

twinsock.exe: twinsock.obj packet.obj winsock.lib
	bcc -WE -lc -lC -ml -v twinsock.obj packet.obj winsock.lib
	rc twinsock
