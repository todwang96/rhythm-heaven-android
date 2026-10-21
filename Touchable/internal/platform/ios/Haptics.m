#import <CoreHaptics/CoreHaptics.h>
#import <UIKit/UIKit.h>
#import <AVFAudio/AVFAudio.h>
#import <GameController/GameController.h>
#include "ss_haptics.h"
#include <stdio.h>

/* All engine and player operations run on this serial queue. Guest HID calls
 * only enqueue parameters. They do not wait for the haptic hardware. */
@interface NitenHaptics : NSObject {
    dispatch_queue_t _queue;
    CHHapticEngine *_engine;
    id<CHHapticAdvancedPatternPlayer> _player;
    BOOL _engineRunning, _retryScheduled;
    NSUInteger _starts, _updates, _stops, _errors;
}
@property(nonatomic, readonly) BOOL supported;
- (void)enqueue:(ss_haptic_output)output;
@end

@implementation NitenHaptics
- (instancetype)init {
    if ((self = [super init])) {
        _supported = CHHapticEngine.capabilitiesForHardware.supportsHaptics;
        _queue = dispatch_queue_create("Nitenphone haptics",
            dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0));
        NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
        [center addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:nil usingBlock:^(NSNotification *note) {
            (void)note;
            ss_haptics_suspend_for(SS_HAPTICS_INACTIVE, 1);
        }];
        [center addObserverForName:UIApplicationDidEnterBackgroundNotification object:nil queue:nil usingBlock:^(NSNotification *note) {
            (void)note;
            ss_haptics_suspend_for(SS_HAPTICS_INACTIVE, 1);
        }];
        [center addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:nil usingBlock:^(NSNotification *note) {
            (void)note;
            ss_haptics_suspend_for(SS_HAPTICS_INACTIVE, 0);
        }];
        [center addObserverForName:AVAudioSessionInterruptionNotification object:AVAudioSession.sharedInstance queue:nil usingBlock:^(NSNotification *note) {
            BOOL interrupted = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan;
            ss_haptics_suspend_for(SS_HAPTICS_INTERRUPTED, interrupted);
        }];
    }
    return self;
}
- (void)reportError:(NSError *)error operation:(const char *)operation {
    if (++_errors <= 8 || _errors % 64 == 0)
        fprintf(stderr, "HAPTIC %s failed: %s\n", operation, error.description.UTF8String);
}
- (void)retryCurrentOutput {
    if (_retryScheduled) return;
    _retryScheduled = YES;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), _queue, ^{
        self->_retryScheduled = NO;
        ss_haptic_status status = ss_haptics_status();
        if (self->_supported && status.supported && !status.suspended && status.output.intensity > 0)
            [self apply:status.output];
    });
}
- (void)discardPlayer {
    if (!_player) return;
    NSError *error = nil;
    if (![_player stopAtTime:CHHapticTimeImmediate error:&error]) [self reportError:error operation:"stop"];
    _player = nil;
    if (++_stops <= 16) fprintf(stderr, "HAPTIC stop %lu\n", (unsigned long)_stops);
}
- (BOOL)startEngine {
    if (!_engine) {
        NSError *error = nil;
        _engine = [[CHHapticEngine alloc] initWithAudioSession:AVAudioSession.sharedInstance error:&error];
        if (!_engine) { [self reportError:error operation:"create engine"]; return NO; }
        _engine.playsHapticsOnly = YES;
        _engine.autoShutdownEnabled = YES;
        __weak NitenHaptics *weakSelf = self;
        _engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
            NitenHaptics *owner = weakSelf;
            if (!owner) return;
            dispatch_async(owner->_queue, ^{
                owner->_player = nil;
                owner->_engineRunning = NO;
                fprintf(stderr, "HAPTIC engine stopped reason=%ld\n", (long)reason);
                [owner retryCurrentOutput];
            });
        };
        _engine.resetHandler = ^{
            NitenHaptics *owner = weakSelf;
            if (!owner) return;
            dispatch_async(owner->_queue, ^{
                owner->_player = nil;
                owner->_engineRunning = NO;
                fprintf(stderr, "HAPTIC engine reset\n");
                [owner retryCurrentOutput];
            });
        };
    }
    if (!_engineRunning) {
        NSError *error = nil;
        if (![_engine startAndReturnError:&error]) { [self reportError:error operation:"start engine"]; return NO; }
        _engineRunning = YES;
    }
    return YES;
}
- (NSArray<CHHapticDynamicParameter *> *)parameters:(ss_haptic_output)output {
    return @[
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl value:output.intensity relativeTime:0],
        /* Sharpness control adds to the event's neutral 0.5 sharpness. */
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl value:output.sharpness - 0.5f relativeTime:0]
    ];
}
- (void)apply:(ss_haptic_output)output {
    ss_haptic_status current = ss_haptics_status();
    if (!_supported || !current.supported || current.suspended || output.intensity == 0) { [self discardPlayer]; return; }
    /* Do not play events queued before a focus change or controller reset. */
    if (output.epoch != current.epoch) return;
    if (_retryScheduled && !_player) return;
    if (![self startEngine]) { [self retryCurrentOutput]; return; }
    NSError *error = nil;
    NSArray<CHHapticDynamicParameter *> *parameters = [self parameters:output];
    if (!_player) {
        CHHapticEvent *event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous parameters:@[
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1],
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:0.5]
        ] relativeTime:0 duration:30];
        /* Keep controls outside the looped pattern. Otherwise its initial
         * strength would be reapplied at each 30-second loop boundary. */
        CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
        if (!pattern) { [self reportError:error operation:"create pattern"]; [self retryCurrentOutput]; return; }
        _player = [_engine createAdvancedPlayerWithPattern:pattern error:&error];
        if (!_player) { [self reportError:error operation:"create player"]; [self retryCurrentOutput]; return; }
        _player.loopEnabled = YES;
        /* Start muted, set this pulse's controls, then enable output. This
         * avoids a full-strength leading tap before the first update. */
        _player.isMuted = YES;
        if (![_player startAtTime:CHHapticTimeImmediate error:&error]) {
            [self reportError:error operation:"start player"]; _player = nil; _engineRunning = NO;
            [self retryCurrentOutput]; return;
        }
        if (![_player sendParameters:parameters atTime:CHHapticTimeImmediate error:&error]) {
            [self reportError:error operation:"initial controls"]; [self discardPlayer];
            [self retryCurrentOutput]; return;
        }
        _player.isMuted = NO;
        if (++_starts <= 16) fprintf(stderr, "HAPTIC start %lu intensity=%.4f sharpness=%.4f\n", (unsigned long)_starts, output.intensity, output.sharpness);
    } else if (![_player sendParameters:parameters atTime:CHHapticTimeImmediate error:&error]) {
        [self reportError:error operation:"update player"]; [self discardPlayer]; _engineRunning = NO;
        [self retryCurrentOutput]; return;
    }
    if (++_updates <= 16 || _updates % 1000 == 0)
        fprintf(stderr, "HAPTIC accepted %lu intensity=%.4f sharpness=%.4f epoch=%llu\n", (unsigned long)_updates, output.intensity, output.sharpness, (unsigned long long)output.epoch);
}
- (void)enqueue:(ss_haptic_output)output {
    dispatch_async(_queue, ^{ @autoreleasepool { [self apply:output]; } });
}
@end

