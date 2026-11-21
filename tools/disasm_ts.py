"""disasm_ts.py -- disassembles functions of one of the game's ARM .so files, annotated (runs in WSL).

Usage: python3 tools/disasm_ts.py <lib.so> <pattern> [<pattern> ...]
Each pattern is looked up (substring) in the demangled .dynsym names; for each matching
function it prints the disassembly (Thumb or ARM by bit 0 of the symbol) with:
  - the strings it loads (ldr rX,[pc,#] followed by add rX, pc  ->  "text")
  - the literals that are addresses of exported functions
Needs pyelftools and binutils-arm-none-eabi. Read-only; the output is for understanding the code.
"""
import re
import subprocess
import sys

from elftools.elf.elffile import ELFFile


def main():
    lib, pats = sys.argv[1], sys.argv[2:]
    data = open(lib, "rb").read()
    f = open(lib, "rb")
    elf = ELFFile(f)
    loads = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]

    def off_of(va):
        for s in loads:
            if s["p_vaddr"] <= va < s["p_vaddr"] + s["p_filesz"]:
                return s["p_offset"] + va - s["p_vaddr"]
        return None

    def word(va):
        o = off_of(va)
        return int.from_bytes(data[o:o + 4], "little") if o is not None else None

    def cstr(va):
        o = off_of(va)
        if o is None:
            return None
        end = data.find(b"\0", o, o + 200)
        s = data[o:end] if end > o else b""
        return s.decode() if s and all(32 <= b < 127 for b in s) else None

    syms = []
    for sym in elf.get_section_by_name(".dynsym").iter_symbols():
        if sym["st_info"]["type"] == "STT_FUNC" and sym["st_value"]:
            syms.append((sym["st_value"], sym["st_size"], sym.name))
    names = [n for _, _, n in syms]
    dm = subprocess.run(["arm-none-eabi-c++filt"], input="\n".join(names), capture_output=True, text=True).stdout.splitlines()
    byaddr = {v & ~1: d for (v, _, _), d in zip(syms, dm)}

    for (val, size, name), dname in zip(syms, dm):
        if not any(p in dname for p in pats):
            continue
        thumb = val & 1
        start = val & ~1
        print(f"\n===== {dname}  @ {start:#x} size {size} {'thumb' if thumb else 'arm'}")
        cmd = ["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", f"--start-address={start:#x}",
               f"--stop-address={start + max(size, 4):#x}", lib]
        if thumb:
            cmd.insert(2, "-Mforce-thumb")
        out = subprocess.run(cmd, capture_output=True, text=True).stdout.splitlines()
        lit = {}
        for line in out:
            m = re.match(r"\s+([0-9a-f]+):\s+(\S+)\s*(.*)", line)
            if not m:
                continue
            addr, op, args = int(m.group(1), 16), m.group(2), m.group(3)
            note = ""
            ml = re.search(r"ldr(?:\.w)?\s+(r\d+|ip|lr), \[pc, #(-?\d+)\]", op + " " + args)
            if ml:
                pc = ((addr + (4 if thumb else 8)) & ~3)
                v = word(pc + int(ml.group(2)))
                if v is not None:
                    lit[ml.group(1)] = v
                    if v in byaddr or (v & ~1) in byaddr:
                        note = f"  ; -> {byaddr.get(v, byaddr.get(v & ~1))}"
            ma = re.match(r"add\s+(r\d+), pc", op + " " + args)
            if ma and ma.group(1) in lit:
                tgt = (lit[ma.group(1)] + addr + (4 if thumb else 8)) & 0xffffffff
                s = cstr(tgt)
                if s:
                    note = f'  ; "{s}"'
                elif (tgt & ~1) in byaddr:
                    note = f"  ; -> {byaddr[tgt & ~1]}"
                else:
                    note = f"  ; -> {tgt:#x}"
            print(f"  {addr:6x}: {op:8s} {args}{note}")


if __name__ == "__main__":
    main()
