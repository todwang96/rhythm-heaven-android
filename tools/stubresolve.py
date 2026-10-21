#!/usr/bin/env python3
"""解析 PLT 桩：给出桩地址，还原它用哪个 GOT 槽、槽里的重定位与符号。

  python tools/stubresolve.py sdk 0x641030 0x641ef0 0x641f30
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capstone
from pltcheck import Image, describe_reloc

ROOT = Path(__file__).resolve().parent.parent
IMGDIR = ROOT / "work/rhythm-heaven/patched-android"


def resolve(img, stub, verbose=True):
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True
    code = img.data[stub:stub + 16]
    base = None
    for ins in md.disasm(code, stub):
        if verbose:
            print(f"    {ins.address:#08x}: {ins.mnemonic:<5} {ins.op_str}")
        if ins.mnemonic == "adrp":
            base = ins.operands[1].imm
        elif ins.mnemonic == "ldr" and base is not None:
            off = ins.operands[1].mem.disp
            slot = base + off
            if verbose:
                print(f"    → GOT 槽 {slot:#x}：文件原值 {img.u64(slot):#x}")
                print(f"    → {describe_reloc(img, slot)}")
            return slot
    return None


def main(argv):
    img = Image(str(IMGDIR / f"{argv[1]}.patched.bin"))
    for a in argv[2:]:
        print(f"--- 桩 {a}")
        resolve(img, int(a, 0))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
