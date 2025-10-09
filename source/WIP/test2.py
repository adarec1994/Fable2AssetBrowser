#!/usr/bin/env python3
"""
Xbox 360 Texture Decompressor and DDS Converter
Handles LZO1X-compressed textures with mipmap support
"""

import struct
import sys
import os
import lzo
from typing import Optional, List
from dataclasses import dataclass

# ============= Texture Parser =============

@dataclass
class MipMap:
    comp_flag: int
    data_offset: int
    data_size: int
    is_compressed: int
    window_bits: int
    mip_width: Optional[int]
    mip_height: Optional[int]
    mipmap_data: bytes

class TextureParser:
    def __init__(self, file_path: str):
        with open(file_path, 'rb') as f:
            self.data = f.read()
        self.pos = 0
        self.parse()

    def read_u32(self) -> int:
        val = struct.unpack('>I', self.data[self.pos:self.pos+4])[0]
        self.pos += 4
        return val

    def read_u16(self) -> int:
        val = struct.unpack('>H', self.data[self.pos:self.pos+2])[0]
        self.pos += 2
        return val

    def parse(self):
        self.sign = self.read_u32()
        self.raw_data_size = self.read_u32()
        self.pos += 8
        self.texture_width = self.read_u32()
        self.texture_height = self.read_u32()
        self.pixel_format = self.read_u32()
        self.mipmap_count = self.read_u32()
        
        self.mipmap_offsets = [self.read_u32() for _ in range(self.mipmap_count)]
        self.mipmaps: List[MipMap] = []
        
        for offset in self.mipmap_offsets:
            self.pos = offset
            header = self.data[self.pos:self.pos+48]
            
            comp_flag = struct.unpack('>I', header[0:4])[0]
            data_offset = struct.unpack('>I', header[4:8])[0]
            data_size = struct.unpack('>I', header[8:12])[0]
            is_compressed = header[0x28]
            window_bits = header[0x29]
            
            self.pos = offset + 48
            
            if comp_flag == 7:
                mipmap_data = self.data[self.pos:self.pos+data_size]
                mip_width = mip_height = None
            else:
                mipmap_data = self.data[self.pos : self.pos + data_size]
                mip_width = mip_height = None
            
            self.mipmaps.append(MipMap(
                comp_flag, data_offset, data_size,
                is_compressed, window_bits,
                mip_width, mip_height, mipmap_data
            ))

# ============= DDS Conversion =============

def convert_xbox_bc1_to_pc(data: bytes) -> bytes:
    result = bytearray()
    for i in range(0, len(data), 8):
        if i + 8 <= len(data):
            block = data[i:i+8]
            color0, color1, indices = struct.unpack('>HHI', block)
            result.extend(struct.pack('<HHI', color0, color1, indices))
    return bytes(result)

def create_dds_header(width: int, height: int) -> bytes:
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

# ============= Main =============

def main():
    if len(sys.argv) < 2:
        print("Usage: python decompressor.py <texture_file.tex>")
        sys.exit(1)

    input_file = sys.argv[1]
    if not os.path.exists(input_file):
        print(f"Error: File '{input_file}' not found")
        sys.exit(1)

    print(f"Processing: {input_file}")
    print("=" * 60)

    try:
        parser = TextureParser(input_file)
    except Exception as e:
        print(f"Error parsing texture file: {e}")
        sys.exit(1)

    print(f"Texture: {parser.texture_width}x{parser.texture_height}")
    print(f"Mipmaps: {parser.mipmap_count}")
    
    output_base = os.path.splitext(input_file)[0]
    successful = 0
    
    for i, mipmap in enumerate(parser.mipmaps):
        width = max(4, parser.texture_width >> i)
        height = max(4, parser.texture_height >> i)
        expected_size = ((width + 3) // 4) * ((height + 3) // 4) * 8
        
        print(f"\nMipmap {i}: {width}x{height}")
        
        if mipmap.comp_flag != 7:
            print("  Mipmap appears compressed. Decompressing LZO...")
            try:
                # FINAL CORRECTION: The LZO error indicates the stream doesn't
                # start at byte 0. We assume a 4-byte header and skip it.
                header_to_skip = 4
                lzo_stream = mipmap.mipmap_data[header_to_skip:]
                
                decompressed = lzo.decompress(lzo_stream)

            except Exception as e:
                print(f"  LZO decompression failed: {e}")
                decompressed = None
        else:
            print("  Mipmap is not compressed.")
            decompressed = mipmap.mipmap_data
        
        if decompressed and len(decompressed) == expected_size:
            converted = convert_xbox_bc1_to_pc(decompressed)
            output_file = f"{output_base}_mip{i}_{width}x{height}.dds"
            
            with open(output_file, "wb") as f:
                f.write(create_dds_header(width, height))
                f.write(converted)
            
            print(f"  ✓ Saved: {output_file}")
            successful += 1
        elif decompressed:
            print(f"  ✗ Failed (Size mismatch: expected {expected_size}, got {len(decompressed)})")
        else:
            print(f"  ✗ Failed (Decompression error)")
    
    print(f"\n{'=' * 60}")
    print(f"Completed: {successful}/{parser.mipmap_count} mipmaps processed.")

if __name__ == "__main__":
    main()