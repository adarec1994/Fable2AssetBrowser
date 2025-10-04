#!/usr/bin/env python3
"""
Xbox 360 Texture Decompressor and DDS Converter
Handles LZX-compressed textures with mipmap support
"""

import struct
import sys
import os
from typing import Optional, List
from dataclasses import dataclass

# ============= LZX Decompressor =============

class LZXError(Exception):
    """Custom exception for LZX decompression errors."""
    pass

# LZX Constants
MIN_MATCH = 2
MAX_MATCH = 257
NUM_CHARS = 256
BLOCKTYPE_VERBATIM = 1
BLOCKTYPE_ALIGNED = 2
BLOCKTYPE_UNCOMPRESSED = 3
PRETREE_NUM_ELEMENTS = 20
ALIGNED_NUM_ELEMENTS = 8
NUM_PRIMARY_LENGTHS = 7
NUM_SECONDARY_LENGTHS = 249

# Position encoding tables
EXTRA_BITS = [
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16
] + [17] * 14

POSITION_BASE = [0] * 51
for i in range(1, 51):
    extra = EXTRA_BITS[i-1] if (i-1) < len(EXTRA_BITS) else 17
    POSITION_BASE[i] = POSITION_BASE[i-1] + (1 << extra)

class BitStream:
    """Reads bits from a byte stream in LZX format (16-bit LE chunks, MSB first)."""
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0
        self.bit_buffer = 0
        self.bits_left = 0

    def _ensure_bits(self, n: int):
        while self.bits_left < n:
            if self.pos + 1 >= len(self.data):
                raise LZXError(f"Read past end of stream")
            chunk = struct.unpack('>H', self.data[self.pos:self.pos+2])[0]
            self.pos += 2
            self.bit_buffer |= (chunk << (32 - 16 - self.bits_left))
            self.bits_left += 16

    def read_bits(self, n: int) -> int:
        if n == 0:
            return 0
        self._ensure_bits(n)
        val = self.bit_buffer >> (32 - n)
        self.bit_buffer <<= n
        self.bits_left -= n
        return val

    def align_to_byte(self):
        self.bits_left -= self.bits_left % 16

