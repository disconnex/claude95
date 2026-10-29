# Builds CLAUDE95.EXE for Windows 95 with MinGW (sudo apt install gcc-mingw-w64-i686).
#   make          build build/CLAUDE95.EXE and check it only uses Windows 95 APIs
#   make install DEST=user@host:/path   copy it somewhere the Windows 95 PC can reach,
#                 e.g. a Samba share (see ../docs/file-share.md)
CC      = i686-w64-mingw32-gcc
WINDRES = i686-w64-mingw32-windres
OBJDUMP = i686-w64-mingw32-objdump
# i586: runs on a plain Pentium (no CMOV). No C runtime, no stack probes.
CFLAGS  = -Os -march=i586 -Wall -Wextra -ffreestanding -fno-stack-protector \
          -fno-asynchronous-unwind-tables -mno-stack-arg-probe \
          -fno-tree-loop-distribute-patterns -fno-ident \
          -DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_WINDOWS=0x0400 -D_WIN32_IE=0x0300
LDFLAGS = -nostdlib -nostartfiles -Wl,-e,_WinMainCRTStartup -s \
          -Wl,--subsystem,windows:4.0 -Wl,--major-os-version,4 -Wl,--minor-os-version,0 \
          -Wl,--disable-dynamicbase -Wl,--disable-nxcompat -Wl,--disable-reloc-section
LIBS    = -lkernel32 -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lwsock32
EXE     = build/CLAUDE95.EXE
DEST    =

all: $(EXE) check

build:
	mkdir -p build

claude95.ico: tools/make-icon.py
	python3 tools/make-icon.py $@

build/claude95.res: claude95.rc resource.h claude95.ico | build
	$(WINDRES) -O coff -o $@ $<

SRCS    = claude95.c assist.c

$(EXE): $(SRCS) claude95.h resource.h build/claude95.res | build
	$(CC) $(CFLAGS) -o $@ $(SRCS) build/claude95.res $(LDFLAGS) $(LIBS)

check: $(EXE)
	OBJDUMP=$(OBJDUMP) ./tools/check-win95.sh $(EXE)

install: all
	@test -n "$(DEST)" || { echo "Usage: make install DEST=user@host:/path/to/share"; exit 1; }
	scp -q $(EXE) $(DEST)/CLAUDE95.EXE
	@echo "Copied to $(DEST)/CLAUDE95.EXE"

clean:
	rm -rf build

.PHONY: all check install clean
