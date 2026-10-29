/* assist.c - Claude 95 Assist: writing help for text in any program.
 *
 * Select text in Word 95, WordPad, Notepad or anywhere else and press the
 * hotkey, Ctrl+Alt+W unless changed in Options (or use the Word 95 macros in word/). Claude 95 copies the
 * selection with a simulated Ctrl+C, shows it in the Assist window with a
 * list of actions, and when you choose Replace or Insert After, puts the
 * result on the clipboard and pastes it back with a simulated Ctrl+V.
 * Nothing changes in your document until you've seen the result.
 */
#include "claude95.h"

#define HOTKEY_ID     1
#define TIMER_KEYS    10    /* waiting for the hotkey's keys to be released */
#define TIMER_CLIP    11    /* waiting for the copied text to arrive */
#define TIMER_TICK    12    /* the Assist window's seconds counter */
#define WM_ASSISTDONE (WM_APP + 3)   /* to the Assist window: lParam = Reply* */

typedef struct { const char *key, *label; } Action;

/* Keys match the gateway's ACTIONS. */
static const Action actions[] = {
    {"rewrite",   "Rewrite it better"},
    {"proofread", "Fix spelling and grammar"},
    {"shorter",   "Make it shorter"},
    {"longer",    "Make it longer"},
    {"formal",    "Make it more formal"},
    {"friendly",  "Make it friendlier"},
    {"summarize", "Summarize it"},
    {"continue",  "Continue writing"},
    {"explain",   "Explain it"},
};
#define NACTIONS ((int)(sizeof actions / sizeof actions[0]))

static HWND hDlg;            /* the Assist window, while open */
static HWND hTarget;         /* the program the text came from */
static char *startText;      /* text for the Assist window to open with */
static int startAction;
static BOOL startNow;        /* send it straight away (from a Word macro) */
static BOOL hadSelection;
static BOOL working;
static int sentAction;        /* the action of the last request, -1 for custom */
static char *result;         /* what Replace / Insert After will paste */
static DWORD started;
static char hotkeyName[40];
static BOOL hotkeyFailed;
static UINT hotkeyVk, hotkeyMods;   /* virtual key and MOD_ flags */

int AssistActionIndex(const char *key)
{
    int i;
    for (i = 0; i < NACTIONS; i++)
        if (StartsWithI(key, actions[i].key) && (key[lstrlenA(actions[i].key)] <= ' ')) return i;
    return -1;
}

const char *AssistHotkeyName(void) { return hotkeyName; }

/* Explains a hotkey that couldn't be registered. Call once the window shows. */
void AssistWarnHotkey(void)
{
    char buf[300];
    if (!hotkeyFailed) return;
    wsprintfA(buf, "Another program is already using %s, so the Writing Assist hotkey is off.\r\n\r\n"
                   "Pick a different one in Options > Writing Assist Hotkey. Writing Assist is also "
                   "on the Edit menu.", hotkeyName);
    MessageBoxA(hMain, buf, APP_TITLE, MB_ICONINFORMATION);
}

/* -------------------------------------------------------------- clipboard */
static char *ReadClipboardText(void)
{
    char *text = NULL;
    HANDLE h;
    if (!OpenClipboard(hMain)) return NULL;
    h = GetClipboardData(CF_TEXT);
    if (h) {
        char *p = GlobalLock(h);
        if (p) {
            text = Dup(p);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return text;
}

static BOOL WriteClipboardText(const char *s)
{
    int n = lstrlenA(s) + 1;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n);
    char *p;
    if (!g) return FALSE;
    p = GlobalLock(g);
    memcpy(p, s, n);
    GlobalUnlock(g);
    if (!OpenClipboard(hMain)) {
        GlobalFree(g);
        return FALSE;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_TEXT, g)) {
        GlobalFree(g);
        CloseClipboard();
        return FALSE;
    }
    CloseClipboard();
    return TRUE;
}

