"""Upload files to the Switch's SD card and fetch logs over wifi (Sphaira's FTP server, which is ftpsrv). Stdlib only.

It uploads under another name, downloads it back and compares the SHA-256 before replacing, and keeps the old file as a copy.

Usage:
  python -I tools/switch_ftp.py ping
  python -I tools/switch_ftp.py push <local folder> <file> [...]      uploads to <root>/<file>, verified
                                                    (<local>=<remote> uploads it under another name;
                                                     <file>%2048 splits it into <file>.0, .1, ... of 2048 MB)
  python -I tools/switch_ftp.py pull <destination folder> <file> [...] fetches those that exist (the rest are skipped)
  python -I tools/switch_ftp.py rm <file> [...]                       deletes from <root> those that exist

Config: tools/switch_ftp.json (not committed): {"host": "192.168.x.y", "port": 5000, "user": "", "pass": "",
"dir": "/switch/titansouls_nx"}. Empty user = anonymous. Without "dir" it is detected (with or without the sdmc: prefix).
Another folder: environment variable TS_FTP_SUB=switch/<folder> (replaces the last part of "dir").
"""
import ftplib
import hashlib
import json
import os
import posixpath
import sys

CFG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "switch_ftp.json")
SUB = os.environ.get("TS_FTP_SUB") or "switch/titansouls_nx"


def load_cfg():
    if not os.path.exists(CFG):
        raise SystemExit(f"FAILED: {CFG} is missing (the Switch's host)")
    with open(CFG, encoding="utf-8-sig") as f:
        return json.load(f)


def connect(cfg):
    ftp = ftplib.FTP()
    try:
        ftp.connect(cfg["host"], int(cfg.get("port", 5000)), timeout=15)
    except OSError as e:
        raise SystemExit(f"FAILED: cannot connect to {cfg['host']}:{cfg.get('port', 5000)} ({e}). "
                         "Is Sphaira open with FTP enabled, and the Switch on the same wifi?")
    user = cfg.get("user") or "anonymous"
    ftp.login(user, cfg.get("pass") or "")
    return ftp


def exists_dir(ftp, path):
    try:
        ftp.cwd(path)
        return True
    except ftplib.error_perm:
        return False


def remote_dir(ftp, cfg, create):
    """The game folder on the SD card. ftpsrv may show the SD card at / or under a device (sdmc:)."""
    if cfg.get("dir"):
        base = cfg["dir"]
        if os.environ.get("TS_FTP_SUB"):
            base = posixpath.join(posixpath.dirname(base.rstrip("/")), posixpath.basename(SUB))
        cands = [base]
    else:
        cands = ["/" + SUB, "/sdmc:/" + SUB, "/sdmc/" + SUB]
    for c in cands:
        if exists_dir(ftp, c):
            return c
    if create:
        for c in cands:
            if exists_dir(ftp, posixpath.dirname(c)):
                ftp.mkd(c)
                return c
    raise SystemExit(f"FAILED: {SUB} not found on the SD card over FTP (tried {', '.join(cands)})")


def names_in(ftp, d):
    try:
        return {posixpath.basename(n) for n in ftp.nlst(d)}
    except ftplib.error_perm:
        return set()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


class Range:
    """A read-only file limited to [offset, offset + length) (to upload parts without copying them)."""

    def __init__(self, path, offset, length):
        self.f = open(path, "rb")
        self.f.seek(offset)
        self.left = length

    def read(self, n=-1):
        n = self.left if n < 0 else min(n, self.left)
        data = self.f.read(n)
        self.left -= len(data)
        return data

    def close(self):
        self.f.close()


def sha256_range(path, offset, length):
    h = hashlib.sha256()
    r = Range(path, offset, length)
    for b in iter(lambda: r.read(1 << 20), b""):
        h.update(b)
    r.close()
    return h.hexdigest()