/* GameController supplies a Core Haptics engine for the active controller.
 * The same mono Switch mix goes to each supported actuator. */
@interface NitenControllerHaptics : NSObject {
    dispatch_queue_t _queue;
    GCController *_controller;
    CHHapticEngine *_engine;
    id<CHHapticPatternPlayer> _player;
    BOOL _engineRunning, _retryScheduled;
    NSUInteger _starts, _updates, _stops, _errors;
    NSArray<id<NSObject>> *_observers;
}
- (void)enqueue:(ss_haptic_output)output;
@end

static GCController *activeHapticController(void) {
    GCController *current = GCController.current;
    if (current.extendedGamepad && current.haptics) return current;
    for (GCController *controller in GCController.controllers)
        if (controller.extendedGamepad && controller.haptics) return controller;
    return nil;
}

@implementation NitenControllerHaptics
- (instancetype)init {
    if ((self = [super init])) {
        _queue = dispatch_queue_create("Nitenphone controller haptics",
            dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0));
        NSMutableArray<id<NSObject>> *observers = [NSMutableArray array];
        NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
        __weak NitenControllerHaptics *weakSelf = self;
        for (NSString *name in @[GCControllerDidConnectNotification, GCControllerDidDisconnectNotification,
                                 GCControllerDidBecomeCurrentNotification, GCControllerDidStopBeingCurrentNotification]) {
            id<NSObject> token = [center addObserverForName:name object:nil queue:nil usingBlock:^(NSNotification *note) {
                (void)note;
                NitenControllerHaptics *owner = weakSelf;
                if (!owner) return;
                dispatch_async(owner->_queue, ^{
                    [owner refreshController];
                    [owner replayCurrentOutput];
                });
            }];
            if (token) [observers addObject:token];
        }
        _observers = observers;
        dispatch_async(_queue, ^{
            [self refreshController];
            [self replayCurrentOutput];
        });
    }
    return self;
}
- (void)reportError:(NSError *)error operation:(const char *)operation {
    if (++_errors <= 8 || _errors % 64 == 0)
        fprintf(stderr, "HAPTIC controller %s failed: %s\n", operation, error.description.UTF8String);
}
- (void)refreshController {
    GCController *controller = activeHapticController();
    if (controller == _controller) return;
    [self discardPlayer];
    if (_engine) {
        if (_engineRunning) [_engine stopWithCompletionHandler:nil];
        _engine = nil;
    }
    _engineRunning = NO;
    _controller = controller;
    if (controller) {
        NSString *name = controller.vendorName ?: controller.productCategory ?: @"controller";
        fprintf(stderr, "HAPTIC controller available: %s localities=%s\n", name.UTF8String,
                controller.haptics.supportedLocalities.description.UTF8String);
    } else {
        fprintf(stderr, "HAPTIC controller unavailable\n");
    }
}
- (void)replayCurrentOutput {
    ss_haptic_status current = ss_haptics_status();
    if (_controller && !current.suspended && current.output.intensity > 0)
        [self apply:current.output];
}
- (void)retryCurrentOutput {
    if (_retryScheduled) return;
    _retryScheduled = YES;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), _queue, ^{
        self->_retryScheduled = NO;
        [self refreshController];
        [self replayCurrentOutput];
    });
}
- (void)discardPlayer {
    if (!_player) return;
    NSError *error = nil;
    if (![_player stopAtTime:CHHapticTimeImmediate error:&error]) [self reportError:error operation:"stop"];
    _player = nil;
    if (++_stops <= 16) fprintf(stderr, "HAPTIC controller stop %lu\n", (unsigned long)_stops);
}
- (BOOL)startEngine {
    if (!_controller || !_controller.haptics) return NO;
    if (!_engine) {
        _engine = [_controller.haptics createEngineWithLocality:GCHapticsLocalityDefault];
        if (!_engine) {
            if (++_errors <= 8 || _errors % 64 == 0)
                fprintf(stderr, "HAPTIC controller engine unavailable\n");
            return NO;
        }
        _engine.autoShutdownEnabled = YES;
        __weak NitenControllerHaptics *weakSelf = self;
        __weak CHHapticEngine *weakEngine = _engine;
        _engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
            NitenControllerHaptics *owner = weakSelf;
            CHHapticEngine *engine = weakEngine;
            if (!owner || !engine) return;
            dispatch_async(owner->_queue, ^{
                if (owner->_engine != engine) return;
                owner->_player = nil;
                owner->_engineRunning = NO;
                fprintf(stderr, "HAPTIC controller engine stopped reason=%ld\n", (long)reason);
                [owner retryCurrentOutput];
            });
        };
        _engine.resetHandler = ^{
            NitenControllerHaptics *owner = weakSelf;
            CHHapticEngine *engine = weakEngine;
            if (!owner || !engine) return;
            dispatch_async(owner->_queue, ^{
                if (owner->_engine != engine) return;
                owner->_player = nil;
                owner->_engineRunning = NO;
                fprintf(stderr, "HAPTIC controller engine reset\n");
                [owner retryCurrentOutput];
            });
        };
    }
    if (!_engineRunning) {
        NSError *error = nil;
        if (![_engine startAndReturnError:&error]) { [self reportError:error operation:"start engine"]; return NO; }
        _engineRunning = YES;
    }
    return YES;
}
- (NSArray<CHHapticDynamicParameter *> *)parameters:(ss_haptic_output)output {
    return @[
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl value:output.intensity relativeTime:0],
        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl value:output.sharpness - 0.5f relativeTime:0]
    ];
}
- (void)apply:(ss_haptic_output)output {
    [self refreshController];
    ss_haptic_status current = ss_haptics_status();
    if (!_controller || current.suspended || output.intensity == 0) { [self discardPlayer]; return; }
    if (output.epoch != current.epoch) return;
    if (_retryScheduled && !_player) return;
    if (![self startEngine]) { [self retryCurrentOutput]; return; }
    NSError *error = nil;
    NSArray<CHHapticDynamicParameter *> *parameters = [self parameters:output];
    if (!_player) {
        CHHapticEvent *event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous parameters:@[
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:output.intensity],
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:output.sharpness]
        ] relativeTime:0 duration:3600];
        /* Controller engines can reject advanced players on iPadOS. A long
         * standard player accepts the same real-time intensity controls. */
        CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
        if (!pattern) { [self reportError:error operation:"create pattern"]; [self retryCurrentOutput]; return; }
        _player = [_engine createPlayerWithPattern:pattern error:&error];
        if (!_player) {
            [self reportError:error operation:"create player"];
            [self retryCurrentOutput];
            return;
        }
        if (![_player startAtTime:CHHapticTimeImmediate error:&error]) {
            [self reportError:error operation:"start player"]; _player = nil; _engineRunning = NO;
            [self retryCurrentOutput]; return;
        }
        if (![_player sendParameters:parameters atTime:CHHapticTimeImmediate error:&error]) {
            [self reportError:error operation:"initial controls"]; [self discardPlayer];
            [self retryCurrentOutput]; return;
        }
        if (++_starts <= 16) fprintf(stderr, "HAPTIC controller start %lu intensity=%.4f sharpness=%.4f\n",
            (unsigned long)_starts, output.intensity, output.sharpness);
    } else if (![_player sendParameters:parameters atTime:CHHapticTimeImmediate error:&error]) {
        [self reportError:error operation:"update player"]; [self discardPlayer]; _engineRunning = NO;
        [self retryCurrentOutput]; return;
    }
    if (++_updates <= 16 || _updates % 1000 == 0)
        fprintf(stderr, "HAPTIC controller accepted %lu intensity=%.4f sharpness=%.4f epoch=%llu\n",
            (unsigned long)_updates, output.intensity, output.sharpness, (unsigned long long)output.epoch);
}
- (void)enqueue:(ss_haptic_output)output {
    dispatch_async(_queue, ^{ @autoreleasepool { [self apply:output]; } });
}
@end

