import struct
from collections import Counter

DLL = r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
data = open(DLL, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
opt = pe + 24
imagebase = struct.unpack_from("<Q", data, opt + 24)[0]
sects = []
for i in range(nsec):
    off = opt + optsz + i * 40
    name = data[off:off+8].rstrip(b"\0").decode()
    vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
    sects.append((name, va, vsize, rawptr, rawsize))

text = next(s for s in sects if s[0] == ".text")
text_va, text_raw, text_sz = text[1], text[3], text[4]
blob = data[text_raw:text_raw+text_sz]

# section containing rva helper
def section_of(rva):
    for s in sects:
        if s[1] <= rva < s[1] + max(s[2], s[4]):
            return s[0]
    return None

cnt = Counter()
combo = []
for rex in (b"", b"\x48", b"\x4c", b"\x49"):
    for op in (b"\x8b", b"\x89", b"\x8d", b"\x8b"):
        for modrm in (0x05, 0x0d, 0x15, 0x1d, 0x25, 0x2d, 0x35, 0x3d):
            combo.append(rex + op + bytes([modrm]))

for pat in combo:
    start = 0
    while True:
        i = blob.find(pat, start)
        if i < 0: break
        start = i + 1
        disp_off = i + len(pat)
        if disp_off + 4 > len(blob): continue
        disp = struct.unpack_from("<i", blob, disp_off)[0]
        tgt_rva = text_va + disp_off + 4 + disp
        sec = section_of(tgt_rva)
        if sec in (".data", ".bss"):
            cnt[tgt_rva] += 1

print("top referenced .data globals (rva, hits, section):")
for rva, c in cnt.most_common(60):
    print(f"  {rva:08X}  {c:5d}  {section_of(rva)}")
