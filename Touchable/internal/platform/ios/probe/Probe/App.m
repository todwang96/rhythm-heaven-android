#import <UIKit/UIKit.h>
#include "../../../../tests/common/branch_probe.h"

@interface ProbeScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation ProbeScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UIViewController *controller = [UIViewController new];
    controller.view.backgroundColor = UIColor.systemBackgroundColor;
    self.window.rootViewController = controller;
    UILabel *label = [UILabel new];
    label.numberOfLines = 0;
    label.font = [UIFont monospacedSystemFontOfSize:16 weight:UIFontWeightRegular];
    label.text = @"Nitenphone CPU Test\n\nRunning…";
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [controller.view addSubview:label];
    [NSLayoutConstraint activateConstraints:@[
        [label.leadingAnchor constraintEqualToAnchor:controller.view.safeAreaLayoutGuide.leadingAnchor constant:24],
        [label.trailingAnchor constraintEqualToAnchor:controller.view.safeAreaLayoutGuide.trailingAnchor constant:-24],
        [label.centerYAnchor constraintEqualToAnchor:controller.view.centerYAnchor]
    ]];
    [self.window makeKeyAndVisible];
    UIApplication.sharedApplication.idleTimerDisabled = YES;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        char output[1024];
        int result = ss_run_branch_probe(output, sizeof(output));
        NSString *report = [NSString stringWithUTF8String:output];
        NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        NSDictionary *record = @{@"passed": @(result == 0), @"report": report,
                                 @"system": UIDevice.currentDevice.systemVersion,
                                 @"date": [[NSISO8601DateFormatter new] stringFromDate:NSDate.date]};
        NSData *json = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingPrettyPrinted error:nil];
        [json writeToFile:[documents stringByAppendingPathComponent:@"cpu-probe.json"] atomically:YES];
        puts(output);
        fflush(stdout);
        dispatch_async(dispatch_get_main_queue(), ^{
            label.text = [@"Nitenphone CPU Test\n\n" stringByAppendingString:report];
            label.textColor = result ? UIColor.systemRedColor : UIColor.labelColor;
            UIApplication.sharedApplication.idleTimerDisabled = NO;
        });
    });
}
@end

@interface ProbeDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation ProbeDelegate
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    UISceneConfiguration *configuration = [[UISceneConfiguration alloc] initWithName:@"CPU Test" sessionRole:session.role];
    configuration.delegateClass = ProbeScene.class;
    return configuration;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(ProbeDelegate.class)); }
}
