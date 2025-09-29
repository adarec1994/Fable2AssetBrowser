"""
BNK File Extractor for Fable II
Extracts and views content of .bnk archive files
"""

import struct
import zlib
import os
from dataclasses import dataclass
from typing import List, Tuple, Optional, BinaryIO
from pathlib import Path


@dataclass
class FileEntry:
    """Represents a single file entry in the BNK archive"""
    name: str
    offset: int
    uncompressed_size: int
    compressed_size: int = 0
    chunk_count: int = 0
    chunk_sizes: List[int] = None

    def __post_init__(self):
        if self.chunk_sizes is None:
            self.chunk_sizes = []


class BNKReader:
    """Reads and extracts Fable II BNK archive files"""

    # Constants from the D code
    CHUNK_SIZE = 32768  # 32KB chunks for compressed files

    def __init__(self, file_path: str):
        self.file_path = file_path
        self.file_handle: Optional[BinaryIO] = None
        self.version = 0
        self.is_compressed = False
        self.file_entries: List[FileEntry] = []
        self.data_offset = 0

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def open(self):
        """Open the BNK file and read its header"""
        self.file_handle = open(self.file_path, 'rb')
        self._read_header()
        self._read_file_table()

    def close(self):
        """Close the BNK file"""
        if self.file_handle:
            self.file_handle.close()
            self.file_handle = None

    def _read_uint32_be(self) -> int:
        """Read a big-endian 32-bit unsigned integer"""
        data = self.file_handle.read(4)
        if len(data) != 4:
            raise ValueError("Unexpected end of file")
        return struct.unpack('>I', data)[0]

    def _read_header(self):
        """Read and parse the BNK file header"""
        # Read basic header (8 bytes)
        self.data_offset = self._read_uint32_be()
        self.version = self._read_uint32_be()

        if self.version not in [2, 3]:
            raise ValueError(f"Unsupported BNK version: {self.version}")

        if self.version == 2:
            # Version 2 header (16 bytes total)
            self.is_compressed = self.file_handle.read(1)[0] != 0
            self.file_handle.read(7)  # Skip padding
        else:
            # Version 3 header (17 bytes total)
            self.is_compressed = self.file_handle.read(1)[0] != 0

    def _decompress_chunk(self, data: bytes) -> bytes:
        """Decompress a single zlib chunk"""
        if len(data) < 2:
            return b''

        # Check for valid zlib header
        if data[0] != 0x78:
            raise ValueError("Invalid zlib header")

        # Create a new decompressor for each chunk
        decompressor = zlib.decompressobj()
        try:
            return decompressor.decompress(data)
        except zlib.error:
            # Try with raw deflate if zlib fails
            return zlib.decompress(data, -15)

    def _read_file_table(self):
        """Read and parse the file table"""
        if self.version == 3:
            # For version 3, read compressed header info
            compressed_size = self._read_uint32_be()
            uncompressed_size = self._read_uint32_be()

            if compressed_size == 0:
                return

            # Read compressed file table
            compressed_data = self.file_handle.read(compressed_size)
            file_table_data = self._decompress_chunk(compressed_data)

            # Check for continuation chunks
            while True:
                next_compressed_size = struct.unpack('>I', self.file_handle.read(4))[0]
                if next_compressed_size == 0:
                    break
                next_uncompressed_size = struct.unpack('>I', self.file_handle.read(4))[0]
                chunk_data = self.file_handle.read(next_compressed_size)
                file_table_data += self._decompress_chunk(chunk_data)

        else:
            # For version 2, seek to data offset and read
            self.file_handle.seek(self.data_offset)
            compressed_size = self._read_uint32_be()
            uncompressed_size = self._read_uint32_be()

            compressed_data = self.file_handle.read(compressed_size)
            file_table_data = self._decompress_chunk(compressed_data)

        # Parse the file table
        self._parse_file_table(file_table_data)

    def _parse_file_table(self, data: bytes):
        """Parse the uncompressed file table data"""
        offset = 0

        # Read file count
        file_count = struct.unpack('>I', data[offset:offset + 4])[0]
        offset += 4

        for _ in range(file_count):
            # Read name length
            name_length = struct.unpack('>I', data[offset:offset + 4])[0]
            offset += 4

            # Read name (convert backslashes to forward slashes)
            name = data[offset:offset + name_length].decode('utf-8', errors='replace')
            name = name.rstrip('\x00').replace('\\', '/')
            offset += name_length

            # Read file offset and size
            file_offset = struct.unpack('>I', data[offset:offset + 4])[0]
            offset += 4
            uncompressed_size = struct.unpack('>I', data[offset:offset + 4])[0]
            offset += 4

            entry = FileEntry(
                name=name,
                offset=file_offset,
                uncompressed_size=uncompressed_size
            )

            # If files are compressed, read additional info
            if self.is_compressed:
                entry.compressed_size = struct.unpack('>I', data[offset:offset + 4])[0]
                offset += 4
                entry.chunk_count = struct.unpack('>I', data[offset:offset + 4])[0]
                offset += 4

                # Read chunk sizes
                for _ in range(entry.chunk_count):
                    chunk_size = struct.unpack('>I', data[offset:offset + 4])[0]
                    entry.chunk_sizes.append(chunk_size)
                    offset += 4
            else:
                entry.compressed_size = entry.uncompressed_size

            self.file_entries.append(entry)

    def list_files(self) -> List[str]:
        """Get a list of all file names in the archive"""
        return [entry.name for entry in self.file_entries]

    def extract_file(self, file_name: str, output_path: str = None) -> bytes:
        """Extract a file by name and optionally save it"""
        # Find the file entry
        entry = None
        for e in self.file_entries:
            if e.name == file_name:
                entry = e
                break

        if not entry:
            raise FileNotFoundError(f"File '{file_name}' not found in archive")

        # Read the file data
        data = self._read_file_data(entry)

        # Save to file if path provided
        if output_path:
            os.makedirs(os.path.dirname(output_path), exist_ok=True)
            with open(output_path, 'wb') as f:
                f.write(data)

        return data

    def _read_file_data(self, entry: FileEntry) -> bytes:
        """Read and decompress file data for an entry"""
        # Calculate actual offset (version 2 uses absolute offsets)
        if self.version == 2:
            actual_offset = entry.offset
        else:
            actual_offset = self.data_offset + entry.offset

        self.file_handle.seek(actual_offset)

        if not self.is_compressed:
            # Uncompressed file - just read it
            return self.file_handle.read(entry.uncompressed_size)
        else:
            # Compressed file - read and decompress chunks
            result = b''
            bytes_read = 0

            for chunk_idx in range(entry.chunk_count):
                # Calculate chunk size
                chunk_size = min(self.CHUNK_SIZE, entry.compressed_size - bytes_read)

                # Read chunk data
                chunk_data = self.file_handle.read(chunk_size)
                bytes_read += chunk_size

                # Decompress chunk
                try:
                    decompressed = self._decompress_chunk(chunk_data)
                    result += decompressed
                except Exception as e:
                    print(f"Warning: Failed to decompress chunk {chunk_idx}: {e}")
                    # Continue with partial data

            return result[:entry.uncompressed_size]

    def extract_all(self, output_dir: str):
        """Extract all files to a directory"""
        output_path = Path(output_dir)
        output_path.mkdir(parents=True, exist_ok=True)

        for entry in self.file_entries:
            file_path = output_path / entry.name
            file_path.parent.mkdir(parents=True, exist_ok=True)

            try:
                data = self._read_file_data(entry)
                with open(file_path, 'wb') as f:
                    f.write(data)
                print(f"Extracted: {entry.name}")
            except Exception as e:
                print(f"Failed to extract {entry.name}: {e}")

    def get_file_info(self, file_name: str) -> dict:
        """Get information about a specific file"""
        for entry in self.file_entries:
            if entry.name == file_name:
                return {
                    'name': entry.name,
                    'offset': entry.offset,
                    'uncompressed_size': entry.uncompressed_size,
                    'compressed_size': entry.compressed_size,
                    'is_compressed': self.is_compressed,
                    'chunk_count': entry.chunk_count
                }
        raise FileNotFoundError(f"File '{file_name}' not found in archive")


def main():
    """Example usage"""
    import sys

    if len(sys.argv) < 2:
        print("Usage: python bnk_extractor.py <bnk_file> [output_directory]")
        sys.exit(1)

    bnk_file = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else "extracted"

    try:
        with BNKReader(bnk_file) as reader:
            print(f"BNK Version: {reader.version}")
            print(f"Compressed: {reader.is_compressed}")
            print(f"Files in archive: {len(reader.file_entries)}")
            print()

            # List files
            print("Files in archive:")
            for name in reader.list_files():
                info = reader.get_file_info(name)
                print(f"  {name} ({info['uncompressed_size']} bytes)")

            # Extract all
            print(f"\nExtracting to {output_dir}...")
            reader.extract_all(output_dir)
            print("Done!")

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()