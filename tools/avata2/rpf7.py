"""Minimal RPF7 archive reader and writer (unencrypted "OPEN" archives only).

GTA V packs mod files in RPF7 archives. OpenRPF/onigiri load OPEN (unencrypted)
archives, so that is all this writes: directories, plain binary files and
resource files (the .ydr/.ytd/.ytyp... RSC7 files Sollumz exports).

    python rpf7.py list <archive.rpf>          # prints the tree, resource flags
"""
import struct
import sys

OPEN = 0x4E45504F
BLOCK = 512


def _entries(data):
    magic, count, names_len, enc = struct.unpack_from("<4I", data, 0)
    if magic != 0x52504637:
        raise ValueError("not an RPF7 archive")
    if enc != OPEN:
        raise ValueError(f"encrypted TOC ({enc:#x}), only OPEN archives can be read")
    toc = data[16:16 + count * 16]
    names = data[16 + count * 16:16 + count * 16 + names_len]
    out = []
    for i in range(count):
        e = toc[i * 16:(i + 1) * 16]
        a, b, c, d = struct.unpack("<4I", e)
        if b == 0x7FFFFF00:
            out.append(("dir", a, c, d))  # name offset, first entry, entry count
        else:
            v = struct.unpack_from("<Q", e, 0)[0]
            name_off = v & 0xFFFF
            size = (v >> 16) & 0xFFFFFF
            offset = (v >> 40) & 0xFFFFFF
            if offset & 0x800000:
                out.append(("res", name_off, size, offset & 0x7FFFFF, c, d))  # system, graphics flags
            else:
                out.append(("bin", name_off, size, offset, c, d))  # uncompressed size, encryption
    name = lambda off: names[off:names.index(b"\0", off)].decode()
    return out, name


def list_archive(path, data=None, indent=""):
    data = data if data is not None else open(path, "rb").read()
    entries, name = _entries(data)

    def walk(i, depth):
        kind = entries[i]
        if kind[0] == "dir":
            for j in range(kind[2], kind[2] + kind[3]):
                e = entries[j]
                label = name(e[1])
                if e[0] == "dir":
                    print(f"{indent}{'  ' * depth}{label}/")
                    walk(j, depth + 1)
                elif e[0] == "res":
                    print(f"{indent}{'  ' * depth}{label}  resource {e[2]} B, system {e[4]:#010x} graphics {e[5]:#010x}")
                else:
                    print(f"{indent}{'  ' * depth}{label}  {e[2] or e[4]} B")
                    if label.endswith(".rpf"):
                        start = e[3] * BLOCK
                        list_archive(None, data[start:start + (e[2] or e[4])], indent + "  " * (depth + 1))

    walk(0, 0)


def read_resource(path):
    """An exported RSC7 resource: (system flags, graphics flags, file bytes). Archives store the
    file whole, its 16-byte RSC7 header included, and repeat the flags in the entry."""
    raw = open(path, "rb").read()
    magic, version, sysf, gfxf = struct.unpack_from("<4I", raw, 0)
    if magic != 0x37435352:  # "RSC7"
        raise ValueError(f"{path}: not an RSC7 resource ({magic:#x})")
    return sysf, gfxf, raw


def build(tree):
    """tree: {"name": bytes | ("res", sysf, gfxf, payload) | dict}. Returns the archive bytes."""
    entries, names, blobs = [], bytearray(b"\0"), []  # root's name is ""
    name_index = {"": 0}

    def name_off(n):
        if n not in name_index:
            name_index[n] = len(names)
            names.extend(n.encode() + b"\0")
        return name_index[n]

    # Breadth-first: each directory's children are consecutive entries, sorted by name.
    entries.append(["dir", 0, 0, 0])
    queue = [(0, tree)]
    while queue:
        di, node = queue.pop(0)
        items = sorted(node.items(), key=lambda kv: kv[0].lower())
        entries[di][2] = len(entries)
        entries[di][3] = len(items)
        for n, v in items:
            idx = len(entries)
            if isinstance(v, dict):
                entries.append(["dir", name_off(n), 0, 0])
                queue.append((idx, v))
            elif isinstance(v, tuple):
                entries.append(["res", name_off(n), v])
            else:
                entries.append(["bin", name_off(n), v])
    while len(names) % 16:
        names.append(0)
    header_size = 16 + len(entries) * 16 + len(names)
    offset = (header_size + BLOCK - 1) // BLOCK
    toc = bytearray()
    for e in entries:
        if e[0] == "dir":
            toc += struct.pack("<4I", e[1], 0x7FFFFF00, e[2], e[3])
            continue
        payload = e[2][3] if e[0] == "res" else e[2]
        if e[0] == "res":
            v = e[1] | (len(payload) << 16) | ((offset | 0x800000) << 40)
            toc += struct.pack("<QII", v, e[2][1], e[2][2])
        else:
            v = e[1] | (0 << 16) | (offset << 40)  # size 0: stored uncompressed
            toc += struct.pack("<QII", v, len(payload), 0)
        blobs.append((offset, payload))
        offset += (len(payload) + BLOCK - 1) // BLOCK
    out = bytearray(struct.pack("<4I", 0x52504637, len(entries), len(names), OPEN) + toc + names)
    for off, payload in blobs:
        out.extend(b"\0" * (off * BLOCK - len(out)))
        out.extend(payload)
    out.extend(b"\0" * (-len(out) % BLOCK))
    return bytes(out)


if __name__ == "__main__" and len(sys.argv) == 3 and sys.argv[1] == "list":
    list_archive(sys.argv[2])
