"""The Switch's screenshots over FTP: python -I tools/switch_album.py list 2026/11/17 | get 2026/11/17 <dest> <prefix>"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import switch_ftp as sf
mode, day = sys.argv[1], sys.argv[2]   # list|get, "2026/11/16"
cfg = sf.load_cfg()
with sf.connect(cfg) as ftp:
    d = "/album_sd:/" + day
    try:
        names = sorted(ftp.nlst(d))
    except Exception as e:
        print("FAILED", d, e); sys.exit(1)
    names = [os.path.basename(n) for n in names]
    if mode == "list":
        print(len(names)); print("\n".join(names[-40:]))
    else:
        dest, prefix = sys.argv[3], sys.argv[4]
        os.makedirs(dest, exist_ok=True)
        for n in names:
            if n >= prefix:
                with open(os.path.join(dest, n), "wb") as f:
                    ftp.retrbinary("RETR " + d + "/" + n, f.write)
                print("downloaded", n)
