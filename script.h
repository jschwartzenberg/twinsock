
#define TIMER_ID_SCRIPT 6

void CheckScripts(HWND hWnd, char c);
void ScriptTimeOut(HWND hWnd);
void PerhapsKillScript(HWND hWnd);
void CheckScriptSysCommands(HWND hWnd, WPARAM wParam, LPARAM lParam);
BOOL CheckScriptCommands(HWND hWnd, WPARAM wParam, BOOL *bpTerminal);
void ConfigureScripts(HINSTANCE hInstance, HWND hWnd, LPSTR lpCmdLine);
void TerminateScripts();
BOOL CustomTranslateAccelerator(HWND hwnd, MSG *msg);

int	SendData(void *pvData, int iDataLen);		// from twinsock.c
void	Shutdown(void);

