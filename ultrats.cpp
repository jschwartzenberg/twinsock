// ULTRATS.CPP - A modification of TwinSock that adds scripting, etc.
// Modified by Misha Koshelev.
// All modifications by Misha Koshelev Copyright (C) 1995 by Misha Koshelev.
// All Rights Reserved.

// The original TwinSock comment is as follows:
   /*
    *  TwinSock - "Troy's Windows Sockets"
    *
    *  Copyright (C) 1994  Troy Rollo <troy@cbme.unsw.EDU.AU>
    *
    *  This program is free software; you can redistribute it and/or modify
    *  it under the terms of the license in the file LICENSE.TXT included
    *  with the TwinSock distribution.
    *
    *  This program is distributed in the hope that it will be useful,
    *  but WITHOUT ANY WARRANTY; without even the implied warranty of
    *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
    */

#include <windows.h>
#include <commdlg.h>
#include <winsock.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <mem.h>
#include "twinsock.h"
#include "tx.h"
#include "ultrats.h"
#include "tss.h"

HINSTANCE       hinst;

extern  void InitProtocol(void);
extern "C" far pascal RegisterManager(HWND hwnd);
extern "C" far pascal ResponseReceived(struct tx_request *ptxr);
extern "C" far pascal SetInitialised(void);
static void SendInitRequest(void);
void    Shutdown(void);
void    OpenPort(void);
extern  int     iPortChanged;

#define READ_MAX        1024
#define TIMER_ID_SEND           1
#define TIMER_ID_RECEIVE        2
#define TIMER_ID_FLUSH          3
#define TIMER_ID_BREAK          4
#define TIMER_ID_COMMCHECK      5

static  int     idComm;
static  HWND    hwnd;
static  BOOL    bFlushing = FALSE;
static  BOOL    bTerminal = TRUE;

#define SCREEN_COLUMNS  80
#define SCREEN_ROWS     25
static  char    aachScreen[SCREEN_ROWS][SCREEN_COLUMNS];
static  int     iScrollRow = 0;
static  int     iRow = 0, iColumn = 0;
static  int     cyRow, cxColumn;
#define ROW_INDEX(x)    ((x + iScrollRow) % SCREEN_ROWS)

static  char    const   achProtoInit[] = "@$TSStart$@";

extern  enum Encoding eLine;
static  int     iInitChar = 0;
long    nDiscarded = 0;
long    nBytesRecvd = 0;

// ***
// ULTRA Variables & Functions BEGIN
// ***

// ***
// SCRIPT STUFF
// ***

// The script timer
#define TIMER_ID_SCRIPT 5000

// Whether a script is loaded
int sloaded = FALSE;

// Whether or not we are waiting for a string
int waitfor = FALSE;

// Whether or not we are searching for a string
int find = FALSE;

// Whether the string searched for is found
int found = FALSE;

// Whether or not we are in user mode
int inuser = FALSE;

// The number of seconds still left to go for when we are saying something
// without echo (0 if we are not saying anything)
int saysecs = 0;

// Whether or not we are already in the WM_TIMER handler, so several do not
// execute
int inhandler = FALSE;

// The string we are waiting for and the position in the string we have matched to
char far strwait[160];
int strpos = 0;

// The string we are finding and the position in the string we have matched
char far strfind[160];
int strfpos = 0;

// A temporary string variable
char far tstr[160];

// The script file
HFILE sf;

// The script file name
char far sname[160];

// The user's response and the procedure created by MakeProcInstance
int response;
DLGPROC dlgProc;

// The offsets to which to jump (used by script)
unsigned long offset, offset2;

// ***
// VT100 Emulation Variables
// ***

// Whether or not we are in a VT100 sequence
int invt100 = FALSE;

// The string for the VT100 sequence we are reading, and the position
// in that string we are at
char vtstr[20];
int vtpos = 0;

// VT100 commands that we process
#define MUP 'A'
#define MDOWN 'B'
#define MLEFT 'D'
#define MRIGHT 'C'
#define MOVE 'H'
#define MOVE2 'f'
#define CLS 'J'
#define ERASELN 'K'

// VT100 commands that we ignore
#define SETMODE 'h'
#define RESTMODE 'l'
#define REMAP 'p'
#define SAVEPOS 's'
#define RESTPOS 'u'
#define REPPOS 'R'

typedef struct
{
   // The quick-run files and their descriptions
   char q1[80];
   char q1desc[80];
   char q2[80];
   char q2desc[80];
   char q3[80];
   char q3desc[80];
   char q4[80];
   char q4desc[80];
   char q5[80];
   char q5desc[80];

   // Whether certain VT100 codes should be processed or
   // discarded
   int mup,
       mdown,
       mleft,
       mright,
       move,
       cls,
       eraseln,
       setmode,
       restmode,
       remap,
       savepos,
       restpos,
       reppos;
} INIParams;

INIParams far defINI;

// Reads the initialization file and puts it into the structure
void LoadINI()
{
   GetPrivateProfileString("Script", "1", "", defINI.q1, 80, "ULTRATS.INI");
   GetPrivateProfileString("Script", "2", "", defINI.q2, 80, "ULTRATS.INI");
   GetPrivateProfileString("Script", "3", "", defINI.q3, 80, "ULTRATS.INI");
   GetPrivateProfileString("Script", "4", "", defINI.q4, 80, "ULTRATS.INI");
   GetPrivateProfileString("Script", "5", "", defINI.q5, 80, "ULTRATS.INI");

   GetPrivateProfileString("Script", "Desc1", "", defINI.q1desc, 80, "ULTRATS.INI");
   GetPrivateProfileString("Script", "Desc2", "", defINI.q2desc, 80, "ULTRATS.INI");
   GetPrivateProfileString("Script", "Desc3", "", defINI.q3desc, 80, "ULTRATS.INI");
   GetPrivateProfileString("Script", "Desc4", "", defINI.q4desc, 80, "ULTRATS.INI");

   defINI.mup = GetPrivateProfileInt("VT100", "MUP", 1, "ULTRATS.INI");
   defINI.mdown = GetPrivateProfileInt("VT100", "MDOWN", 1, "ULTRATS.INI");
   defINI.mleft = GetPrivateProfileInt("VT100", "MLEFT", 1, "ULTRATS.INI");
   defINI.mright = GetPrivateProfileInt("VT100", "MRIGHT", 1, "ULTRATS.INI");
   defINI.move = GetPrivateProfileInt("VT100", "MOVE", 1, "ULTRATS.INI");
   defINI.cls = GetPrivateProfileInt("VT100", "CLS", 1, "ULTRATS.INI");
   defINI.eraseln = GetPrivateProfileInt("VT100", "ERASELN", 1, "ULTRATS.INI");

   defINI.setmode = GetPrivateProfileInt("VT100", "SETMODE", 0, "ULTRATS.INI");
   defINI.restmode = GetPrivateProfileInt("VT100", "RESTMODE", 0, "ULTRATS.INI");
   defINI.remap = GetPrivateProfileInt("VT100", "REMAP", 0, "ULTRATS.INI");
   defINI.savepos = GetPrivateProfileInt("VT100", "SAVEPOS", 0, "ULTRATS.INI");
   defINI.restpos = GetPrivateProfileInt("VT100", "RESTPOS", 0, "ULTRATS.INI");
   defINI.reppos = GetPrivateProfileInt("VT100", "REPPOS", 0, "ULTRATS.INI");
}

