#!/usr/bin/env python3
"""
Fable 2 DEFLATE Texture Decompressor
Implements a custom DEFLATE decompressor based on the reverse-engineered
lh_run function from the Fable 2 engine.
"""

import struct
import sys
import os
from typing import Optional, List, Tuple

# This is a placeholder for the actual TextureParser.
# The user's original script depends on it.
class TextureParser:
    """A placeholder class to allow the script to run."""
    def __init__(self, file_path, debug=False):
        print(f"--- NOTE: Using placeholder for TextureParser ---")
        print(f"--- This script will not parse .tex files without the real texparser.py ---")
        # Dummy data to simulate a single compressed mipmap
        self.texture_width = 512
        self.texture_height = 512
        
        class MockMipmap:
            def __init__(self):
                self.comp_flag = 1 # Compressed
                # Sample compressed data would go here.
                # Since we don't have it, we'll let it fail gracefully.
                self.mipmap_data = b''
        
        self.mipmap_defs = [MockMipmap()] if not os.path.exists(file_path) else []
        if os.path.exists(file_path):
            try:
                # Attempt to read a real file if provided, assuming it's just raw data
                with open(file_path, 'rb') as f:
                    mip = MockMipmap()
                    mip.mipmap_data = f.read()
                    self.mipmap_defs.append(mip)
            except IOError as e:
                print(f"Could not read placeholder file: {e}")

    def print_info(self):
        print(f"Texture Info (Mock): {self.texture_width}x{self.texture_height}")

# --- Custom DEFLATE Implementation ---

