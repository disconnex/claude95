#!/bin/bash
# Prints "DLL FUNCTION" for every named export of the given DLLs, e.g.
# a Windows 95 PC's own C:\WINDOWS\SYSTEM DLLs, to rebuild win95-api.txt from the real thing:
#   tools/exports-from-dlls.sh /path/to/*.DLL | sort > tools/win95-exports.txt
set -euo pipefail
objdump=${OBJDUMP:-i686-w64-mingw32-objdump}
for dll in "$@"; do
  name=$(basename "$dll" | tr a-z A-Z); name=${name%.DLL}
  $objdump -p "$dll" | awk -v n="$name" '
    /\[Ordinal\/Name Pointer\] Table/ {on = 1; next}
    on && /^\t\[ *[0-9]+\]/ {print n " " $NF; next}
    on && !/^\t/ {on = 0}'
done