// Writes an integer to the VT100 section of ULTRATS.INI, with integer
// variable name ivar, and entry name in entry
void writevt(int ivar, char *entry)
{
   char str[80];

   itoa(ivar, str, 10);
   WritePrivateProfileString("VT100", entry, str, "ULTRATS.INI");
}

// Saves the settings in the initialization structure into our INI file
void SaveINI()
{
   WritePrivateProfileString("Script", "1", defINI.q1, "ULTRATS.INI");
   WritePrivateProfileString("Script", "2", defINI.q2, "ULTRATS.INI");
   WritePrivateProfileString("Script", "3", defINI.q3, "ULTRATS.INI");
   WritePrivateProfileString("Script", "4", defINI.q4, "ULTRATS.INI");
   WritePrivateProfileString("Script", "5", defINI.q5, "ULTRATS.INI");

   WritePrivateProfileString("Script", "Desc1", defINI.q1desc, "ULTRATS.INI");
   WritePrivateProfileString("Script", "Desc2", defINI.q2desc, "ULTRATS.INI");
   WritePrivateProfileString("Script", "Desc3", defINI.q3desc, "ULTRATS.INI");
   WritePrivateProfileString("Script", "Desc4", defINI.q4desc, "ULTRATS.INI");

   writevt(defINI.mup, "MUP");
   writevt(defINI.mdown, "MDOWN");
   writevt(defINI.mleft, "MLEFT");
   writevt(defINI.mright, "MRIGHT");
   writevt(defINI.move, "MOVE");
   writevt(defINI.cls, "CLS");
   writevt(defINI.eraseln, "ERASELN");

   writevt(defINI.setmode, "SETMODE");
   writevt(defINI.restmode, "RESTMODE");
   writevt(defINI.remap, "REMAP");
   writevt(defINI.savepos, "SAVEPOS");
   writevt(defINI.restpos, "RESTPOS");
   writevt(defINI.reppos, "REPPOS");
}

// Sets the menu items from the initialization file
void setmenus()
{
   HMENU hMenu, hMenuPopup;
   char str[80];

   hMenu = GetMenu(hwnd);
   hMenuPopup = GetSubMenu(hMenu, 1);

   strcpy(str, "&1 ");
   strcat(str, defINI.q1desc);
   ModifyMenu(hMenuPopup, MN_S1, MF_BYCOMMAND, MN_S1, str);

   strcpy(str, "&2 ");
   strcat(str, defINI.q2desc);
   ModifyMenu(hMenuPopup, MN_S2, MF_BYCOMMAND, MN_S2, str);

   strcpy(str, "&3 ");
   strcat(str, defINI.q3desc);
   ModifyMenu(hMenuPopup, MN_S3, MF_BYCOMMAND, MN_S3, str);

   strcpy(str, "&4 ");
   strcat(str, defINI.q4desc);
   ModifyMenu(hMenuPopup, MN_S4, MF_BYCOMMAND, MN_S4, str);

   strcpy(str, "&5 ");
   strcat(str, defINI.q5desc);
   ModifyMenu(hMenuPopup, MN_S5, MF_BYCOMMAND, MN_S5, str);

   // Draw the menu bar
   DrawMenuBar(hwnd);
}

#pragma argsused
BOOL CALLBACK AboutWndProc(HWND hDlg, UINT message,
			WPARAM wParam, LPARAM lParam)
{
   switch  (message)
   {
      case WM_INITDIALOG: 
	 return (TRUE);

      case WM_COMMAND:
	 if (wParam == IDOK)
	 {
	    EndDialog (hDlg, TRUE);
	    return (TRUE);
	 }
	 break;
   }
   return (FALSE);               
}

