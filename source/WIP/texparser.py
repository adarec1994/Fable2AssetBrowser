import struct
import io
import os
import sys
from typing import List, Optional, BinaryIO
from dataclasses import dataclass


@dataclass
class MipMapDef:
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
    unknown_10: int
    unknown_11: int
    mip_width: Optional[int] = None
    mip_height: Optional[int] = None
    unk_data: Optional[bytes] = None
    mipmap_data: bytes = b''


class TextureParser:
    def __init__(self, file_path: str = None, file_obj: BinaryIO = None, debug: bool = False):
        """
        Initialize the parser with either a file path or file object.

        Args:
            file_path: Path to the binary file
            file_obj: Binary file object
            debug: Enable debug output
        """
        self.debug = debug

        if file_path:
            self.file = open(file_path, 'rb')
            self.should_close = True
            # Get file size
            self.file.seek(0, 2)  # Seek to end
            self.file_size = self.file.tell()
            self.file.seek(0)  # Seek back to start
            if self.debug:
                print(f"File size: {self.file_size} bytes")
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
        """Read a big-endian uint32 from the file."""
        data = self.file.read(4)
        if len(data) < 4:
            raise ValueError(f"Expected 4 bytes, got {len(data)} at position {self.file.tell() - len(data)}")
        return struct.unpack('>I', data)[0]

    def read_uint16_be(self) -> int:
        """Read a big-endian uint16 from the file."""
        data = self.file.read(2)
        if len(data) < 2:
            raise ValueError(f"Expected 2 bytes, got {len(data)} at position {self.file.tell() - len(data)}")
        return struct.unpack('>H', data)[0]

    def parse(self):
        """Parse the texture file according to the template structure."""
        if self.debug:
            print("Reading header...")

        # Read header
        self.sign = self.read_uint32_be()
        if self.debug:
            print(f"Signature: 0x{self.sign:08X}")

        self.raw_data_size = self.read_uint32_be()
        self.unknown_0 = self.read_uint32_be()
        self.unknown_1 = self.read_uint32_be()  # maybe mipmap def size
        self.texture_width = self.read_uint32_be()
        self.texture_height = self.read_uint32_be()
        self.pixel_format = self.read_uint32_be()
        self.mipmap_count = self.read_uint32_be()

        if self.debug:
            print(f"Texture: {self.texture_width}x{self.texture_height}")
            print(f"MipMap count: {self.mipmap_count}")
            print(f"Current position: {self.file.tell()}")

        # Sanity check
        if self.mipmap_count > 100:  # Unlikely to have more than 100 mipmaps
            print(f"Warning: Unusual mipmap count: {self.mipmap_count}")
            print("File might not match expected format")

        # Read mipmap offsets
        self.mipmap_offsets: List[int] = []
        for i in range(self.mipmap_count):
            offset = self.read_uint32_be()
            self.mipmap_offsets.append(offset)
            if self.debug:
                print(f"MipMap {i} offset: 0x{offset:08X}")

        # Read mipmap definitions
        self.mipmap_defs: List[MipMapDef] = []
        for i in range(self.mipmap_count):
            if self.debug:
                print(f"\nParsing MipMap {i} at offset 0x{self.mipmap_offsets[i]:08X}")

            # Check if offset is within file bounds
            if self.mipmap_offsets[i] >= self.file_size:
                print(
                    f"Warning: MipMap {i} offset (0x{self.mipmap_offsets[i]:08X}) exceeds file size ({self.file_size})")
                continue

            self.file.seek(self.mipmap_offsets[i])
            try:
                mipmap_def = self.parse_mipmap_def()
                self.mipmap_defs.append(mipmap_def)
            except Exception as e:
                print(f"Error parsing MipMap {i}: {e}")
                if self.debug:
                    print(f"File position: {self.file.tell()}")
                break

    def parse_mipmap_def(self) -> MipMapDef:
        start_pos = self.file.tell()

        mipmap = MipMapDef(
            comp_flag=self.read_uint32_be(),
            data_offset=self.read_uint32_be(),
            data_size=self.read_uint32_be(),
            unknown_3=self.read_uint32_be(),
            unknown_4=self.read_uint32_be(),
            unknown_5=self.read_uint32_be(),
            unknown_6=self.read_uint32_be(),
            unknown_7=self.read_uint32_be(),
            unknown_8=self.read_uint32_be(),
            unknown_9=self.read_uint32_be(),
            unknown_10=self.read_uint32_be(),
            unknown_11=self.read_uint32_be()
        )

        if self.debug:
            print(f"  CompFlag: {mipmap.comp_flag}")
            print(f"  DataOffset: 0x{mipmap.data_offset:08X}")
            print(f"  DataSize: {mipmap.data_size}")

        if mipmap.comp_flag == 7:
            # Read exactly DataSize bytes for compressed data
            mipmap.mipmap_data = self.file.read(mipmap.data_size)
        else:
            mipmap.mip_width = self.read_uint16_be()
            mipmap.mip_height = self.read_uint16_be()
            mipmap.unk_data = self.file.read(440)
            
            # Read exactly (DataSize - 448) bytes
            # 448 = 2 (MipWidth) + 2 (MipHeight) + 440 (UnkData) + 4 (padding for alignment)
            remaining_size = mipmap.data_size - 448
            if remaining_size > 0:
                mipmap.mipmap_data = self.file.read(remaining_size)
            else:
                mipmap.mipmap_data = b''

        return mipmap

    def get_info(self) -> dict:
        return {
            'sign': self.sign,
            'raw_data_size': self.raw_data_size,
            'texture_width': self.texture_width,
            'texture_height': self.texture_height,
            'pixel_format': self.pixel_format,
            'mipmap_count': self.mipmap_count,
            'mipmap_offsets': self.mipmap_offsets,
            'mipmap_defs': self.mipmap_defs
        }

    def print_info(self):
        print(f"Signature: 0x{self.sign:08X}")
        print(f"Raw Data Size: {self.raw_data_size}")
        print(f"Unknown_0: 0x{self.unknown_0:08X}")
        print(f"Unknown_1: 0x{self.unknown_1:08X}")
        print(f"Texture Dimensions: {self.texture_width}x{self.texture_height}")
        print(f"Pixel Format: 0x{self.pixel_format:08X}")
        print(f"MipMap Count: {self.mipmap_count}")
        print(f"MipMap Offsets: {[f'0x{off:08X}' for off in self.mipmap_offsets]}")

        for i, mipmap in enumerate(self.mipmap_defs):
            print(f"\nMipMap {i}:")
            print(f"  Compression Flag: {mipmap.comp_flag}")
            print(f"  Data Offset: 0x{mipmap.data_offset:08X}")
            print(f"  Data Size: {mipmap.data_size}")
            if mipmap.mip_width is not None:
                print(f"  Mip Dimensions: {mipmap.mip_width}x{mipmap.mip_height}")
            print(f"  MipMap Data Length: {len(mipmap.mipmap_data)}")

    def export_mipmaps(self, base_filename: str):
        base_name = os.path.splitext(base_filename)[0]
        for i, mipmap in enumerate(self.mipmap_defs):
            output_file = f"{base_name}_mipmap_{i}.bin"
            with open(output_file, 'wb') as f:
                f.write(mipmap.mipmap_data)
            print(f"Exported: {output_file} ({len(mipmap.mipmap_data)} bytes)")

    def dump_header(self, num_bytes: int = 256):
        self.file.seek(0)
        data = self.file.read(num_bytes)
        print("\nFile header dump (hex):")
        for i in range(0, len(data), 16):
            hex_str = ' '.join(f'{b:02X}' for b in data[i:i + 16])
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i + 16])
            print(f"{i:08X}: {hex_str:<48} {ascii_str}")

    def __del__(self):
        if hasattr(self, 'should_close') and self.should_close and hasattr(self, 'file'):
            self.file.close()


