/* claude95.c - Claude 95: chat with Claude from Windows 95.
 *
 * A plain Win32 program for Windows 95. It talks HTTP/1.0 over Winsock 1.1 to
 * the claude95 gateway (gateway/claude95d.py) on another computer on the local
 * network, which holds the API key and talks to Claude. No C runtime: it uses
 * only Windows 95 APIs, so the
 * one EXE runs on a stock Windows 95 install. Settings live in CLAUDE95.INI
 * next to the EXE. The writing assistant (Ctrl+Alt+Space) is in assist.c.
 */
#include "claude95.h"

#define DEF_PORT      8095
#define TRIM_AT       48000     /* Windows 95 edit controls top out near 64K */
#define NET_TIMEOUT   180       /* seconds to wait for a reply */
#define TIMER_WAIT    1

HINSTANCE hInst;
HWND hMain;
char iniPath[MAX_PATH];
static HWND hTranscript, hInput, hSend, hStatus;
static HFONT hFont;
static HBRUSH hWhite;
static WNDPROC oldInputProc;
static char server[64];
static int port;
static char convId[40];
static char model[64];
static BOOL busy;
static DWORD waitStart;

/* ------------------------------------------------------------ no-CRT basics */
/* GCC may emit calls to these for struct copies and zeroing. */
void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}

