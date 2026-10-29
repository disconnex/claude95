# Changelog

## 0.3.2
- Writing Assist's hotkey is now Ctrl+Alt+W, and Options > Writing Assist
  Hotkey lets you pick any combination. (Ctrl+Alt+Space was a BIOS suspend key
  on some PCs, and Word 95 uses Ctrl+Alt+A for annotations.)
- Claude 95 says so if another program already has its hotkey.

## 0.3
- **Writing Assist**: select text in any program and press the hotkey to have
  Claude rewrite, proofread, shorten, lengthen, change the tone of, summarize,
  continue or explain it, or follow your own instruction. You review the
  result before Replace Text or Insert After puts it back.
- Word 95 macros and a Claude menu (`word/`).
- Only one copy runs at a time; a second copy hands its request to the first.
- Gateway: `POST /assist`.

## 0.2
- Ready for anyone to set up: no built-in addresses, a settings template,
  Docker support, docs, tests and GitHub Actions builds.
- The client asks for the gateway's address on first run.

## 0.1
- Chat with Claude from Windows 95 through a gateway on the local network,
  with web search, daily limits and saved transcripts.