#pragma argsused
BOOL CALLBACK PropWndProc(HWND hDlg, UINT message,
			WPARAM wParam, LPARAM lParam)
{
   int i;

   switch  (message)
   {
      case WM_INITDIALOG:
	 SetDlgItemText(hDlg, 100, defINI.q1);
	 SetDlgItemText(hDlg, 101, defINI.q1desc);
	 SetDlgItemText(hDlg, 102, defINI.q2);
	 SetDlgItemText(hDlg, 103, defINI.q2desc);
	 SetDlgItemText(hDlg, 104, defINI.q3);
	 SetDlgItemText(hDlg, 105, defINI.q3desc);
	 SetDlgItemText(hDlg, 106, defINI.q4);
	 SetDlgItemText(hDlg, 107, defINI.q4desc);
	 SetDlgItemText(hDlg, 108, defINI.q5);
	 SetDlgItemText(hDlg, 109, defINI.q5desc);

	 CheckDlgButton(hDlg, 111, defINI.mup);
	 CheckDlgButton(hDlg, 112, defINI.mdown);
	 CheckDlgButton(hDlg, 113, defINI.mleft);
	 CheckDlgButton(hDlg, 114, defINI.mright);
	 CheckDlgButton(hDlg, 115, defINI.move);
	 CheckDlgButton(hDlg, 116, defINI.cls);
	 CheckDlgButton(hDlg, 117, defINI.eraseln);

	 CheckDlgButton(hDlg, 118, defINI.setmode);
	 CheckDlgButton(hDlg, 119, defINI.restmode);
	 CheckDlgButton(hDlg, 120, defINI.remap);
	 CheckDlgButton(hDlg, 121, defINI.savepos);
	 CheckDlgButton(hDlg, 122, defINI.restpos);
	 CheckDlgButton(hDlg, 123, defINI.reppos);
	 return (TRUE);

      case WM_COMMAND:   
	 // If we need to enable or disable all VT commands, then
	 // do it
	 if (wParam == 110)
	 {
	    for (i=111; i<=123; i++)
	    {
	       CheckDlgButton(hDlg, i, IsDlgButtonChecked(hDlg, 110));
	    }
	 }

	 if (wParam == IDOK)
	 {
	    GetDlgItemText(hDlg, 100, defINI.q1, 80);
	    GetDlgItemText(hDlg, 101, defINI.q1desc, 80);
	    GetDlgItemText(hDlg, 102, defINI.q2, 80);
	    GetDlgItemText(hDlg, 103, defINI.q2desc, 80);
	    GetDlgItemText(hDlg, 104, defINI.q3, 80);
	    GetDlgItemText(hDlg, 105, defINI.q3desc, 80);
	    GetDlgItemText(hDlg, 106, defINI.q4, 80);
	    GetDlgItemText(hDlg, 107, defINI.q4desc, 80);
	    GetDlgItemText(hDlg, 108, defINI.q5, 80);
	    GetDlgItemText(hDlg, 109, defINI.q5desc, 80);

	    defINI.mup = IsDlgButtonChecked(hDlg, 111);
	    defINI.mdown = IsDlgButtonChecked(hDlg, 112);
	    defINI.mleft = IsDlgButtonChecked(hDlg, 113);
	    defINI.mright = IsDlgButtonChecked(hDlg, 114);
	    defINI.move = IsDlgButtonChecked(hDlg, 115);
	    defINI.cls = IsDlgButtonChecked(hDlg, 116);
	    defINI.eraseln = IsDlgButtonChecked(hDlg, 117);

	    defINI.setmode = IsDlgButtonChecked(hDlg, 118);
	    defINI.restmode = IsDlgButtonChecked(hDlg, 119);
	    defINI.remap = IsDlgButtonChecked(hDlg, 120);
	    defINI.savepos = IsDlgButtonChecked(hDlg, 121);
	    defINI.restpos = IsDlgButtonChecked(hDlg, 122);
	    defINI.reppos = IsDlgButtonChecked(hDlg, 123);
	 }
	 if (wParam == IDOK || wParam == IDCANCEL)
	 {
	    EndDialog (hDlg, wParam);
	    return (TRUE);
	 }
	 break;
   }
   return (FALSE);
}

#pragma argsused
BOOL CALLBACK InputWndProc(HWND hDlg, UINT message,
			WPARAM wParam, LPARAM lParam)
{
   int num;

   switch  (message)
   {
      case WM_INITDIALOG:
	 // Set the prompt
	 SetDlgItemText(hDlg, 100, tstr);
	 return (TRUE);

      case WM_COMMAND:   
	 if (wParam == IDOK)
	 {
	    // Set tstr to what the user typed
	    num = GetDlgItemText(hDlg, 101, tstr, 160);
	    tstr[num] = '\n';
	    tstr[num+1] = '\0';
	 }
	 if (wParam == IDOK || wParam == IDCANCEL)
	 {
	    EndDialog (hDlg, wParam);
	    return (TRUE);
	 }
	 break;
   }
   return (FALSE);
}

#pragma argsused
BOOL CALLBACK InputNEWndProc(HWND hDlg, UINT message,
			WPARAM wParam, LPARAM lParam)
{
   int num;

   switch  (message)
   {
      case WM_INITDIALOG:
	 // Set the prompt
	 SetDlgItemText(hDlg, 100, tstr);
	 return (TRUE);

      case WM_COMMAND:   
	 if (wParam == IDOK)
	 {
	    // Set tstr to what the user typed
	    num = GetDlgItemText(hDlg, 101, tstr, 160);
	    tstr[num] = '\n';
	    tstr[num+1] = '\0';
	 }
	 if (wParam == IDOK || wParam == IDCANCEL)
	 {
	    EndDialog (hDlg, wParam);
	    return (TRUE);
	 }
	 break;
   }
   return (FALSE);
}


// Gets the name of the script the user wants to open using the standard dialogs
void GetScriptName(HWND hWnd)
{
   OPENFILENAME ofn;
   int stat;
   char *fileTypes = "Compiled Script (*.TSC)\0*.TSC\0All Files (*.*)\0*.*\0";

   ofn.lStructSize = sizeof(OPENFILENAME);
   ofn.hwndOwner = hWnd; 
   ofn.hInstance = 0;
   ofn.lpstrFilter = (LPSTR)fileTypes;
   ofn.lpstrCustomFilter = NULL;
   ofn.nMaxCustFilter = 0;
   ofn.nFilterIndex = 1;
   ofn.lpstrFile = (LPSTR)sname; 
   ofn.nMaxFile = sizeof(sname);
   ofn.lpstrFileTitle = NULL;
   ofn.nMaxFileTitle = 0;
   ofn.lpstrInitialDir = NULL;
   ofn.lpstrTitle = "Run...";
   ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
   ofn.nFileOffset = 0;
   ofn.nFileExtension = 0;
   ofn.lpstrDefExt = "*";
   ofn.lCustData = NULL;
   ofn.lpfnHook = NULL;
   ofn.lpTemplateName = NULL;

   GetOpenFileName(&ofn);
}

// Starts the script timer
void SetScriptTimer(void)
{
   SetTimer(hwnd, TIMER_ID_SCRIPT, 1000, 0);
}

// Ends the script timer
void KillScriptTimer(void)
{
   KillTimer(hwnd, TIMER_ID_SCRIPT);
}

// Stops execution of the current script
void StopScript()
{
   sloaded = waitfor = inhandler = FALSE;
   _lclose(sf);
   KillScriptTimer();
}

// ***
// ULTRA Variables & Functions END
// ***

extern  void PacketReceiveData(void *pvData, int iLen);

void    SetTransmitTimeout(void)
{
	KillTimer(hwnd, TIMER_ID_SEND);
	SetTimer(hwnd, TIMER_ID_SEND, 10000, 0);
}

