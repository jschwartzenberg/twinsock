
#include <winsock.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dir.h>
#include "script.h"


// static data & structures

#define MAXTOKENLEN	80
#define MAXLINELEN	120
#define MAXSCRIPTLINES	40


static HACCEL	hAccel;

static	BOOL	 bRunningScript = FALSE, bEof = FALSE;
static	int	 iScriptLine = 0, iScriptIndex = 0, iLineNum = 0;
static	int	 iNextMenuId = 2000;
static  FILE	*fScriptFile;

static  char	token[MAXTOKENLEN];
static  char	input_line[MAXLINELEN], *input_line_p = 0;

struct ScriptLine { char *out, *in; int time; };

struct Script
{
	char *name;
	int accel, vkeys;		// vkey is CONTROL|SHIFT|ALT
	BOOL onsysmenu, disconnect, quitatend;
	int menuid;
	struct Script *next;

	struct ScriptLine *text;
};

static struct Script *RunningScript;
static struct Script *FirstScript = 0;


// procedures

static void KillScriptTimeout(HWND hWnd)
{
	KillTimer(hWnd, TIMER_ID_SCRIPT);
}

static void SetScriptTimeout(HWND hWnd, unsigned int seconds)
{
	KillScriptTimeout(hWnd);
	if (seconds > 0) SetTimer(hWnd, TIMER_ID_SCRIPT, 1000U * seconds, 0);
}

static void SendScriptLine(HWND hWnd)
{
	char *s = RunningScript->text[iScriptLine].out;
	SendData(s, strlen(s));
	SetScriptTimeout(hWnd, RunningScript->text[iScriptLine].time);
}

static void InterpolateMessage(char *msg)
{
	extern void SendToScreen(char c);
	int i;
	for (i=0; msg[i] != 0; i++) SendToScreen(msg[i]);
}

static void StartScript(HWND hWnd, struct Script *script)
{
	RunningScript = script;
	iScriptLine = iScriptIndex = 0;
	bRunningScript = TRUE;
	SendScriptLine(hWnd);
}


void CheckScripts(HWND hWnd, char c)
{
	char *s;
	if (! bRunningScript) return;
	s = RunningScript->text[iScriptLine].in;
	if (c == s[iScriptIndex])
	{
		iScriptIndex += 1;
		if (iScriptIndex >= strlen(s))
		{
			iScriptLine += 1;
			iScriptIndex = 0;
			SendScriptLine(hWnd);
			if (RunningScript->text[iScriptLine].in == NULL)
			{
				bRunningScript = FALSE;
				if (RunningScript->quitatend) PostQuitMessage(0);
			}
		}
	}
	else iScriptIndex = 0;
}

void PerhapsKillScript(HWND hWnd)
{
	if (bRunningScript)
	{
		bRunningScript = FALSE;
		KillScriptTimeout(hWnd);
		InterpolateMessage("\r\n** Script Cancelled **\r\n");
	}
}

void ScriptTimeOut(HWND hWnd)
{
	KillScriptTimeout(hWnd);
	bRunningScript = FALSE;
	MessageBeep(MB_ICONEXCLAMATION);
	InterpolateMessage("\r\n** Script Timed Out **\r\n");
}

void CheckScriptSysCommands(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	struct Script *s;
	for (s = FirstScript; s; s = s->next) {
		if (s->onsysmenu && s->menuid == wParam) {
			if (IsIconic(hWnd)) ShowWindow(hWnd, SW_RESTORE);
			SendMessage(hWnd, WM_COMMAND, wParam, lParam);
			break;
			}
		}
}

BOOL CheckScriptCommands(HWND hWnd, WPARAM wParam, BOOL *bpTerminal)
{
	struct Script *s;
	for (s = FirstScript; s; s = s->next) {
		if (s->menuid == wParam) {
			if (s->disconnect && ! *bpTerminal) {
				Shutdown();				// changes bTerminal
				SendData("\030\030\030\030\030", 5);
				}
			if (*bpTerminal) {
				StartScript(hWnd, s);
				}
			return TRUE;
			}
		}
	return FALSE;
}

static BOOL compare_names(char *name, char *param)
{
	while (*name) {
		while (*name == '&' || *name == ' ') name++;
		if (tolower(*name) != tolower(*param)) return FALSE;
		name++;
		param++;
		}
	if (*param != 0) return FALSE;
	return TRUE;
}

static char *menu_name(char *buff, struct Script *s)
{
	char kn[10];
	strcpy(buff, s->name);
	if (s->accel) {
		strcat(buff, "\t");
		if (s->vkeys & 4) strcat(buff, "Ctrl+");
		if (s->vkeys & 2) strcat(buff, "Shift+");
		if (s->vkeys & 1) strcat(buff, "Alt+");
		strcat(buff, "F");
		sprintf(kn, "%d", s->accel);
		strcat(buff, kn);
		}
	return buff;
}

