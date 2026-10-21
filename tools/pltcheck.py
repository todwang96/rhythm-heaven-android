#!/usr/bin/env python3
"""离线核对 PLT / GOT：不跑手机也能看清「某个 bl 到底跳去了哪个槽、槽里该放什么」。

用法：
  python tools/pltcheck.py <patched.bin> <bl 地址> [更多地址…]
  python tools/pltcheck.py sdk.patched.bin 0xa5d28

做的事：
  1. 解析 MOD0 / dynamic / symtab / strtab / RELA / JMPREL / RELR
  2. 反汇编给定地址处的指令（BL 跟到目标，ADRP+LDR+ADD+BR 还原出 GOT 槽）
  3. 报告该 GOT 槽在重定位表里的条目、文件里预置的原值、符号名与模块内偏移
  4. 列出该地址所属符号（按 st_value 最近且 ≤ 地址）
"""
import struct
import sys

R_AARCH64_ABS64 = 257
R_AARCH64_GLOB_DAT = 1025
R_AARCH64_JUMP_SLOT = 1026
R_AARCH64_RELATIVE = 1027
RELOC_NAME = {R_AARCH64_ABS64: "ABS64", R_AARCH64_GLOB_DAT: "GLOB_DAT",
              R_AARCH64_JUMP_SLOT: "JUMP_SLOT", R_AARCH64_RELATIVE: "RELATIVE"}