void    KillTransmitTimeout(void)
{
	KillTimer(hwnd, TIMER_ID_SEND);
}

void    SetReceiveTimeout(void)
{
	KillTimer(hwnd, TIMER_ID_RECEIVE);
	SetTimer(hwnd, TIMER_ID_RECEIVE, 1500, 0);
}

void    KillReceiveTimeout(void)
{
	KillTimer(hwnd, TIMER_ID_RECEIVE);
}

void    FlushInput(void)
{
	KillTimer(hwnd, TIMER_ID_FLUSH);
	bFlushing = TRUE;
	SetTimer(hwnd, TIMER_ID_FLUSH, 1500, 0);
}

static  void
SendToScreen(char c)
{

	RECT    rcClient;
	RECT    rcRedraw;

// ***
// VT100 emulation begin
// ***
	char str[40];
	int i;


	// If the character is a n ESC, then start reading the
	// vt100 sequence
	if (!invt100 && c == 27)
	{
	   invt100 = TRUE;
	   vtpos = 0;
	   return;
	}

	// If we are in a VT100 sequence and the character we are
	// reading is not a letter, then add it to the sequence,
	// otherwise, finish the VT100 sequence and perform the
	// specified operation
	if (invt100)
	{
	   if (!isupper(c) && !islower(c))
	   {
	      vtstr[vtpos] = c;
	      vtpos++;
	   }
	   else
	   {
	      invt100 = FALSE;
	      vtstr[vtpos] = c;
	      switch (vtstr[vtpos])
	      {
		 case MUP:
		    if (!defINI.mup)
		       return;

		    if (vtstr[1] == MUP)
		    {
		       iRow--;
		    }
		    else
		    {
		       iRow -= atoi(vtstr);
		    }
		    break;

		 case MDOWN:
		    if (!defINI.mdown)
		       return;

		    if (vtstr[1] == MDOWN)
		    {
		       iRow++;
		    }
		    else
		    {
		       iRow += atoi(vtstr);
		    }
		    break;

		 case MLEFT:
		    if (!defINI.mleft)
		       return;

		    if (vtstr[1] == MLEFT)
		    {
		       iColumn--;
		    }
		    else
		    {
		       iColumn -= atoi(vtstr);
		    }
		    break;

		 case MRIGHT:
		    if (!defINI.mright)
		       return;

		    if (vtstr[1] == MRIGHT)
		    {
		       iColumn++;
		    }
		    else
		    {
		       iColumn += atoi(vtstr);
		    }
		    break;

		 case MOVE:
		 case MOVE2:
		    if (!defINI.move)
		       return;

		    iRow = atoi(&vtstr[1]);
		    i = 1;
		    while (vtstr[i] != ';')
		       i++;
		    iColumn = atoi(&vtstr[i+1]);
		    break;

		 case CLS:
		    if (!defINI.cls)
		       return;

		    memset(aachScreen, 0x20, SCREEN_ROWS*SCREEN_COLUMNS);
		    InvalidateRect(hwnd, NULL, TRUE);
		    break;

		 case ERASELN:
		    if (!defINI.eraseln)
		       return;

		    memset(&aachScreen[iRow][iColumn], 0x20, SCREEN_COLUMNS-iColumn-1);
		    UpdateWindow(hwnd);
		    break;

		 case SETMODE:
		 case RESTMODE:
		 case REMAP:
		 case SAVEPOS:
		 case RESTPOS:
		 case REPPOS:
		    break;

		 default:
#ifdef DEBUG
		    sprintf(str, "Unknown VT100 command %s", vtstr);
		    MessageBox(hwnd, str,
			       "Ultra TwinSock", MB_OK | MB_ICONINFORMATION);
#endif
		    break;
	      }
	   }

	   return;
	}

// ***
// VT100 emulation end
// ***

	if (c == '\r')
	{
		iColumn = 0;
	}
	else if (c == '\b')
	{
		if (iColumn > 0)
			iColumn--;
	}
	else if (c == '\n')
	{
		if (iRow < SCREEN_ROWS - 1)
		{
			iRow++;
		}
		else
		{
			memset(aachScreen[iScrollRow], 0x20, sizeof(aachScreen[iScrollRow]));
			iScrollRow++;
			while (iScrollRow >= SCREEN_ROWS)
				iScrollRow -= SCREEN_ROWS;
			GetClientRect(hwnd, &rcClient);
			ScrollWindow(hwnd, 0, -cyRow, &rcClient, 0);
			UpdateWindow(hwnd);
		}
	}
	else if (c >= 0x20 && c <= 0x7e)
	{
		aachScreen[ROW_INDEX(iRow)][iColumn] = c;
		rcRedraw.top = iRow * cyRow;
		rcRedraw.left = iColumn * cxColumn;
		rcRedraw.bottom = rcRedraw.top + cyRow;
		rcRedraw.right = rcRedraw.left + cxColumn;
		InvalidateRect(hwnd, &rcRedraw, TRUE);
		if (iColumn < SCREEN_COLUMNS - 1)
			iColumn++;
	}
}

// ***
// ULTRA Modified for scripting
// ***
static  void AddChar(char c)
{
	SendToScreen(c);

// ***
// WAITFOR Keyword Begin
// ***
	if (waitfor)
	{
	   if (strpos && c != strwait[strpos])
	      strpos = 0;

	   if (c == strwait[strpos])
	   {
	      strpos++;
	      if (strwait[strpos] == 0)
	      {
		 waitfor = FALSE;
		 found = FALSE;
	      }
	   }
	}
// ***
// WAITFOR Keyword End
// ***

// ***
// WAITFIND Keyword Begin
// ***
	if (find)
	{
	   if (strfpos && c != strfind[strfpos])
	      strfpos = 0;

	   if (c == strfind[strfpos])
	   {
	      strfpos++;
	      if (strfind[strfpos] == 0)
	      {
		 waitfor = FALSE;
		 found = TRUE;
	      }
	   }
	}
// ***
// WAITFIND Keyword End
// ***

	if (c == achProtoInit[iInitChar])
	{
		iInitChar++;
		if (iInitChar == strlen(achProtoInit))
		{
// ***
// WAITFOR Modified for scripts BEGIN
// ***
			if (sloaded)
			   StopScript();
// ***
// WAITFOR Modified for scripts END
// ***
			iInitChar = 0;
			bTerminal = 0;
			RegisterManager(hwnd);
			InitProtocol();
			SendInitRequest();
		}
	}
	else if (iInitChar == 9 && isdigit(c))
	{
		eLine = (enum Encoding) (c - '0');
	}
	else if (iInitChar)
	{
		iInitChar = 0;
		eLine = E_6Bit;
	}
}

