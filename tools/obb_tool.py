"""obb_tool.py -- lists or extracts files from the Titan Souls OBB (its own format, not a zip).

Format (seen in main.31.com.devolver.titansouls.obb):
  u32 count, and for each file: u32 name_length, name, u64 size, u64 offset
  (the offset counts from the end of the index).

Usage:
  python -I tools/obb_tool.py list <obb> [filter]           names, sizes and offsets
  python -I tools/obb_tool.py extract <obb> <output> <name> [...]
What is extracted is the user's game: it goes to game/ (ignored by git), never into the repository.
"""
import struct
import sys
from pathlib import Path


def index(path):
    with open(path, "rb") as f:
        n, = struct.unpack("<I", f.read(4))
        entries = []
        for _ in range(n):
            ln, = struct.unpack("<I", f.read(4))
            name = f.read(ln).decode("utf-8", "replace")
            size, off = struct.unpack("<QQ", f.read(16))
            entries.append((name, size, off))
        base = f.tell()
    return entries, base


def main():
    cmd, obb = sys.argv[1], sys.argv[2]
    entries, base = index(obb)
    if cmd == "list":
        flt = sys.argv[3].lower() if len(sys.argv) > 3 else ""
        total = 0
        for name, size, off in entries:
            if flt in name.lower():
                print(f"{size:10d}  {base + off:#011x}  {name}")
                total += 1
        print(f"{total} of {len(entries)} files; data from {base:#x}")
    elif cmd == "extract":
        out = Path(sys.argv[3])
        want = set(sys.argv[4:])
        with open(obb, "rb") as f:
            for name, size, off in entries:
                if name in want:
                    f.seek(base + off)
                    dst = out / name
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    dst.write_bytes(f.read(size))
                    print("extracted", dst, size)


if __name__ == "__main__":
    main()