class Fable2Deflate:
    """
    A custom DEFLATE implementation that correctly handles the bitstream
    and Huffman coding found in Fable 2's lh_run function.
    """

    def __init__(self, data: bytes):
        # Bitstream state
        self.data = data
        self.byte_pos = 0
        self.hold = 0
        self.bits = 0

        # Output buffer
        self.out = bytearray()
        
        # Fixed Huffman tables (pre-calculated)
        self.fixed_litlen_table = self._build_fixed_litlen_table()
        self.fixed_dist_table = self._build_fixed_dist_table()

    # --- Bitstream Handling ---
    
    def _fill_hold(self):
        """Refills the bit buffer, mirroring the game's assembly."""
        while self.bits <= 24 and self.byte_pos < len(self.data):
            if self.byte_pos < len(self.data):
                byte = self.data[self.byte_pos]
                self.hold |= byte << self.bits
                self.bits += 8
                self.byte_pos += 1

    def _read_bits(self, n: int) -> int:
        """Reads n bits from the LSB end of the bit buffer."""
        if self.bits < n:
            self._fill_hold()
        if self.bits < n:
            raise ValueError("Unexpected end of compressed stream.")
        
        mask = (1 << n) - 1
        val = self.hold & mask
        self.hold >>= n
        self.bits -= n
        return val
        
    def _align_to_byte(self):
        """Discards bits to align to the next byte boundary."""
        self.bits -= self.bits % 8
        self.hold &= (1 << self.bits) - 1

    # --- Huffman Table Generation ---

    def _build_huffman_table(self, code_lengths: List[int]) -> List[Tuple[int, int]]:
        """
        Builds a canonical Huffman code table from code lengths.
        Returns a list of (code, length) for each symbol.
        """
        if not any(code_lengths): # Handle empty/all-zero lengths
            return []
            
        max_bits = max(l for l in code_lengths if l > 0)
        bl_count = [0] * (max_bits + 1)
        for length in code_lengths:
            if length > 0:
                bl_count[length] += 1

        code = 0
        next_code = [0] * (max_bits + 1)
        for bits in range(1, max_bits + 1):
            code = (code + bl_count[bits - 1]) << 1
            next_code[bits] = code

        table = [ (0, 0) ] * len(code_lengths)
        for i, length in enumerate(code_lengths):
            if length != 0:
                table[i] = (next_code[length], length)
                next_code[length] += 1
        return table
        
    def _build_fixed_litlen_table(self):
        """Generates the fixed Huffman table for literal/length codes."""
        lengths = ([8] * 144) + ([9] * 112) + ([7] * 24) + ([8] * 8)
        return self._build_huffman_table(lengths)
        
    def _build_fixed_dist_table(self):
        """Generates the fixed Huffman table for distance codes."""
        return self._build_huffman_table([5] * 32)

    # --- Symbol Decoding ---

    def _decode_symbol(self, table: List[Tuple[int, int]]) -> int:
        """Decodes one symbol using the provided Huffman table."""
        code = 0
        length = 0
        while True:
            length += 1
            code = (code << 1) | self._read_bits(1)
            # Find matching code at current length
            for symbol, (sym_code, sym_len) in enumerate(table):
                if sym_len == length and sym_code == code:
                    return symbol
            if length > 15: # Max DEFLATE code length
                raise ValueError("Invalid Huffman code found in stream.")

    # --- Main Decompression Logic ---

    def decompress(self, expected_size: int) -> bytes:
        """Executes the main DEFLATE state machine."""
        is_last_block = False
        while not is_last_block:
            is_last_block = self._read_bits(1) == 1
            block_type = self._read_bits(2)

            if block_type == 0:  # Stored (uncompressed)
                self._process_stored_block()
            elif block_type == 1:  # Fixed Huffman
                self._process_block(self.fixed_litlen_table, self.fixed_dist_table)
            elif block_type == 2:  # Dynamic Huffman
                litlen_table, dist_table = self._process_dynamic_header()
                self._process_block(litlen_table, dist_table)
            else:
                raise ValueError("Invalid DEFLATE block type (3).")
        
        if len(self.out) != expected_size:
            print(f"Warning: Decompressed size ({len(self.out)}) differs from expected ({expected_size}).")
            
        return bytes(self.out)

    def _process_stored_block(self):
        """Handles uncompressed blocks."""
        self._align_to_byte()
        length = self._read_bits(16)
        nlength = self._read_bits(16)
        if length != (~nlength & 0xFFFF):
            raise ValueError("Invalid stored block length.")
        
        # Manually copy from any remaining bits in the hold buffer first
        while self.bits >= 8 and length > 0:
            self.out.append(self.hold & 0xFF)
            self.hold >>= 8
            self.bits -= 8
            length -= 1
        
        if self.byte_pos + length > len(self.data):
             raise ValueError("Stored block requests more data than available.")
        
        # Then copy remaining bulk data from the source array
        self.out.extend(self.data[self.byte_pos : self.byte_pos + length])
        self.byte_pos += length

    def _process_dynamic_header(self) -> Tuple[List, List]:
        """Reads dynamic Huffman table definitions and builds them."""
        hlit = self._read_bits(5) + 257
        hdist = self._read_bits(5) + 1
        hclen = self._read_bits(4) + 4
        
        # Read code lengths for the code-length table
        cl_order = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15]
        cl_lengths = [0] * 19
        for i in range(hclen):
            cl_lengths[cl_order[i]] = self._read_bits(3)
            
        cl_table = self._build_huffman_table(cl_lengths)
        
        # Decode the literal/length and distance code lengths
        all_lengths = []
        while len(all_lengths) < (hlit + hdist):
            symbol = self._decode_symbol(cl_table)
            if symbol <= 15:
                all_lengths.append(symbol)
            elif symbol == 16:
                repeat_len = self._read_bits(2) + 3
                all_lengths.extend([all_lengths[-1]] * repeat_len)
            elif symbol == 17:
                repeat_len = self._read_bits(3) + 3
                all_lengths.extend([0] * repeat_len)
            elif symbol == 18:
                repeat_len = self._read_bits(7) + 11
                all_lengths.extend([0] * repeat_len)

        litlen_table = self._build_huffman_table(all_lengths[:hlit])
        dist_table = self._build_huffman_table(all_lengths[hlit:])
        return litlen_table, dist_table

    def _process_block(self, litlen_table: List, dist_table: List):
        """Decompresses a block using the provided Huffman tables."""
        # Length and distance extra bits and base values
        LEN_EXTRA = [0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0]
        LEN_BASE = [3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258]
        DIST_EXTRA = [0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13]
        DIST_BASE = [1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577]

        while True:
            symbol = self._decode_symbol(litlen_table)
            if symbol < 256:
                self.out.append(symbol)
            elif symbol == 256:  # End of block
                break
            else:  # Length/distance pair
                # Decode length
                sym_idx = symbol - 257
                length = LEN_BASE[sym_idx] + self._read_bits(LEN_EXTRA[sym_idx])
                
                # Decode distance
                dist_symbol = self._decode_symbol(dist_table)
                dist_idx = dist_symbol
                distance = DIST_BASE[dist_idx] + self._read_bits(DIST_EXTRA[dist_idx])
                
                # Copy from output buffer
                for _ in range(length):
                    self.out.append(self.out[-distance])


