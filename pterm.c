/*Portable POSIX replacement for term.c*/

#include <termios.h>
#include <stdio.h>

static struct   termios Old;
static struct   termios New;

void InitTerm(void)
{
	if(tcgetattr(0, &Old)<0)                /*get current term attributes*/
	{
		perror("tcgetattr");
		exit(1);
	}

	New=Old;                                /*in case nonstandard fields*/

	New.c_iflag = IXOFF|IXON;               /*raw except for flow control*/
	New.c_oflag = 0;
	New.c_cflag = CREAD|CS8|HUPCL;
	New.c_lflag = 0;

	New.c_cc[VSTOP] = '\023';               /*XOFF*/
	New.c_cc[VSTART] = '\021';              /*XON*/
	New.c_cc[VMIN]= 255;                    /*max*/
	New.c_cc[VTIME]= 1;                     /*0.1 second*/

	cfsetispeed(&New, cfgetispeed(&Old));   /*preserve old baud rate*/
	cfsetospeed(&New, cfgetospeed(&Old));

	if(tcsetattr(0, TCSAFLUSH, &New)<0)     /*set new attributes*/
	{
		perror("tcsetattr");
		exit(1);
	}
}

void UnInitTerm(void)
{
	if(tcsetattr(0, TCSAFLUSH, &Old)<0)     /*restore old attributes*/
	{
		perror("tcsetattr");
		exit(1);
	}
}