static void PressKey(BYTE vk, BOOL ctrl)
{
    if (ctrl) keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event(vk, 0, 0, 0);
    keybd_event(vk, 0, KEYEVENTF_KEYUP, 0);
    if (ctrl) keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
}

static BOOL IsOurs(HWND h)
{
    while (h) {
        if (h == hMain) return TRUE;
        h = GetParent(h);
    }
    return FALSE;
}

/* Puts the result back into the program the text came from. */
static void PasteBack(const char *text, BOOL insert, BOOL isContinue)
{
    char *t = Alloc(lstrlenA(text) + 3);
    if (insert && hadSelection) lstrcpyA(t, isContinue ? " " : "\r\n");
    lstrcatA(t, text);
    if (!WriteClipboardText(t)) {
        Free(t);
        MessageBoxA(hMain, "Couldn't use the clipboard.", APP_TITLE, MB_ICONEXCLAMATION);
        return;
    }
    Free(t);
    if (!hTarget || !IsWindow(hTarget)) {
        MessageBoxA(hMain, "The result is on the clipboard. Paste it with Ctrl+V.", APP_TITLE, MB_ICONINFORMATION);
        return;
    }
    SetForegroundWindow(hTarget);
    Sleep(250);   /* let the program become active before the keys arrive */
    if (insert && hadSelection) PressKey(VK_RIGHT, FALSE);   /* to the end of the selection */
    PressKey('V', TRUE);
}

/* ----------------------------------------------------------- Assist window */
static void SetDlgStatus(const char *s) { SetDlgItemTextA(hDlg, IDC_ASTATUS, s); }

static void EnableResult(BOOL on)
{
    BOOL canPaste = on && hTarget && IsWindow(hTarget);
    EnableWindow(GetDlgItem(hDlg, IDC_REPLACE), canPaste && hadSelection);
    EnableWindow(GetDlgItem(hDlg, IDC_INSERT), canPaste);
    EnableWindow(GetDlgItem(hDlg, IDC_COPYRESULT), on);
}

static void Go(void)
{
    char instr[512];
    int len, a, i;
    char *text;
    if (working) return;
    len = GetWindowTextLengthA(GetDlgItem(hDlg, IDC_ATEXT));
    if (len > MAX_MESSAGE) {
        SetDlgStatus("That's too much text. Select less than 60,000 characters.");
        return;
    }
    GetDlgItemTextA(hDlg, IDC_INSTR, instr, sizeof instr);
    for (i = 0; instr[i]; i++)
        if (instr[i] == '\r' || instr[i] == '\n') instr[i] = ' ';
    if (len == 0 && !instr[0]) {
        SetDlgStatus("Type or paste some text first, or tell Claude what to write.");
        return;
    }
    text = Alloc(len + 1);
    GetDlgItemTextA(hDlg, IDC_ATEXT, text, len + 1);
    a = (int)SendDlgItemMessageA(hDlg, IDC_ACTION, CB_GETCURSEL, 0, 0);
    if (a < 0 || a >= NACTIONS) a = 0;
    sentAction = instr[0] ? -1 : a;
    StartRequest(REQ_ASSIST, text, len, instr[0] ? "custom" : actions[a].key, instr);
    working = TRUE;
    started = GetTickCount();
    EnableWindow(GetDlgItem(hDlg, IDC_GO), FALSE);
    EnableResult(FALSE);
    SetTimer(hDlg, TIMER_TICK, 1000, NULL);
    SetDlgStatus("Claude is working on it...");
}