def main():
    if len(sys.argv) < 2:
        print("Usage: texparser.py <texture_file> [--debug]")
        print("Example: texparser.py suicide_bomber_base_diffuse_merged.tex")
        print("         texparser.py texture.tex --debug")
        sys.exit(1)

    texture_file = sys.argv[1]
    debug_mode = '--debug' in sys.argv

    # Check if file exists
    if not os.path.exists(texture_file):
        print(f"Error: File '{texture_file}' not found.")
        sys.exit(1)

    print(f"Parsing texture file: {texture_file}")
    print("=" * 60)

    try:
        parser = TextureParser(texture_file, debug=debug_mode)
        parser.print_info()

        if debug_mode:
            print("\n" + "=" * 60)
            response = input("Show hex dump of file header? (y/n): ").lower()
            if response == 'y':
                parser.dump_header()

        # Optional: Ask if user wants to export mipmaps
        if parser.mipmap_count > 0 and len(parser.mipmap_defs) > 0:
            print("\n" + "=" * 60)
            response = input("Export mipmap data to separate files? (y/n): ").lower()
            if response == 'y':
                parser.export_mipmaps(texture_file)

    except Exception as e:
        print(f"Error parsing file: {e}")
        if debug_mode:
            import traceback
            traceback.print_exc()

        try:
            print("\nShowing file header for debugging:")
            with open(texture_file, 'rb') as f:
                data = f.read(256)
                for i in range(0, len(data), 16):
                    hex_str = ' '.join(f'{b:02X}' for b in data[i:i + 16])
                    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i + 16])
                    print(f"{i:08X}: {hex_str:<48} {ascii_str}")
        except:
            pass

        sys.exit(1)


if __name__ == "__main__":
    main()

