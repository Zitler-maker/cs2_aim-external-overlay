import struct, sys, json

DLL = r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"

targets = {
 "dwCSGOInput": 0x2580A10,
 "dwEntityList": 0x271FBB8,
 "dwGameEntitySystem": 0x271FBB8,
 "dwGameRules": 0x1BB8D60,
 "dwGlobalVars": 0x2235108,
 "dwGlowManager": 0x2567120,
 "dwLocalPlayerController": 0x2542548,
 "dwLocalPlayerPawn": 0x256A948,
 "dwPlantedC4": 0x24D0B70,
 "dwPrediction": 0x256A850,
 "dwSensitivity": 0x2567C48,
 "dwViewAngles": 0x2581098,
 "dwViewMatrix": 0x2570250,
 "dwViewRender": 0x2570C18,
 "dwWeaponC4": 0x24CF118,
}

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
    print(f"{name:10s} va={va:08X} vsize={vsize:08X} raw={rawptr:08X} rawsz={rawsize:08X}")

def va_to_off(va):
    rva = va - imagebase
    for name, sva, vsize, rawptr, rawsize in sects:
        if sva <= rva < sva + max(vsize, rawsize):
            return rva - sva + rawptr
    return None

text = next(s for s in sects if s[0] == ".text")
text_va, text_raw, text_sz = text[1], text[3], text[4]
blob = data[text_raw:text_raw+text_sz]
print("text size", len(blob))

combo = []
for rex in (b"", b"\x48", b"\x4c", b"\x49"):
    for op in (b"\x8b", b"\x89", b"\x8d", b"\x3b", b"\x39", b"\x85", b"\xc7"):
        for modrm in (0x05, 0x0d, 0x15, 0x1d, 0x25, 0x2d, 0x35, 0x3d):
            combo.append(rex + op + bytes([modrm]))

results = {}
for name, tgt in targets.items():
    absva = imagebase + tgt
    hits = []
    for pat in combo:
        start = 0
        while True:
            i = blob.find(pat, start)
            if i < 0:
                break
            start = i + 1
            insn_start = i
            disp_off = i + len(pat)
            if disp_off + 4 > len(blob):
                continue
            disp = struct.unpack_from("<i", blob, disp_off)[0]
            next_va = imagebase + text_va + disp_off + 4
            if (next_va + disp) & 0xFFFFFFFFFFFFFFFF == absva:
                hits.append((insn_start, pat.hex(), disp_off + 4 - insn_start))
    results[name] = hits
    print(f"\n{name} ({tgt:X}) hits={len(hits)}")
    for (pos, pat, ilen) in hits[:8]:
        ctx = blob[max(0,pos-8):pos+24]
        print(f"  pos_va={imagebase+text_va+pos:X} pat={pat} ctx={ctx.hex(' ')}")
