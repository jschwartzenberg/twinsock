all:	twinsock.exe twnsck32.exe winsock.dll wsock32.dll


winsock.dll: winsock.obj ddedll.obj gsdll16.obj \
	     res_comp.obj res_init.obj res_mkq.obj
	bcc -WD -ml -v -lc -lm -ls $**

winsock.lib : winsock.dll
	implib winsock.lib winsock.dll

res_comp.obj: res_comp.c
	bcc -WD -ml -v -c res_comp.c

res_init.obj: res_init.c
	bcc -WD -ml -v -c res_init.c

res_mkq.obj: res_mkq.c
	bcc -WD -ml -v -c res_mkq.c

winsock.obj: winsock.c
	bcc -WD -ml -v -c winsock.c

ddedll.obj: dde.c
	bcc -WD -ml -v -oddedll -c dde.c

gsdll16.obj: getsock.c
	bcc -WD -ml -v -ogsdll16 -c getsock.c

ddedll32.obj: dde.c
	bcc32 -WD -v -oddedll32 -c dde.c

gsdll32.obj: getsock.c
	bcc32 -WD -v -ogsdll32 -c getsock.c

wsock32.dll: wsock32.obj ddedll32.obj gsdll32.obj \
		r32_comp.obj r32_init.obj r32_mkq.obj
	bcc32 -WD -v -lc -lm -ls -ewsock32 $**

wsock32.obj: winsock.c
	bcc32 -WD -v -owsock32 -c winsock.c

r32_comp.obj: res_comp.c
	bcc32 -WD -v -or32_comp -c res_comp.c

r32_init.obj: res_init.c
	bcc32 -WD -v -or32_init -c res_init.c

r32_mkq.obj: res_mkq.c
	bcc32 -WD -v -or32_mkq -c res_mkq.c

dde.obj: dde.c
	bcc -WE -ml -v -c dde.c

packet.obj: packet.c
	bcc -WE -ml -v -c packet.c

getentry.obj: getentry.c
	bcc -WE -ml -v -c getentry.c

showprot.obj: showprot.c
	bcc -WE -ml -v -c showprot.c

twinsock.obj: twinsock.c
	bcc -WE -ml -v -c twinsock.c

script.obj: script.c
	bcc -WE -ml -v -c script.c

about.obj: about.c
	bcc -WE -ml -v -c about.c

comms.obj: comms.c
	bcc -WE -ml -v -c comms.c

getsock.obj: getsock.c
	bcc -WE -ml -v -c getsock.c

ns.obj: ns.c
	bcc -WE -ml -v -c ns.c

twinsock.exe: 	twinsock.obj packet.obj about.obj comms.obj \
		showprot.obj getentry.obj script.obj \
		dde.obj getsock.obj ns.obj
	bcc -WE -lc -lm -ls -ml -v @&&!
		twinsock.obj packet.obj \
		about.obj comms.obj showprot.obj getentry.obj \
		script.obj dde.obj getsock.obj ns.obj
!
	rc twinsock

dde32.obj: dde.c
	bcc32 -WE -v -odde32 -c dde.c

pkt32.obj: packet.c
	bcc32 -WE -v -opkt32 -c packet.c

getent32.obj: getentry.c
	bcc32 -WE -v -ogetent32 -c getentry.c

shwprt32.obj: showprot.c
	bcc32 -WE -v -oshwprt32 -c showprot.c

twnsck32.obj: twinsock.c
	bcc32 -WE -v -otwnsck32 -c twinsock.c

script32.obj: script.c
	bcc32 -WE -v -oscript32 -c script.c

about32.obj: about.c
	bcc32 -WE -v -oabout32 -c about.c

comms32.obj: comms.c
	bcc32 -WE -v -ocomms32 -c comms.c

getsck32.obj: getsock.c
	bcc32 -WE -v -ogetsck32 -c getsock.c

ns32.obj: ns.c
	bcc32 -WE -v -ons32 -c ns.c

twnsck32.exe: 	twnsck32.obj pkt32.obj about32.obj comms32.obj \
		shwprt32.obj getent32.obj script32.obj \
		dde32.obj getsck32.obj ns32.obj
	brc32 -r -fotwnsck32 twinsock.rc
	bcc32 -WE -lc -lm -ls -v @&&!
	twnsck32.obj pkt32.obj \
		about32.obj comms32.obj shwprt32.obj getent32.obj \
		script32.obj dde32.obj getsck32.obj ns32.obj
!
	brc32 twnsck32.res

