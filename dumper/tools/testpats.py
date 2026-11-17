import struct, re, json, sys

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
    chars = struct.unpack_from("<I", data, off + 36)[0]
    sects.append((name, va, vsize, rawptr, rawsize, chars))

secmap = []
txt = next(s for s in sects if s[0] == ".text")
text_va, text_raw, text_sz = txt[1], txt[3], txt[4]
blob = data[text_raw:text_raw+text_sz]
print("text", hex(text_va), hex(text_sz), "imagebase", hex(imagebase))

def parse(pat):
    ops = []
    for tok in pat.split():
        if tok in ("?", "??"):
            ops.append(("w",))
            continue
        i = 0
        while i < len(tok):
            c = tok[i]
            if c == "$":
                if tok[i:i+2] == "${":
                    if tok[i+2] == "}":
                        ops.append(("rip", 0, False)); i += 3
                    elif tok[i+2] == "'":
                        ops.append(("rip", 0, True)); i += 4
                    else:
                        m = re.match(r"\$\{\[(\d+)\]'\}", tok[i:])
                        ops.append(("rip", int(m.group(1)), True)); i += m.end()
                    continue
                raise ValueError(tok)
            if c == "u":
                n = int(tok[i+1]); ops.append(("u", n)); i += 2
                continue
            if c == "[":
                j = tok.index("]", i)
                ops.append(("s", int(tok[i+1:j]))); i = j + 1
                continue
            if c == "?":
                j = i
                while j < len(tok) and tok[j] == "?": j += 1
                ops.append(("s", j - i)); i = j
                continue
            ops.append(("b", int(tok[i:i+2], 16))); i += 2
    return ops

def match_at(buf, i, ops):
    save = []
    for op in ops:
        if op[0] == "b":
            if i >= len(buf) or buf[i] != op[1]: return None
            i += 1
        elif op[0] == "w":
            i += 1
        elif op[0] == "s":
            i += op[1]
        elif op[0] == "u":
            n = op[1]
            if i + n > len(buf): return None
            save.append(struct.unpack_from("<I" if n == 4 else "<B", buf, i)[0])
            i += n
        elif op[0] == "rip":
            add, dosave = op[1], op[2]
            if i + 4 > len(buf): return None
            disp = struct.unpack_from("<i", buf, i)[0]
            tgt = text_va + i + 4 + disp  # rva
            i += 4
            if dosave: save.append((tgt + add) & 0xFFFFFFFF)
    return save

def find(pat):
    ops = parse(pat)
    for i in range(0, len(blob) - 32):
        s = match_at(blob, i, ops)
        if s is not None:
            return text_va + i, s
    return None

pats = {
 "dwCSGOInput": ("488905${'} 0f57c0 0f1105", None),
 "dwEntityList": ("48890d${'} e9${} cc", None),
 "dwGameEntitySystem": ("488b1d${'} 48891d[4] 4c63b3", None),
 "dwGameEntitySystem_highestEntityIndex": ("ff81u4 4885d2", None),
 "dwGameRules": ("f6c1010f85${} 4c8b05${'} 4d85", None),
 "dwGlobalVars": ("488915${'} 488942", None),
 "dwGlowManager": ("488b05${'} c3 cccccccccccccccc 8b41", None),
 "dwLocalPlayerController": ("488b05${'} 4189be", None),
 "dwPlantedC4": ("488b1d${'} 4532f6", None),
 "dwPrediction": ("488d05${'} c3 cccccccccccccccc 405356 4154", None),
 "dwSensitivity": ("488d0d${[8]'} 0f57c90f28f0", None),
 "dwViewMatrix": ("488d0d${'} 48c1e006", None),
 "dwViewRender": ("488905${'} 488bc8 4885c0", None),
 "dwWeaponC4": ("488b15${'} 488b5c24? ffc0 8905${} 488bc6 488934ea 80be", None),
}
a2x = json.load(open("a2x_offsets.json"))["client.dll"]
print(json.dumps(a2x))
for name, (p, _) in pats.items():
    r = find(p)
    if not r:
        print(f"{name:40s} PATTERN NOT FOUND")
        continue
    rva, save = r
    val = save[1] if len(save) > 1 else save[0]
    ref = a2x.get(name)
    ok = "OK" if val == ref else "MISMATCH"
    print(f"{name:40s} found_at={rva:X} val={val:X} a2x={ref:X} {ok}")

r = find("4c39b6u4 74? 4488be")
print("localpawn extra:", r, "a2x dwLocalPlayerPawn", hex(a2x["dwLocalPlayerPawn"]))
r2 = find("f2420f108428u4")
print("viewangles extra:", r2, "a2x dwViewAngles", hex(a2x["dwViewAngles"]))
