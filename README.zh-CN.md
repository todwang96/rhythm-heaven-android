# 节奏天国 Android

把《节奏天国》（リズム天国 / Rhythm Heaven）**原版 Switch ARM64 模块**直接跑在 Android 上：
游戏自己的代码在手机上原生执行，NVN 图形层重实现为 **Vulkan**，声音走 **AAudio**，
触屏操作和液态玻璃启动器按手机/平板重做。

本仓库只包含 **宿主/移植代码和工具，不包含任何游戏内容**，和
[d2r-android](https://github.com/sydeEvans/d2r-android) 一样属于 "自带游戏文件" 项目：
你需要自己合法拥有的游戏本体（Switch v0，Title ID `0100D9F01D474000`）和自己的 `prod.keys` 来解包。

> 与任天堂无关；《节奏天国》是任天堂的商标。请仅使用你合法拥有的游戏文件。

## 当前状态

- Android 15/16 + 较新 Adreno GPU 上可玩（在 vivo 手机、小米平板 7 Pro 上实测；
  菜单、关卡介绍和实际游玩场景 60fps）。
- 触屏：左侧扇形十字键、右侧 A/B/X/Y 与 `+`/`-`，带按压反馈，非游玩界面自动隐藏方向键。
- 启动器：全屏背景图（仓库里是占位图，可换成自己的图）、右侧液态玻璃面板、
  数据包导入、画面比例 / 震动强度 / 画面延迟（声画同步）/ 节拍校准。
- 数据：RomFS 用**单文件 `romfs.img`** 挂载（不是 7000 个小文件）；模块和 shader 在本地生成，不再分发。
- 画面延迟预设 + 跟着节拍点击的校准（蓝牙耳机用）。
- 暂未完成：游戏文件准备流程的全自动化（目前按文档手动跑）、逐关卡 QA、非 Windows 构建脚本。

## 环境要求

| 组件 | 版本 |
|---|---|
| 系统 | Windows 10/11（脚本是 PowerShell；欢迎补 macOS/Linux） |
| Python | 3.10+ |
| Android SDK | platform 35/36 + build-tools 35/36 |
| Android NDK | r28（`ANDROID_NDK_HOME`） |
| JDK | 17（`JAVA_HOME`） |
| 解包 | [hactool](https://github.com/SciresM/hactool) 或 [nstool](https://github.com/jakcron/nstool) |
| Shader 翻译 | .NET 10 SDK（d2r-mobile 里的 `dmsh_xlate`） |
| 游戏文件 | 节奏天国 v0 的 XCI/NSP + `prod.keys` |

## 快速开始

```powershell
git clone https://github.com/todwang96/rhythm-heaven-android
cd rhythm-heaven-android

# 1) 拉取固定版本的 d2r-mobile 依赖并打补丁
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1

# 2) 把自己的游戏文件解到 game-dump\ 下：
#    game-dump\exefs\main、game-dump\exefs\sdk、game-dump\romfs.img
#    （解包和 shader 命令见 docs/PREPARE-GAME.md）

# 3) 生成模块 → 构建 APK → 安装
powershell -ExecutionPolicy Bypass -File scripts\prepare-modules.ps1
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Release
powershell -ExecutionPolicy Bypass -File scripts\install.ps1
```

生成启动器可导入的数据包（romfs.img + modules + shaders）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\pack-data.ps1
```

## 目录

| 路径 | 内容 |
|---|---|
| `android/` | Android 宿主：native 前端（`src/`、`compat/`）、Java UI + 启动器（`app/`）、构建脚本 |
| `Touchable/` | 游戏适配运行时（vendor 自 [Touchable](https://github.com/MZhao-ouo/Touchable)，MIT） |
| `patches/` | `d2r-mobile-android.patch`：Android 平台层 + Vulkan 后端改动 |
| `tools/` | NSO 改写器、shader 缓存工具（见 `NOTICE`） |
| `scripts/` | setup / prepare / build / pack / install 辅助脚本 |
| `docs/` | 构建、游戏文件准备、架构说明 |
| `game-dump/`、`work/`、`.nitenphone/` | **不提交**：你的游戏文件、生成数据、拉取的依赖 |

## 原理（简述）

```
节奏天国 main/sdk（本地改写的 ARM64 Switch 模块）
        │  HLE：nn:: 服务、音频、输入、文件系统
        ▼
d2r-mobile 宿主层（loader、HLE、存档 FS、时间）
        │  NVN 命令流
        ▼
nvn_vk.c  →  Vulkan（Adreno）        AAudio ← ss_audio_*        Android 输入 ← PadView
```

- 用 `tools/patch_nso.py` 把 Switch 模块改写成位置无关镜像，宿主加载后原生执行（不模拟 CPU）。
- RomFS 通过 `romfs_image.c` 挂载：整份原始 RomFS 镜像作为一个只读目录树，文件字节用 `pread` 读。
- `nvn_vk.c` 把 NVN 的绘制/管线状态翻译成 Vulkan（含 present 路径），可选画面延迟做声画同步。
- 启动器和触屏按键是纯 Android View（无 Gradle/Compose），用 `aapt2 + javac + d8` 打包。

## 法律

本仓库不包含、也不得用于分发游戏 ROM、解密后的 NCA/RomFS 内容、`prod.keys` 或改写后的游戏二进制。
你必须拥有游戏本体。无任何担保，见 `LICENSE`。

## 致谢

- [Touchable](https://github.com/MZhao-ouo/Touchable)（MIT）— 游戏适配运行时。
- [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile)（MIT）— HLE 宿主层、Vulkan NVN 后端。
- 以及 shader 翻译用到的 Ryujinx / dmsh_xlate 项目。

详见 `NOTICE`。本项目自身代码为 MIT（见 `LICENSE`）。
