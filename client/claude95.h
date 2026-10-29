/* claude95.h - shared declarations for Claude 95 (claude95.c, assist.c) */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#include <commctrl.h>
#include <commdlg.h>
#include "resource.h"

#define APP_TITLE     "Claude 95"
#define APP_VERSION   "0.3.2"
#define APP_URL       "github.com/disconnex/claude95"
#define APP_CLASS     "Claude95Main"
#define MAX_MESSAGE   60000     /* the gateway's limit */
#define WM_NETREPLY   (WM_APP + 1)   /* lParam = Reply*, from the network thread */
#define WM_ASSIST     (WM_APP + 2)   /* wParam = action index or -1, lParam = use clipboard */

enum { REQ_HELLO, REQ_CHAT, REQ_USAGE, REQ_ASSIST };

typedef struct {
    int kind;
    char host[64];
    int port;
    char conv[40];
    char action[16];     /* REQ_ASSIST: rewrite, proofread, ... or custom */
    char instr[512];     /* REQ_ASSIST: the user's own instruction, if any */
    char *body;
    int bodyLen;
} Request;

typedef struct {
    int kind;
    int status;          /* HTTP status, or 0 when the network failed */
    char conv[40];
    int searches;
    char *text;
} Reply;

/* claude95.c */
extern HINSTANCE hInst;
extern HWND hMain;
extern char iniPath[MAX_PATH];
void *Alloc(DWORD n);
void Free(void *p);
char *Dup(const char *s);
BOOL StartsWithI(const char *s, const char *prefix);
void StartRequest(int kind, char *body, int bodyLen, const char *action, const char *instr);

/* assist.c */
void AssistInit(void);
void AssistDone(void);
BOOL AssistMessage(UINT msg, WPARAM wp, LPARAM lp);
void AssistReply(Reply *rp);
void AssistOpen(void);
int AssistActionIndex(const char *key);
const char *AssistHotkeyName(void);
void AssistWarnHotkey(void);
BOOL AssistHotkeyDialog(void);
