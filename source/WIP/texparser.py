# texparser.py (Updated)
import struct
import io
import os
import sys
from typing import List, Optional, BinaryIO
from dataclasses import dataclass

@dataclass
class MipMapDef:
    # --- Standard header fields ---
    comp_flag: int
    data_offset: int
    data_size: int
    unknown_3: int
    unknown_4: int
    unknown_5: int
    unknown_6: int
    unknown_7: int
    unknown_8: int
    unknown_9: int
    
    # --- Discovered via reverse-engineering ---
    is_compressed: int   # Byte at offset 0x28. 1 = compressed, 0 = raw data.
    window_bits: int     # Byte at offset 0x29. LZX window size parameter.
    
    # --- Remainder of header ---
    unknown_10_rem: int  # Remaining 2 bytes of the original unknown_10 field
    unknown_11: int
    
    # --- Dynamic fields ---
    mip_width: Optional[int] = None
    mip_height: Optional[int] = None
    unk_data: Optional[bytes] = None
    mipmap_data: bytes = b''

class TextureParser:
    def __init__(self, file_path: str = None, file_obj: BinaryIO = None, debug: bool = False):
        self.debug = debug
        if file_path:
            self.file = open(file_path, 'rb')
            self.should_close = True
            self.file.seek(0, 2)
            self.file_size = self.file.tell()
            self.file.seek(0)
        elif file_obj:
            self.file = file_obj
            self.should_close = False
            current_pos = file_obj.tell()
            file_obj.seek(0, 2)
            self.file_size = file_obj.tell()
            file_obj.seek(current_pos)
        else:
            raise ValueError("Either file_path or file_obj must be provided")
        self.parse()

    def read_uint32_be(self) -> int:
        return struct.unpack('>I', self.file.read(4))[0]

    def read_uint16_be(self) -> int:
        return struct.unpack('>H', self.file.read(2))[0]

    def parse(self):
        self.sign = self.read_uint32_be()
        self.raw_data_size = self.read_uint32_be()
        self.unknown_0 = self.read_uint32_be()
        self.unknown_1 = self.read_uint32_be()
        self.texture_width = self.read_uint32_be()
        self.texture_height = self.read_uint32_be()
        self.pixel_format = self.read_uint32_be()
        self.mipmap_count = self.read_uint32_be()

        self.mipmap_offsets: List[int] = [self.read_uint32_be() for _ in range(self.mipmap_count)]
        
        self.mipmap_defs: List[MipMapDef] = []
        for i in range(self.mipmap_count):
            if self.debug: print(f"\nParsing MipMap {i} at offset 0x{self.mipmap_offsets[i]:08X}")
            if self.mipmap_offsets[i] >= self.file_size:
                print(f"Warning: MipMap {i} offset exceeds file size.")
                continue
            self.file.seek(self.mipmap_offsets[i])
            try:
                self.mipmap_defs.append(self.parse_mipmap_def())
            except Exception as e:
                print(f"Error parsing MipMap {i}: {e}")
                break

    def parse_mipmap_def(self) -> MipMapDef:
        header_data = self.file.read(48) # The full header is 48 bytes

        # Unpack the main fields we already knew
        comp_flag, data_offset, data_size = struct.unpack('>III', header_data[0:12])

        # Unpack the crucial bytes we discovered from the assembly!
        # lbz r3, 0x28(r3)  -> is_compressed flag
        # lbz r28, 0x29(r3) -> window_bits flag
        is_compressed = header_data[0x28]
        window_bits = header_data[0x29]

        if self.debug:
            print(f"  Header byte 0x28 (is_compressed): {is_compressed}")
            print(f"  Header byte 0x29 (window_bits):   {window_bits}")

        # Unpack the rest of the header for completeness
        (
            unknown_3, unknown_4, unknown_5, unknown_6,
            unknown_7, unknown_8, unknown_9, unknown_10, unknown_11
        ) = struct.unpack('>IIIIIIIII', header_data[12:])
        
        # Create the MipMapDef object
        mipmap = MipMapDef(
            comp_flag=comp_flag, data_offset=data_offset, data_size=data_size,
            unknown_3=unknown_3, unknown_4=unknown_4, unknown_5=unknown_5,
            unknown_6=unknown_6, unknown_7=unknown_7, unknown_8=unknown_8,
            unknown_9=unknown_9, is_compressed=is_compressed, window_bits=window_bits,
            unknown_10_rem=struct.unpack('>H', header_data[0x2A:0x2C])[0], # The 2 bytes after our flags
            unknown_11=unknown_11
        )

        if mipmap.comp_flag == 7:
            mipmap.mipmap_data = self.file.read(mipmap.data_size)
        else:
            mipmap.mip_width = self.read_uint16_be()
            mipmap.mip_height = self.read_uint16_be()
            mipmap.unk_data = self.file.read(440)
            remaining_size = mipmap.data_size - 448
            if remaining_size > 0:
                mipmap.mipmap_data = self.file.read(remaining_size)

        return mipmap
        
    def print_info(self):
        print(f"Signature: 0x{self.sign:08X}")
        print(f"Texture Dimensions: {self.texture_width}x{self.texture_height}")
        print(f"MipMap Count: {self.mipmap_count}")

        for i, mipmap in enumerate(self.mipmap_defs):
            print(f"\nMipMap {i}:")
            print(f"  Compression Flag: {mipmap.comp_flag}")
            print(f"  Is Compressed (0x28): {mipmap.is_compressed}")
            print(f"  Window Bits (0x29):   {mipmap.window_bits}")
            print(f"  Data Size: {mipmap.data_size}")
            if mipmap.mip_width is not None:
                print(f"  Mip Dimensions: {mipmap.mip_width}x{mipmap.mip_height}")
            print(f"  MipMap Data Length: {len(mipmap.mipmap_data)}")

    def __del__(self):
        if hasattr(self, 'should_close') and self.should_close and hasattr(self, 'file'):
            self.file.close()

# (main function for standalone testing can be added here if desired)