def s64(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


class Image:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.size = len(self.data)
        self.mod0 = struct.unpack_from("<I", self.data, 4)[0]
        if self.data[self.mod0:self.mod0 + 4] != b"MOD0":
            raise SystemExit(f"{path}: 偏移 0x{self.mod0:x} 处不是 MOD0")
        self.dyn_off = self.mod0 + struct.unpack_from("<i", self.data, self.mod0 + 4)[0]
        self.dyn = {}
        p = self.dyn_off
        while True:
            tag, val = struct.unpack_from("<QQ", self.data, p)
            p += 16
            if tag == 0:
                break
            self.dyn.setdefault(tag, val)
        self.symtab = self.dyn.get(6)
        self.strtab = self.dyn.get(5)
        self.nsym = (self.strtab - self.symtab) // 24
        self.relocs = {}          # r_offset -> (type, symidx, addend, from_plt)
        self.relr = []
        self._parse_relocs()
        self._parse_syms()

    def u32(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def u64(self, off):
        return struct.unpack_from("<Q", self.data, off)[0]

    def _parse_relocs(self):
        rela, relasz = self.dyn.get(7, 0), self.dyn.get(8, 0)
        jmprel, pltsz = self.dyn.get(23, 0), self.dyn.get(2, 0)
        for base, size, is_plt in ((rela, relasz, False), (jmprel, pltsz, True)):
            for i in range(size // 24):
                off, info, addend = struct.unpack_from("<QQq", self.data, base + i * 24)
                self.relocs[off] = (info & 0xFFFFFFFF, info >> 32, addend, is_plt)
        relr, relrsz = self.dyn.get(36, 0), self.dyn.get(35, 0)
        if relr and relrsz:
            where = 0
            for i in range(relrsz // 8):
                e = self.u64(relr + i * 8)
                if not (e & 1):
                    where = e
                    self.relr.append(where)
                    where += 8
                else:
                    bits, w = e >> 1, where
                    while bits:
                        if bits & 1:
                            self.relr.append(w)
                        bits >>= 1
                        w += 8
                    where = w

    def _parse_syms(self):
        self.syms = []
        self.strs = self.data[self.strtab:]
        for i in range(self.nsym):
            name, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", self.data, self.symtab + i * 24)
            end = self.strs.find(b"\0", name)
            self.syms.append((self.strs[name:end].decode("utf-8", "replace"), info, shndx, value, size))

    def sym(self, i):
        return self.syms[i] if 0 <= i < len(self.syms) else ("<bad>", 0, 0, 0, 0)

    def owner(self, addr):
        """返回 <= addr 的最近符号（按 st_value），以及它的大小（0 表示未知）。"""
        best = None
        for s in self.syms:
            if s[2] and s[3] <= addr and (best is None or s[3] > best[3]):
                best = s
        return best


def decode(img, addr, depth=0, indent="  "):
    """解码一条指令，返回 (文本, 分支目标或 None)。"""
    w = img.u32(addr)
    pc = addr
    if w & 0xFC000000 == 0x94000000:                       # BL
        tgt = pc + (s64(w & 0x3FFFFFF, 26) << 2)
        return f"bl {tgt:#x}", tgt
    if w & 0xFF000000 == 0x14000000:                       # B
        tgt = pc + (s64(w & 0x3FFFFFF, 26) << 2)
        return f"b {tgt:#x}", tgt
    if w & 0xFFFFFC1F == 0xD63F0000:                       # BLR Xn
        return f"blr x{(w >> 5) & 31}", None
    if w & 0xFFFFFC1F == 0xD61F0000:                       # BR Xn
        return f"br x{(w >> 5) & 31}", None
    if w & 0xFFFFFC1F == 0xD65F0000:                       # RET
        return f"ret x{(w >> 5) & 31}", None
    if w & 0x9F000000 == 0x90000000:                       # ADRP
        rd = w & 31
        immlo = (w >> 29) & 3
        immhi = (w >> 5) & 0x7FFFF
        tgt = (pc & ~0xFFF) + (s64((immhi << 2) | immlo, 21) << 12)
        return f"adrp x{rd}, {tgt:#x}", tgt
    if w & 0xFFC00000 == 0xF9400000:                       # LDR Xt, [Xn, #imm]
        rt, rn = w & 31, (w >> 5) & 31
        imm = ((w >> 10) & 0xFFF) << 3
        return f"ldr x{rt}, [x{rn}, {imm:#x}]", None
    if w & 0xFF800000 == 0x91000000:                       # ADD Xd, Xn, #imm
        rd, rn = w & 31, (w >> 5) & 31
        imm = (w >> 10) & 0xFFF
        if (w >> 22) & 1:
            imm <<= 12
        return f"add x{rd}, x{rn}, {imm:#x}", None
    if w & 0xFFC00000 == 0xF8400000:                       # LDUR-ish / LDR Xt,[Xn,#simm9]
        rt, rn = w & 31, (w >> 5) & 31
        imm = s64((w >> 12) & 0x1FF, 9)
        return f"ldur x{rt}, [x{rn}, {imm:#x}]", None
    if w & 0xFF000000 == 0x58000000:                       # LDR Xt, literal
        rt = w & 31
        tgt = pc + (s64((w >> 5) & 0x7FFFF, 19) << 2)
        return f"ldr x{rt}, {tgt:#x}", tgt
    if w & 0xFF000000 == 0x18000000:                       # LDR Wt, literal
        rt = w & 31
        tgt = pc + (s64((w >> 5) & 0x7FFFF, 19) << 2)
        return f"ldr w{rt}, {tgt:#x}", tgt
    if w & 0xFFE0FFE0 == 0xAA0003E0:                       # MOV Xd, Xn (orr xd,xzr,xn)
        return f"mov x{w & 31}, x{(w >> 16) & 31}", None
    if w & 0x9F00001F == 0x90000010:                       # 占位，避免误判
        pass
    return f".word {w:#010x}", None


def show(img, addr, label=""):
    print(f"\n=== {img.path} @ {addr:#x} {label} ===")
    own = img.owner(addr)
    if own:
        print(f"  所属符号：{own[0]} +{addr - own[3]:#x}（st_value {own[3]:#x}，size {own[4]:#x}）")
    print(f"  文件原值 @{addr:#x} = {img.u64(addr):#018x}")
    print(f"  重定位项：{img.relocs.get(addr, '（没有）')}")


def trace(img, addr, maxsteps=12):
    print(f"\n=== 从 {addr:#x} 开始跟踪 ===")
    pc = addr
    reg = {}
    for _ in range(maxsteps):
        text, tgt = decode(img, pc)
        print(f"  {pc:#08x}: {img.u32(pc):#010x}  {text}")
        if text.startswith("adrp"):
            rd = int(text.split("x")[1].split(",")[0])
            reg[rd] = ("page", tgt)
        elif text.startswith("add x"):
            parts = text[4:].replace(",", " ").split()
            rd, rn = int(parts[0][1:]), int(parts[1][1:])
            if rn in reg and reg[rn][0] == "page":
                reg[rd] = ("abs", reg[rn][1] + int(parts[2], 16))
        elif text.startswith("ldr x") and "[" in text:
            rt = int(text.split("x")[1].split(",")[0])
            rn = int(text.split("[x")[1].split(",")[0])
            if rn in reg and reg[rn][0] == "abs":
                slot = reg[rn][1]
                print(f"          → GOT 槽 {slot:#x}：文件原值 {img.u64(slot):#x}"
                      f"，重定位 {describe_reloc(img, slot)}")
                own = img.owner(img.u64(slot)) if img.u64(slot) < img.size else None
                if own:
                    print(f"          → 文件原值落在符号 {own[0]} +{img.u64(slot) - own[3]:#x}")
        elif text.startswith("br "):
            print("          → 跳转目标由上面那条 ldr 决定")
            return
        if tgt is not None and text.startswith("bl"):
            print(f"          → BL 目标 {tgt:#x}，继续跟")
            return trace(img, tgt, maxsteps)
        pc += 4


def describe_reloc(img, slot):
    r = img.relocs.get(slot)
    if r is None:
        return "无（装载器不会写这个槽！）" if slot not in img.relr else f"RELR +base（原值需是偏移）"
    typ, si, addend, is_plt = r
    name, info, shndx, value, size = img.sym(si)
    kind = RELOC_NAME.get(typ, f"type{typ}")
    where = f"本模块 +{value:#x}" if shndx else "外部导入"
    return (f"{kind} {name!r} symidx={si} addend={addend:#x} {where}"
            f"（{'JMPREL' if is_plt else 'RELA'}，st_info 绑定={info >> 4}）")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    img = Image(sys.argv[1])
    print(f"{img.path}: {img.size:#x} 字节，MOD0 @{img.mod0:#x}，dynamic @{img.dyn_off:#x}")
    print(f"  symtab @{img.symtab:#x} 符号 {img.nsym}  strtab @{img.strtab:#x}")
    print(f"  RELA {img.dyn.get(8, 0):#x} 字节（{img.dyn.get(8, 0) // 24} 项），"
          f"JMPREL {img.dyn.get(2, 0):#x} 字节（{img.dyn.get(2, 0) // 24} 项），"
          f"RELR {len(img.relr)} 槽")
    counts = {}
    for slot, (typ, si, addend, is_plt) in img.relocs.items():
        counts[RELOC_NAME.get(typ, typ)] = counts.get(RELOC_NAME.get(typ, typ), 0) + 1
    print(f"  重定位类型分布：{counts}")
    for a in sys.argv[2:]:
        trace(img, int(a, 16))
    return 0


if __name__ == "__main__":
    sys.exit(main())
