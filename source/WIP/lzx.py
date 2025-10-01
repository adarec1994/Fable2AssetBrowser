# lzx.py (Final Version)
import struct
from typing import List, Tuple

# --- Custom Exception ---
class LZXError(Exception):
    """Custom exception for LZX decompression errors."""
    pass

# --- Constants from lzx.h ---
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
PRETREE_TABLEBITS = 6
MAINTREE_TABLEBITS = 12
LENGTH_TABLEBITS = 12
ALIGNED_TABLEBITS = 7

# --- Data tables from lzxd.c ---
EXTRA_BITS = [
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16
] + [17] * (50 - 36)

POSITION_BASE = [0] * 51
for i in range(1, 51):
    extra = EXTRA_BITS[i-1] if (i-1) < len(EXTRA_BITS) else 17
    POSITION_BASE[i] = POSITION_BASE[i-1] + (1 << extra)

class BitStream:
    """
    Reads bits from a byte stream in the specific way LZX requires:
    1. Read 16-bit little-endian chunks.
    2. Provide bits from MSB to LSB.
    """
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0
        self.bit_buffer = 0
        self.bits_left = 0

    def _ensure_bits(self, n: int):
        while self.bits_left < n:
            if self.pos + 1 >= len(self.data):
                raise LZXError(f"Read past end of stream. pos={self.pos}, data_len={len(self.data)}")
            
            chunk = struct.unpack('<H', self.data[self.pos:self.pos+2])[0]
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

    def peek_bits(self, n: int) -> int:
        self._ensure_bits(n)
        return self.bit_buffer >> (32 - n)

    def align_to_byte(self):
        # Aligns to a 16-bit boundary in LZX
        self.bits_left -= self.bits_left % 16

class LZXDecompressor:
    def __init__(self, window_bits: int):
        self.window_size = 1 << window_bits
        self.window_pos = 0
        self.window = bytearray(self.window_size)
        
        self.R0 = self.R1 = self.R2 = 1
        
        num_pos_slots = 1 << (window_bits - 4) # Simplified from game's logic, but common
        if window_bits == 20: num_pos_slots = 42
        if window_bits == 21: num_pos_slots = 50
        
        self.main_tree_lens = [0] * (NUM_CHARS + (num_pos_slots * 8))
        self.main_tree_table = []
        self.len_tree_lens = [0] * (NUM_SECONDARY_LENGTHS + 1)
        self.len_tree_table = []
        self.aligned_lens = [0] * ALIGNED_NUM_ELEMENTS
        self.aligned_table = []
        
    def _make_huffman_table(self, lengths: List[int], n_symbols: int) -> list:
        max_len = 0
        for length in lengths[:n_symbols]:
            if length > max_len:
                max_len = length
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
            raise LZXError("Attempted to read from an empty Huffman table.")
        code = 0
        length = 0
        while True:
            length += 1
            if length >= len(table):
                raise LZXError(f"Invalid Huffman code read from stream (len > {len(table)})")
            
            code = (code << 1) | stream.read_bits(1)
            
            if table[length] is not None and code in table[length]:
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
            self.main_tree_table = self._make_huffman_table(self.main_tree_lens, main_elements)
            
            self._read_lengths(stream, self.len_tree_lens, 0, NUM_SECONDARY_LENGTHS + 1)
            self.len_tree_table = self._make_huffman_table(self.len_tree_lens, NUM_SECONDARY_LENGTHS + 1)
            
            decoded_bytes_in_block = 0
            while decoded_bytes_in_block < block_size:
                main_sym = self._read_huffman_symbol(stream, self.main_tree_table)
                
                if main_sym < NUM_CHARS:
                    out.append(main_sym)
                    self.window[self.window_pos] = main_sym
                    self.window_pos = (self.window_pos + 1) % self.window_size
                    decoded_bytes_in_block += 1
                else:
                    pos_slot = (main_sym - NUM_CHARS) >> 3
                    match_len = main_sym & NUM_PRIMARY_LENGTHS
                    if match_len == NUM_PRIMARY_LENGTHS:
                        len_sym = self._read_huffman_symbol(stream, self.len_tree_table)
                        match_len += len_sym
                    match_len += MIN_MATCH
                    
                    if pos_slot == 0: match_offset = self.R0
                    elif pos_slot == 1: match_offset = self.R1; self.R1 = self.R0; self.R0 = match_offset
                    elif pos_slot == 2: match_offset = self.R2; self.R2 = self.R0; self.R0 = match_offset
                    else:
                        extra = EXTRA_BITS[pos_slot]
                        base = POSITION_BASE[pos_slot]
                        verbatim_bits = stream.read_bits(extra)
                        match_offset = base + verbatim_bits
                        self.R2 = self.R1; self.R1 = self.R0; self.R0 = match_offset

                    for _ in range(match_len):
                        b = self.window[(self.window_pos - match_offset) % self.window_size]
                        out.append(b)
                        self.window[self.window_pos] = b
                        self.window_pos = (self.window_pos + 1) % self.window_size
                    
                    decoded_bytes_in_block += match_len
        return bytes(out)

def decompress(data: bytes, window_bits: int, uncompressed_size: int) -> bytes:
    """High-level function to decompress LZX data."""
    decompressor = LZXDecompressor(window_bits)
    return decompressor.decompress(data, uncompressed_size)