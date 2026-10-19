//
//  WonderView.swift
//  Wonderland
//
//  Created by Admin on 02.08.2021.
//

#import "WonderView.h"
#import "Fern/Events/EventQueue.hpp"
#import "Fern/Fern.hpp"
#import "UIKitWindow.h"

#include <Foundation/Profile.hpp>

#import <OpenGLES/ES3/gl.h>
#ifdef PANDA_GRAPHICS_METAL
#import <QuartzCore/CAMetalLayer.h>
#endif

using namespace Fern;

namespace Fern {
EventQueue* getEventQueue();
}

@implementation WonderView {
    CADisplayLink* displayLink;
    CGSize lastPostedSize;
    CFTimeInterval lastFrameTimestamp;
    BOOL hasPostedSize;
    BOOL hasFrameTimestamp;
}

// MARK: - Init

- (instancetype)init {
    self = [super initWithFrame:UIScreen.mainScreen.bounds];
    [self commonInit];
    return self;
}

- (instancetype)deinit {
    NSLog(@"UIView deinit");
}

- (void)commonInit {
    hasPostedSize = NO;
    hasFrameTimestamp = NO;
    lastPostedSize = CGSizeMake(0, 0);
    lastFrameTimestamp = 0;
    [self setupPlatform];
    [self initDisplayLink];
}

- (void) setupPlatform {
    // self.backgroundColor = UIColor.redColor;
    [self setMultipleTouchEnabled:true];
    CGFloat scale = UIScreen.mainScreen.nativeScale;
    [self setContentScaleFactor:scale];
    CGFloat width = self.frame.size.width * scale;
    CGFloat height = self.frame.size.height * scale;
    NSLog(@"UIView initialized, width: %f, height: %f\n", width, height);
}

// MARK: - Backing layer (graphics backend)

+ (Class) layerClass {
#ifdef PANDA_GRAPHICS_METAL
    return [CAMetalLayer class];
#else
    return [CAEAGLLayer class];
#endif
}

// MARK: - Action handlers

- (void) initDisplayLink {
    displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(displayRefreshed:)];
    NSInteger maximumFramesPerSecond = UIScreen.mainScreen.maximumFramesPerSecond;
    if (@available(iOS 15.0, *)) {
        displayLink.preferredFrameRateRange =
            CAFrameRateRangeMake(60, maximumFramesPerSecond, maximumFramesPerSecond);
    } else {
        displayLink.preferredFramesPerSecond = maximumFramesPerSecond;
    }
    [displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

-(void) displayRefreshed:(CADisplayLink*)displayLink {
    PANDA_PROFILE_SCOPE_N("CADisplayLink frame");
    CFTimeInterval deltaTime = displayLink.duration;
    if (hasFrameTimestamp) {
        deltaTime = displayLink.timestamp - lastFrameTimestamp;
    }
    if (deltaTime <= 0) {
        deltaTime = displayLink.duration;
    }
    lastFrameTimestamp = displayLink.timestamp;
    hasFrameTimestamp = YES;
    Fern::dispatchFrame(deltaTime);
}

- (void)refreshMetricsAndPostResize:(BOOL)force {
    UIKitWindow *window = FernIOSRefreshMainWindowMetrics();
    if (!window) { return; }

    CGSize size = self.bounds.size;
    if (size.width <= 0 || size.height <= 0) { return; }
    if (!force && hasPostedSize && CGSizeEqualToSize(size, lastPostedSize)) { return; }

    hasPostedSize = YES;
    lastPostedSize = size;
    getEventQueue()->postSizeEvent(static_cast<uint32_t>(size.width), static_cast<uint32_t>(size.height), window);
}

- (void)layoutSubviews {
    [super layoutSubviews];
    [self refreshMetricsAndPostResize:NO];
}

- (void)safeAreaInsetsDidChange {
    [super safeAreaInsetsDidChange];
    [self refreshMetricsAndPostResize:YES];
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        CGPoint touchLocation = [touch locationInView:self];
        // touchLocation.x *= self.contentScaleFactor;
        // touchLocation.y *= self.contentScaleFactor;
        // printf("TOUCH BEGAN: %d\n", touch.hash);
        getEventQueue()->postTouchBeganEvent(int(touch.hash), touchLocation.x, touchLocation.y, nullptr);
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        // printf("TOUCH ENDED: %d\n", touch.hash);
        getEventQueue()->postTouchEndedEvent(int(touch.hash), nullptr);
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        CGPoint touchLocation = [touch locationInView:self];
        // touchLocation.x *= self.contentScaleFactor;
        // touchLocation.y *= self.contentScaleFactor;
        // printf("TOUCH MOVED: %d\n", touch.hash);
        getEventQueue()->postTouchMovedEvent(int(touch.hash), touchLocation.x, touchLocation.y, nullptr);
    }
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (UITouch *touch in touches) {
        // printf("TOUCH CANCELLED: %d\n", touch.hash);
        getEventQueue()->postTouchEndedEvent(int(touch.hash), nullptr);
    }
}

@end
