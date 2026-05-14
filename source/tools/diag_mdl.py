"""Diagnose MDL parsing — mirrors parse_mdl_info from src/MDL/ModelParser.cpp
just enough to figure out where each problem file falls off the happy path."""
import struct
import sys

class R:
    def __init__(self, data, path=""):
        self.p = data
        self.n = len(data)
        self.i = 0
        lp = path.lower()
        self.is_foliage = ('/foliage/' in lp) or ('\\foliage\\' in lp)
    def need(self, k): return self.i + k <= self.n
    def u8(self):
        if not self.need(1): return None
        v = self.p[self.i]; self.i += 1; return v
    def u16(self):
        if not self.need(2): return None
        v = (self.p[self.i] << 8) | self.p[self.i + 1]
        self.i += 2; return v
    def u32(self):
        if not self.need(4): return None
        v = (self.p[self.i] << 24) | (self.p[self.i + 1] << 16) | (self.p[self.i + 2] << 8) | self.p[self.i + 3]
        self.i += 4; return v
    def f32(self):
        v = self.u32()
        if v is None: return None
        return struct.unpack('>f', struct.pack('>I', v))[0]
    def strz(self, maxlen=8192):
        s = bytearray(); lim = min(self.n, self.i + maxlen)
        while self.i < lim:
            c = self.p[self.i]; self.i += 1
            if c == 0: return s.decode('latin-1', errors='replace')
            s.append(c)
        return s.decode('latin-1', errors='replace')
    def skip(self, k):
        if not self.need(k): return False
        self.i += k; return True

def parse(path):
    data = open(path, 'rb').read()
    print(f'\n===== {path} ({len(data)} bytes) =====')
    r = R(data, path)
    magic = data[:8].decode('latin-1', errors='replace')
    has_magic = (magic == 'MeshFile')
    print(f'magic={magic!r} has_magic={has_magic}')
    if has_magic:
        r.i = 8
        tmp = r.u32(); hdrsz = r.u32()
        print(f'  tmp32=0x{tmp:x} HeaderSize=0x{hdrsz:x}')
        r.skip(88)
    r.skip(32)
    bcount = r.u32()
    print(f'BoneCount={bcount} (after magic+32, r.i=0x{r.i-4:x})')
    for i in range(bcount):
        nm = r.strz(); pid = r.u32()
        if i < 4 or i + 1 == bcount:
            print(f'  bone[{i}] name={nm!r} pid=0x{pid:x}')
    btc = r.u32()
    print(f'BoneTransformCount={btc}')
    if btc == bcount and bcount > 0:
        for i in range(btc): r.skip(44)
    else:
        m = min(btc, 65535) if btc is not None else 0
        r.skip(m * 44)
    for k in range(10): r.f32()
    mcount = r.u32()
    r.skip(2 * 4)
    print(f'MeshCount={mcount} r.i=0x{r.i:x}')
    r.skip(12)
    save = r.i
    tc = r.u8()
    if tc and 0 < tc < 32:
        ok = True
        for k in range(tc):
            scan_lim = min(r.n, r.i + 64)
            found_null = False
            for pp in range(r.i, scan_lim):
                b = r.p[pp]
                if b == 0: found_null = (pp > r.i); break
                is_alpha = (0x41 <= b <= 0x5A) or (0x61 <= b <= 0x7A)
                is_alnum = is_alpha or (0x30 <= b <= 0x39) or b == 0x5F
                if pp == r.i:
                    if not is_alpha: found_null = False; break
                else:
                    if not is_alnum: found_null = False; break
            if not found_null: ok = False; break
            s = r.strz(64)
            if not s or len(s) > 32: ok = False; break
            r.u8()
        if not ok: r.i = save
    elif tc != 0:
        r.i = save
    r.skip(5 * 4)
    unk6 = r.u32()
    if unk6 and 0 < unk6 < 65535:
        for i in range(unk6): r.f32()
    sbc = r.u32()
    print(f'StringBlockCount={sbc} Unk6Count={unk6}')
    if sbc and 0 < sbc < 1000000:
        for i in range(sbc): r.strz()
    print(f'BEFORE mesh metadata loop, r.i=0x{r.i:x}')

    # mesh metadata
    for mi in range(mcount):
        if r.i >= r.n:
            print(f'  mesh[{mi}]: EOF')
            return r, mcount
        u1 = r.u32(); nm = r.strz()
        f1 = r.f32(); f2 = r.f32()
        r.skip(21)
        f4 = r.f32()
        u5 = [r.u32() for _ in range(3)]
        mcnt = r.u32()
        print(f'  mesh[{mi}] u1=0x{u1:x} name={nm!r} mat_count={mcnt} (r.i=0x{r.i:x})')
        if mcnt and 0 < mcnt < 65535:
            for j in range(mcnt):
                d = r.strz(); sp = r.strz(); nrm = r.strz(); mt = r.strz(); ex = r.strz()
                r.u32(); r.u32(); r.u32()
                keep = r.i; peek = r.u8()
                if peek != 0x01: r.i = keep
                if j == 0:
                    print(f'    mat[0] diff={d!r}')
    print(f'AFTER mesh metadata, r.i=0x{r.i:x} remaining={len(data) - r.i}')

    # Look ahead: the next section depends on path.  Print the next 64 bytes.
    rem = data[r.i:r.i + 64]
    print(f'NEXT 64 bytes: {rem.hex(" ")}')
    return r, mcount

for p in [
    'cmake-build-debug/extracted/Art/Environment/Regions/Ravenscar/Props/dotXSI/RS_Golden_Acorn/RS_Golden_Acorn.mdl',
    'cmake-build-debug/extracted/Art/Environment/Shared assets/Props/dotXSI/ESA_Wheatsheaves/ESA_Wheatsheaves.mdl',
    'cmake-build-debug/extracted/Art/Environment/Regions/Dunecrest/Buildings/dotXSI/DC_Abbey/DC_Abbey.mdl',
]:
    parse(p)