static BOOL ScriptError(char *s)
{
	char buff[80];
	sprintf(buff, "%s on line %d", s, iLineNum);
	MessageBox(0, buff, "TwinSock Error", MB_ICONSTOP | MB_OK);
	return FALSE;
}

static BOOL Ignorable(char c)
{
	if (c == ' ' || c == '\n' || c == '\t' || c == '\f' || c == ',') return TRUE;
	return FALSE;
}

static BOOL NextLine(void)
{
	while (1) {
		if (! fgets(input_line, MAXLINELEN, fScriptFile)) {
			bEof = TRUE;
			return FALSE;
			}
		iLineNum++;
		input_line_p = input_line;
		if (! *input_line_p || *input_line_p == ';') continue;
		return TRUE;
		}
}

static char ReadChar(void)
{
	if (*input_line_p != '\\') return *input_line_p;
	input_line_p++;
	if (! *input_line_p) { input_line_p--; return 0; }
	switch (*input_line_p) {
		default:	return *input_line_p;
		case 'r':	return '\r';
		case 'n':	return '\n';
		case 'b':	return '\b';
		case 't':	return '\t';
		case 'f':	return '\f';
		case 'a':	return '\a';
		case 'e':	return '\033';
		}
}

static BOOL ReadToken(BOOL quoted)
{
	int i = 0;
	while (1) {
		if (! *input_line_p) {
			if (quoted) return ScriptError("Unterminated string in script file");
			break;
			}
		if (quoted ? *input_line_p == '"' : Ignorable(*input_line_p)) break;
		if (i >= MAXTOKENLEN) return ScriptError("String too long in script file");
		token[i] = ReadChar();
		input_line_p++;
		i++;
		}
	token[i] = 0;
	if (*input_line_p) input_line_p++;
	return TRUE;
}

static BOOL NextToken(void)
{
	while (1) {
		while (*input_line_p && Ignorable(*input_line_p)) input_line_p++;
		if (*input_line_p) break;
		if (! NextLine()) return FALSE;
		}
	if (*input_line_p == '"') {
		input_line_p++;
		return ReadToken(TRUE);
		}
	else return ReadToken(FALSE);
}

static BOOL OpenScriptFile(void)
{
	char filename[MAXPATH];
	GetPrivateProfileString("Config", "Scripts", "", filename, MAXPATH, "TWINSOCK.INI");
	if (filename[0] == 0) return FALSE;
	fScriptFile = fopen(filename, "rt");
	if (fScriptFile == NULL) return ScriptError("Could not open script file");
	if (! NextLine()) return ScriptError("Script file is empty");
	if (! NextToken()) return ScriptError("No scripts in script file");
	return TRUE;
}

static BOOL ReadIntError(char *s, char *fieldname)
{
	char buff[80];
	sprintf(buff, "%s field is %s", fieldname, s);
	return ScriptError(buff);
}

static BOOL read_int(int *v, int min, int max, char *fieldname)
{
	int i;
	if (token[0] == 0) return ReadIntError("empty", fieldname);
	for (i=0; token[i]; i++) if (token[i] < '0' || token[i] > '9') return ReadIntError("non-numeric", fieldname);
	*v = atoi(token);
	if (*v < min || *v > max) return ReadIntError("out of range", fieldname);
	return TRUE;
}

static void BuildScript(struct Script *scripthdr, struct ScriptLine *scriptlines, int nlines)
{
	struct Script *newscript;
	int i;

	newscript = (struct Script*)malloc(sizeof(struct Script));
	*newscript = *scripthdr;
	newscript->menuid = iNextMenuId++;
	newscript->text = (struct ScriptLine*)malloc((nlines+1)*sizeof(struct ScriptLine));
	for (i=0; i<nlines; i++) newscript->text[i] = scriptlines[i];
	newscript->text[nlines].out = strdup("");
	newscript->text[nlines].in = 0;
	newscript->text[nlines].time = 0;
	newscript->next = 0;
	if (FirstScript == 0) FirstScript = newscript;
	else RunningScript->next = newscript;
	RunningScript = newscript;

}