static BOOL CALLBACK AssistProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    char buf[200];
    int i;
    switch (msg) {
    case WM_INITDIALOG:
        hDlg = dlg;
        for (i = 0; i < NACTIONS; i++)
            SendDlgItemMessageA(dlg, IDC_ACTION, CB_ADDSTRING, 0, (LPARAM)actions[i].label);
        SendDlgItemMessageA(dlg, IDC_ACTION, CB_SETCURSEL, startAction, 0);
        SendDlgItemMessageA(dlg, IDC_ATEXT, EM_LIMITTEXT, MAX_MESSAGE, 0);
        SendDlgItemMessageA(dlg, IDC_RESULT, EM_LIMITTEXT, 0, 0);
        SetDlgItemTextA(dlg, IDC_ATEXT, startText ? startText : "");
        EnableResult(FALSE);
        if (hadSelection && hTarget) {
            char title[80];
            if (!GetWindowTextA(hTarget, title, sizeof title)) lstrcpyA(title, "the program");
            wsprintfA(buf, "Text from %s. Pick what Claude should do.", title);
        } else {
            lstrcpyA(buf, "No text selected. Type or paste some, or tell Claude what to write.");
        }
        SetDlgStatus(buf);
        SetWindowPos(dlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(dlg);
        if (startNow) PostMessageA(dlg, WM_COMMAND, IDC_GO, 0);
        SetFocus(GetDlgItem(dlg, hadSelection ? IDC_ACTION : IDC_INSTR));
        return FALSE;   /* we set the focus */

    case WM_TIMER:
        if (wp == TIMER_TICK && working) {
            wsprintfA(buf, "Claude is working on it... %lu s", (GetTickCount() - started) / 1000);
            SetDlgStatus(buf);
        }
        return TRUE;

    case WM_ASSISTDONE: {
        Reply *rp = (Reply *)lp;
        working = FALSE;
        KillTimer(dlg, TIMER_TICK);
        EnableWindow(GetDlgItem(dlg, IDC_GO), TRUE);
        if (rp->status == 200) {
            SetDlgItemTextA(dlg, IDC_RESULT, rp->text ? rp->text : "");
            EnableResult(TRUE);
            SetDlgStatus(hadSelection ? "Done. Check it, then Replace, Insert After or Copy."
                                      : "Done. Check it, then Insert or Copy.");
            SetFocus(GetDlgItem(dlg, IDC_RESULT));
        } else {
            MessageBeep(MB_ICONEXCLAMATION);
            lstrcpynA(buf, rp->text ? rp->text : "Something went wrong.", sizeof buf);
            SetDlgStatus(buf);
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_GO:
            Go();
            return TRUE;
        case IDC_REPLACE:
        case IDC_INSERT:
        case IDC_COPYRESULT: {
            int len = GetWindowTextLengthA(GetDlgItem(dlg, IDC_RESULT));
            Free(result);
            result = Alloc(len + 1);
            GetDlgItemTextA(dlg, IDC_RESULT, result, len + 1);
            if (LOWORD(wp) == IDC_COPYRESULT) {
                SetDlgStatus(WriteClipboardText(result) ? "Copied to the clipboard." : "Couldn't use the clipboard.");
                return TRUE;
            }
            EndDialog(dlg, LOWORD(wp));
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        KillTimer(dlg, TIMER_TICK);
        hDlg = NULL;
        working = FALSE;   /* a late reply is dropped */
        break;
    }
    return FALSE;
}

/* Shows the Assist window. Takes ownership of text (may be NULL). */
static void Open(char *text, int action, BOOL now)
{
    int r;
    if (hDlg) {
        Free(text);
        SetForegroundWindow(hDlg);
        return;
    }
    startText = text;
    startAction = action >= 0 ? action : 0;
    hadSelection = text && text[0];
    startNow = now && hadSelection;
    sentAction = -1;
    r = DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_ASSIST), NULL, (DLGPROC)AssistProc, 0);
    Free(startText);
    startText = NULL;
    if ((r == IDC_REPLACE || r == IDC_INSERT) && result)
        PasteBack(result, r == IDC_INSERT, sentAction >= 0 && lstrcmpiA(actions[sentAction].key, "continue") == 0);
    Free(result);
    result = NULL;
}

void AssistOpen(void)
{
    hTarget = NULL;
    Open(NULL, 0, FALSE);
}

/* ----------------------------------------------------------------- hotkey */
/* The hotkey control's HOTKEYF_ flags have Shift and Alt the other way round
 * from RegisterHotKey's MOD_ flags. */
static UINT ModsFromHotkeyF(UINT f)
{
    return ((f & HOTKEYF_SHIFT) ? MOD_SHIFT : 0) | ((f & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) |
           ((f & HOTKEYF_ALT) ? MOD_ALT : 0);
}

static UINT HotkeyFFromMods(UINT m)
{
    return ((m & MOD_SHIFT) ? HOTKEYF_SHIFT : 0) | ((m & MOD_CONTROL) ? HOTKEYF_CONTROL : 0) |
           ((m & MOD_ALT) ? HOTKEYF_ALT : 0);
}

static void NameHotkey(void)
{
    char key[32];
    LONG scan = (LONG)MapVirtualKeyA(hotkeyVk, 0) << 16;
    if (hotkeyVk >= VK_PRIOR && hotkeyVk <= VK_DELETE) scan |= 1L << 24;   /* PgUp..Del, arrows */
    if (!GetKeyNameTextA(scan, key, sizeof key)) wsprintfA(key, "Key %u", hotkeyVk);
    hotkeyName[0] = 0;
    if (hotkeyMods & MOD_CONTROL) lstrcatA(hotkeyName, "Ctrl+");
    if (hotkeyMods & MOD_ALT) lstrcatA(hotkeyName, "Alt+");
    if (hotkeyMods & MOD_SHIFT) lstrcatA(hotkeyName, "Shift+");
    lstrcatA(hotkeyName, key);
}

static void UpdateMenu(void)
{
    char text[64];
    wsprintfA(text, "Writing &Assist...\t%s", hotkeyName);
    ModifyMenuA(GetMenu(hMain), IDM_ASSIST, MF_BYCOMMAND | MF_STRING, IDM_ASSIST, text);
}

void AssistInit(void)
{
    char key[8];
    hotkeyVk = GetPrivateProfileIntA("Assist", "Key", 0, iniPath);
    hotkeyMods = GetPrivateProfileIntA("Assist", "Mods", MOD_CONTROL | MOD_ALT, iniPath);
    if (!hotkeyVk) {
        /* Ctrl+Alt+W by default: a letter, because many 90s BIOSes use Ctrl+Alt with
         * Space, Esc or Ins for suspend and setup. Older INIs say Hotkey=<letter>. */
        GetPrivateProfileStringA("Assist", "Hotkey", "W", key, sizeof key, iniPath);
        hotkeyVk = (key[0] >= 'a' && key[0] <= 'z') ? key[0] - 32 : key[0];
        if (!((hotkeyVk >= 'A' && hotkeyVk <= 'Z') || (hotkeyVk >= '0' && hotkeyVk <= '9'))) hotkeyVk = 'W';
        hotkeyMods = MOD_CONTROL | MOD_ALT;
    }
    NameHotkey();
    UpdateMenu();
    hotkeyFailed = !RegisterHotKey(hMain, HOTKEY_ID, hotkeyMods, hotkeyVk);
}

static BOOL CALLBACK HotkeyProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        /* No modifiers, or Shift alone, would steal ordinary typing: make those Ctrl+Alt. */
        SendDlgItemMessageA(dlg, IDC_HOTKEY, HKM_SETRULES, HKCOMB_NONE | HKCOMB_S,
                            MAKELPARAM(HOTKEYF_CONTROL | HOTKEYF_ALT, 0));
        SendDlgItemMessageA(dlg, IDC_HOTKEY, HKM_SETHOTKEY,
                            MAKEWORD(hotkeyVk, HotkeyFFromMods(hotkeyMods)), 0);
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDOK) {
        WORD hk = (WORD)SendDlgItemMessageA(dlg, IDC_HOTKEY, HKM_GETHOTKEY, 0, 0);
        UINT vk = LOBYTE(hk), mods = ModsFromHotkeyF(HIBYTE(hk));
        char buf[16];
        if (!vk) {
            MessageBoxA(dlg, "Click in the box and press the keys you want.", APP_TITLE, MB_ICONEXCLAMATION);
            return TRUE;
        }
        UnregisterHotKey(hMain, HOTKEY_ID);
        if (!RegisterHotKey(hMain, HOTKEY_ID, mods, vk)) {
            if (!hotkeyFailed) RegisterHotKey(hMain, HOTKEY_ID, hotkeyMods, hotkeyVk);
            MessageBoxA(dlg, "Another program is already using those keys. Try another combination.",
                        APP_TITLE, MB_ICONEXCLAMATION);
            return TRUE;
        }
        hotkeyVk = vk;
        hotkeyMods = mods;
        hotkeyFailed = FALSE;
        wsprintfA(buf, "%u", vk);
        WritePrivateProfileStringA("Assist", "Key", buf, iniPath);
        wsprintfA(buf, "%u", mods);
        WritePrivateProfileStringA("Assist", "Mods", buf, iniPath);
        WritePrivateProfileStringA("Assist", "Hotkey", NULL, iniPath);
        NameHotkey();
        UpdateMenu();
        EndDialog(dlg, IDOK);
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDCANCEL) {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

/* Options > Writing Assist Hotkey. Returns TRUE if the hotkey changed. */
BOOL AssistHotkeyDialog(void)
{
    return DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_HOTKEY), hMain, (DLGPROC)HotkeyProc, 0) == IDOK;
}

