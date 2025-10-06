#!/usr/bin/env python3
"""
Diagnostic build: Fable2 .tex inspector
Prints first 128 bytes of each decompressed mip to verify actual content.
"""

import os, sys, struct
from texparser import TextureParser
from lzx import decompress as lzx_decompress, LZXError

def _expected_bc1_size(w,h): return ((w+3)//4)*((h+3)//4)*8

def _try_lzx_variants(blob,want,wb_hint):
    for wb in range(15,22):
        for ofs in (0,2,4,8,12,16):
            try:
                out=lzx_decompress(blob[ofs:],wb,want)
                if len(out)==want:
                    print(f"[LZX] ok @offset={ofs} wb={wb} ({len(out)}/{want})")
                    return out
            except LZXError:
                pass
    return None

def main(path):
    parser=TextureParser(file_path=path,debug=False)
    print(f"File: {os.path.basename(path)}")
    print(f"Dimensions: {parser.texture_width} x {parser.texture_height}")
    print(f"PixelFormat: {parser.pixel_format}   MipCount: {parser.mipmap_count}")
    print("="*60)
    for i,m in enumerate(parser.mipmap_defs):
        w=max(4,parser.texture_width>>i)
        h=max(4,parser.texture_height>>i)
        want=_expected_bc1_size(w,h)
        print(f"Mip {i}: {w}x{h}  comp_flag={m.comp_flag}  data_size={m.data_size}")
        if m.comp_flag==7:
            data=m.mipmap_data
            print(f" [RAW] {len(data)} bytes")
        else:
            comp=m.mipmap_data
            out=_try_lzx_variants(comp,want,m.window_bits or 21)
            if not out:
                print("  ✗ decompress failed\n")
                continue
            # dump first 128 bytes
            print("  First 128 bytes of decompressed data:")
            print("   "+" ".join(f"{b:02X}" for b in out[:128]))
            print()
    print("="*60)

if __name__=="__main__":
    if len(sys.argv)<2:
        print("usage: python inspect_tex.py <file.tex>")
        sys.exit(1)
    main(sys.argv[1])
