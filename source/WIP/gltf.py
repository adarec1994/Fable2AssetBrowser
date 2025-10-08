import struct
import sys

def read_dword_be(f):
    """Read big-endian DWORD"""
    return struct.unpack('>I', f.read(4))[0]

def read_string(f):
    """Read null-terminated string"""
    chars = []
    while True:
        c = f.read(1)
        if c == b'\x00' or not c:
            break
        chars.append(c)
    return b''.join(chars).decode('utf-8', errors='ignore')

def read_bone_transform(f):
    """Read 11 big-endian floats"""
    data = struct.unpack('>11f', f.read(44))
    return {
        'quat': data[0:4],
        'pos': data[4:7],
        'float7_9': data[7:10],
        'float10': data[10]
    }

def main():
    if len(sys.argv) < 2:
        print("Usage: python bone_parser.py <mdl_file>")
        sys.exit(1)
    
    filename = sys.argv[1]
    
    with open(filename, 'rb') as f:
        # char MagicNumber[8]
        f.seek(8, 1)
        # DWORD Unk
        f.seek(4, 1)
        # DWORD HeaderSize
        f.seek(4, 1)
        # ubyte Headerst[88]
        f.seek(88, 1)
        # DWORD Unk1[8]
        f.seek(32, 1)
        
        # DWORD BoneCount
        bone_count = read_dword_be(f)
        
        # BoneEntry Bones[BoneCount]
        for i in range(bone_count):
            bone_name = read_string(f)
            parent_id = read_dword_be(f)
        
        # DWORD BoneTransformCount
        bone_transform_count = read_dword_be(f)
        
        # Read bone transforms
        print(f"Reading {bone_transform_count} bone transforms:\n")
        
        for i in range(bone_transform_count):
            bt = read_bone_transform(f)
            print(f"BoneTransform[{i}]:")
            print(f"  Quat: ({bt['quat'][0]:.6f}, {bt['quat'][1]:.6f}, {bt['quat'][2]:.6f}, {bt['quat'][3]:.6f})")
            print(f"  Pos:  ({bt['pos'][0]:.6f}, {bt['pos'][1]:.6f}, {bt['pos'][2]:.6f})")
            print(f"  F7-9: ({bt['float7_9'][0]:.6f}, {bt['float7_9'][1]:.6f}, {bt['float7_9'][2]:.6f})")
            print(f"  F10:  {bt['float10']:.6f}")
            print()

if __name__ == "__main__":
    main()