static BOOL ReadOneScript(void)
{
	struct Script temp_script;
	struct ScriptLine temp_lines[MAXSCRIPTLINES];
	int nlines = 0;

	if (stricmp(token, "script") != 0) return ScriptError("Scripts must start with 'script' keyword");
	if (! NextToken()) goto PREMATURE_END;
	temp_script.name = strdup(token);
	if (! NextToken()) goto PREMATURE_END;
	if (! read_int(&temp_script.accel, 0, 12, "Accelerator key")) return FALSE;
	if (! NextToken()) goto PREMATURE_END;
	if (! read_int(&temp_script.vkeys, 0, 7, "Accelerator shifts")) return FALSE;
	if (! NextToken()) goto PREMATURE_END;
	if (! read_int(&temp_script.onsysmenu, 0, 1, "System-menu-flag")) return FALSE;
	if (! NextToken()) goto PREMATURE_END;
	if (! read_int(&temp_script.disconnect, 0, 1, "Disconnect-flag")) return FALSE;
	if (! NextToken()) goto PREMATURE_END;
	if (! read_int(&temp_script.quitatend, 0, 1, "Quit-flag")) return FALSE;
	if (! NextToken()) goto PREMATURE_END;
	if (stricmp(token, "script") == 0) goto PREMATURE_END;
	while (1) {
		if (strcmp(token, "-") == 0) token[0] = 0;
		temp_lines[nlines].out = strdup(token);
		temp_lines[nlines].in = 0;
		temp_lines[nlines].time = 0;
		if (! NextToken() || stricmp(token, "script") == 0) break;

		if (strcmp(token, "-") == 0) token[0] = 0;
		temp_lines[nlines].in = strdup(token);
		if (! NextToken() || stricmp(token, "script") == 0) break;

		if (! read_int(&temp_lines[nlines].time, 0, 64, "Time-out")) return FALSE;
		if (! NextToken() || stricmp(token, "script") == 0) break;
		nlines++;
		if (nlines >= MAXSCRIPTLINES) return ScriptError("Too many lines in script");
		}
	BuildScript(&temp_script, temp_lines, ++nlines);
	return ! bEof;
PREMATURE_END:
	return ScriptError("Premature EOF in script file");
}

static void ReadScriptFile(void)
{
	if (! OpenScriptFile()) return;
	while (1) {
		if (! ReadOneScript()) break;	// error or eof
		}
	fclose(fScriptFile);
}

void ConfigureScripts(HINSTANCE hInstance, HWND hWnd, LPSTR lpCmdLine)
{
	HMENU	hScriptMenu, hSysMenu = 0;
	char buff[80];
	struct Script *s;

	hAccel = LoadAccelerators(hInstance, "TS_ACCELS");
	ReadScriptFile();
	hScriptMenu = CreateMenu();
	for (s = FirstScript; s; s = s->next) {
		AppendMenu(hScriptMenu, MF_STRING, s->menuid, menu_name(buff, s));
		if (s->onsysmenu) {
			if (hSysMenu == 0) {
				hSysMenu = GetSystemMenu(hWnd, FALSE);
				AppendMenu(hSysMenu, MF_SEPARATOR, 0, 0);
				}
			AppendMenu(hSysMenu, MF_STRING, s->menuid, menu_name(buff, s));
			}
		}
	AppendMenu(GetMenu(hWnd), MF_POPUP, (UINT) hScriptMenu, "S&cripts");
	DrawMenuBar(hWnd);
	if (lpCmdLine && *lpCmdLine) {
		char buff[80];
		for (s = FirstScript; s; s = s->next) {
			if (compare_names(s->name, lpCmdLine)) {
				StartScript(hWnd, s);
				return;
				}
			}
		sprintf(buff, "Can't find script '%s'", lpCmdLine);
		MessageBox(hWnd, buff, "TwinSock Error", MB_ICONSTOP | MB_OK);
		}
}

void TerminateScripts()
{
	struct Script *s;
	for (s = FirstScript; s; s = s->next) {
//		free(s->name);
		}
}

static void ActivateMenuItem(HWND hWnd, WORD wID)
{
	HiliteMenuItem(hWnd, GetMenu(hWnd), wID, MF_BYCOMMAND | MF_HILITE);
	SendMessage(hWnd, WM_COMMAND, wID, 0L);
	HiliteMenuItem(hWnd, GetMenu(hWnd), wID, MF_BYCOMMAND | MF_UNHILITE);
}

BOOL CustomTranslateAccelerator(HWND hWnd, MSG *msg)
{
	if (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) {
		char ks = -1;
		struct Script *s;
		for (s = FirstScript; s; s = s->next) {
			if (s->accel && msg->wParam == VK_F1-1 + s->accel) {
				if (ks == -1) {
					ks = 0;
					if ((UINT)GetKeyState(VK_CONTROL) & 0x1000) ks |= 4;
					if ((UINT)GetKeyState(VK_SHIFT)   & 0x1000) ks |= 2;
					if ((UINT)GetKeyState(VK_MENU)    & 0x1000) ks |= 1;
					}
				if (s->vkeys == ks) {
					ActivateMenuItem(hWnd, s->menuid);
					return TRUE;
					}
				}
			}
		}
	if (hAccel) return TranslateAccelerator(hWnd, hAccel, msg);
	return FALSE;
}

