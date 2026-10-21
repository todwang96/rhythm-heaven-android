#!/usr/bin/env python3
"""节奏天国 · 把模块表与 RomFS 推到手机。

目标：/sdcard/Android/media/com.rhport.rhythm/rhdata/
      ├── modules/   改写产物（main/sdk 的 patched.bin + 十种 tsv，约 1.4 MB）
      ├── romfs/     游戏资源（约 3.4 GB，6411+ 个文件）
      └── save/      app 自己建

为什么用 Android/media：/data 下 adb 建的目录 shell 所有、app 进不去；app 建的目录
app 所有、adb 进不去。media 走路径级授权，双方都能访问，且不需要任何存储权限。

用法：
  python push_data.py            # 全推
  python push_data.py --modules  # 只推模块表（改写了 NSO 之后重推这个就行）
  python push_data.py --check    # 只看设备上现有的
"""
import argparse
import subprocess
import sys
from pathlib import Path

ADB = Path("C:/Users/Administrator/AppData/Local/Android/Sdk/platform-tools/adb.exe")
PKG = "com.rhport.rhythm"
DEST = f"/sdcard/Android/media/{PKG}/rhdata"

PORT = Path(__file__).resolve().parent.parent
PATCHED = PORT / "work/rhythm-heaven/patched-android"
ROMFS = Path("D:/workspace/2026-10-17-22-51-50/rh/romfs")
ROMFS_IMAGE = Path("D:/workspace/2026-10-17-22-51-50/rh/romfs.img")
SHADERS = PORT / "work/shaders/vk-cache"
SERIAL = None   # --serial 指定设备；不填则用 adb 的第一台


def adb(*args, **kw):
    cmd = [str(ADB)] + (["-s", SERIAL] if SERIAL else []) + list(args)
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace", **kw)


def shell(cmd):
    result = adb("shell", cmd)
    return (result.stdout or "").strip()


def check():
    print(f"设备上的 {DEST}:")
    print(shell(f"ls -la {DEST} 2>/dev/null || echo '(不存在)'"))
    print("\n空间：")
    print(shell("df -h /data /storage/emulated/0 2>/dev/null | tail -3"))
    print("\n已装包：")
    print(shell(f"pm path {PKG} 2>/dev/null || echo '(未安装)'"))


def main():
    global SERIAL
    parser = argparse.ArgumentParser()
    parser.add_argument("--modules", action="store_true", help="只推模块表")
    parser.add_argument("--shaders", action="store_true", help="只推 Vulkan shader 缓存")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--serial", default=None, help="目标设备序列号（adb devices 里的）")
    parser.add_argument("--tree", action="store_true", help="推解包后的 romfs 目录树（默认推单文件 romfs.img）")
    args = parser.parse_args()
    SERIAL = args.serial

    devices = adb("devices").stdout.strip().splitlines()[1:]
    if not any("device" in line for line in devices):
        sys.exit("没有设备")
    print(f"设备：{devices[0].strip()}")

    if args.check:
        check()
        return 0

    if not shell(f"pm path {PKG}"):
        sys.exit(f"{PKG} 未安装，先装 APK")

    # 1) 模块表
    tables = sorted(PATCHED.glob("*.*.tsv")) + sorted(PATCHED.glob("*.patched.bin"))
    if not tables:
        sys.exit(f"{PATCHED} 里没有改写产物")
    print(f"[1/3] 推模块表 {len(tables)} 个文件（{sum(t.stat().st_size for t in tables)/2**20:.1f} MB）")
    # 注意：adb push 一个目录到已存在的目标会把它"嵌套"进去（dest/patched-android/…），
    # Windows 上连 <dir>/. 也会被规范化掉。所以逐个文件推，铺平到 modules/。
    shell(f"mkdir -p {DEST}/modules")
    for table in tables:
        subprocess.run([str(ADB)] + (["-s", SERIAL] if SERIAL else []) + ["push", str(table), f"{DEST}/modules/"], check=True)

    if args.modules:
        print("模块表已更新")
        return 0

    # 2) Vulkan shader 缓存（缺这个会在运行到第一个 shader 时 die 崩溃——pad 上就是这样）
    if not SHADERS.is_dir():
        print(f"[2/3] 跳过 shader 缓存（{SHADERS} 不存在，可先跑 tools/make_vk_shader_cache.py）")
    else:
        cache = sorted(SHADERS.glob("*"))
        print(f"[2/3] 推 Vulkan shader 缓存 {len(cache)} 个文件（{sum(f.stat().st_size for f in cache)/2**20:.2f} MB）")
        shell(f"mkdir -p {DEST}/shaders")
        for f in cache:
            subprocess.run([str(ADB)] + (["-s", SERIAL] if SERIAL else []) + ["push", str(f), f"{DEST}/shaders/"], check=True)
    if args.shaders:
        print("shader 缓存已更新")
        return 0

    # 3) RomFS：优先推单文件镜像（1 个文件，启动时 romfs_image 挂载）；--tree 则推目录树
    if ROMFS_IMAGE.is_file() and not args.tree:
        print(f"[3/3] 推 romfs.img {ROMFS_IMAGE.stat().st_size/2**30:.2f} GB（单文件镜像）")
        shell(f"rm -rf {DEST}/romfs")
        subprocess.run([str(ADB)] + (["-s", SERIAL] if SERIAL else []) + ["push", str(ROMFS_IMAGE), f"{DEST}/romfs.img"], check=True)
    else:
        if not ROMFS.is_dir():
            sys.exit(f"{ROMFS} 不存在")
        files = sum(1 for _ in ROMFS.rglob("*") if _.is_file())
        size = sum(f.stat().st_size for f in ROMFS.rglob("*") if f.is_file())
        print(f"[3/3] 推 RomFS {files} 个文件 / {size/2**30:.2f} GB（USB 3.x 约 1 分钟）")
        # RomFS 目录多、体积大，用整目录推（目标不存在时不会嵌套），先删掉旧目录
        shell(f"rm -rf {DEST}/romfs")
        subprocess.run([str(ADB)] + (["-s", SERIAL] if SERIAL else []) + ["push", str(ROMFS), f"{DEST}/romfs"], check=True)

    print("\n完成。设备上的结果：")
    print(shell(f"du -sh {DEST}/modules {DEST}/romfs.img {DEST}/romfs {DEST}/shaders 2>/dev/null; ls {DEST}/modules | head -5"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
