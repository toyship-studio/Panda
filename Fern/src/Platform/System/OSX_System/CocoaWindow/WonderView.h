//
//  GameViewController.h
//
//  Created by Andreichev Mikhail on 07.02.2025.
//

#import <Cocoa/Cocoa.h>

@interface WonderView : NSView <NSTextInputClient> {
    NSMutableAttributedString *markedText;
}

@property(nonatomic, assign) int ignoreMouseEvents;
@property(nonatomic, assign) void *eventWindow;

// Posts the releases of the keys the view has posted as pressed and not yet released: the window
// resigned key and macOS will not deliver them.
- (void)releaseHeldKeys;

@end