void *Alloc(DWORD n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
void Free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

char *Dup(const char *s)
{
    char *d = Alloc(lstrlenA(s) + 1);
    if (d) lstrcpyA(d, s);
    return d;
}

static int Lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Case-insensitive "does s start with prefix". */
BOOL StartsWithI(const char *s, const char *prefix)
{
    while (*prefix)
        if (Lower(*s++) != Lower(*prefix++)) return FALSE;
    return TRUE;
}

static int ParseInt(const char *s)
{
    int n = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n;
}

/* --------------------------------------------------------------- settings */
static void LoadSettings(void)
{
    int n = GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    while (n > 0 && iniPath[n - 1] != '.' && iniPath[n - 1] != '\\') n--;
    if (n > 0 && iniPath[n - 1] == '.') iniPath[n - 1] = 0;
    lstrcatA(iniPath, ".INI");
    GetPrivateProfileStringA("Gateway", "Server", "", server, sizeof server, iniPath);
    port = GetPrivateProfileIntA("Gateway", "Port", DEF_PORT, iniPath);
}

static void SaveSettings(void)
{
    char buf[16];
    wsprintfA(buf, "%d", port);
    WritePrivateProfileStringA("Gateway", "Server", server, iniPath);
    WritePrivateProfileStringA("Gateway", "Port", buf, iniPath);
}

/* ---------------------------------------------------------------- network */
static char *GetHeader(const char *headers, const char *name, char *out, int outLen)
{
    const char *p = headers;
    int nameLen = lstrlenA(name);
    out[0] = 0;
    while (*p) {
        if (StartsWithI(p, name) && p[nameLen] == ':') {
            int i = 0;
            p += nameLen + 1;
            while (*p == ' ') p++;
            while (*p && *p != '\r' && *p != '\n' && i < outLen - 1) out[i++] = *p++;
            out[i] = 0;
            return out;
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return NULL;
}

static BOOL SendAll(SOCKET s, const char *buf, int len)
{
    while (len > 0) {
        int n = send(s, buf, len, 0);
        if (n <= 0) return FALSE;
        buf += n;
        len -= n;
    }
    return TRUE;
}

/* Does one HTTP exchange with the gateway. Fills rp; returns FALSE with an
 * error message in rp->text when the network fails. */
static BOOL Exchange(Request *rq, Reply *rp)
{
    char head[1024], value[64];
    unsigned long addr;
    struct sockaddr_in sin;
    SOCKET s;
    char *buf, *body;
    int cap = 8192, len = 0;

    addr = inet_addr(rq->host);
    if (addr == INADDR_NONE) {
        struct hostent *he = gethostbyname(rq->host);
        if (!he) { rp->text = Dup("Can't find the gateway's address. Check Options > Gateway."); return FALSE; }
        memcpy(&addr, he->h_addr_list[0], 4);
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { rp->text = Dup("Winsock couldn't open a connection."); return FALSE; }
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons((u_short)rq->port);
    sin.sin_addr.s_addr = addr;
    if (connect(s, (struct sockaddr *)&sin, sizeof sin) == SOCKET_ERROR) {
        closesocket(s);
        rp->text = Dup("Can't connect to the gateway. Is it running? (Options > Gateway)");
        return FALSE;
    }

    if (rq->kind == REQ_CHAT) {
        wsprintfA(head, "POST /chat HTTP/1.0\r\nHost: %s\r\nContent-Type: text/plain\r\n"
                        "Content-Length: %d\r\n%s%s%s\r\n",
                  rq->host, rq->bodyLen, rq->conv[0] ? "X-Conversation: " : "",
                  rq->conv, rq->conv[0] ? "\r\n" : "");
    } else if (rq->kind == REQ_ASSIST) {
        wsprintfA(head, "POST /assist HTTP/1.0\r\nHost: %s\r\nContent-Type: text/plain\r\n"
                        "Content-Length: %d\r\nX-Action: %s\r\n%s%s%s\r\n",
                  rq->host, rq->bodyLen, rq->action, rq->instr[0] ? "X-Instruction: " : "",
                  rq->instr, rq->instr[0] ? "\r\n" : "");
    } else {
        wsprintfA(head, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n",
                  rq->kind == REQ_HELLO ? "/hello" : "/usage", rq->host);
    }
    if (!SendAll(s, head, lstrlenA(head)) ||
        (rq->bodyLen && !SendAll(s, rq->body, rq->bodyLen))) {
        closesocket(s);
        rp->text = Dup("The connection to the gateway dropped while sending.");
        return FALSE;
    }

    buf = Alloc(cap);
    for (;;) {
        fd_set fds;
        struct timeval tv;
        int n;
        FD_ZERO(&fds);
        FD_SET(s, &fds);
        tv.tv_sec = NET_TIMEOUT;
        tv.tv_usec = 0;
        if (select(0, &fds, NULL, NULL, &tv) <= 0) {
            closesocket(s);
            Free(buf);
            rp->text = Dup("No answer from the gateway in time. Try again.");
            return FALSE;
        }
        if (cap - len < 4096) {
            char *bigger = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, buf, cap * 2);
            if (!bigger) break;
            buf = bigger;
            cap *= 2;
        }
        n = recv(s, buf + len, cap - len - 1, 0);
        if (n <= 0) break;
        len += n;
    }
    closesocket(s);
    buf[len] = 0;

    body = buf;
    while (*body && !(body[0] == '\r' && body[1] == '\n' && body[2] == '\r' && body[3] == '\n')) body++;
    if (!*body || !StartsWithI(buf, "HTTP/")) {
        Free(buf);
        rp->text = Dup("The gateway sent something I couldn't read.");
        return FALSE;
    }
    *body = 0;
    body += 4;
    {
        const char *p = buf;
        while (*p && *p != ' ') p++;
        rp->status = ParseInt(p);
    }
    if (GetHeader(buf, "X-Conversation", value, sizeof value)) lstrcpynA(rp->conv, value, sizeof rp->conv);
    if (GetHeader(buf, "X-Searches", value, sizeof value)) rp->searches = ParseInt(value);
    rp->text = Dup(body);
    Free(buf);
    return TRUE;
}

static DWORD WINAPI NetThread(LPVOID arg)
{
    Request *rq = arg;
    Reply *rp = Alloc(sizeof *rp);
    rp->kind = rq->kind;
    Exchange(rq, rp);
    if (!PostMessageA(hMain, WM_NETREPLY, 0, (LPARAM)rp)) {
        Free(rp->text);
        Free(rp);
    }
    Free(rq->body);
    Free(rq);
    return 0;
}

static void SetStatus(const char *text) { SendMessageA(hStatus, SB_SETTEXTA, 0, (LPARAM)text); }

static void SetBusy(BOOL on)
{
    busy = on;
    EnableWindow(hSend, !on);
    if (on) {
        waitStart = GetTickCount();
        SetTimer(hMain, TIMER_WAIT, 1000, NULL);
        SetStatus("Waiting for Claude...");
    } else {
        KillTimer(hMain, TIMER_WAIT);
    }
}

/* Starts a request on a worker thread; takes ownership of body. */
void StartRequest(int kind, char *body, int bodyLen, const char *action, const char *instr)
{
    Request *rq = Alloc(sizeof *rq);
    DWORD tid;   /* Windows 95's CreateThread fails without a thread ID pointer */
    HANDLE h;
    rq->kind = kind;
    lstrcpynA(rq->host, server, sizeof rq->host);
    rq->port = port;
    lstrcpyA(rq->conv, convId);
    if (action) lstrcpynA(rq->action, action, sizeof rq->action);
    if (instr) lstrcpynA(rq->instr, instr, sizeof rq->instr);
    rq->body = body;
    rq->bodyLen = bodyLen;
    if (kind == REQ_CHAT || kind == REQ_USAGE) SetBusy(TRUE);
    h = CreateThread(NULL, 0, NetThread, rq, 0, &tid);
    if (h) {
        CloseHandle(h);
    } else {
        Free(body);
        Free(rq);
        if (kind == REQ_CHAT || kind == REQ_USAGE) SetBusy(FALSE);
        SetStatus("Couldn't start the network thread.");
    }
}

/* ------------------------------------------------------------- transcript */
static void TrimTranscript(void)
{
    int len = GetWindowTextLengthA(hTranscript);
    char *text = Alloc(len + 1);
    int cut = len / 2;
    GetWindowTextA(hTranscript, text, len + 1);
    while (cut < len - 4 && !(text[cut] == '\r' && text[cut + 1] == '\n' &&
                              text[cut + 2] == '\r' && text[cut + 3] == '\n')) cut++;
    cut += 4;
    Free(text);
    if (cut >= len) cut = len / 2;
    SendMessageA(hTranscript, EM_SETSEL, 0, cut);
    SendMessageA(hTranscript, EM_REPLACESEL, FALSE, (LPARAM)"");
}

static void AppendText(const char *s)
{
    int len;
    if (GetWindowTextLengthA(hTranscript) + lstrlenA(s) > TRIM_AT) TrimTranscript();
    len = GetWindowTextLengthA(hTranscript);
    SendMessageA(hTranscript, EM_SETSEL, len, len);
    SendMessageA(hTranscript, EM_REPLACESEL, FALSE, (LPARAM)s);
    SendMessageA(hTranscript, EM_SCROLLCARET, 0, 0);
}

static void Welcome(void)
{
    char buf[256];
    SetWindowTextA(hTranscript, "");
    if (!server[0]) {
        AppendText("Welcome to Claude 95!\r\n\r\nFirst, tell Claude 95 where the claude95 gateway is running "
                   "(Options > Gateway). See " APP_URL " for how to set one up.\r\n\r\n");
        return;
    }
    wsprintfA(buf, "Claude 95 - chatting through the gateway at %s:%d.\r\n"
                   "Type below and press Enter to send. Shift+Enter starts a new line.\r\n",
              server, port);
    AppendText(buf);
    wsprintfA(buf, "For writing help in any program, select text and press %s.\r\n\r\n", AssistHotkeyName());
    AppendText(buf);
}

static void SendChat(void)
{
    int len;
    char *text;
    const char *p;
    if (busy) return;
    len = GetWindowTextLengthA(hInput);
    if (len == 0) return;
    if (len > MAX_MESSAGE) {
        MessageBoxA(hMain, "That message is too long. Keep it under 60,000 characters.", APP_TITLE, MB_ICONEXCLAMATION);
        return;
    }
    text = Alloc(len + 1);
    GetWindowTextA(hInput, text, len + 1);
    for (p = text; *p == ' ' || *p == '\r' || *p == '\n' || *p == '\t'; p++) ;
    if (!*p) { Free(text); return; }
    AppendText("You: ");
    AppendText(text);
    AppendText("\r\n\r\n");
    SetWindowTextA(hInput, "");
    StartRequest(REQ_CHAT, text, len, NULL, NULL);
}

static void OnReply(Reply *rp)
{
    char buf[200];
    if (rp->kind == REQ_ASSIST) {
        AssistReply(rp);
    } else if (rp->kind == REQ_HELLO) {
        if (rp->status == 200) {
            const char *m = rp->text;
            while (*m && *m != '\n') m++;
            if (*m) m++;
            if (StartsWithI(m, "model ")) lstrcpynA(model, m + 6, sizeof model);
            wsprintfA(buf, "Connected to the gateway (%s)", model[0] ? model : "gateway OK");
            SetStatus(buf);
        } else {
            wsprintfA(buf, "Can't reach the gateway at %s:%d - see Options > Gateway", server, port);
            SetStatus(buf);
        }
    } else {
        SetBusy(FALSE);
        if (rp->kind == REQ_USAGE) {
            SetStatus("Ready");
            MessageBoxA(hMain, rp->text ? rp->text : "No answer.", "Today's Usage",
                        rp->status == 200 ? MB_ICONINFORMATION : MB_ICONEXCLAMATION);
        } else if (rp->status == 200) {
            if (rp->conv[0]) lstrcpyA(convId, rp->conv);
            AppendText("Claude: ");
            AppendText(rp->text ? rp->text : "");
            AppendText("\r\n\r\n");
            if (rp->searches > 0)
                wsprintfA(buf, "Ready - used %d web search%s", rp->searches, rp->searches == 1 ? "" : "es");
            else
                lstrcpyA(buf, "Ready");
            SetStatus(buf);
        } else {
            AppendText("*** ");
            AppendText(rp->text ? rp->text : "Something went wrong.");
            AppendText("\r\n\r\n");
            SetStatus("Not sent - see the message above");
        }
    }
    Free(rp->text);
    Free(rp);
}

static void SaveTranscript(void)
{
    char file[MAX_PATH] = "Chat.txt";
    OPENFILENAMEA ofn;
    HANDLE h;
    int len;
    char *text;
    DWORD written;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400A;
    ofn.hwndOwner = hMain;
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file;
    ofn.lpstrDefExt = "txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&ofn)) return;
    len = GetWindowTextLengthA(hTranscript);
    text = Alloc(len + 1);
    GetWindowTextA(hTranscript, text, len + 1);
    h = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE || !WriteFile(h, text, len, &written, NULL) || (int)written != len)
        MessageBoxA(hMain, "Couldn't save the transcript there.", APP_TITLE, MB_ICONEXCLAMATION);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    Free(text);
}

/* ----------------------------------------------------------------- dialogs */
static BOOL CALLBACK GatewayProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        SetDlgItemTextA(dlg, IDC_SERVER, server);
        SetDlgItemInt(dlg, IDC_PORT, port, FALSE);
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDOK) {
        BOOL ok;
        int p = GetDlgItemInt(dlg, IDC_PORT, &ok, FALSE);
        char s[64];
        GetDlgItemTextA(dlg, IDC_SERVER, s, sizeof s);
        if (!s[0] || !ok || p < 1 || p > 65535) {
            MessageBoxA(dlg, "Enter the gateway's address and a port from 1 to 65535.", APP_TITLE, MB_ICONEXCLAMATION);
            return TRUE;
        }
        lstrcpyA(server, s);
        port = p;
        EndDialog(dlg, IDOK);
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDCANCEL) {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void About(void)
{
    char buf[400];
    wsprintfA(buf, "Claude 95 version " APP_VERSION "\r\n\r\n"
                   "Chat with Claude, the AI assistant made by Anthropic, from Windows 95.\r\n"
                   "Your messages go through the claude95 gateway at %s:%d.\r\n"
                   "Model: %s\r\n\r\n"
                   "Open source: " APP_URL,
              server[0] ? server : "(not set)", port, model[0] ? model : "unknown (gateway not reached yet)");
    MessageBoxA(hMain, buf, "About Claude 95", MB_ICONINFORMATION);
}

/* ------------------------------------------------------------------ window */
static LRESULT CALLBACK InputProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    BOOL shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
    if (msg == WM_KEYDOWN && wp == VK_RETURN && !shift && !ctrl) {
        SendChat();
        return 0;
    }
    if (msg == WM_CHAR && wp == '\r' && !shift && !ctrl) return 0;   /* no beep, no newline */
    return CallWindowProcA(oldInputProc, h, msg, wp, lp);
}

static void Layout(int w, int h)
{
    RECT sr;
    TEXTMETRICA tm;
    HDC dc = GetDC(hMain);
    HGDIOBJ old = SelectObject(dc, hFont);
    int sb, inputH, m = 6, btnW = 75;
    GetTextMetricsA(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(hMain, dc);
    SendMessageA(hStatus, WM_SIZE, 0, 0);
    GetWindowRect(hStatus, &sr);
    sb = sr.bottom - sr.top;
    inputH = tm.tmHeight * 3 + 8;
    MoveWindow(hTranscript, m, m, w - 2 * m, h - sb - inputH - 3 * m, TRUE);
    MoveWindow(hInput, m, h - sb - inputH - m, w - 3 * m - btnW, inputH, TRUE);
    MoveWindow(hSend, w - m - btnW, h - sb - inputH - m, btnW, 25, TRUE);
}

static HWND FocusedEdit(void)
{
    HWND f = GetFocus();
    return (f == hInput || f == hTranscript) ? f : hTranscript;
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (AssistMessage(msg, wp, lp)) return 0;
    switch (msg) {
    case WM_CREATE:
        hMain = hwnd;
        hTranscript = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_TRANSCRIPT, hInst, NULL);
        hInput = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)IDC_INPUT, hInst, NULL);
        hSend = CreateWindowExA(0, "BUTTON", "&Send",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_SEND, hInst, NULL);
        hStatus = CreateStatusWindowA(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, "Connecting to the gateway...",
                                      hwnd, IDC_STATUS);
        hFont = GetStockObject(DEFAULT_GUI_FONT);
        SendMessageA(hTranscript, WM_SETFONT, (WPARAM)hFont, 0);
        SendMessageA(hInput, WM_SETFONT, (WPARAM)hFont, 0);
        SendMessageA(hSend, WM_SETFONT, (WPARAM)hFont, 0);
        SendMessageA(hTranscript, EM_LIMITTEXT, 0, 0);
        SendMessageA(hInput, EM_LIMITTEXT, MAX_MESSAGE, 0);
        oldInputProc = (WNDPROC)SetWindowLongA(hInput, GWL_WNDPROC, (LONG)InputProc);
        AssistInit();
        Welcome();
        if (server[0]) {
            StartRequest(REQ_HELLO, NULL, 0, NULL, NULL);
        } else {
            SetStatus("Set the gateway address in Options > Gateway");
            PostMessageA(hwnd, WM_COMMAND, IDM_GATEWAY, 0);   /* first run */
        }
        return 0;

    case WM_SIZE:
        Layout(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_SETFOCUS:
        SetFocus(hInput);
        return 0;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = 320;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = 240;
        return 0;

    case WM_CTLCOLORSTATIC:   /* read-only edits paint grey by default */
        if ((HWND)lp == hTranscript) {
            SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
            SetTextColor((HDC)wp, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)hWhite;
        }
        break;

    case WM_TIMER:
        if (wp == TIMER_WAIT && busy) {
            char buf[64];
            wsprintfA(buf, "Waiting for Claude... %lu s", (GetTickCount() - waitStart) / 1000);
            SetStatus(buf);
        }
        return 0;

    case WM_NETREPLY:
        OnReply((Reply *)lp);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SEND:
            SendChat();
            SetFocus(hInput);
            return 0;
        case IDM_NEWCHAT:
            if (busy) return 0;
            convId[0] = 0;
            Welcome();
            SetStatus("New chat");
            SetFocus(hInput);
            return 0;
        case IDM_SAVE:
            SaveTranscript();
            return 0;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        case IDM_COPY:
            SendMessageA(FocusedEdit(), WM_COPY, 0, 0);
            return 0;
        case IDM_SELECTALL:
            SendMessageA(FocusedEdit(), EM_SETSEL, 0, -1);
            return 0;
        case IDM_GATEWAY:
            if (DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_GATEWAY), hwnd, (DLGPROC)GatewayProc, 0) == IDOK) {
                SaveSettings();
                model[0] = 0;
                SetStatus("Connecting to the gateway...");
                Welcome();
                StartRequest(REQ_HELLO, NULL, 0, NULL, NULL);
            }
            return 0;
        case IDM_HOTKEY:
            if (AssistHotkeyDialog()) {
                char buf[80];
                wsprintfA(buf, "Writing Assist hotkey is now %s", AssistHotkeyName());
                SetStatus(buf);
            }
            return 0;
        case IDM_USAGE:
            if (!busy) StartRequest(REQ_USAGE, NULL, 0, NULL, NULL);
            return 0;
        case IDM_ABOUT:
            About();
            return 0;
        case IDM_ASSIST:
            AssistOpen();
            return 0;
        }
        break;

    case WM_DESTROY:
        AssistDone();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* Finds "/assist [action] [/clip]" on the command line (used by the Word 95
 * macros). Returns FALSE if it isn't there. */