static  void    DoReading(void)
{
	static  char    achBuffer[READ_MAX];
	static  BOOL    bAlreadyHere = FALSE;
	int     nRead;
	COMSTAT cs;
	int     i;

	if (bAlreadyHere)
		return;
	bAlreadyHere = TRUE;
	do
	{
		nRead = ReadComm(idComm, achBuffer, READ_MAX);
		if (nRead <= 0)
		{
			GetCommError(idComm, &cs);
			nRead = -nRead;
		}
		if (nRead)
		{
			if (bTerminal)
			{
				HideCaret(hwnd);
				for (i = 0; i < nRead; i++)
					AddChar(achBuffer[i]);
				SetCaretPos(iColumn * cxColumn, iRow * cyRow);
				ShowCaret(hwnd);
			}
			else if (bFlushing)
			{
				nDiscarded += nRead;
				FlushInput();
			}
			else
			{
				nBytesRecvd += nRead;
				PacketReceiveData(achBuffer, nRead);
			}
		}
	} while (nRead || cs.cbInQue);
	bAlreadyHere = FALSE;
}

int     SendData(void *pvData, int iDataLen)
{
	int     nWritten;
	COMSTAT cs;
	int     iLen;

	if (bFlushing)
		return iDataLen; /* Lie */
	iLen = iDataLen;
	do
	{
		nWritten = WriteComm(idComm, pvData, iLen);
		if (nWritten < 0)
		{
			GetCommError(idComm, &cs);
			nWritten = -nWritten;
		}
		iLen -= nWritten;
		pvData = (char *) pvData + nWritten;
	} while (iLen);
	return iDataLen;
}

void
PaintScreen(    HWND    hWnd)
{
	int     i, iRow;
	int     iCol;
	TEXTMETRIC tm;
	HFONT   hfontOld;
	HFONT   hfontFixed;
	PAINTSTRUCT ps;
	int     cyHeight;
	int     yPos;

	BeginPaint(hWnd, &ps);
	hfontFixed = (HFONT) GetStockObject(SYSTEM_FIXED_FONT);
	hfontOld = (HFONT) SelectObject(ps.hdc, (HGDIOBJ) hfontFixed);
	GetTextMetrics(ps.hdc, &tm);
	cyHeight = tm.tmHeight + tm.tmExternalLeading;
	for (i = 0; i < SCREEN_ROWS; i++)
	{
		iRow = ROW_INDEX(i);
		TextOut(ps.hdc, 0, i * cyRow, aachScreen[iRow], SCREEN_COLUMNS);
	}
	SelectObject(ps.hdc, (HGDIOBJ) hfontOld);
	EndPaint(hWnd, &ps);
}

