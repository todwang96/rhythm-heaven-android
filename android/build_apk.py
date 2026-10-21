#!/usr/bin/env python3
"""节奏天国 · Android APK 打包（零 Gradle）。

流程：native 编译 → aapt2 compile/link → javac → d8 → 塞 so + dex → zipalign → apksigner。

用法：
  python build_apk.py                # 全套（含 native）
  python build_apk.py --skip-native  # 只重打包壳（native 没改时）
"""
import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent          # rh-port/android
PORT = ROOT.parent
APP = ROOT / "app"
OUT = ROOT / "out"

def find_sdk():
    for var in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        value = os.environ.get(var)
        if value and Path(value).is_dir():
            return Path(value)
    for candidate in (Path.home() / "AppData/Local/Android/Sdk", Path("/usr/local/lib/android/sdk")):
        if candidate.is_dir():
            return candidate
    sys.exit("找不到 Android SDK。请设置 ANDROID_HOME。")

def newest_child(directory, prefix):
    items = sorted((p for p in directory.glob(prefix + "*") if p.is_dir()),
                   key=lambda p: p.name, reverse=True)
    if not items:
        sys.exit(f"SDK 里缺少 {prefix}*：{directory}")
    return items[0]

SDK = find_sdk()
BUILD_TOOLS = Path(os.environ.get("DM_BUILD_TOOLS") or newest_child(SDK / "build-tools", ""))
PLATFORM = newest_child(SDK / "platforms", "android-") / "android.jar"
_jdk = os.environ.get("DM_JAVA_HOME") or os.environ.get("JAVA_HOME")
if _jdk:
    JDK = Path(_jdk)
else:
    _java = shutil.which("java")
    if not _java:
        sys.exit("找不到 java。请设置 JAVA_HOME（JDK 17）。")
    JDK = Path(_java).resolve().parent.parent
PY = sys.executable

PACKAGE = "com.rhport.rhythm"
VERSION_CODE = "1"
VERSION_NAME = "0.1-bringup"
RELEASE_VERSION_CODE = "1"
RELEASE_VERSION_NAME = "1.0.0"
KEYSTORE = ROOT / "rhport.keystore"
KS_PASS = "rhport"

# d8 / apksigner 是 .bat，靠 JAVA_HOME 找 java
os.environ["JAVA_HOME"] = str(JDK)
os.environ["PATH"] = str(JDK / "bin") + os.pathsep + os.environ.get("PATH", "")


def run(cmd, **kw):
    print("  $", " ".join(str(c) for c in cmd[:6]), "..." if len(cmd) > 6 else "")
    result = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors="replace", **kw)
    if result.returncode:
        print(result.stdout[-3000:])
        print(result.stderr[-3000:])
        sys.exit(f"失败：{cmd[0]}")
    return result


def ensure_keystore():
    if KEYSTORE.exists():
        return
    print("[0/7] 生成签名用的 keystore")
    run([JDK / "bin/keytool.exe", "-genkeypair", "-v",
         "-keystore", KEYSTORE, "-alias", "rhport", "-keyalg", "RSA", "-keysize", "2048",
         "-validity", "10000", "-storepass", KS_PASS, "-keypass", KS_PASS,
         "-dname", "CN=RHPort, OU=Port, O=RHPort, L=, S=, C=CN"])


def build_native(release=False):
    print("[1/7] 编译 native" + ("（release: -O2 / strip）" if release else ""))
    cmd = [PY, ROOT / "build.py", "--link"] + (["--release"] if release else [])
    run(cmd)
    so = ROOT / "build/libdmandroid.so"
    if not so.exists():
        sys.exit("native 没产出 libdmandroid.so")
    return so