def custom_deflate(data: bytes, expected_size: int) -> Optional[bytes]:
    """
    Decompresses Fable 2 texture data by trying different header offsets.
    """
    for offset in [0, 2, 4, 8, 16]:
        if offset >= len(data):
            continue
        try:
            decompressor = Fable2Deflate(data[offset:])
            result = decompressor.decompress(expected_size)
            if len(result) == expected_size:
                print(f"  ✓ Success: Custom DEFLATE with {offset}-byte header offset")
                return result
        except (ValueError, IndexError):
            # This offset failed, try the next one
            pass

    print(f"  ✗ Custom DEFLATE failed at all attempted offsets.")
    return None


def convert_xbox_bc1_to_pc(data: bytes) -> bytes:
    """Convert Xbox 360 big-endian BC1/DXT1 to PC little-endian."""
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
    flags = 0x1 | 0x2 | 0x4 | 0x1000  # CAPS, HEIGHT, WIDTH, PIXELFORMAT
    pitch = max(1, (width + 3) // 4) * 8
    struct.pack_into('<IIII', header, 8, flags, height, width, pitch)
    # Pixel format
    struct.pack_into('<I', header, 76, 32)  # Size
    struct.pack_into('<I', header, 80, 0x4)  # FOURCC flag
    header[84:88] = b'DXT1'
    # Caps
    struct.pack_into('<I', header, 108, 0x1000)  # TEXTURE
    return bytes(header)


def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print("Usage: python fable2_deflate.py <texture_file.tex or compressed.bin>")
        sys.exit(1)

    texture_file = sys.argv[1]
    if not os.path.exists(texture_file):
        print(f"Error: File '{texture_file}' not found")
        sys.exit(1)

    print(f"Processing: {texture_file}")
    print("=" * 60)

    # Use TextureParser for .tex files, otherwise treat as raw compressed data
    if texture_file.lower().endswith(".tex"):
        try:
            # Assumes the user has the real texparser.py
            from texparser import TextureParser
            parser = TextureParser(texture_file, debug=False)
        except ImportError:
            print("Real 'texparser.py' not found. Using placeholder.")
            parser = TextureParser(texture_file)
    else:
        # Handle raw bin files
        parser = TextureParser(texture_file) # Use placeholder to run

    parser.print_info()
    print("\n" + "=" * 60)

    successful_count, failed_count = 0, 0

    for i, mipmap in enumerate(parser.mipmap_defs):
        print(f"\nMipmap {i}:")
        print(f"  Compression flag: {mipmap.comp_flag}")
        print(f"  Data size: {len(mipmap.mipmap_data)} bytes")

        width = max(4, parser.texture_width >> i)
        height = max(4, parser.texture_height >> i)
        print(f"  Dimensions: {width}x{height}")

        expected_size = ((width + 3) // 4) * ((height + 3) // 4) * 8
        print(f"  Expected BC1 size: {expected_size} bytes")

        output_base = os.path.splitext(texture_file)[0]

        if mipmap.comp_flag == 7:  # Uncompressed
            print("  Status: Uncompressed BC1 data")
            decompressed_data = mipmap.mipmap_data
        elif mipmap.comp_flag == 1:  # Compressed
            print("  Status: Compressed (attempting custom DEFLATE)")
            print(f"  First 16 bytes: {mipmap.mipmap_data[:16].hex()}")
            decompressed_data = custom_deflate(mipmap.mipmap_data, expected_size)
        else:
            print(f"  Skipping unknown compression flag: {mipmap.comp_flag}")
            continue

        if decompressed_data:
            if len(decompressed_data) == expected_size:
                converted = convert_xbox_bc1_to_pc(decompressed_data)
                output_file = f"{output_base}_mip{i}_{width}x{height}.dds"
                with open(output_file, "wb") as f:
                    f.write(create_dds_header(width, height))
                    f.write(converted)
                print(f"  ✓ Saved: {output_file}")
                successful_count += 1
            else:
                 print(f"  ✗ Decompressed size mismatch!")
                 failed_count += 1
        else:
            print(f"  ✗ Failed to decompress")
            compressed_file = f"{output_base}_mip{i}_{width}x{height}_failed.bin"
            with open(compressed_file, "wb") as f:
                f.write(mipmap.mipmap_data)
            print(f"    Saved compressed data for analysis: {compressed_file}")
            failed_count += 1

    print("\n" + "=" * 60)
    print(f"Summary: Successful: {successful_count} | Failed: {failed_count}")
    if failed_count > 0:
        print("\nFor failed files, double-check the 'lh_run' logic or texture format.")

if __name__ == "__main__":
    main()

