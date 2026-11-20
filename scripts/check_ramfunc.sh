#!/usr/bin/env bash
# Verify that the RAM-resident flash code never branches into flash.
#
# Disassembles the .RamFunc code of the ELF (loaded in SRAM 0x2000xxxx) and fails if any
# branch/call target lies in flash (0x08xxxxxx) or a call goes through a register
# (literal 0x08xxxxxx words are fine: they are the flash addresses written). A compiler-emitted
# memcpy/memset call, a jump table in .rodata or a stray helper would show up here and
# would stall the core while the bootloader pages or the slot are being erased.
#
# usage: scripts/check_ramfunc.sh build/Release/open-lifu-transmitter-bl-updater.elf
set -euo pipefail
ELF="${1:?elf path}"
OBJDUMP="${OBJDUMP:-arm-none-eabi-objdump}"
NM="${NM:-arm-none-eabi-nm}"

# Symbols defined in the RAM function section (section name as emitted by GCC).
SYMS=$("$NM" --defined-only "$ELF" | awk '$3 ~ /^(flash_ram_rewrite_bootloader|flash_ram_erase_pages_and_reset|ram_wait_idle|ram_unlock|ram_erase_page|ram_flush_caches|ram_word)$/ {print $3}')
if [ -z "$SYMS" ]; then
  echo "check_ramfunc: RAM function symbols not found in $ELF" >&2; exit 1
fi

fail=0
for s in $SYMS; do
  addr=$("$NM" --defined-only "$ELF" | awk -v s="$s" '$3==s {print $1}' | head -1)
  case "$addr" in
    2000*|2001*) ;;
    *) echo "check_ramfunc: $s is at 0x$addr, not in SRAM (.RamFunc not placed in RAM?)" >&2; fail=1 ;;
  esac
  # Disassemble just this symbol and look for flash addresses in branch targets or literals.
  dis=$("$OBJDUMP" -d --disassemble="$s" "$ELF")
  bad=$(echo "$dis" | grep -E '\b(bl|b|b\.w|bl\.w|blx|bx|cbz|cbnz|b(eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)(\.w)?)\s+(0x)?8[0-9a-f]{6}|<memcpy|<memset|<__aeabi|\bblx\s+r[0-9]' || true)
  if [ -n "$bad" ]; then
    echo "check_ramfunc: $s references flash:" >&2; echo "$bad" >&2; fail=1
  fi
done
if [ "$fail" -eq 0 ]; then echo "check_ramfunc: OK ($SYMS run from SRAM, no flash references)"; fi
exit $fail
