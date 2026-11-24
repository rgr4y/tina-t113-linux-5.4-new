#!/usr/bin/env python3
"""mkbootimg.py VENDOR_BOOT.img zImage OUT.img [--max-bytes N]

Repack the MK10 vendor Android v0 boot image (boot partition, mmcblk0p4) with a new
kernel. The header is kept byte for byte (load addresses, page size, name "t113-pro",
empty cmdline: U-Boot builds bootargs from its env), and so are the 12-byte ramdisk and
the second stage. Only kernel_size and the id (SHA1 over the payloads, as AOSP mkbootimg
computes it) change. The id is checked against the vendor image first, so a wrong
layout guess fails loudly instead of producing a bad image.
"""
import hashlib
import struct
import sys


def pad(n, page):
    return (n + page - 1) // page * page


def parse(img):
    if img[:8] != b'ANDROID!':
        sys.exit('not an Android boot image')
    ksz, kaddr, rsz, raddr, ssz, saddr, tags, page = struct.unpack_from('<8I', img, 8)
    koff = page
    roff = koff + pad(ksz, page)
    soff = roff + pad(rsz, page)
    return dict(ksz=ksz, rsz=rsz, ssz=ssz, page=page,
                kernel=img[koff:koff + ksz], ramdisk=img[roff:roff + rsz], second=img[soff:soff + ssz])


def image_id(kernel, ramdisk, second):
    h = hashlib.sha1()
    for blob in (kernel, ramdisk, second):
        h.update(blob)
        h.update(struct.pack('<I', len(blob)))
    return h.digest()


def main():
    args = sys.argv[1:]
    max_bytes = None
    if '--max-bytes' in args:
        i = args.index('--max-bytes')
        max_bytes = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 3:
        sys.exit(__doc__)
    vendor_path, zimage_path, out_path = args
    vendor = open(vendor_path, 'rb').read()
    p = parse(vendor)
    stored = vendor[576:576 + 20]
    if image_id(p['kernel'], p['ramdisk'], p['second']) != stored:
        sys.exit('vendor id mismatch: header layout assumption is wrong, refusing')
    print(f'vendor: kernel {p["ksz"]} B, ramdisk {p["rsz"]} B, second {p["ssz"]} B, page {p["page"]}, id ok')

    kernel = open(zimage_path, 'rb').read()
    if struct.unpack_from('<I', kernel, 0x24)[0] != 0x016f2818:
        sys.exit(f'{zimage_path}: no zImage magic at 0x24')
    page = p['page']
    hdr = bytearray(vendor[:page])
    struct.pack_into('<I', hdr, 8, len(kernel))
    hdr[576:576 + 32] = image_id(kernel, p['ramdisk'], p['second']).ljust(32, b'\0')

    out = bytearray(hdr)
    for blob in (kernel, p['ramdisk'], p['second']):
        if blob:
            out += blob + b'\0' * (pad(len(blob), page) - len(blob))
    if max_bytes is not None and len(out) > max_bytes:
        sys.exit(f'{len(out)} B does not fit the {max_bytes} B boot partition')
    open(out_path, 'wb').write(out)

    q = parse(bytes(out))
    assert q['kernel'] == kernel and q['ramdisk'] == p['ramdisk'] and q['second'] == p['second']
    assert bytes(out[576:596]) == image_id(kernel, p['ramdisk'], p['second'])
    assert bytes(out[:8]) == vendor[:8] and bytes(out[12:576]) == vendor[12:576]
    print(f'wrote {out_path}: {len(out)} B, kernel {len(kernel)} B'
          + (f', fits {max_bytes} B' if max_bytes else ''))


if __name__ == '__main__':
    main()
