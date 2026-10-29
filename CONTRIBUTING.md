# Contributing to Claude 95

Thanks for wanting to help! This is a hobby project, so keep it fun and kind.

## Good ways to help

- **Test on real hardware.** Tell us what machine, Windows version (95 RTM,
  OSR2, 98, NT 4.0...) and network card you tried, and what happened. Photos
  of it running on a CRT are very welcome.
- **Report bugs** with an issue: what you did, what you expected, what
  happened, and the gateway's log (`journalctl -u claude95` or `docker logs claude95`).
- **Pick something from the roadmap** in the README, or propose an idea in an issue first.

## Ground rules

- **Never commit secrets.** API keys belong in `claude95.env`, which git ignores.
  Check `git diff --staged` before you commit.
- **The client must keep running on Windows 95.** Only use Win32 functions that
  Windows 95 has (ANSI `...A` versions, Winsock 1.1, no C runtime). The build
  checks every import against `client/tools/win95-api.txt`; if you need a new
  function, add it there in the same pull request, with a note on how you
  checked that Windows 95 exports it. `client/tools/exports-from-dlls.sh`
  lists the exports of real Windows 95 DLLs.
- **Keep it small.** No installers, frameworks or extra DLLs on the PC side.
  Match the existing style: plain C, short functions, comments that explain why.
- **The gateway stays dependency-light:** the standard library plus the
  `anthropic` SDK. Add tests to `gateway/test_claude95d.py` for new behaviour;
  they use a fake API client, so no key is needed.

## Building and testing

```bash
# Gateway tests (Python 3.10+)
pip install -r gateway/requirements.txt
cd gateway && python -m unittest -v test_claude95d

# Client (Linux)
sudo apt install gcc-mingw-w64-i686 make
make -C client
wine client/build/CLAUDE95.EXE    # optional: try it under Wine
```

GitHub Actions runs both on every pull request.
