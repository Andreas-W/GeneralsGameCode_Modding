# Reads game files the way the engine finds them (Win32BIGFileSystem, ArchiveFileSystem): loose files in the game folder first, then
# every .big under it in sorted path order, where the first archive holding a file wins, then the original Generals archives from the
# registry's InstallPath. Nothing is extracted.
# Usage as a script: bigfs.py list <pattern> | bigfs.py cat <name> | bigfs.py where <name>   (GAME_DIR must be set)
import fnmatch, os, struct, sys


class BigFS:
    def __init__(self, game_dir=None, generals_dir=None):
        self.game_dir = game_dir or os.environ["GAME_DIR"]
        self.index = {}    # lower-case name -> (archive path, offset, size)
        roots = [self.game_dir]
        g = generals_dir if generals_dir is not None else _generals_install_path()
        if g and os.path.isdir(g) and os.path.normcase(g) != os.path.normcase(self.game_dir):
            roots.append(g)
        for root in roots:
            bigs = []
            for d, _, files in os.walk(root):
                bigs += [os.path.join(d, f) for f in files if f.lower().endswith(".big")]
            for path in sorted(bigs):
                # The engine skips Zero Hour's duplicate INIZH.big in Data\INI.
                if path.replace("/", "\\").lower().endswith("data\\ini\\inizh.big"):
                    continue
                for name, off, size in _read_index(path):
                    self.index.setdefault(name.lower().replace("/", "\\"), (path, off, size))

    def loose(self, name):
        p = os.path.join(self.game_dir, name.replace("\\", os.sep))
        return p if os.path.isfile(p) else None

    def exists(self, name):
        return bool(self.loose(name)) or name.lower().replace("/", "\\") in self.index

    def where(self, name):
        p = self.loose(name)
        if p:
            return p
        e = self.index.get(name.lower().replace("/", "\\"))
        return e[0] if e else None

    def read(self, name):
        p = self.loose(name)
        if p:
            return open(p, "rb").read()
        e = self.index.get(name.lower().replace("/", "\\"))
        if not e:
            raise FileNotFoundError(name)
        with open(e[0], "rb") as f:
            f.seek(e[1])
            return f.read(e[2])

    def glob(self, pattern):
        """Archived names matching a pattern (lower case, backslashes), loose files not included."""
        pat = pattern.lower().replace("/", "\\")
        return sorted(n for n in self.index if fnmatch.fnmatch(n, pat))


def _read_index(path):
    # BIGF / BIG4: magic, archive size (little endian), file count and index size (big endian), then per file offset and size
    # (big endian) and a zero-terminated name.
    with open(path, "rb") as f:
        head = f.read(16)
        if head[:4] not in (b"BIGF", b"BIG4"):
            return []
        count = struct.unpack(">I", head[8:12])[0]
        out = []
        for _ in range(count):
            off, size = struct.unpack(">II", f.read(8))
            name = bytearray()
            while True:
                c = f.read(1)
                if not c or c == b"\0":
                    break
                name += c
            out.append((name.decode("latin-1"), off, size))
        return out


def _generals_install_path():
    try:
        import winreg
    except ImportError:
        return None
    for key in (r"SOFTWARE\WOW6432Node\Electronic Arts\EA Games\Generals", r"SOFTWARE\Electronic Arts\EA Games\Generals"):
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as k:
                return winreg.QueryValueEx(k, "InstallPath")[0]
        except OSError:
            continue
    return None


if __name__ == "__main__":
    fs = BigFS()
    cmd, arg = sys.argv[1], sys.argv[2]
    if cmd == "list":
        for n in fs.glob(arg):
            print(n, fs.index[n][0])
    elif cmd == "cat":
        sys.stdout.buffer.write(fs.read(arg))
    elif cmd == "where":
        print(fs.where(arg))