static BOOL AssistArgs(int *action, BOOL *clip)
{
    const char *p = GetCommandLineA(), *q;
    BOOL found = FALSE;
    *action = -1;
    *clip = FALSE;
    for (; *p; p++) {
        if (StartsWithI(p, "/assist")) {
            found = TRUE;
            for (q = p + 7; *q == ' '; q++) ;
            if (*q && *q != '/') *action = AssistActionIndex(q);
        } else if (StartsWithI(p, "/clip")) {
            *clip = TRUE;
        }
    }
    return found;
}

/* Tells Word macros where we are: WIN.INI [Claude95] Path, as a short
 * (8.3) path so WordBasic's Shell needn't worry about spaces. */
static void RegisterPath(void)
{
    char path[MAX_PATH], shortPath[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof path);
    if (!GetShortPathNameA(path, shortPath, sizeof shortPath)) lstrcpyA(shortPath, path);
    WriteProfileStringA("Claude95", "Path", shortPath);
}

static int Main(void)
{
    WNDCLASSEXA wc;
    WSADATA wsa;
    STARTUPINFOA si;
    HACCEL accel;
    MSG msg;
    HWND hwnd;
    int show;

    int assistAction;
    BOOL assistClip, assist = AssistArgs(&assistAction, &assistClip);
    HWND running = FindWindowA(APP_CLASS, NULL);

    /* One copy at a time: a second one hands its request to the first. */
    if (running) {
        if (assist) {
            PostMessageA(running, WM_ASSIST, (WPARAM)assistAction, assistClip);
        } else {
            ShowWindow(running, SW_RESTORE);
            SetForegroundWindow(running);
        }
        return 0;
    }
    hInst = GetModuleHandleA(NULL);
    InitCommonControls();
    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
        MessageBoxA(NULL, "Claude 95 needs TCP/IP networking (Winsock).", APP_TITLE, MB_ICONSTOP);
        return 1;
    }
    LoadSettings();
    RegisterPath();
    hWhite = CreateSolidBrush(GetSysColor(COLOR_WINDOW));

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(IDI_APP));
    wc.hIconSm = LoadImageA(hInst, MAKEINTRESOURCEA(IDI_APP), IMAGE_ICON, 16, 16, 0);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEA(IDR_MENU);
    wc.lpszClassName = APP_CLASS;
    if (!RegisterClassExA(&wc)) return 1;

    hwnd = CreateWindowExA(0, APP_CLASS, APP_TITLE, WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 560, 440, NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    GetStartupInfoA(&si);
    show = (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : SW_SHOWDEFAULT;
    if (assist) show = SW_SHOWMINNOACTIVE;   /* started by a Word macro: stay out of the way */
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    if (assist) PostMessageA(hwnd, WM_ASSIST, (WPARAM)assistAction, assistClip);
    else AssistWarnHotkey();

    accel = LoadAcceleratorsA(hInst, MAKEINTRESOURCEA(IDR_ACCEL));
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!TranslateAcceleratorA(hwnd, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    WSACleanup();
    return (int)msg.wParam;
}

void WinMainCRTStartup(void)
{
    ExitProcess(Main());
}