class LZXDecompressor:
    def __init__(self, window_bits: int):
        self.window_size = 1 << window_bits
        self.window_pos = 0
        self.window = bytearray(self.window_size)
        self.R0 = self.R1 = self.R2 = 1
        
        num_pos_slots = 1 << (window_bits - 4)
        if window_bits == 20: num_pos_slots = 42
        if window_bits == 21: num_pos_slots = 50
        
        self.main_tree_lens = [0] * (NUM_CHARS + (num_pos_slots * 8))
        self.len_tree_lens = [0] * (NUM_SECONDARY_LENGTHS + 1)
        
    def _make_huffman_table(self, lengths: List[int], n_symbols: int) -> list:
        max_len = max(lengths[:n_symbols]) if n_symbols > 0 else 0
        if max_len == 0:
            return []

        tbl = [None] * (max_len + 1)
        bl_count = [0] * (max_len + 1)
        
        for i in range(n_symbols):
            if lengths[i] > 0:
                bl_count[lengths[i]] += 1

        code = 0
        next_code = [0] * (max_len + 1)
        for bits in range(1, max_len + 1):
            code = (code + bl_count[bits - 1]) << 1
            next_code[bits] = code

        for i in range(n_symbols):
            length = lengths[i]
            if length != 0:
                if tbl[length] is None:
                    tbl[length] = {}
                tbl[length][next_code[length]] = i
                next_code[length] += 1
        return tbl
        
    def _read_huffman_symbol(self, stream: BitStream, table: list) -> int:
        if not table:
            raise LZXError("Empty Huffman table")
        code = 0
        length = 0
        while True:
            length += 1
            if length >= len(table):
                raise LZXError("Invalid Huffman code")
            code = (code << 1) | stream.read_bits(1)
            if table[length] and code in table[length]:
                return table[length][code]

    def _read_lengths(self, stream: BitStream, lengths: List[int], first: int, last: int):
        pretree_lens = [stream.read_bits(4) for _ in range(PRETREE_NUM_ELEMENTS)]
        pretree_table = self._make_huffman_table(pretree_lens, PRETREE_NUM_ELEMENTS)
        
        i = first
        while i < last:
            sym = self._read_huffman_symbol(stream, pretree_table)
            if sym <= 16:
                lengths[i] = (lengths[i] - sym + 17) % 17
                i += 1
            elif sym == 17:
                run = stream.read_bits(4) + 4
                for _ in range(run):
                    if i < last: lengths[i] = 0; i += 1
            elif sym == 18:
                run = stream.read_bits(5) + 20
                for _ in range(run):
                    if i < last: lengths[i] = 0; i += 1
            elif sym == 19:
                run = stream.read_bits(1) + 4
                z = self._read_huffman_symbol(stream, pretree_table)
                val = (lengths[i] - z + 17) % 17
                for _ in range(run):
                    if i < last: lengths[i] = val; i += 1
    
    def decompress(self, data: bytes, uncompressed_size: int) -> bytes:
        stream = BitStream(data)
        out = bytearray()
        
        num_pos_slots = 1 << (self.window_size.bit_length() - 1 - 4)
        main_elements = NUM_CHARS + num_pos_slots * 8
        
        while len(out) < uncompressed_size:
            block_type = stream.read_bits(3)
            if not (1 <= block_type <= 3):
                raise LZXError(f"Invalid block type: {block_type}")
                
            block_size = (stream.read_bits(8) << 16) | (stream.read_bits(8) << 8) | stream.read_bits(8)
            
            if block_type == BLOCKTYPE_UNCOMPRESSED:
                stream.align_to_byte()
                self.R0 = struct.unpack('<I', stream.data[stream.pos:stream.pos+4])[0]; stream.pos += 4
                self.R1 = struct.unpack('<I', stream.data[stream.pos:stream.pos+4])[0]; stream.pos += 4
                self.R2 = struct.unpack('<I', stream.data[stream.pos:stream.pos+4])[0]; stream.pos += 4
                
                chunk = stream.data[stream.pos : stream.pos + block_size]
                out.extend(chunk)
                
                start_pos = self.window_pos
                end_pos = start_pos + block_size
                if end_pos <= self.window_size:
                    self.window[start_pos:end_pos] = chunk
                else:
                    part1_len = self.window_size - start_pos
                    self.window[start_pos:] = chunk[:part1_len]
                    self.window[:end_pos % self.window_size] = chunk[part1_len:]

                self.window_pos = (self.window_pos + block_size) % self.window_size
                stream.pos += block_size
                continue

            self.main_tree_lens = [0] * main_elements
            self.len_tree_lens = [0] * (NUM_SECONDARY_LENGTHS + 1)
            
            self._read_lengths(stream, self.main_tree_lens, 0, 256)
            self._read_lengths(stream, self.main_tree_lens, 256, main_elements)
            main_tree_table = self._make_huffman_table(self.main_tree_lens, main_elements)
            
            self._read_lengths(stream, self.len_tree_lens, 0, NUM_SECONDARY_LENGTHS + 1)
            len_tree_table = self._make_huffman_table(self.len_tree_lens, NUM_SECONDARY_LENGTHS + 1)
            
            decoded_bytes = 0
            while decoded_bytes < block_size:
                main_sym = self._read_huffman_symbol(stream, main_tree_table)
                
                if main_sym < NUM_CHARS:
                    out.append(main_sym)
                    self.window[self.window_pos] = main_sym
                    self.window_pos = (self.window_pos + 1) % self.window_size
                    decoded_bytes += 1
                else:
                    pos_slot = (main_sym - NUM_CHARS) >> 3
                    match_len = main_sym & NUM_PRIMARY_LENGTHS
                    if match_len == NUM_PRIMARY_LENGTHS:
                        len_sym = self._read_huffman_symbol(stream, len_tree_table)
                        match_len += len_sym
                    match_len += MIN_MATCH
                    
                    if pos_slot == 0:
                        match_offset = self.R0
                    elif pos_slot == 1:
                        match_offset = self.R1
                        self.R1 = self.R0
                        self.R0 = match_offset
                    elif pos_slot == 2:
                        match_offset = self.R2
                        self.R2 = self.R0
                        self.R0 = match_offset
                    else:
                        extra = EXTRA_BITS[pos_slot]
                        base = POSITION_BASE[pos_slot]
                        verbatim_bits = stream.read_bits(extra)
                        match_offset = base + verbatim_bits
                        self.R2 = self.R1
                        self.R1 = self.R0
                        self.R0 = match_offset

                    for _ in range(match_len):
                        b = self.window[(self.window_pos - match_offset) % self.window_size]
                        out.append(b)
                        self.window[self.window_pos] = b
                        self.window_pos = (self.window_pos + 1) % self.window_size
                    
                    decoded_bytes += match_len
        return bytes(out)

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
        self.pos += 8  # Skip unknown fields
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
                # Uncompressed
                mipmap_data = self.data[self.pos:self.pos+data_size]
                mip_width = mip_height = None
            else:
                # Compressed
                mip_width = self.read_u16()
                mip_height = self.read_u16()
                self.pos += 440  # Skip unknown data
                remaining = data_size - 448
                mipmap_data = self.data[self.pos:self.pos+remaining]
            
            self.mipmaps.append(MipMap(
                comp_flag, data_offset, data_size,
                is_compressed, window_bits,
                mip_width, mip_height, mipmap_data
            ))

