#!/usr/bin/env python3
"""节奏天国 · Android 宿主编译驱动。

用途：把 Touchable 的运行时 + d2r-mobile 的宿主/图形层按 Android arm64 编译。
参数：
  --syntax   只做语法检查（快，用来枚举还缺什么）
  --check    真编译到 .o（不链接）
不带参数 = 编译并链接成 libdmandroid.so 的雏形。

用法示例：
  python build.py --syntax
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent          # rh-port/android
PORT = ROOT.parent                              # rh-port
TOUCH = PORT / "Touchable"
UPSTREAM = PORT / ".nitenphone/dependencies/d2r-mobile"
WORK = PORT / "work/rhythm-heaven"

CLANG = Path("D:/workspace/2026-10-17-22-51-50/d2r-android/tools/ndkdl/android-ndk-r28c/"
             "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe")
TARGET = "aarch64-linux-android29"

DEFINES = [
    "_GNU_SOURCE",                     # bionic 的 cpu_set_t / CPU_ZERO 在 __USE_GNU 下
    "SS_HOST_CLI",
    "NITENPHONE_SWITCH_RUNTIME",
    "SS_SHADER_KEY_CODE",
    "SS_GUEST_LARGE_HEAP",
    "SS_NVN_LOWER_LEFT_ORIGIN",
]

INCLUDES = [
    ROOT / "compat",
    ROOT,                              # 苹果头替身，必须排在最前
    WORK / "host",                                # niten_game_config.h
    TOUCH / "internal/runtime/common",
    TOUCH / "internal/games/rhythm-heaven/runtime",
    UPSTREAM / "dm/host",
    UPSTREAM / "dm/gfx",
    UPSTREAM / "dm/gfx/nvn/include",
    UPSTREAM / "dm/gfx/nvn",
    UPSTREAM / "dm/gfx/tex",
    UPSTREAM / "dm/android",
]

HOST_SOURCES = (
    "loader.c", "hle.c", "hle_svc.c", "hle_thread.c", "hle_os.c",
    "hle_sdkinit.c", "hle_table.c", "hle_count.c",
    # 离线本地用户（节奏天国 nnMain 开头会调用 account::Initialize/GetNickname；
    # D2R 不调，所以上游 Android 分支原来没编它）
    "hle_account.c",
)


def sources():
    out = []
    # 游戏适配层 + 通用运行时
    out += sorted((TOUCH / "internal/runtime/common").glob("*.c"))
    out += sorted((TOUCH / "internal/games/rhythm-heaven/runtime").glob("*.c"))
    # 上游宿主
    out += [UPSTREAM / "dm/host" / n for n in HOST_SOURCES]
    # 图形：只要 C（Metal 的 .m 由 nvn_vk.c 顶替）
    out += sorted((UPSTREAM / "dm/gfx").glob("*.c"))
    out += sorted((UPSTREAM / "dm/gfx/nvn").glob("*.c"))
    out += sorted((UPSTREAM / "dm/gfx/tex").glob("*.c"))
    # 平台层：时钟/调度（vk 后端依赖）+ guest TLS 槽位（Android 的 TSD 布局和 Darwin 不同）
    out += [UPSTREAM / "dm/android/nvn_platform_clock.c",
            UPSTREAM / "dm/android/thread_slots.c",
            # ss_* 运行时直接调这几个平台实现（Touchable 只写过 iOS 侧，Android 的由 vickwv 提供）
            UPSTREAM / "dm/android/services_fs.c",
            UPSTREAM / "dm/android/services_platform.c",
            UPSTREAM / "dm/android/services_sdk.c",
            UPSTREAM / "dm/android/romfs_image.c"]
    # 移植层
    out += sorted((ROOT / "src").glob("*.c"))
    # 排除：Darwin 专有实现（Android 有替身）
    skip = {"ss_indirect_branch.c"}
    result = []
    for path in dict.fromkeys(out):
        if path.name in skip:
            continue
        if path.exists():
            result.append(path)
        else:
            print(f"  (缺) {path}")
    return result


ASM_SOURCES = (
    # 宿主入口（dm_gate / dm_thread_enter / dm_thread_exit_host）：vickwv 的 entry.S 自带
    # __ANDROID__ 的 #define，把下划线名映射到 ELF 名，直接编即可
    "dm/host/entry.S",
    # vickwv 分支自带的 ELF 版 NVN 跳板与跳转表（#ifdef __ANDROID__ 分支）
    "dm/gfx/nvn/nvn_entry.S",
    "dm/gfx/nvn/nvn_thunks.S",
)


RELEASE = False


def command(source, mode):
    if RELEASE:
        # 正式包：-O2、无调试信息，保留帧指针（崩溃栈继续可用）
        opt = ["-O2", "-fno-omit-frame-pointer"]
    else:
        opt = ["-O1", "-g"]
    args = [str(CLANG), f"--target={TARGET}", "-fPIC"] + opt
    args += ["-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
            "-Wno-deprecated-declarations", "-Wno-pointer-sign"]
    args += ["-fsyntax-only"] if mode == "syntax" else ["-c"]
    args += [f"-D{d}" for d in DEFINES]
    args += ["-include", str(ROOT / "compat/force_apple.h")]
    args += [f"-I{inc}" for inc in INCLUDES]
    if mode == "object":
        out = ROOT / "build" / "obj" / (source.stem + ".o")
        out.parent.mkdir(parents=True, exist_ok=True)
        args += ["-o", str(out)]
    args.append(str(source))
    return args


def asm_command(source):
    """汇编：走预处理器（.S 里有 #ifdef __ANDROID__），但不注入 C 头。"""
    out = ROOT / "build" / "obj" / (source.stem + ".o")
    out.parent.mkdir(parents=True, exist_ok=True)
    return [str(CLANG), f"--target={TARGET}", "-fPIC", "-c", "-o", str(out), str(source)]


