#!/usr/bin/env python3

import struct
import sys
import os
from typing import Optional

# Import the LZX decompressor and the TextureParser
try:
    from lzx import decompress as lzx_decompress, LZXError
    from texparser import TextureParser
except ImportError as e:
    print(f"ERROR: A required file is missing. ({e})")
    print("Please ensure lzx.py and texparser.py are in the same directory.")
    sys.exit(1)


def decompress_lzx(data: bytes, expected_size: int, window_bits: int) -> Optional[bytes]:
    """
    Attempts to decompress LZX data using a known window size,
    trying a few common header offsets.
    """
    if not (15 <= window_bits <= 21):
        print(f"  ✗ Invalid window_bits value ({window_bits}). Must be 15-21.")
        return None

    # Try a few small offsets for any remaining unknown header bytes before the stream
    for offset in [0, 2, 4, 8]:
        if offset >= len(data):
            continue
        try:
            result = lzx_decompress(data[offset:], window_bits, expected_size)
            if len(result) == expected_size:
                print(f"  ✓ Success: LZX with {offset}-byte offset and {1 << window_bits}B window.")
                return result
        except LZXError:
            pass # This is expected if the offset is wrong
    
    print(f"  ✗ LZX decompression failed.")
    return None

def convert_xbox_bc1_to_pc(data: bytes) -> bytes:
    # (This function is unchanged)
    result = bytearray()
    for i in range(0, len(data), 8):
        if i + 8 <= len(data):
            block = data[i:i+8]
            color0, color1, indices = struct.unpack('>HHI', block)
            result.extend(struct.pack('<HHI', color0, color1, indices))
    return bytes(result)

def create_dds_header(width: int, height: int) -> bytes:
    # (This function is unchanged)
    header = bytearray(128)
    struct.pack_into('<4sI', header, 0, b'DDS ', 124)
    flags = 0x1 | 0x2 | 0x4 | 0x1000
    pitch = max(1, (width + 3) // 4) * 8
    struct.pack_into('<IIII', header, 8, flags, height, width, pitch)
    struct.pack_into('<I', header, 76, 32)
    struct.pack_into('<I', header, 80, 0x4)
    header[84:88] = b'DXT1'
    struct.pack_into('<I', header, 108, 0x1000)
    return bytes(header)

def main():
    if len(sys.argv) < 2:
        print("Usage: python lhdecompress.py <texture_file.tex>")
        sys.exit(1)

    input_file = sys.argv[1]
    if not os.path.exists(input_file):
        print(f"Error: File '{input_file}' not found")
        sys.exit(1)

    print(f"Processing: {input_file}")
    print("=" * 60)

    try:
        parser = TextureParser(input_file, debug=False)
    except Exception as e:
        print(f"An error occurred parsing the .tex file: {e}")
        sys.exit(1)

    parser.print_info()
    print("\n" + "=" * 60)

    successful_count, failed_count = 0, 0

    for i, mipmap in enumerate(parser.mipmap_defs):
        print(f"\nMipmap {i}:")
        
        # This assumes the mipmap dimensions are stored in the texture header,
        # not the mipmap header for compressed mipmaps.
        width = max(4, parser.texture_width >> i)
        height = max(4, parser.texture_height >> i)
        
        expected_size = ((width + 3) // 4) * ((height + 3) // 4) * 8
        print(f"  Dimensions: {width}x{height} (Expected uncompressed size: {expected_size})")

        output_base = os.path.splitext(input_file)[0]
        decompressed_data = None

        if mipmap.is_compressed == 1:
            print("  Status: Compressed. Attempting LZX Decompression...")
            decompressed_data = decompress_lzx(mipmap.mipmap_data, expected_size, mipmap.window_bits)
        else:
            print("  Status: Uncompressed or unknown format. Treating as raw.")
            # This handles both flag 7 and files like dunecrest_cliff
            decompressed_data = mipmap.mipmap_data
        
        if decompressed_data:
            if len(decompressed_data) == expected_size:
                converted_bc1 = convert_xbox_bc1_to_pc(decompressed_data)
                output_file = f"{output_base}_mip{i}_{width}x{height}.dds"
                with open(output_file, "wb") as f:
                    f.write(create_dds_header(width, height))
                    f.write(converted_bc1)
                print(f"  ✓ Saved: {output_file}")
                successful_count += 1
            else:
                 print(f"  ✗ Size mismatch! Got {len(decompressed_data)}, expected {expected_size}")
                 failed_count += 1
        else:
            print(f"  ✗ Failed to decompress mipmap {i}")
            failed_file = f"{output_base}_mip{i}_failed.bin"
            with open(failed_file, "wb") as f:
                f.write(mipmap.mipmap_data)
            print(f"    Saved raw mipmap data for analysis: {failed_file}")
            failed_count += 1

    print("\n" + "=" * 60)
    print(f"Summary: Successful: {successful_count} | Failed: {failed_count}")

if __name__ == "__main__":
    main()