'''

// psuedocode 

int lh_decompress(
        struct _LHStats *stats,
        struct _LHCtx *ctx,
        unsigned __int8 *in_start,
        unsigned __int8 *in_end,
        unsigned __int8 *out_start,
        unsigned __int8 *out_end)
{
  uint32_t version; // r11
  int result; // r3
  uint32_t v14; // r8

  version = ctx->version;
  if ( version != 2 )
  {
    if ( version == 1 )
      lh_init_v1(&ctx->in_ptr);
    lh_set_ident(&ctx->in_ptr, 47, "1.2.3", 0x38u);
  }
  ctx->version = 2;
  if ( in_end && out_end )
  {
    ctx->in_ptr = in_start;
    ctx->in_end = in_end;
    ctx->out_ptr = out_start;
    ctx->out_end = out_end;
    lh_run(&ctx->in_ptr, 2);
    result = (int)stats;
    v14 = out_end - ctx->out_end;
    stats->in_used = in_end - ctx->in_end;
    stats->out_written = v14;
  }
  else
  {
    result = (int)stats;
    stats->in_used = 0;
    stats->out_written = 0;
  }
  return result;
}

// assembly

.text:82B49D50 # =============== S U B R O U T I N E =======================================
.text:82B49D50
.text:82B49D50
.text:82B49D50 # int lh_decompress(struct _LHStats *stats, struct _LHCtx *ctx, unsigned __int8 *in_start, unsigned __int8 *in_end, unsigned __int8 *out_start, unsigned __int8 *out_end)
.text:82B49D50 lh_decompress:                          # CODE XREF: sub_82371150+68↑p
.text:82B49D50                                         # handle_resource_LH+1A4↓p ...
.text:82B49D50
.text:82B49D50 .set back_chain, -0x90
.text:82B49D50
.text:82B49D50                 mflr      r12
.text:82B49D54                 bl        __savegprlr_26
.text:82B49D58                 stwu      r1, back_chain(r1)
.text:82B49D5C                 mr        r31, r4
.text:82B49D60                 mr        r30, r3
.text:82B49D64                 mr        r27, r5
.text:82B49D68                 mr        r29, r6
.text:82B49D6C                 mr        r26, r7
.text:82B49D70                 lwz       r11, 4(r31)
.text:82B49D74                 mr        r28, r8
.text:82B49D78                 cmpwi     cr6, r11, 2
.text:82B49D7C                 beq       cr6, loc_82B49DA8
.text:82B49D80                 cmpwi     cr6, r11, 1
.text:82B49D84                 bne       cr6, loc_82B49D90
.text:82B49D88                 addi      r3, r31, 8    # state
.text:82B49D8C                 bl        lh_init_v1
.text:82B49D90
.text:82B49D90 loc_82B49D90:                           # CODE XREF: lh_decompress+34↑j
.text:82B49D90                 lis       r11, a123@ha  # "1.2.3"
.text:82B49D94                 li        r6, 0x38 # '8' # value
.text:82B49D98                 addi      r5, r11, a123@l # "1.2.3"
.text:82B49D9C                 li        r4, 0x2F # '/' # sep_char
.text:82B49DA0                 addi      r3, r31, 8    # state
.text:82B49DA4                 bl        lh_set_ident
.text:82B49DA8
.text:82B49DA8 loc_82B49DA8:                           # CODE XREF: lh_decompress+2C↑j
.text:82B49DA8                 li        r11, 2
.text:82B49DAC                 cmplwi    cr6, r29, 0
.text:82B49DB0                 stw       r11, 4(r31)
.text:82B49DB4                 beq       cr6, loc_82B49E00
.text:82B49DB8                 cmplwi    cr6, r28, 0
.text:82B49DBC                 beq       cr6, loc_82B49E00
.text:82B49DC0                 stw       r27, 8(r31)
.text:82B49DC4                 addi      r3, r31, 8    # state
.text:82B49DC8                 stw       r29, 0xC(r31)
.text:82B49DCC                 li        r4, 2         # mode
.text:82B49DD0                 stw       r26, 0x14(r31)
.text:82B49DD4                 stw       r28, 0x18(r31)
.text:82B49DD8                 bl        lh_run
.text:82B49DDC                 lwz       r11, 0xC(r31)
.text:82B49DE0                 lwz       r10, 0x18(r31)
.text:82B49DE4                 mr        r3, r30
.text:82B49DE8                 subf      r9, r11, r29
.text:82B49DEC                 subf      r8, r10, r28
.text:82B49DF0                 stw       r9, 0(r30)
.text:82B49DF4                 stw       r8, 4(r30)
.text:82B49DF8                 addi      r1, r1, 0x90
.text:82B49DFC                 b         __restgprlr_26
.text:82B49E00 # ---------------------------------------------------------------------------
.text:82B49E00
.text:82B49E00 loc_82B49E00:                           # CODE XREF: lh_decompress+64↑j
.text:82B49E00                                         # lh_decompress+6C↑j
.text:82B49E00                 li        r11, 0
.text:82B49E04                 mr        r3, r30
.text:82B49E08                 stw       r11, 0(r30)
.text:82B49E0C                 stw       r11, 4(r30)
.text:82B49E10                 addi      r1, r1, 0x90
.text:82B49E14                 b         __restgprlr_26
.text:82B49E14 # End of function lh_decompress
.text:82B49E14
.text:82B49E18
'''
