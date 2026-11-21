"""symbolize_ts.py -- names the "[prof]" lines of debug.log (ts_sampler.c).

Runs in WSL (needs pyelftools and arm-none-eabi-c++filt):
  python3 tools/symbolize_ts.py <debug.log> <folder with the .so files>
Each "<lib>.so+0xOFF" becomes "<lib>.so+0xOFF function+delta", with the nearest
exported function below it (the game's .so files export almost everything).
"""
import bisect
import re
import subprocess
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile


def load_syms(path):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        out = []
        for secname in (".dynsym", ".symtab"):
            sec = elf.get_section_by_name(secname)
            if not sec:
                continue
            for s in sec.iter_symbols():
                if s["st_info"]["type"] == "STT_FUNC" and s["st_value"] and s.name:
                    out.append(((s["st_value"] & ~1), s.name))
    out.sort()
    return [a for a, _ in out], [n for _, n in out]


def demangle(names):
    if not names:
        return {}
    r = subprocess.run(["arm-none-eabi-c++filt"], input="\n".join(names), capture_output=True, text=True)
    return dict(zip(names, r.stdout.splitlines()))


def main():
    log, libdir = sys.argv[1], Path(sys.argv[2])
    tables = {}
    lines = [l.rstrip("\n") for l in open(log, encoding="utf-8", errors="replace") if "[prof]" in l]
    refs = set(re.findall(r"(lib\w+\.so)\+0x([0-9a-f]+)", "\n".join(lines)))
    for lib, _ in refs:
        if lib not in tables and (libdir / lib).exists():
            tables[lib] = load_syms(libdir / lib)
    raw = {}
    for lib, off in refs:
        if lib not in tables:
            continue
        addrs, names = tables[lib]
        o = int(off, 16)
        i = bisect.bisect_right(addrs, o) - 1
        if i >= 0:
            raw[(lib, off)] = (names[i], o - addrs[i])
    dm = demangle(sorted({n for n, _ in raw.values()}))

    def sub(m):
        key = (m.group(1), m.group(2))
        if key not in raw:
            return m.group(0)
        n, d = raw[key]
        name = dm.get(n, n)
        if len(name) > 90:
            name = name[:87] + "..."
        return f"{m.group(0)} [{name}+{d:#x}]"

    for l in lines:
        print(re.sub(r"(lib\w+\.so)\+0x([0-9a-f]+)", sub, l))


if __name__ == "__main__":
    main()
