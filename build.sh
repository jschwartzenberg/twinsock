#!/bin/sh
# Remove all carriage returns and Control-Zs
rm -f a.out test.c
rm -f *.o tshost
echo "Cleaning the source code of MS-DOS control characters"
for i in *.c *.h
do
	tr -d '\015\032' < $i > tempfile
	mv tempfile $i
done

# Test for a few things we need to know about

echo "Testing for sys/select.h"
if [ -f /usr/include/sys/select.h ]
then
	SELECT_H=-DNEED_SELECT_H
	echo "You have sys/select.h"
	if grep 'Santa Cruz Operation' /usr/include/sys/select.h >/dev/null
	then
		SELECT_H=
		echo "but we won't use it since the SCO select.h is broken ..."
	fi
fi

echo "Testing for sys/ttold.h"
if [ -f /usr/include/sys/ttold.h ]
then
	TTOLD_H=-DNEED_TTOLD_H
	echo "You have sys/ttold.h"
fi

echo "Testing for sgtty.h"
if [ -f /usr/include/sgtty.h ]
then
	echo "You have it"
else
	echo "You don't have it - I will use ioctl.h instead"
	SGTTY_H=-DNO_SGTTY_H
fi

# Try to find a C compiler that does ANSI.
# Note that just testing for no error exit is not sufficient
# because what we find may not be a compiler, so we test for
# an a.out file.

echo "main(int argc, char **argv) { return (int) argv[argc]; }" > test.c
echo "Attempting to find a compiler that will work"
for i in bsdcc ucbcc cc acc gcc /usr/local/bin/gcc
do
	( $i test.c ) </dev/null >/dev/null 2>&1
	if [ -f a.out ]
	then
		CC=$i
		break
	fi
done

case "$CC" in
"")
	echo "Unable to find an ANSI C compiler"
	exit 1
	;;
esac

echo "Using $CC as the C compiler"

echo "main() {}" > test.c

echo "Testing for -lsocket"
if $CC test.c -lsocket > /dev/null 2>/dev/null
then
	echo "You will need -lsocket"
	L_SOCKET=-lsocket
else
	echo "You don't need it"
fi

echo "Testing for -lresolv"
if $CC test.c -lresolv ${L_SOCKET} > /dev/null 2>/dev/null
then
	echo "You will need -lresolv"
	L_RESOLV=-lresolv
else
	if [ -f /lib/resolv.so ]
	then
		echo "Found the resolver libraries in /lib/resolv.so"
		L_RESOLV=/lib/resolv.so
	else
		if [ -f /usr/lib/resolv.so ]
		then
			echo "Found the resolver libraries in /usr/lib/resolv.so"
			L_RESOLV=/usr/lib/resolv.so
		else
			echo "You don't appear to need resolver libraries"
		fi
	fi
fi

echo "Testing for -lnsl"
if $CC test.c -lnsl ${L_RESOLV} -lresolv ${L_SOCKET} > /dev/null 2>/dev/null
then
	echo "You will need -lnsl"
	L_NSL=-lnsl
else
	echo "You don't appear to need -lnsl"
fi

echo "main() {char *pch1, *pch2; memcpy(pch1, pch2, 10); memset(pch1, 0, 10); }" > test.c
if $CC test.c > /dev/null 2>&1
then
	echo "You have memcpy and memset"
else
	echo "We will use TwinSock's memcpy and memset"
	NEED_MEM=mem.o
fi

echo "Testing for h_errno"
echo "#include <netdb.h>
main() { return h_errno; }" > test.c
if $CC test.c > /dev/null 2>&1
then
	echo "h_errno is where it should be"
else
	echo "extern int h_errno; main() { return h_errno; }" > test.c
	if $CC test.c > /dev/null 2>&1
	then
		echo "h_errno is not declared in netdb.h, but exists"
		H_ERRNO=-DNEED_H_ERRNO
	else
		echo "h_errno does not exist, using errno"
		H_ERRNO=-DNO_H_ERRNO
	fi
fi

if [ -f /usr/include/termios.h ]
then
	echo "Using POSIX terminals"
	TERM_OBJECT=pterm.o
else
	echo "Using BSD terminals"
	TERM_OBJECT=term.o
fi

case "`uname -s`" in
OSF*)
	echo "OSF doesn't like POSIX terminals, so we'd better use BSD ones."
	TERM_OBJECT=term.o
	;;
esac

rm -f a.out test.c

OBJECTS="tshost.o packet.o getentry.o commands.o getsock.o sockinfo.o ${TERM_OBJECT} $NEED_MEM"

echo "Building makefile"
echo ".c.o:" > Makefile
echo "	${CC} ${SELECT_H} ${TTOLD_H} ${SGTTY_H} ${H_ERRNO} -c "'$*.c' >> Makefile
echo >> Makefile
echo "tshost: ${OBJECTS}" >> Makefile
echo "	${CC} -o tshost ${OBJECTS} ${L_NSL} ${L_RESOLV} ${L_SOCKET}" >> Makefile

echo
echo 'Running "make -f Makefile"'
echo
exec make -f Makefile