LRESULT CALLBACK _export
WindowProc(     HWND    hWnd,
		UINT    wMsg,
		WPARAM  wParam,
		LPARAM  lParam)
{
	char    c;

	switch(wMsg)
	{
// ***
// ULTRA MESSAGE HANDLERS BEGIN
// ***
	case WM_SYSCOMMAND:
	    // If close, then do close
	    if (wParam == SC_CLOSE)
	    {
	       response = MessageBox(hWnd, "Are you sure you want to exit?",
			  "Ultra TwinSock", MB_YESNO | MB_ICONQUESTION);

	       if (response == IDYES)
	       {
		  PostQuitMessage(0);
	       }

	       return (FALSE);
	    }

	    break;

// ***
// ULTRA MESSAGE HANDLERS END
// ***

	case WM_COMMAND:
	    switch(wParam)
	    {
// ***
// ULTRA COMMAND HANDLERS BEGIN
// ***
	       case 103:
		     response = MessageBox(hWnd, "Are you sure you want to exit?",
		     "Ultra TwinSock", MB_YESNO | MB_ICONQUESTION);

		     if (response == IDYES)
		     {
			PostQuitMessage(0);
		     }
		     break;

	       case MN_HHELP:
		 // Load TwinSock help
		 WinHelp(hwnd, "TWINSOCK.HLP", HELP_CONTENTS, 0L);
		 break;

	       // Handle Ultra TwinSock help
	       case 5009:
		 MessageBox(hwnd, "For help on using Ultra TwinSock, please see the file README.TXT",
			    "Ultra TwinSock", MB_OK | MB_ICONINFORMATION);
		 break;

	       case MN_HABOUT:
		 dlgProc = (DLGPROC)MakeProcInstance((FARPROC)AboutWndProc, hinst);
		 DialogBox(hinst, MAKEINTRESOURCE(dlg_About), hWnd, dlgProc);
		 FreeProcInstance((FARPROC)dlgProc);
		 break;

	       case MN_PROP:
		 dlgProc = (DLGPROC)MakeProcInstance((FARPROC)PropWndProc, hinst);
		 if (DialogBox(hinst, MAKEINTRESOURCE(dlg_Properties), hWnd, dlgProc) ==
		     IDOK)
		 {
		    setmenus();
		    SaveINI();
		 }
		 FreeProcInstance((FARPROC)dlgProc);
		 break;

	    case MN_SCOMP:
		 // Tell the user that the compiler can only be run
		 // from DOS
		 MessageBox(hwnd, "The compiler is not internally supported in this version.\
Use TSSCC.EXE instead", "Ultra TwinSock", MB_OK | MB_ICONINFORMATION);
		 break;

	    case MN_SRUN:
		 if (!bTerminal)
		 {
		    MessageBox(hwnd, "Cannot run a script while connected, disconnect first",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (sloaded)
		 {
		    MessageBox(hwnd, "Cannot run a script while another script is executing",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
				  
		 GetScriptName(hWnd);

		 sf = _lopen(sname, READ);
		 if (sf == HFILE_ERROR)
		 {
		    MessageBox(hWnd, "Unable to open specified script", "Ultra TwinSock", MB_OK | MB_ICONSTOP);
		    return (FALSE);
		 }

		 SetScriptTimer();

		 sloaded = TRUE;
		 break;

	    case MN_S1:
		 if (!bTerminal)
		 {
		    MessageBox(hwnd, "Cannot run a script while connected, disconnect first",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (sloaded)
		 {
		    MessageBox(hwnd, "Cannot run a script while another script is executing",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (!strcmp(defINI.q1, ""))
		 {
		    MessageBox(hwnd, "A script is not defined for this quick-run item",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
				  
		 strcpy(sname, defINI.q1);

		 sf = _lopen(sname, READ);
		 if (sf == HFILE_ERROR)
		 {
		    MessageBox(hWnd, "Unable to open specified script", "Ultra TwinSock", MB_OK | MB_ICONSTOP);
		    return (FALSE);
		 }

		 SetScriptTimer();

		 sloaded = TRUE;
		 break;

	    case MN_S2:
		 if (!bTerminal)
		 {
		    MessageBox(hwnd, "Cannot run a script while connected, disconnect first",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (sloaded)
		 {
		    MessageBox(hwnd, "Cannot run a script while another script is executing",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (!strcmp(defINI.q2, ""))
		 {
		    MessageBox(hwnd, "A script is not defined for this quick-run item",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
				  
		 strcpy(sname, defINI.q2);

		 sf = _lopen(sname, READ);
		 if (sf == HFILE_ERROR)
		 {
		    MessageBox(hWnd, "Unable to open specified script", "Ultra TwinSock", MB_OK | MB_ICONSTOP);
		    return (FALSE);
		 }

		 SetScriptTimer();

		 sloaded = TRUE;
		 break;

	    case MN_S3:
		 if (!bTerminal)
		 {
		    MessageBox(hwnd, "Cannot run a script while connected, disconnect first",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (sloaded)
		 {
		    MessageBox(hwnd, "Cannot run a script while another script is executing",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (!strcmp(defINI.q3, ""))
		 {
		    MessageBox(hwnd, "A script is not defined for this quick-run item",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
				  
		 strcpy(sname, defINI.q3);

		 sf = _lopen(sname, READ);
		 if (sf == HFILE_ERROR)
		 {
		    MessageBox(hWnd, "Unable to open specified script", "Ultra TwinSock", MB_OK | MB_ICONSTOP);
		    return (FALSE);
		 }

		 SetScriptTimer();

		 sloaded = TRUE;
		 break;

	    case MN_S4:
		 if (!bTerminal)
		 {
		    MessageBox(hwnd, "Cannot run a script while connected, disconnect first",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (sloaded)
		 {
		    MessageBox(hwnd, "Cannot run a script while another script is executing",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (!strcmp(defINI.q4, ""))
		 {
		    MessageBox(hwnd, "A script is not defined for this quick-run item",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
				  
		 strcpy(sname, defINI.q4);

		 sf = _lopen(sname, READ);
		 if (sf == HFILE_ERROR)
		 {
		    MessageBox(hWnd, "Unable to open specified script", "Ultra TwinSock", MB_OK | MB_ICONSTOP);
		    return (FALSE);
		 }

		 SetScriptTimer();

		 sloaded = TRUE;
		 break;

	    case MN_S5:
		 if (!bTerminal)
		 {
		    MessageBox(hwnd, "Cannot run a script while connected, disconnect first",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (sloaded)
		 {
		    MessageBox(hwnd, "Cannot run a script while another script is executing",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
		 if (!strcmp(defINI.q5, ""))
		 {
		    MessageBox(hwnd, "A script is not defined for this quick-run item",
			       "Ultra TwinSock", MB_OK);
		    return (FALSE);
		 }
				  
		 strcpy(sname, defINI.q5);

		 sf = _lopen(sname, READ);
		 if (sf == HFILE_ERROR)
		 {
		    MessageBox(hWnd, "Unable to open specified script", "Ultra TwinSock", MB_OK | MB_ICONSTOP);
		    return (FALSE);
		 }

		 SetScriptTimer();

		 sloaded = TRUE;
		 break;

// ***
// ULTRA COMMAND HANDLERS END
// ***
		case 100:
			if (CommsEdit(hWnd))
			{
				if (iPortChanged)
				{
					CloseComm(idComm);
					OpenPort();
				}
				else
				{
					InitComm(idComm);
				}
			}
			break;

		case 101:
			if (bTerminal)
			{
				bTerminal = 0;
				RegisterManager(hwnd);
				SendInitRequest();
			}
			break;

		case 102:
			if (!bTerminal)
			{
				Shutdown();
				SendData("\030\030\030\030\030", 5);
			}
			break;

		case 104:
			if (bTerminal)
			{
				SetCommBreak(idComm);
				SetTimer(hwnd, TIMER_ID_BREAK, 1500, 0);
			}
			break;

		case 105:
			if (bTerminal)
				DialNumber(hWnd);
			break;

		case 106:
			if (!bTerminal)
				ShowProtoInfo(hWnd);
			break;

		case 201:
			About(hWnd);
			break;
		}
		break;

	case WM_COMMNOTIFY:
		switch(LOWORD(lParam))
		{
		case CN_RECEIVE:
			DoReading();
			break;
		}
		break;

	case WM_CHAR:
		if (bTerminal)
		{
		   // If we are in a script, then end the script
		   // (with confirmation first) execept for
		   // if we are not in user mode
		   if (sloaded && !inuser)
		   {
		      response = MessageBox(hWnd, "Are you sure you want to stop the script?",
				 "Ultra TwinSock", MB_YESNO | MB_ICONQUESTION);
		      if (response == IDYES)
		      {
			 StopScript();
			 c = wParam;
			 SendData(&c, 1);
		      }
		   }
		   else
		   {
		      c = wParam;
		      SendData(&c, 1);
		   }
		}
		break;

	case WM_TIMER:
		switch(wParam)
		{
		case TIMER_ID_SEND:
			TimeoutReceived();
			break;

		case TIMER_ID_RECEIVE:
			KillTimer(hWnd, TIMER_ID_RECEIVE);
			PacketReceiveData(0, 0);
			break;

// ***
// ULTRA SCIRPT BEGIN
// ***
		case TIMER_ID_SCRIPT: 

	 char ch;
	 int i;

	 if (sloaded && !inhandler && !waitfor)
	 {
	    // We are in the handler
	    inhandler = TRUE;

	    // If we were doing WAITFIND and we found the string,
	    // jump to the label at offset2
	    if (find)
	    {
	       if (found)
	       {
		  _llseek(sf, offset2, 0);
	       }
	       else
	       {
		  _llseek(sf, offset, 0);
	       }

	       find = found = inhandler = FALSE;

	       return (FALSE);
	    }

	    // If we were in user mode, turn it off
	    if (inuser)
	       inuser = FALSE;

	    _lread(sf, &ch, 1);

DoCommand:
	    // Clear tstr
	    strcpy(tstr, "");

	    switch (ch)
	    {  
	       case CEND:
		  StopScript();
		  break;

	       case CGOTO:
		  // Read in the location to jump to
		  _lread(sf, &offset, 4);

		  // And jump to it
		  _llseek(sf, offset, 0);
		  break;

	       case CSAY:
		  // Read in the string
		  _lread(sf, &ch, 1);
		  _lread(sf, tstr, ch);

		  // And display a message box with that string until the
		  // user presses OK
		  MessageBox(hwnd, tstr, sname, MB_OK);
		  break;

	       case CYESNO:
		  // Read in the prompt
		  _lread(sf, &ch, 1);
		  _lread(sf, tstr, ch);

		  // Read in the offsets
		  _lread(sf, &offset, 4);
		  _lread(sf, &offset2, 4);

		  // And display a YES/NO message box to the user with
		  // that prompt
		  response = MessageBox(hwnd, tstr, sname, MB_YESNO);

		  // If the user answered yes, go to the first offset specified
		  if (response == IDYES)
		  {
		     _llseek(sf, offset, 0);
		  }
		  // Otherwise go to the second
		  else
		  {
		     _llseek(sf, offset2, 0);
		  }
		  break;

	       case CSEND:
		  // Read in the string
		  _lread(sf, &ch, 1);
		  _lread(sf, tstr, ch);

		  // And send it to the COM port
		  SendData(tstr, ch);
		  break;

	       case CSENDI:
		  // Read in the prompt
		  _lread(sf, &ch, 1);
		  _lread(sf, tstr, ch);

		  // Get the string (via dialog box)
		  dlgProc = (DLGPROC)MakeProcInstance((FARPROC)InputWndProc, hinst);
		  DialogBox(hinst, MAKEINTRESOURCE(dlg_Input), hWnd, dlgProc);
		  FreeProcInstance((FARPROC)dlgProc);

		  // And send it to the COM port
		  SendData(tstr, strlen(tstr));
		  break;

	       case CSENDINE:
		  // Read in the prompt
		  _lread(sf, &ch, 1);
		  _lread(sf, tstr, ch);

		  // Get the string (via no echo dialog box)
		  dlgProc = (DLGPROC)MakeProcInstance((FARPROC)InputNEWndProc, hinst);
		  DialogBox(hinst, MAKEINTRESOURCE(dlg_InputNE), hWnd, dlgProc);
		  FreeProcInstance((FARPROC)dlgProc);

		  // And send it to the COM port
		  SendData(tstr, strlen(tstr));
		  break;

	       case CWAITFOR:
		  // Read in the string
		  _lread(sf, &ch, 1);
		  _lread(sf, strwait, ch);

		  strwait[ch] = 0;
		  strpos = 0;

		  waitfor = TRUE;
		  break;

	       case CWAITFIND:
		  // Read in the string to wait for
		  _lread(sf, &ch, 1);
		  _lread(sf, strwait, ch);

		  strwait[ch] = 0;
		  strpos = 0;

		  // And the string to find
		  _lread(sf, &ch, 1);
		  _lread(sf, strfind, ch);

		  strfind[ch] = 0;
		  strfpos = 0;

		  // Read in the wait label offset and the find label
		  // offset
		  _lread(sf, &offset, 4);
		  _lread(sf, &offset2, 4);

		  waitfor = find = TRUE;
		  found = FALSE;
		  break;

	       case CUSER:
		  // Read in the wait string
		  _lread(sf, &ch, 1);
		  _lread(sf, strwait, ch);

		  strwait[ch] = 0;
		  strpos = 0;

		  // Set the in user mode and waitfor to true
		  inuser = waitfor = TRUE;
		  break;

	       default:
		  // If this is an unknown command, then inform the user and stop the script
		  StopScript();
		  MessageBox(hWnd, "Unknown command encountered", "Ultra TwinSock", MB_OK | MB_ICONINFORMATION);
	    }

	    // Peek at the next instruction, and if it is
	    // a wait, then do it now if we can
	    _lread(sf, &ch, 1);
	    if (sloaded && !waitfor &&
		ch == CWAITFOR || ch == CWAITFIND)
	    {
	       goto DoCommand;
	    }
	    else
	    {
	       _llseek(sf, -1L, 1);
	    }

	    // Done handling
	    inhandler = FALSE;
	 }
	 break;
// ***
// ULTRA SCRIPT END
// ***

		case TIMER_ID_FLUSH:
			KillTimer(hWnd, TIMER_ID_FLUSH);
			bFlushing = FALSE;
			break;

		case TIMER_ID_BREAK:
			ClearCommBreak(idComm);
			KillTimer(hWnd, TIMER_ID_BREAK);
			break;

		case TIMER_ID_COMMCHECK:
			DoReading();
			break;
		}
		break;

	case WM_PAINT:
		PaintScreen(hWnd);
		return 0;
		break;

	case WM_SETFOCUS:
		CreateCaret(hWnd, 0, cxColumn, cyRow);
		SetCaretPos(iColumn * cxColumn, iRow * cyRow);
		ShowCaret(hWnd);
		break;

	case WM_KILLFOCUS:
		DestroyCaret();
		break;

	case WM_USER:
		PacketTransmitData((void *) lParam, wParam, 0);
		DoReading();
		break;

	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hWnd, wMsg, wParam, lParam);
}

void
DataReceived(void *pvData, int iLen)
{
	static  struct tx_request *ptxr = 0;
	static  struct tx_request txrHeader;
	static  int     nBytes = 0;
	short   nPktLen;
	enum Functions ft;
	int     nCopy;
	int     i;

	while (iLen)
	{
		if (nBytes < 10)
		{
			nCopy = 10 - nBytes;
			if (nCopy > iLen)
				nCopy = iLen;
			memcpy((char *) &txrHeader + nBytes, pvData, nCopy);
			nBytes += nCopy;
			pvData = (char *) pvData + nCopy;
			iLen -= nCopy;
			if (nBytes == 10)
			{
				nPktLen = ntohs(txrHeader.nLen);
				ptxr = (struct tx_request *) malloc(sizeof(struct tx_request) + nPktLen - 1);
				memcpy(ptxr, &txrHeader, 10);
			}
		}
		if (nBytes >= 10)
		{
			nPktLen = ntohs(txrHeader.nLen);
			ft = (enum Functions) ntohs(txrHeader.iType);
			nCopy = nPktLen - nBytes;
			if (nCopy > iLen)
				nCopy = iLen;
			if (nCopy)
			{
				memcpy((char *) ptxr + nBytes, pvData, nCopy);
				nBytes += nCopy;
				pvData = (char *) pvData + nCopy;
				iLen -= nCopy;
			}
			if (nBytes == nPktLen)
			{
				if (ft == FN_Init)
				{
					if (ptxr->id == -1)
					{
						SetWindowText(hwnd, "Ultra TwinSock - Connected");
						CloseWindow(hwnd);
						SetInitialised();
					}
				}
				else if (ft == FN_Message)
				{
					SendToScreen('\r');
					SendToScreen('\n');
					for (i = 0; i < nPktLen - 10; i++)
						SendToScreen(ptxr->pchData[i]);
				}
				else
				{
					ResponseReceived(ptxr);
				}
				free(ptxr);
				ptxr = 0;
				nBytes = 0;
			}
		}
	}
}

static void
SendInitRequest(void)
{
	struct  tx_request txr;

	txr.iType = htons(FN_Init);
	txr.nArgs = 0;
	txr.nLen = htons(10);
	txr.id = -1;
	txr.nError = 0;
	PacketTransmitData(&txr, 10, 0);
}

void
Shutdown(void)
{
	bTerminal = 1;
	SetWindowText(hwnd, "Ultra TwinSock - No Connection");
	KillTimer(hwnd, TIMER_ID_SEND);
	KillTimer(hwnd, TIMER_ID_RECEIVE);
	KillTimer(hwnd, TIMER_ID_FLUSH);
	ReInitPackets();
}

void
OpenPort(void)
{
	char    achProfileEntry[256];
	char    achMsgBuf[512];
	char    *pchError;

	do
	{
		iPortChanged = 0;
		GetPrivateProfileString("Config", "Port", "COM1", achProfileEntry, 256, "TWINSOCK.INI");
		idComm = OpenComm(achProfileEntry, 16384, 16384);
		if (idComm < 0)
		{
		switch(idComm)
		{
			case IE_BADID:
				pchError = "No such device";
				break;

			case IE_BAUDRATE:
				pchError = "Speed not supported";
				break;

			case IE_BYTESIZE:
				pchError = "Byte size not supported";
				break;

			case IE_DEFAULT:
				pchError = "Bad default parameters for port";
				break;

			case IE_HARDWARE:
				pchError = "Port in use";
				break;

			case IE_MEMORY:
				pchError = "Out of memory";
				break;

			case IE_NOPEN:
				pchError = "Device not open";
				break;

			case IE_OPEN:
				pchError = "Device is already open";
				break;

			default:
				pchError = "Error Unknown";
				break;
			}
			sprintf(achMsgBuf,
				"Unable to open port \"%s\": %s",
				achProfileEntry,
				pchError);              
			if (MessageBox(0, achMsgBuf, "TwinSock", MB_OKCANCEL) == IDCANCEL)
				exit(1);
			if (!CommsEdit(hwnd))
				exit(1);
		}
	} while (idComm < 0);

	InitComm(idComm);
	EnableCommNotification(idComm, hwnd, 1, 0);
	SetTimer(hwnd, TIMER_ID_COMMCHECK, 1000, 0);
}

#pragma argsused
int     far     pascal
WinMain(HINSTANCE       hInstance,
	HINSTANCE       hPrec,
	LPSTR           lpCmdLine,
	int             nShow)
{
	WNDCLASS wc;
	MSG     msg;
	char    *pchError;
	TEXTMETRIC      tm;
	HDC     hdc;
	HWND    hwndDesktop;
	HFONT   hfontOld;
	HFONT   hfontFixed;

	hinst = hInstance;
	hwndDesktop = GetDesktopWindow();
	hdc = GetDC(hwndDesktop);
	hfontFixed = (HFONT) GetStockObject(SYSTEM_FIXED_FONT);
	hfontOld = (HFONT) SelectObject(hdc, (HGDIOBJ) hfontFixed);
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, (HGDIOBJ) hfontOld);
	ReleaseDC(hwnd, hdc);
	cxColumn = tm.tmAveCharWidth;
	cyRow = tm.tmHeight + tm.tmExternalLeading;

	memset(aachScreen, 0x20, sizeof(aachScreen));
	wc.style = 0;
	wc.lpfnWndProc = WindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(ico_Main));
	wc.hCursor = 0;
	wc.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName = "TS_MENU";
	wc.lpszClassName = "TwinSock Communications";
	RegisterClass(&wc);
	hwnd = CreateWindow(    "TwinSock Communications",
				"Ultra TwinSock - No Connection",
				WS_OVERLAPPED |
				 WS_CAPTION |
				 WS_SYSMENU |
				 WS_MINIMIZEBOX |
				 WS_VISIBLE,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				cxColumn * SCREEN_COLUMNS + 2,
				cyRow * SCREEN_ROWS + 4 +
				  GetSystemMetrics(SM_CYCAPTION) +
				  GetSystemMetrics(SM_CYMENU),
				0,
				0,
				hInstance,
				0);

	OpenPort();

	// Load BWCC.DLL
	HINSTANCE bwccDLL;

	bwccDLL = LoadLibrary("BWCC.DLL");
	if (bwccDLL < HINSTANCE_ERROR)
	{
	   MessageBox(NULL, "Unable to load library BWCC.DLL",
		      "Ultra TwinSock", MB_OK | MB_SYSTEMMODAL);
	   return (FALSE);
	}

	// Load the initialization file
	LoadINI();

	// Set the menus
	setmenus();

	// Message processing loop
	while (GetMessage(&msg, 0, 0, 0))
	{
	   TranslateMessage(&msg);
	   DispatchMessage(&msg);
	}
}
