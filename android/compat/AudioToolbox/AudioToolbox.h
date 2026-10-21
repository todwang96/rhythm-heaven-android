/* AudioToolbox/AudioToolbox.h → 编译期替身。
 *
 * 目的只有一个：让 ss_audio.c / ss_audio_renderer.c 编过，并且**主动走它们已有的 silent sink 兜底**。
 * 做法是 AudioOutputUnitStart 返回非 0（-1），调用方于是打印一行日志、改用按采样率走时钟的静音线程——
 * 那条路本来就是给没有 CoreAudio 的场景准备的，时序正确，只是没有声音。
 *
 * 真音频（AAudio）下一步再替换这一层，不改调用方。 */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef int32_t OSStatus;
typedef uint32_t UInt32;
typedef int32_t SInt32;
typedef double Float64;
typedef uint32_t AudioUnitRenderActionFlags;

#define noErr 0
#define kAudioUnitType_Output 0x61756F75u
#define kAudioUnitSubType_DefaultOutput 0x64656620u
#define kAudioUnitSubType_RemoteIO 0x72696F63u
#define kAudioUnitManufacturer_Apple 0x6170706Cu

#define kAudioUnitScope_Global 0
#define kAudioUnitScope_Input 1
#define kAudioUnitScope_Output 2

#define kAudioUnitProperty_StreamFormat 8
#define kAudioUnitProperty_SetRenderCallback 23

#define kAudioFormatLinearPCM 0x6C70636Du
#define kLinearPCMFormatFlagIsFloat 0x1u
#define kLinearPCMFormatFlagIsPacked 0x8u
#define kLinearPCMFormatFlagIsSignedInteger 0x4u

#define kAudioUnitRenderAction_OutputIsSilence 0x10u

typedef void *AudioUnit;
typedef void *AudioComponent;
typedef void *AudioComponentInstance;

typedef struct AudioTimeStamp {
    Float64 mSampleTime;
    uint64_t mHostTime;
    Float64 mRateScalar;
    uint64_t mWordClockTime;
    uint32_t mFlags;
    uint32_t mReserved;
} AudioTimeStamp;

typedef struct AudioBuffer { UInt32 mNumberChannels; UInt32 mDataByteSize; void *mData; } AudioBuffer;
typedef struct AudioBufferList { UInt32 mNumberBuffers; AudioBuffer mBuffers[1]; } AudioBufferList;

typedef struct AudioStreamBasicDescription {
    Float64 mSampleRate;
    UInt32 mFormatID;
    UInt32 mFormatFlags;
    UInt32 mBytesPerPacket;
    UInt32 mFramesPerPacket;
    UInt32 mBytesPerFrame;
    UInt32 mChannelsPerFrame;
    UInt32 mBitsPerChannel;
    UInt32 mReserved;
} AudioStreamBasicDescription;

typedef struct AudioComponentDescription {
    UInt32 componentType;
    UInt32 componentSubType;
    UInt32 componentManufacturer;
    UInt32 componentFlags;
    UInt32 componentFlagsMask;
} AudioComponentDescription;

typedef OSStatus (*AURenderCallback)(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                                     const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                                     UInt32 inNumberFrames, AudioBufferList *ioData);

typedef struct AURenderCallbackStruct { AURenderCallback inputProc; void *inputProcRefCon; } AURenderCallbackStruct;

static inline AudioComponent AudioComponentFindNext(AudioComponent prev, const AudioComponentDescription *desc) {
    (void)prev; (void)desc;
    return (AudioComponent)(uintptr_t)1;   /* 非 NULL，让调用方继续往下走 */
}
static inline OSStatus AudioComponentInstanceNew(AudioComponent component, AudioUnit *out) {
    (void)component;
    if (out) *out = (AudioUnit)(uintptr_t)1;
    return noErr;
}
static inline OSStatus AudioComponentInstanceDispose(AudioUnit unit) { (void)unit; return noErr; }
static inline OSStatus AudioUnitSetProperty(AudioUnit unit, UInt32 id, UInt32 scope, UInt32 element,
                                            const void *data, UInt32 size) {
    (void)unit; (void)id; (void)scope; (void)element; (void)data; (void)size;
    return noErr;
}
static inline OSStatus AudioUnitInitialize(AudioUnit unit) { (void)unit; return noErr; }
static inline OSStatus AudioUnitUninitialize(AudioUnit unit) { (void)unit; return noErr; }

/* 关键：故意失败，让宿主改用 silent sink（时序正确、无声音）。 */
static inline OSStatus AudioOutputUnitStart(AudioUnit unit) { (void)unit; return -1; }
static inline OSStatus AudioOutputUnitStop(AudioUnit unit) { (void)unit; return noErr; }