def make_release_manifest(dest, version_code, version_name):
    """从调试 manifest 生成正式包 manifest：debuggable=false、MicRecActivity 不再导出、版本号替换。"""
    import re
    src = (APP / "AndroidManifest.xml").read_text(encoding="utf-8")
    src = src.replace('android:debuggable="true"', 'android:debuggable="false"')
    src = re.sub(r'(android:name="\.MicRecActivity"[^>]*?android:exported=")true', r'\1false', src)
    src = re.sub(r'android:versionCode="[^"]*"', f'android:versionCode="{version_code}"', src)
    src = re.sub(r'android:versionName="[^"]*"', f'android:versionName="{version_name}"', src)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(src, encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-native", action="store_true")
    parser.add_argument("--release", action="store_true", help="正式包：-O2 native、debuggable=false、版本 1.0.0")
    args = parser.parse_args()
    release = args.release
    version_code = RELEASE_VERSION_CODE if release else VERSION_CODE
    version_name = RELEASE_VERSION_NAME if release else VERSION_NAME

    OUT.mkdir(parents=True, exist_ok=True)
    gen = OUT / "gen"
    classes = OUT / "classes"
    dex_dir = OUT / "dex"
    for d in (gen, classes, dex_dir):
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True, exist_ok=True)

    ensure_keystore()
    if args.skip_native:
        so = ROOT / "build/libdmandroid.so"
        print("[1/7] 跳过 native")
    else:
        so = build_native(release)

    manifest = APP / "AndroidManifest.xml"
    if release:
        manifest = OUT / "release/AndroidManifest.xml"
        make_release_manifest(manifest, version_code, version_name)
        print(f"[2/7] 正式 manifest: {manifest}")

    aapt2 = BUILD_TOOLS / "aapt2.exe"
    print("[2/7] aapt2 compile / link")
    res_zip = OUT / "res.zip"
    run([aapt2, "compile", "--dir", APP / "res", "-o", res_zip])
    base_apk = OUT / "base.apk"
    link = [aapt2, "link", "-I", PLATFORM, "--manifest", manifest,
            "-o", base_apk, res_zip, "--java", gen,
            "--min-sdk-version", "29", "--target-sdk-version", "34",
            "--version-code", version_code, "--version-name", version_name]
    # 有图标就带上；现在没有，缺文件不报错
    if (APP / "res/mipmap-anydpi-v26").exists():
        link.append("--auto-add-overlay")
    run(link)

    print("[3/7] javac")
    java_files = sorted(APP.rglob("*.java")) + sorted(gen.rglob("*.java"))
    run([JDK / "bin/javac.exe", "-source", "17", "-target", "17", "-encoding", "UTF-8",
         "-classpath", PLATFORM, "-d", classes] + java_files)

    print("[4/7] d8")
    class_files = sorted(classes.rglob("*.class"))
    run([BUILD_TOOLS / "d8.bat", "--lib", PLATFORM, "--min-api", "29", "--output", dex_dir]
        + class_files)

    print("[5/7] 塞 dex 与 so")
    apk_name = "rhythm-heaven-android-release-unsigned.apk" if release else "rhythm-heaven-android.apk"
    apk = OUT / apk_name
    shutil.copyfile(base_apk, apk)
    with zipfile.ZipFile(apk, "a", zipfile.ZIP_STORED) as z:
        have = set(z.namelist())
        entries = [("classes.dex", dex_dir / "classes.dex"),
                   ("lib/arm64-v8a/libdmandroid.so", so)]
        for arc, path in entries:
            if arc in have:
                continue
            if not path.exists():
                sys.exit(f"缺 {path}")
            z.write(path, arc)

    print("[6/7] zipalign")
    aligned = OUT / "aligned.apk"
    run([BUILD_TOOLS / "zipalign.exe", "-f", "-p", "4", apk, aligned])

    print("[7/7] apksigner")
    signed = OUT / ("rhythm-heaven-android-release.apk" if release else "rhythm-heaven-android-signed.apk")
    run([BUILD_TOOLS / "apksigner.bat", "sign", "--ks", KEYSTORE,
         "--ks-pass", f"pass:{KS_PASS}", "--key-pass", f"pass:{KS_PASS}",
         "--out", signed, aligned])
    run([BUILD_TOOLS / "apksigner.bat", "verify", signed])

    size = signed.stat().st_size / 2 ** 20
    print(f"\n完成：{signed}  {size:.2f} MB  ({version_name} / {version_code})")
    if release:
        print("正式包特征：debuggable=false、MicRecActivity exported=false、native -O2 + strip。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