# ============= DDS Conversion =============

def convert_xbox_bc1_to_pc(data: bytes) -> bytes:
    """Convert Xbox 360 BC1 (big-endian) to PC BC1 (little-endian)."""
    result = bytearray()
    for i in range(0, len(data), 8):
        if i + 8 <= len(data):
            block = data[i:i+8]
            color0, color1, indices = struct.unpack('>HHI', block)
            result.extend(struct.pack('<HHI', color0, color1, indices))
    return bytes(result)

def create_dds_header(width: int, height: int) -> bytes:
    """Create a DDS header for BC1/DXT1 texture."""
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

def decompress_lzx(data: bytes, expected_size: int, window_bits: int) -> Optional[bytes]:
    """Decompress LZX data with various header offsets."""
    if not (15 <= window_bits <= 21):
        return None
    
    for offset in [0, 2, 4, 8]:
        if offset >= len(data):
            continue
        try:
            decompressor = LZXDecompressor(window_bits)
            result = decompressor.decompress(data[offset:], expected_size)
            if len(result) == expected_size:
                return result
        except LZXError:
            pass
    return None

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
        
        if mipmap.is_compressed == 1:
            print(f"  Decompressing LZX (window_bits={mipmap.window_bits})...")
            decompressed = decompress_lzx(mipmap.mipmap_data, expected_size, mipmap.window_bits)
        else:
            decompressed = mipmap.mipmap_data
        
        if decompressed and len(decompressed) == expected_size:
            converted = convert_xbox_bc1_to_pc(decompressed)
            output_file = f"{output_base}_mip{i}_{width}x{height}.dds"
            
            with open(output_file, "wb") as f:
                f.write(create_dds_header(width, height))
                f.write(converted)
            
            print(f"  ✓ Saved: {output_file}")
            successful += 1
        else:
            print(f"  ✗ Failed (size mismatch or decompression error)")
    
    print(f"\n{'=' * 60}")
    print(f"Completed: {successful}/{parser.mipmap_count} mipmaps")

if __name__ == "__main__":
    main()