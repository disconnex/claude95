# Claude 95 in Word 95

Claude 95's **Writing Assist** works in any program: select text and press
**Ctrl+Alt+W**. These macros add a **Claude** menu to Word 95 as well, for
one-click Rewrite, Proofread, Make Shorter, Summarize and Continue Writing.

In every case you see Claude's result first, can edit it, and then choose
**Replace Text**, **Insert After** or **Copy**. Nothing in your document
changes until you do.

## Install

1. Run Claude 95 once, so it can tell Word where it is.
2. Copy `CLAUDE95.TXT` to the PC and open it in Notepad.
3. In Word: **Tools > Macro**, type `ClaudeAssist` in Macro Name, and click
   **Create**. A macro window opens with `Sub MAIN` / `End Sub`.
4. Select everything in that window and replace it with the `ClaudeAssist`
   block from `CLAUDE95.TXT` (from `Sub MAIN` to `End Sub`).
5. **File > Close**, and click **Yes** to save the changes.
6. Repeat steps 3 to 5 for the other macros you want: `ClaudeRewrite`,
   `ClaudeProofread`, `ClaudeShorter`, `ClaudeSummarize`, `ClaudeContinue`.

The macros are saved in `NORMAL.DOT`, so they're there in every document.

## Add the Claude menu

The quick way: create the `ClaudeInstallMenu` macro the same way, then run it
once (**Tools > Macro**, pick it, **Run**).

If it can't, add the menu by hand:

1. **Tools > Customize > Menus** tab, then **Menu Bar...**, type `&Claude`,
   and click **Add**.
2. Back on the Menus tab, pick **Macros** under Categories.
3. For each macro: select it, set **Change What Menu** to `&Claude`, set
   **Name on Menu** (for example `&Rewrite`), and click **Add**.

Want a keyboard shortcut or toolbar button instead? Use the **Keyboard** or
**Toolbars** tab of the same dialog, with the Macros category.

## Good to know

- The result goes in as plain text, so Replace Text drops formatting such
  as bold in the replaced part. Use Copy and paste it yourself if you'd rather.
- Writing Assist uses the clipboard to fetch the selection and to put the
  result back, so whatever was on the clipboard before is replaced.
- Long selections (several pages) work; they just take longer. The limit is
  60,000 characters.
