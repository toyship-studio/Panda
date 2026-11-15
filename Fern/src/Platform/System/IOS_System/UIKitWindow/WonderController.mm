#import "WonderController.h"
#import "WonderView.h"

@implementation WonderController {
    WonderView* customView;
}

- (void)loadView {
    customView = [[WonderView alloc] init];
    self.view = customView;
}

@end