static int controllerHapticsEnabled;

static NitenHaptics *sharedHaptics(void) {
    static NitenHaptics *instance;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ instance = [NitenHaptics new]; });
    return instance;
}
static NitenControllerHaptics *sharedControllerHaptics(void) {
    if (!controllerHapticsEnabled) return nil;
    static NitenControllerHaptics *instance;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ instance = [NitenControllerHaptics new]; });
    return instance;
}
void ss_haptics_platform_apply(ss_haptic_output output) {
    [sharedHaptics() enqueue:output];
    [sharedControllerHaptics() enqueue:output];
}
void ss_haptics_ios_init(void) {
    NitenHaptics *haptics = sharedHaptics();
    controllerHapticsEnabled = [[NSBundle.mainBundle objectForInfoDictionaryKey:@"NitenControllerHaptics"] boolValue];
    ss_haptics_configure(haptics.supported || controllerHapticsEnabled);
    ss_haptics_suspend_for(SS_HAPTICS_INACTIVE, UIApplication.sharedApplication.applicationState != UIApplicationStateActive);
    if (controllerHapticsEnabled) (void)sharedControllerHaptics();
    fprintf(stderr, "HAPTIC Core Haptics hardware=%d controller=%d\n", haptics.supported, controllerHapticsEnabled);
}
