#!/usr/bin/env python3
"""离线重定位检查器（不依赖手机）。

用途：模块映像里如果某个槽存了一个「模块内偏移」（例如 0x514700）而没有任何
重定位覆盖它，运行期这个槽一旦被当函数指针调用，PC 就会落在裸偏移上（崩溃地址
小得不像映射地址）。这个脚本把这类槽全找出来，并反查它指向哪个符号。

原理：
  1. 从 fixups.tsv 头部读布局（TEXT_END / RO_OFF / DATA_OFF / PATCH_OFF）
  2. 从 MOD0 的动态段读 RELA / JMPREL / RELR 三张重定位表，展开成「被覆盖的槽」
  3. 扫 [RO_OFF, PATCH_OFF) 里 8 字节对齐的值，找出
       值 < 模块大小、且没有被任何重定位覆盖、且能对应上一个符号
     的槽 —— 这些就是嫌疑
  4. 反过来也报：覆盖了但值不像地址的槽（可能重定位表本身有问题）

用法：
  python tools/relocheck.py                      # 检查 patched-android 下的 main 与 sdk
  python tools/relocheck.py --module main        # 只查一个
  python tools/relocheck.py --dir <目录>          # 换产物目录
"""
import argparse
import struct
import sys
from pathlib import Path

PORT = Path(__file__).resolve().parent.parent
DEFAULT_DIR = PORT / "work/rhythm-heaven/patched-android"

DT_STRTAB, DT_SYMTAB, DT_RELA, DT_RELASZ = 5, 6, 7, 8
DT_STRSZ, DT_JMPREL, DT_PLTRELSZ = 10, 23, 2
DT_RELRSZ, DT_RELR = 35, 36
R_AARCH64_RELATIVE = 1027


def read_layout(tsv: Path):
    layout = {}
    for line in tsv.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("#"):
            break
        parts = line[1:].split("\t")
        if len(parts) >= 2:
            try:
                layout[parts[0].strip()] = int(parts[1].strip(), 0)
            except ValueError:
                pass
    return layout


def dynamic_tags(d: bytes, mod0: int):
    dyn = mod0 + struct.unpack_from("<i", d, mod0 + 4)[0]
    tags = {}
    for k in range(96):
        tag, val = struct.unpack_from("<qQ", d, dyn + k * 16)
        if tag == 0:
            break
        tags[tag] = val
    return tags


def symbols(d: bytes, tags, count):
    symtab, strtab = tags.get(DT_SYMTAB), tags.get(DT_STRTAB)
    if not symtab or not strtab:
        return {}
    by_value = {}
    for s in range(count):
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from(
            "<IBBHQQ", d, symtab + s * 24)
        if not st_value or not st_shndx:
            continue
        end = d.index(b"\0", strtab + st_name)
        name = d[strtab + st_name:end].decode("latin-1")
        # 优先后取函数名；遇到同名地址保留第一个
        by_value.setdefault(st_value, name)
    return by_value


