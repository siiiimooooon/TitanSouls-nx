"""apk_info.py -- the APK facts the port needs, without Android tools.

Usage: python -I tools/apk_info.py <apk> [<folder with the extracted .so files>]
Prints: package, versionCode/versionName and activities (from the binary AndroidManifest.xml),
relevant Java classes in classes.dex (FMOD, Amazon, gpg) and, for each .so, its size in
memory (highest p_vaddr + p_memsz of the PT_LOADs), which has to fit in PORT_SO_REGION_BYTES.
"""
import struct
import sys
import zipfile
from pathlib import Path


def axml_strings(data, off):
    _t, _h, size, count, _styles, flags, str_start, _sty_start = struct.unpack_from("<HHIIIIII", data, off)
    utf8 = bool(flags & 0x100)
    offs = struct.unpack_from(f"<{count}I", data, off + 28)
    out = []
    base = off + str_start
    for o in offs:
        p = base + o
        if utf8:
            n = data[p]
            p += 2 if n & 0x80 else 1
            n = data[p]
            if n & 0x80:
                n = ((n & 0x7F) << 8) | data[p + 1]
                p += 2
            else:
                p += 1
            out.append(data[p:p + n].decode("utf-8", "replace"))
        else:
            n = struct.unpack_from("<H", data, p)[0]
            p += 2
            if n & 0x8000:
                n = ((n & 0x7FFF) << 16) | struct.unpack_from("<H", data, p)[0]
                p += 2
            out.append(data[p:p + n * 2].decode("utf-16le", "replace"))
    return out, off + size


def axml(data):
    """Walks the binary XML: a list of (tag, {attr: value})."""
    strings, p = axml_strings(data, 8)
    elems = []
    while p < len(data):
        typ, hsz, size = struct.unpack_from("<HHI", data, p)
        if typ == 0x0102:  # START_TAG
            name = strings[struct.unpack_from("<I", data, p + 20)[0]]
            acount = struct.unpack_from("<H", data, p + 28)[0]
            attrs = {}
            for i in range(acount):
                a = p + 36 + i * 20
                _ns, an, raw, _sz, _z, dtype, dval = struct.unpack_from("<IIIHBBI", data, a)
                key = strings[an] if an < len(strings) else f"#{an}"
                if raw != 0xFFFFFFFF:
                    attrs[key] = strings[raw]
                elif dtype == 0x03:
                    attrs[key] = strings[dval]
                else:
                    attrs[key] = dval
            elems.append((name, attrs))
        p += size
    return elems


def elf_span(path):
    d = Path(path).read_bytes()
    assert d[:4] == b"\x7fELF" and d[4] == 1, "not an ELF32"
    phoff, = struct.unpack_from("<I", d, 28)
    phentsize, phnum = struct.unpack_from("<HH", d, 42)
    top = 0
    for i in range(phnum):
        ptype, _off, vaddr, _pa, _fs, memsz = struct.unpack_from("<IIIIII", d, phoff + i * phentsize)
        if ptype == 1:
            top = max(top, vaddr + memsz)
    return top


def main():
    apk = zipfile.ZipFile(sys.argv[1])
    for name, attrs in axml(apk.read("AndroidManifest.xml")):
        if name in ("manifest", "uses-sdk", "activity", "application", "meta-data", "uses-feature"):
            keep = {k: v for k, v in attrs.items() if k in (
                "package", "versionCode", "versionName", "name", "minSdkVersion", "targetSdkVersion",
                "value", "screenOrientation", "required", "hasCode")}
            print(f"{name}: {keep}")
    dex = apk.read("classes.dex")
    for needle in (b"Lorg/fmod/FMODAudioDevice;", b"FMODAudioDevice", b"Lcom/amazon/ags", b"Lcom/google/android/gms/games",
                   b"Lcom/devolver/titansouls/", b"loadLibrary", b"getObbDir", b"getExternalFilesDir", b"OpenSL"):
        print(f"dex {needle.decode():40s} {'yes' if needle in dex else 'no'}")
    # class names in the game's package
    import re
    cls = sorted(set(m.decode() for m in re.findall(rb"Lcom/devolver/titansouls/[A-Za-z0-9_$/]+;", dex)))
    print("game classes:", ", ".join(cls))
    if len(sys.argv) > 2:
        for so in sorted(Path(sys.argv[2]).glob("*.so")):
            top = elf_span(so)
            print(f"{so.name}: {top:#x} bytes in memory ({top / 2**20:.1f} MB)")


if __name__ == "__main__":
    main()
