// TSSCC.CPP - Script compiler for Plug-In for Twinsock
// Created by Misha Koshelev
// Copyright (C) 1995 by Misha Koshelev

// Commands are converted as follows (numbers are in hex):
//
//
//     Command         Description       Converted to
//
//     ; str             Comment           Nothing
//
//     END           Ends the script         00
//
//   LABEL str     Puts a label referenced  Nothing
//                 by str at the specified
//                      position
//
//   GOTO str      Goes to the specified     01(4-byte offset in file)
//                    label
//
//   SAY str       Displays a string and     02(String Length Byte)(Str)
//                 waits for the user to
//                 press OK
//
// YESNO prompt    Asks the user a yes or    03(String Length Byte)(Prompt)
//    label1       no question with the        (4-byte offset)(4-byte offs)
//    label2       specified prompt and
//                 jumps to label1 for yes
//                 or to label2 for no
//
//    USER str     Lets the user freely      04(String Length Byte)(Prompt)
//                 type until str is
//                 received
//
//    SEND str       Sends a string out   10(String Length Byte)(Str)
//                   the COM port, where
//                    # is a newline,
//                   and , is a space
//
//     SENDI         Gets a string from           11
//                   the user and sends
//                   it to the COM port
//
//     SENDINE       Gets a string from           12
//                   the user (with no
//                   echo, i.e. for
//                   passwords) and sends
//                   it to the COM port
//
//
//  WAITFOR str      Stops execution      20(String Length Byte)(Str)
//                   until the given
//                   string is received
//
// WAITFIND        Checks for the string  21(String Length Byte)(WaitStr)
//  waitstr        to find constantly        (String Length Byte)(FindStr)
//  findstr        until either a match is   (4-byte offset)(4-byte offs)
//  waitlbl        found or the string
//  findlbl        we are waiting for is
//                 found
//

#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <string.h>
#include <dos.h>
#include "tss.h"

// Several NULLs in a row
#define NULL9 NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL
#define NULL7 NULL,NULL,NULL,NULL,NULL,NULL,NULL
#define NULL6 NULL,NULL,NULL,NULL,NULL,NULL

// The type for a command parameter
#define TINTEGER 0
#define TSTRING 1
#define TLABEL 2

typedef struct
{
   int type;              // The type of the parameter
} Param;

// Command keywords with their appropriate indices
typedef struct
{
   char mnemonic[40];  // The command's mnemonic
   int bm;             // The command's byte mnemonic
   int nparam;         // The number of parameters needed

   Param param[10];    // The parameters
} Commands;

// *** NOTE: ; is not a command
#define NCOMMAND 11

Commands commands[NCOMMAND] =
  {{"END", CEND, 0, NULL},
   {"GOTO", CGOTO, 1, {{TLABEL}, NULL9}},
   {"SAY", CSAY, 1, {{TSTRING}, NULL9}},
   {"YESNO", CYESNO, 3, {{TSTRING}, {TLABEL}, {TLABEL}, NULL7}},
   {"USER", CUSER, 1, {{TSTRING}, NULL9}},
   {"SEND", CSEND, 1, {{TSTRING}, NULL9}},
   {"SENDI", CSENDI, 1, {{TSTRING}, NULL9}},
   {"SENDINE", CSENDINE, 1, {{TSTRING}, NULL9}},
   {"WAITFOR", CWAITFOR, 1, {{TSTRING}, NULL9}},
   {"WAITFIND", CWAITFIND, 4, {{TSTRING}, {TSTRING}, {TLABEL}, {TLABEL}, NULL6}},
   {" þ", 255, 0, NULL}};   // Used so that the command-searching will work:

// A table of references to labels (to be filled in the second pass)
typedef struct
{
   long offset; // The offset of the label
   char label[80];   // The name of the label
} RefTable;

#define MAX_REFS 40

RefTable refs[MAX_REFS];
int nrefs = 0;

// A table of labels and their offsets
typedef struct
{
   long offset; // The label's offset in the file
   char label[80];  // The label's name
} LabelTable;

#define MAX_LABELS 40

LabelTable labels[MAX_LABELS];
int nlabels;

char str[160];

// Zero offset
long zerooffs = 0;