def covered_slots(d: bytes, tags):
    """所有被重定位覆盖的槽地址集合。"""
    covered = {}
    rela, relasz = tags.get(DT_RELA, 0), tags.get(DT_RELASZ, 0)
    jmprel, pltsz = tags.get(DT_JMPREL, 0), tags.get(DT_PLTRELSZ, 0)
    for off, size, kind in ((rela, relasz, "RELA"), (jmprel, pltsz, "JMPREL")):
        if not off or not size:
            continue
        for i in range(size // 24):
            r_offset, r_info, r_addend = struct.unpack_from("<QQq", d, off + i * 24)
            covered[r_offset] = (kind, r_info & 0xFFFFFFFF, r_info >> 32, r_addend)
    relr, relrsz = tags.get(DT_RELR, 0), tags.get(DT_RELRSZ, 0)
    if relr and relrsz:
        where = 0
        for i in range(relrsz // 8):
            entry, = struct.unpack_from("<Q", d, relr + i * 8)
            if not (entry & 1):
                where = entry
                covered[where] = ("RELR", 0, 0, 0)
                where += 8
            else:
                bits = entry >> 1
                while bits:
                    if bits & 1:
                        covered[where] = ("RELR", 0, 0, 0)
                    bits >>= 1
                    where += 8
    return covered


def check(name: str, directory: Path, limit: int):
    image = directory / f"{name}.patched.bin"
    tsv = directory / f"{name}.fixups.tsv"
    if not image.exists() or not tsv.exists():
        print(f"跳过 {name}：缺 {image.name} 或 {tsv.name}")
        return None

    d = image.read_bytes()
    layout = read_layout(tsv)
    text_end = layout.get("TEXT_END", 0)
    ro_off = layout.get("RO_OFF", text_end)
    patch_off = layout.get("PATCH_OFF", len(d))
    mod0 = d.find(b"MOD0")
    tags = dynamic_tags(d, mod0)
    nsym = (tags[DT_STRTAB] - tags[DT_SYMTAB]) // 24
    syms = symbols(d, tags, nsym)
    covered = covered_slots(d, tags)

    # 元数据区（符号表/字符串表/三张重定位表/键页）本身就是按固定格式排的字节，
    # 当成指针槽扫会产生大量假阳性 —— 先排除掉。
    exclude = []
    symtab, strtab = tags.get(DT_SYMTAB, 0), tags.get(DT_STRTAB, 0)
    if symtab and strtab:
        exclude.append((symtab, strtab + tags.get(DT_STRSZ, 0)))
    if tags.get(DT_RELA):
        exclude.append((tags[DT_RELA], tags[DT_RELA] + tags.get(DT_RELASZ, 0)))
    if tags.get(DT_JMPREL):
        exclude.append((tags[DT_JMPREL], tags[DT_JMPREL] + tags.get(DT_PLTRELSZ, 0)))
    if tags.get(DT_RELR):
        exclude.append((tags[DT_RELR], tags[DT_RELR] + tags.get(DT_RELRSZ, 0)))
    key_page = layout.get("KEY_PAGE_OFF")
    if key_page:
        exclude.append((key_page, key_page + layout.get("KEY_PAGE_SIZE", 0x4000)))

    def in_metadata(offset):
        return any(a <= offset < b for a, b in exclude)

    kinds = {}
    for kind, *_ in covered.values():
        kinds[kind] = kinds.get(kind, 0) + 1
    print(f"=== {name} ===")
    print(f"  映像 {len(d)/2**20:.1f} MB · text 到 {text_end:#x} · ro {ro_off:#x} · "
          f"data 到 {patch_off:#x}")
    print(f"  重定位覆盖 {len(covered)} 个槽（" + "、".join(f"{k} {v}" for k, v in sorted(kinds.items())) + "）")

    # 嫌疑槽：值像模块内偏移、落在 text 内、能对上符号、但没有重定位覆盖
    suspects = []
    for off in range(ro_off & ~7, min(patch_off, len(d)) - 8, 8):
        value, = struct.unpack_from("<Q", d, off)
        if not (0x1000 <= value < text_end):
            continue
        if off in covered or in_metadata(off):
            continue
        symbol = syms.get(value)
        if not symbol:
            continue
        suspects.append((off, value, symbol))

    print(f"\n  嫌疑槽（值像模块内偏移 + 对得上符号 + 无重定位覆盖）：{len(suspects)} 个")
    for off, value, symbol in suspects[:limit]:
        print(f"    {off:#010x} -> {value:#x}  {symbol[:88]}")
    if len(suspects) > limit:
        print(f"    …另有 {len(suspects) - limit} 个")

    # 反向检查：覆盖了、但值不是「基址无关的偏移」也不像地址 —— 这里只统计 0 值
    zeros = [o for o, (k, t, s, a) in covered.items()
             if k != "RELR" and t != R_AARCH64_RELATIVE and a == 0 and s == 0]
    if zeros:
        print(f"\n  注意：{len(zeros)} 个 RELA 槽是「符号 0 + 加数 0」，会写成 0")
        for o in zeros[:5]:
            print(f"    {o:#010x}")
    return suspects


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", type=Path, default=DEFAULT_DIR)
    parser.add_argument("--module", action="append")
    parser.add_argument("--limit", type=int, default=20)
    args = parser.parse_args()

    modules = args.module or ["main", "sdk"]
    total = 0
    for name in modules:
        result = check(name, args.dir, args.limit)
        if result is not None:
            total += len(result)
        print()
    print(f"合计嫌疑槽 {total} 个")
    return 0


if __name__ == "__main__":
    sys.exit(main())
