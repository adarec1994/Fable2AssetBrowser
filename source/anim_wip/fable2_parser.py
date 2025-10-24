#!/usr/bin/env python3
"""
Fable 2 Animation Chunk Analyzer
Analyzes animation data chunks to determine format
"""

import struct
import sys
from pathlib import Path


def analyze_animation_chunk(data, chunk_index):
    """Deep analysis of a single animation chunk"""
    
    print(f"\n{'='*70}")
    print(f"DETAILED ANALYSIS - Animation Chunk {chunk_index}")
    print(f"Size: {len(data)} bytes (0x{len(data):X})")
    print(f"{'='*70}")
    
    if len(data) < 16:
        print("Chunk too small to analyze")
        return
    
    # === HEADER ANALYSIS ===
    print("\n--- HEADER ANALYSIS ---")
    
    # Try different header interpretations
    byte0, byte1, byte2, byte3 = data[0], data[1], data[2], data[3]
    print(f"First 4 bytes: {byte0:02X} {byte1:02X} {byte2:02X} {byte3:02X}")
    print(f"  As separate bytes: {byte0}, {byte1}, {byte2}, {byte3}")
    
    word0 = struct.unpack('<H', data[0:2])[0]
    word1 = struct.unpack('<H', data[2:4])[0]
    print(f"  As WORDs (LE): {word0}, {word1}")
    
    dword0 = struct.unpack('<I', data[0:4])[0]
    print(f"  As DWORD (LE): {dword0}")
    
    # Check for common patterns
    if byte0 == 0:
        print(f"  -> Byte[0] = 0: likely version/flags")
    if byte1 < 100:
        print(f"  -> Byte[1] = {byte1}: could be bone count or frame count")
    if byte2 < 100:
        print(f"  -> Byte[2] = {byte2}: could be frame count or bone count")
    
    # === PATTERN DETECTION ===
    print("\n--- PATTERN DETECTION ---")
    
    # Look for repeating bytes
    byte_counts = {}
    for b in data:
        byte_counts[b] = byte_counts.get(b, 0) + 1
    
    common_bytes = sorted(byte_counts.items(), key=lambda x: x[1], reverse=True)[:5]
    print("Most common bytes:")
    for byte_val, count in common_bytes:
        print(f"  0x{byte_val:02X}: appears {count} times ({count/len(data)*100:.1f}%)")
    
    # Look for 2-byte patterns
    if len(data) >= 32:
        patterns = {}
        for i in range(len(data) - 1):
            pattern = (data[i], data[i+1])
            patterns[pattern] = patterns.get(pattern, 0) + 1
        
        common_patterns = sorted(patterns.items(), key=lambda x: x[1], reverse=True)[:5]
        if common_patterns[0][1] > 2:  # Only show if pattern repeats
            print("\nRepeating 2-byte patterns:")
            for (b1, b2), count in common_patterns[:3]:
                if count > 2:
                    print(f"  {b1:02X} {b2:02X}: appears {count} times")
    
    # === DATA STRUCTURE GUESS ===
    print("\n--- STRUCTURE HYPOTHESIS ---")
    
    # Try to deduce structure based on size and patterns
    if len(data) < 100:
        print("Small chunk: likely simple animation or reference")
    elif len(data) > 400:
        print("Large chunk: complex animation with many frames/bones")
    
    # Calculate possible structures
    if byte1 > 0 and byte1 < 80:
        # Assume byte1 is bone/frame count
        remainder = len(data) - 16  # Assume 16 byte header
        bytes_per_unit = remainder / byte1
        print(f"\nIf byte[1]={byte1} is count (with 16-byte header):")
        print(f"  -> {bytes_per_unit:.1f} bytes per unit")
        
        # Check if it matches known transform sizes
        if 40 <= bytes_per_unit <= 50:
            print(f"  -> Matches quaternion (16) + position (12) + scale (12) + padding")
        elif 25 <= bytes_per_unit <= 35:
            print(f"  -> Matches quaternion (16) + position (12)")
        elif 12 <= bytes_per_unit <= 20:
            print(f"  -> Matches compressed/quantized data")
    
    # === HEX DUMP ===
    print("\n--- HEX DUMP (First 128 bytes) ---")
    for i in range(0, min(128, len(data)), 16):
        hex_part = ' '.join(f'{b:02x}' for b in data[i:i+16])
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
        print(f"{i:04x}: {hex_part:<48} {ascii_part}")
    
    # === TRY TO PARSE AS TRANSFORMS ===
    print("\n--- ATTEMPTING TRANSFORM INTERPRETATION ---")
    
    if len(data) >= 32:
        print("\nFirst potential transform (assuming 16-byte header):")
        offset = 16
        
        # Try as quaternion + position
        if offset + 28 <= len(data):
            qx, qy, qz, qw = struct.unpack('<4f', data[offset:offset+16])
            px, py, pz = struct.unpack('<3f', data[offset+16:offset+28])
            
            print(f"  Quaternion: ({qx:.4f}, {qy:.4f}, {qz:.4f}, {qw:.4f})")
            print(f"  Position: ({px:.4f}, {py:.4f}, {pz:.4f})")
            
            # Check if quaternion is normalized (should be ~1.0)
            quat_len = (qx*qx + qy*qy + qz*qz + qw*qw) ** 0.5
            print(f"  Quaternion length: {quat_len:.4f} {'✓ normalized' if 0.95 < quat_len < 1.05 else '✗ not normalized'}")
            
            # Check if position is reasonable (not huge numbers)
            if abs(px) < 100 and abs(py) < 100 and abs(pz) < 100:
                print(f"  Position seems reasonable ✓")
            else:
                print(f"  Position seems unreasonable ✗")


def main():
    if len(sys.argv) < 2:
        print("Usage: python fable2_anim_decoder.py <animation_data_file>")
        sys.exit(1)
    
    filepath = sys.argv[1]
    
    with open(filepath, 'rb') as f:
        data = f.read()
    
    # Parse header
    magic, version, unk1, unk2, unk3, offset_count = struct.unpack('>6I', data[0:24])
    
    print(f"Animation Data File: {filepath}")
    print(f"Magic: 0x{magic:08X}")
    print(f"Offset Count: {offset_count}")
    
    # Read offsets
    offsets = []
    for i in range(offset_count):
        offset = struct.unpack('>I', data[32 + i*4:32 + i*4 + 4])[0]
        offsets.append(offset)
    
    # Analyze first 3 chunks in detail
    print("\n" + "="*70)
    print("ANALYZING FIRST 3 ANIMATION CHUNKS")
    print("="*70)
    
    for i in range(min(3, len(offsets) - 1)):
        start = offsets[i]
        end = offsets[i + 1]
        chunk = data[start:end]
        analyze_animation_chunk(chunk, i)
    
    print(f"\n{'='*70}")
    print("Analysis complete!")
    print(f"{'='*70}")


if __name__ == '__main__':
    main()