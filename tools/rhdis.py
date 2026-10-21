#!/usr/bin/env python3
"""反汇编 patched.bin 里的一段，带符号标注和「槽位值」旁注。

  python tools/dis.py sdk 0xa5c40 0xa5e00        # sdk 模块 [起, 止)
  python tools/dis.py main 0x514600 0x514800

地址都是模块内偏移（与日志里的 sdk+0x… 一致）。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capstone
from pltcheck import Image

ROOT = Path(__file__).resolve().parent.parent
IMGDIR = ROOT / "work/rhythm-heaven/patched-android"


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 1
    name, start, stop = argv[1], int(argv[2], 0), int(argv[3], 0)
    img = Image(str(IMGDIR / f"{name}.patched.bin"))
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True
    code = img.data[start:stop]
    print(f"# {name}.patched.bin  [{start:#x}, {stop:#x})   模块内偏移")
    for ins in md.disasm(code, start):
        note = ""
        # 注释里带上目标地址所在的符号，跳转类指令一眼可见
        for op in ins.operands:
            if op.type == capstone.arm64.ARM64_OP_IMM:
                tgt = op.imm
                if 0 <= tgt < img.size:
                    o = img.owner(tgt)
                    if o:
                        note = f"   ; {o[0]}+{tgt - o[3]:#x}"
                        if tgt == o[3]:
                            note = f"   ; {o[0]}"
                break
        print(f"{ins.address:#08x}: {ins.bytes.hex():<8} {ins.mnemonic:<8} {ins.op_str}{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