void main(int argc, char *argv[])
{
   char sname[80], cname[80];
   FILE *inf, *of;
   int done, pos, cnum, i, j;
   char ch, tch;

   printf("TSSCC - Copyright (C) 1995 by Misha Koshelev. All Rights Reserved.\n");

   if (argc < 2)
   {
      printf("Usage: TSSCC [filename]");
      exit(1);
   }

   strcpy(sname, argv[1]);
   strcpy(cname, argv[1]);
   strcat(sname, ".TSS");
   strcat(cname, ".TSC");

   inf = fopen(sname, "rt");
   if (inf == NULL)
   {
      printf("þ Can't open script %s", sname);
      exit(1);
   }
   of = fopen(cname, "wb");

   done = 0;
   while (!done)
   {
CheckLine:
      // If the first character is a comment, then read in the comment
      tch = fgetc(inf);

      if (tch == ';')
      {
	 ch = 0;
	 while (ch != '\n')
	 {
	    ch = fgetc(inf);
	 }
      }

      if (tch == '\n')
      {
	 // Go back to the beginning because the next character
	 // could be a comment
	 goto CheckLine;
      }

      if (tch != ';')
      {
	 fseek(inf, -1L, SEEK_CUR);

	 // Read in a command until a ' ', '\n', ';', or EOF is reached
	 pos = 0;
	 while (str[pos-1] != ' ' && str[pos-1] != '\n' &&
		str[pos-1] != ';' && !feof(inf))
	 {
	    str[pos] = fgetc(inf);
	    pos++;
	 }

	 // Save the last character and set it to zero
	 pos--;
	 ch = str[pos];
	 str[pos] = 0;

	 // Set the command read to the right command needed
	 cnum = 0;
	 while (strcmp(str, commands[cnum].mnemonic) != 0 && cnum < NCOMMAND)
	 {
	    cnum++;
	 }

	 // If this command is a label, then process it as a special case
	 if (!strcmp(str, "LABEL"))
	 {
	    // Read in the parameter into the first available label slot,
	    // or error if none are left
	    if (nlabels >= MAX_LABELS-1)
	    {
	       printf("Error: Too many labels");
	       exit(1);
	    }

	    if (feof(inf))
	    {
	       printf("Error: END directive not found before end of file");
	       exit(1);
	    }

	    // If the last character read was an EOL, then error
	    if (ch == '\n')
	    {
	       printf("Error: Command %s has no parameters on its line",
		      commands[cnum].mnemonic);
	       exit(2);
	    }

	    // Read in the parameter until either a ' ', '\n', or EOF
	    // is reached
	    pos = 0;
	    while (labels[nlabels].label[pos-1] != ' ' &&
		   labels[nlabels].label[pos-1] != '\n' &&
		   !feof(inf))
	    {
	       labels[nlabels].label[pos] = fgetc(inf);
	       pos++;
	    }

	    pos--;

	    labels[nlabels].label[pos] = 0;

	    if (feof(inf))
	    {
	       printf("Error: END directive not found before end of file");
	       exit(1);
	    }

	    // Save the current offset
	    labels[nlabels].offset = ftell(of);

	    nlabels++;
	 }
	 // If this is not a label, then process it as a command
	 else
	 {
	    // If we have not found this command, then give an error message
	    if (cnum == NCOMMAND)
	    {
	       printf("Error: No such command %s", str);
	       exit(2);
	    }

	    // If the last character read is a comment, then skip to the end
	    // of the line if the command read does not need any parameters
	    if (ch == ';')
	    {
	       if (commands[cnum].nparam != 0)
	       {
		  printf("Error: Comment found where parameter expected in\n\
			  command %s", str);
		  exit(2);
	       }

	       // Read in the comment (saving the character in the process)
	       str[pos] = ch;
	       while (ch != '\n')
	       {
		  ch = fgetc(inf);
	       }
	       ch = str[pos];
	    }

	    // Put the command's byte mnemonic into the output compiled
	    // script
	    fwrite(&commands[cnum].bm, 1, 1, of);

	    // Read in the parameters as needed (and error if the last
	    // character read was an EOF)
	    if (commands[cnum].nparam != 0)
	    {
	       if (feof(inf))
	       {
		  printf("Error: END directive not found before end of file");
		  exit(3);
	       }

	       // If the last character read was an EOL, then error
	       if (ch == '\n')
	       {
		  printf("Error: Command %s has no parameters on its line",
			 commands[cnum].mnemonic);
		  exit(1);
	       }

	       for (i=0; i<commands[cnum].nparam; i++)
	       {
	       switch(commands[cnum].param[i].type)
	       {
		case TSTRING:
		  // Read in the parameter until either a ' ', '\n', or EOF
		  // is reached
		  pos = 0;
		  while (str[pos-1] != ' ' && str[pos-1] != '\n' && !feof(inf))
		  {
		     str[pos] = fgetc(inf);
		     pos++;
		  }

		  pos--;

		  if (feof(inf))
		  {
		     printf("Error: END directive not found before end of file");
		     exit(3);
		  }

		  // If an EOL was read, and all of the parameters have
		  // not been read yet, then error
		  if (str[pos] == '\n' && i < commands[cnum].nparam-1)
		  {
		     printf("Error: One or several of the parameters for the command\n\
			     %s could not be found", commands[cnum].mnemonic);
		     exit(1);
		  }

		  // Search through the string, replacing characters
		  // as needed
		  str[pos] = '\x0';

		  for (j=0; j<strlen(str); j++)
		  {
		     if (str[j] == '#') str[j] = '\r';
		     if (str[j] == ',') str[j] = ' ';
		  }

		  // If everything is OK, then store the parameter
		  ch = strlen(str);
		  fwrite(&ch, 1, 1, of);
		  fwrite(str, ch, 1, of);
		  break;

		case TLABEL:
		  // If there are too many label references, then exit
		  if (nrefs >= MAX_REFS-1)
		  {
		     printf("Error: Too many label references");
		     exit(1);
		  }

		  // Read in the parameter until either a ' ', '\n', or EOF
		  // is reached
		  pos = 0;
		  while (refs[nrefs].label[pos-1] != ' ' &&
			 refs[nrefs].label[pos-1] != '\n' &&
			 !feof(inf))
		  {
		     refs[nrefs].label[pos] = fgetc(inf);
		     pos++;
		  }

		  pos--;

		  if (feof(inf))
		  {
		     printf("Error: END directive not found before end of file");
		     exit(3);
		  }

		  // If an EOL was read, and all of the parameters have
		  // not been read yet, then error
		  if (str[pos] == '\n' && i < commands[cnum].nparam-1)
		  {
		     printf("Error: One or several of the parameters for the command\n\
			     %s could not be found", commands[cnum].mnemonic);
		     exit(1);
		  }

		  refs[nrefs].label[pos] = '\x0';

		  // Set the offset to write to, and store a zero offset
		  // for now
		  refs[nrefs].offset = ftell(of);
		  fwrite(&zerooffs, 4, 1, of);

		  // Increase the number of references
		  nrefs++;
		  break;

		default:
		  sound(440);
		  delay(500);
		  nosound();
		  printf("Error in command definition array\n");
		  printf("Please contact me (see the README.TXT file to\n");
		  printf("find out how)");
		  exit(255);
	       }
	       }
	    }

	    // If END has been reached, then stop compiling
	    if (commands[cnum].bm == CEND)
	    {
	       done = 1;
	    }
	 }
      }
   }

   // Make a dummy last label
   nlabels++;

   // Tell the user that we are processing label references
   printf("Processing label references... ");

   // Loop through all of the label references, writing the appropriate
   // label offset
   for (i=0; i<nrefs; i++)
   {
      j = 0;
      while (strcmp(refs[i].label, labels[j].label) != 0 && j < nlabels)
      {
	 j++;
      }

      // If we did not find the label, then give an error
      if (j == nlabels-1)
      {
	 printf("Error: Could not find label %s", refs[i].label);
      }

      // Otherwise write the appropriate label offset
      fseek(of, refs[i].offset, SEEK_SET);
      fwrite((long *)&(long)labels[j].offset, 4, 1, of);
   }

   printf("Done\nFile compiled successfully");

   fclose(inf);
   fclose(of);
}