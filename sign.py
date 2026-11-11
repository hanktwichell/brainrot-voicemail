#!/usr/bin/env python3
"""Ad-hoc code signing for a thin Mach-O, binding Info.plist and CodeResources (like `codesign -s -`)."""
import hashlib, struct, sys

LC_SEGMENT_64, LC_CODE_SIGNATURE = 0x19, 0x1D
PAGE = 4096


def parse(buf):
    magic, cpu, sub, ftype, ncmds, sizeofcmds, flags, _ = struct.unpack_from("<IiiIIIII", buf, 0)
    assert magic == 0xFEEDFACF
    off, segs, cs = 32, {}, None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", buf, off)
        if cmd == LC_SEGMENT_64:
            name = buf[off + 8:off + 24].rstrip(b"\0").decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", buf, off + 24)
            segs[name] = dict(off=off, vmaddr=vmaddr, vmsize=vmsize, fileoff=fileoff, filesize=filesize)
        elif cmd == LC_CODE_SIGNATURE:
            cs = dict(off=off, dataoff=struct.unpack_from("<I", buf, off + 8)[0], datasize=struct.unpack_from("<I", buf, off + 12)[0])
        off += size
    return cpu, segs, cs


def code_directory(code, ident, text_seg, special, flags):
    n_code = (len(code) + PAGE - 1) // PAGE
    nspecial = len(special)
    ident_b = ident.encode() + b"\0"
    ident_b += b"\0" * ((-(88 + len(ident_b))) % 8)   # align like ld64/lld
    hdr_len = 88
    hash_off = hdr_len + len(ident_b) + 32 * nspecial
    length = hash_off + 32 * n_code
    hdr = struct.pack(">IIIIIIIIIBBBBIIIIQQQQ",
                      0xFADE0C02, length, 0x20400, flags, hash_off, hdr_len, nspecial, n_code, len(code),
                      32, 2, 0, 12, 0, 0, 0, 0, 0,
                      text_seg["fileoff"], text_seg["filesize"], 1)
    assert len(hdr) == hdr_len
    specials = b"".join(special[i] for i in range(nspecial - 1, -1, -1))  # slot -n ... -1
    pages = b"".join(hashlib.sha256(code[i:i + PAGE]).digest() for i in range(0, len(code), PAGE))
    return hdr + ident_b + specials + pages


def superblob(blobs):
    count = len(blobs)
    off = 12 + 8 * count
    index, body = b"", b""
    off = (off + 7) & ~7
    for typ, blob in blobs:
        body += b"\0" * ((-(off + len(body))) % 8)   # 8-byte align blobs like lld
        index += struct.pack(">II", typ, off + len(body))
        body += blob
    head = struct.pack(">III", 0xFADE0CC0, off + len(body), count) + index
    return head + b"\0" * (off - len(head)) + body


def sign(buf, ident, info_plist=None, resources=None, flags=0x2):
    buf = bytearray(buf)
    cpu, segs, cs = parse(buf)
    assert cs, "linker did not reserve LC_CODE_SIGNATURE"
    codelimit = cs["dataoff"]
    requirements = struct.pack(">III", 0xFADE0C01, 12, 0)
    special = []
    if info_plist is not None:
        special = [hashlib.sha256(info_plist).digest(),            # -1 Info.plist
                   hashlib.sha256(requirements).digest(),          # -2 requirements
                   hashlib.sha256(resources).digest()]             # -3 CodeResources
    # size is deterministic: compute with dummy code first, patch headers, then hash for real
    def build(code):
        cd = code_directory(code, ident, segs["__TEXT"], special, flags)
        blobs = [(0, cd)]
        if info_plist is not None:
            blobs += [(2, requirements), (0x10000, struct.pack(">II", 0xFADE0B01, 8))]
        return superblob(blobs)
    size = len(build(bytes(codelimit)))
    size_padded = (size + 15) & ~15
    struct.pack_into("<I", buf, cs["off"] + 12, size_padded)
    le = segs["__LINKEDIT"]
    new_filesize = codelimit + size_padded - le["fileoff"]
    align = 0x4000 if cpu == 0x0100000C else 0x1000
    struct.pack_into("<Q", buf, le["off"] + 32, new_filesize)  # vmsize (lld convention)
    struct.pack_into("<Q", buf, le["off"] + 48, new_filesize)                                # filesize
    code = bytes(buf[:codelimit])
    sig = build(code)
    assert len(sig) == size
    return code + sig + bytes(size_padded - size)


if __name__ == "__main__":
    src, dst, ident = sys.argv[1:4]
    info = open(sys.argv[4], "rb").read() if len(sys.argv) > 4 else None
    res = open(sys.argv[5], "rb").read() if len(sys.argv) > 5 else None
    out = sign(open(src, "rb").read(), ident, info, res)
    open(dst, "wb").write(out)
