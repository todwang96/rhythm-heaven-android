/* TargetConditionals.h for Android — 让原本按 Apple 平台分支的代码落到 Linux 分支上。
 * 只定义代码里真正用到的宏；取值按“不是苹果”设。 */
#pragma once

#ifndef TARGET_OS_MAC

#define TARGET_OS_MAC 0
#define TARGET_OS_OSX 0
#define TARGET_OS_IPHONE 0
#define TARGET_OS_IOS 0
#define TARGET_OS_TV 0
#define TARGET_OS_WATCH 0
#define TARGET_OS_VISION 0
#define TARGET_OS_SIMULATOR 0
#define TARGET_OS_EMBEDDED 0
#define TARGET_OS_LINUX 1
#define TARGET_OS_ANDROID 1
#define TARGET_OS_WIN32 0

#define TARGET_CPU_ARM64 1
#define TARGET_CPU_ARM 0
#define TARGET_CPU_X86_64 0
#define TARGET_CPU_X86 0

#define TARGET_RT_64_BIT 1
#define TARGET_RT_LITTLE_ENDIAN 1

#endif