def jobs_for(local_dir, entry):
    """'file', 'local=remote' or 'file%MB' (split into <remote>.0, .1, ... of MB megabytes: FAT32 takes no 4 GB files)."""
    entry, _, split = entry.partition("%")
    local_name, _, remote_name = entry.partition("=")
    src = os.path.join(local_dir, local_name)
    name = remote_name or local_name
    size = os.path.getsize(src)
    if not split:
        return [(src, 0, size, name, True)]
    part = int(split) << 20
    return [(src, off, min(part, size - off), f"{name}.{i}", False) for i, off in enumerate(range(0, size, part))]


def push(ftp, d, local_dir, files):
    ok = True
    for entry in files:
        for src, offset, length, name, keep_previous in jobs_for(local_dir, entry):
            if name.endswith(".nro"):
                with open(src, "rb") as f:
                    f.seek(16)
                    if f.read(4) != b"NRO0":
                        print(f"  FAILED: {name} has no NRO header")
                        ok = False
                        continue
            want = sha256_range(src, offset, length)
            dst = posixpath.join(d, name)
            # name may carry subfolders (disc/MODULES/X.IRX): create them, and compare within that folder.
            parent = posixpath.dirname(dst)
            if parent != d:
                walk = d
                for part in posixpath.relpath(parent, d).split("/"):
                    walk = posixpath.join(walk, part)
                    if not exists_dir(ftp, walk):
                        ftp.mkd(walk)
            base = posixpath.basename(dst)
            tmp = dst + ".upload"
            r = Range(src, offset, length)
            try:
                ftp.storbinary("STOR " + tmp, r, blocksize=1 << 18)
            finally:
                r.close()
            got = hashlib.sha256()
            ftp.retrbinary("RETR " + tmp, got.update, blocksize=1 << 18)
            if got.hexdigest() != want:
                print(f"  FAILED: {name} arrived different (SHA-256); the previous one stays")
                try:
                    ftp.delete(tmp)
                except ftplib.Error:
                    pass
                ok = False
                continue
            present = names_in(ftp, parent)
            prev = dst + ".previous"
            if base in present:
                if base + ".previous" in present:
                    ftp.delete(prev)
                if keep_previous:
                    ftp.rename(dst, prev)
                else:
                    ftp.delete(dst)
            try:
                ftp.rename(tmp, dst)
            except ftplib.Error:
                if base in present and keep_previous:
                    ftp.rename(prev, dst)
                raise
            print(f"  {name}: {length} bytes, SHA-256 matches", flush=True)
    return ok


def pull(ftp, d, dest, files):
    os.makedirs(dest, exist_ok=True)
    present = names_in(ftp, d)
    got = []
    for name in files:
        if name not in present:
            continue
        part = os.path.join(dest, name + ".partial")
        try:
            with open(part, "wb") as f:
                ftp.retrbinary("RETR " + posixpath.join(d, name), f.write)
        except ftplib.Error as e:
            print(f"  {name}: could not download ({e})")
            os.remove(part)
            continue
        os.replace(part, os.path.join(dest, name))
        got.append(name)
    print("  fetched: " + (", ".join(got) if got else "none"))
    return bool(got)


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("ping", "push", "pull", "rm"):
        print(__doc__)
        return 2
    cfg = load_cfg()
    mode = sys.argv[1]
    with connect(cfg) as ftp:
        d = remote_dir(ftp, cfg, create=(mode == "push"))
        if mode == "ping":
            print(f"OK: FTP at {cfg['host']}, folder {d}")
            return 0
        if mode == "rm":
            present = names_in(ftp, d)
            for name in sys.argv[2:]:
                if name in present:
                    ftp.delete(f"{d}/{name}")
                    print(f"  deleted {name}")
            return 0
        if len(sys.argv) < 4:
            print(__doc__)
            return 2
        if mode == "push":
            return 0 if push(ftp, d, sys.argv[2], sys.argv[3:]) else 1
        return 0 if pull(ftp, d, sys.argv[2], sys.argv[3:]) else 1


if __name__ == "__main__":
    sys.exit(main())
