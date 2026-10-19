#include "SystemTools.hpp"

#include <Foundation/PlatformDetection.hpp>

#ifdef PLATFORM_MACOS

#import <Cocoa/Cocoa.h>

namespace Panda {

SystemTools::SaveChangesChoice SystemTools::showSaveChangesDialog(const char *title, const char *message) {
    @autoreleasepool {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithUTF8String:title];
        alert.informativeText = [NSString stringWithUTF8String:message];
        [alert addButtonWithTitle:@"Save"];       // rightmost / default → NSAlertFirstButtonReturn
        [alert addButtonWithTitle:@"Cancel"];      // middle             → NSAlertSecondButtonReturn
        [alert addButtonWithTitle:@"Don't Save"];  // leftmost           → NSAlertThirdButtonReturn
        NSModalResponse response = [alert runModal];
        if (response == NSAlertFirstButtonReturn) { return SaveChangesChoice::Save; }
        if (response == NSAlertThirdButtonReturn) { return SaveChangesChoice::DontSave; }
        return SaveChangesChoice::Cancel;
    }
}

} // namespace Panda

#endif