/* ------------------------------------------------------------ main window */
void AssistDone(void)
{
    UnregisterHotKey(hMain, HOTKEY_ID);
}

void AssistReply(Reply *rp)
{
    if (hDlg) SendMessageA(hDlg, WM_ASSISTDONE, 0, (LPARAM)rp);
}

/* Handles the hotkey and its timers, and WM_ASSIST from a second copy of
 * Claude 95 started by a Word macro. Returns TRUE if it handled msg. */
BOOL AssistMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_HOTKEY && wp == HOTKEY_ID) {
        if (hDlg) {
            SetForegroundWindow(hDlg);
            return TRUE;
        }
        hTarget = GetForegroundWindow();
        if (IsOurs(hTarget)) {
            hTarget = NULL;
            Open(NULL, 0, FALSE);
            return TRUE;
        }
        started = GetTickCount();
        SetTimer(hMain, TIMER_KEYS, 50, NULL);
        return TRUE;
    }
    if (msg == WM_TIMER && wp == TIMER_KEYS) {
        /* Wait until Ctrl, Alt and the key are up, or our Ctrl+C becomes Ctrl+Alt+C. */
        if ((GetAsyncKeyState(VK_MENU) < 0 || GetAsyncKeyState(VK_CONTROL) < 0 ||
             GetAsyncKeyState(VK_SHIFT) < 0 || GetAsyncKeyState(hotkeyVk) < 0) &&
            GetTickCount() - started < 2000)
            return TRUE;
        KillTimer(hMain, TIMER_KEYS);
        if (OpenClipboard(hMain)) {   /* empty it, so we can tell when the copy lands */
            EmptyClipboard();
            CloseClipboard();
        }
        PressKey('C', TRUE);
        started = GetTickCount();
        SetTimer(hMain, TIMER_CLIP, 50, NULL);
        return TRUE;
    }
    if (msg == WM_TIMER && wp == TIMER_CLIP) {
        if (!IsClipboardFormatAvailable(CF_TEXT) && GetTickCount() - started < 1500) return TRUE;
        KillTimer(hMain, TIMER_CLIP);
        Open(ReadClipboardText(), 0, FALSE);
        return TRUE;
    }
    if (msg == WM_ASSIST) {
        hTarget = GetForegroundWindow();
        if (IsOurs(hTarget)) hTarget = NULL;
        Open(lp ? ReadClipboardText() : NULL, (int)wp, (int)wp >= 0);
        return TRUE;
    }
    return FALSE;
}
