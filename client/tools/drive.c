/* drive.c - test helper that drives a running Claude 95 under Wine.
 *
 *   wine drive.exe chat "message"     type a chat message and press Enter
 *   wine drive.exe cmd 40021          send a menu command (see resource.h)
 *   wine drive.exe notepad "text"     put text in Notepad, select it, bring it forward
 *   wine drive.exe hotkey             act as if the Assist hotkey was pressed
 *   wine drive.exe action 1           pick an action in the Assist window (0-based)
 *   wine drive.exe click 1025         press a button in the Assist window (resource.h IDs)
 *   wine drive.exe notepadtext        print Notepad's text
 *
 * Build: i686-w64-mingw32-gcc -O2 -o build/drive.exe tools/drive.c
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../resource.h"

static HWND WaitFor(const char *cls, const char *title)
{
    HWND h = NULL;
    int i;
    for (i = 0; i < 50 && !h; i++) {
        h = FindWindowA(cls, title);
        if (!h) Sleep(200);
    }
    return h;
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "";
    const char *arg = argc > 2 ? argv[2] : "";
    HWND app = WaitFor("Claude95Main", NULL), h;
    if (!lstrcmpA(what, "chat") && app) {
        h = GetDlgItem(app, IDC_INPUT);
        SendMessageA(h, WM_SETTEXT, 0, (LPARAM)arg);
        PostMessageA(h, WM_KEYDOWN, VK_RETURN, 0);
    } else if (!lstrcmpA(what, "cmd") && app) {
        PostMessageA(app, WM_COMMAND, atoi(arg), 0);
    } else if (!lstrcmpA(what, "hotkey") && app) {
        PostMessageA(app, WM_HOTKEY, 1, 0);
    } else if (!lstrcmpA(what, "notepad") || !lstrcmpA(what, "notepadtext")) {
        HWND np = WaitFor("Notepad", NULL), edit;
        if (!np) return 2;
        edit = FindWindowExA(np, NULL, "Edit", NULL);
        if (!lstrcmpA(what, "notepad")) {
            SendMessageA(edit, WM_SETTEXT, 0, (LPARAM)arg);
            SendMessageA(edit, EM_SETSEL, 0, -1);
            SetForegroundWindow(np);
            SetFocus(edit);
        } else {
            static char buf[20000];
            SendMessageA(edit, WM_GETTEXT, sizeof buf, (LPARAM)buf);
            printf("%s\n", buf);
        }
    } else if (!lstrcmpA(what, "action") || !lstrcmpA(what, "click")) {
        HWND dlg = WaitFor(NULL, "Claude 95 - Writing Assist");
        if (!dlg) return 3;
        if (!lstrcmpA(what, "action"))
            SendDlgItemMessageA(dlg, IDC_ACTION, CB_SETCURSEL, atoi(arg), 0);
        else
            PostMessageA(dlg, WM_COMMAND, atoi(arg), 0);
    } else {
        return 1;
    }
    return 0;
}