def link():
    """把目标文件链成 libdmandroid.so，并回显未定义符号。"""
    out = ROOT / "build" / "libdmandroid.so"
    objs = sorted((ROOT / "build" / "obj").glob("*.o"))
    cmd = [str(CLANG), f"--target={TARGET}", "-shared", "-fPIC", "-o", str(out)]
    if RELEASE:
        cmd += ["-Wl,-s"]        # 去掉符号表，正式包更小
    cmd += [str(o) for o in objs]
    cmd += ["-Wl,--no-undefined", "-Wl,-soname,libdmandroid.so",
            "-lm", "-llog", "-ldl", "-landroid", "-lz", "-lvulkan", "-laaudio"]
    result = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    print((result.stderr or "")[:4000])
    if out.exists():
        print(f"\n已链接 {out}  {out.stat().st_size / 2**20:.2f} MB")
    return result.returncode


def main():
    global RELEASE
    parser = argparse.ArgumentParser()
    parser.add_argument("--syntax", action="store_true", help="只做语法检查")
    parser.add_argument("--link", action="store_true", help="编译后链接")
    parser.add_argument("--release", action="store_true", help="正式包：-O2、无 -g、链接去符号")
    args = parser.parse_args()
    RELEASE = args.release
    mode = "syntax" if args.syntax else "object"
    files = sources()
    print(f"源文件 {len(files)} 个，模式 {mode}")
    failures = []
    for path in files:
        result = subprocess.run(command(path, mode), capture_output=True, text=True, errors="replace")
        if result.returncode:
            errors = [line for line in (result.stderr or "").splitlines() if "error" in line.lower()]
            failures.append((path, errors[:3]))
    print(f"\n通过 {len(files) - len(failures)} / {len(files)}")
    for path, errors in failures:
        print(f"\n✗ {path.relative_to(PORT)}")
        for line in errors:
            print(f"    {line.strip()[:160]}")

    asm_failures = []
    if mode == "object":
        for name in ASM_SOURCES:
            src = (PORT / name) if name.startswith("android/") else (UPSTREAM / name)
            if not src.exists():
                print(f"  (缺汇编) {src}")
                continue
            result = subprocess.run(asm_command(src), capture_output=True, text=True, errors="replace")
            if result.returncode:
                asm_failures.append((src, (result.stderr or "").splitlines()[:3]))
        print(f"汇编 {len(ASM_SOURCES) - len(asm_failures)} / {len(ASM_SOURCES)}")
        for src, errors in asm_failures:
            print(f"\n✗ {src.name}")
            for line in errors:
                print(f"    {line.strip()[:160]}")

    if failures or asm_failures:
        return 1
    if args.link:
        return link()
    return 0


if __name__ == "__main__":
    sys.exit(main())
