#!/bin/bash
# Checks that an EXE will load on Windows 95: every imported function must be
# in tools/win95-api.txt (functions Windows 95's own DLLs export), the PE must
# ask for Windows 4.0, and there must be no Pentium Pro instructions (CMOV etc.).
set -euo pipefail
exe=$1
objdump=${OBJDUMP:-i686-w64-mingw32-objdump}
list="$(dirname "$0")/win95-api.txt"
fail=0
# Works with old and new objdump layouts: in each DLL's block, take lines that
# start with a hex address and end with a function name.
imports=$($objdump -p "$exe" | awk '
  /DLL Name:/ {dll = toupper($3); sub(/\.DLL$/, "", dll); next}
  dll && /^\t/ && $1 ~ /^[0-9a-f]+$/ && $NF ~ /^[A-Za-z_][A-Za-z0-9_@]*$/ && NF >= 2 {print dll " " $NF}
  /^$/ {dll = ""}')
[ -n "$imports" ] || { echo "Couldn't read the import table"; exit 1; }
while read -r dll fn; do
  if ! grep -qx "$dll $fn" "$list"; then
    echo "NOT IN WINDOWS 95: $dll $fn"; fail=1
  fi
done <<< "$imports"
sub=$($objdump -p "$exe" | awk '/MajorSubsystemVersion/{print $2}')
os=$($objdump -p "$exe" | awk '/MajorOSystemVersion/{print $2}')
[ "$sub" = 4 ] && [ "$os" = 4 ] || { echo "PE asks for Windows $os / subsystem $sub, not 4"; fail=1; }
if $objdump -d "$exe" | grep -qE '\s(cmov|fcmov|fcomi|fucomi|sysenter|ud2)'; then
  echo "Uses instructions a Pentium doesn't have"; fail=1
fi
n=$(wc -l <<< "$imports")
[ $fail = 0 ] && echo "OK: $n imports, all in Windows 95; PE 4.0; Pentium-safe ($(stat -c %s "$exe") bytes)"
exit $fail
