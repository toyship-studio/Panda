#include "SystemColorSampler.hpp"

#ifdef PLATFORM_MACOS

#import <AppKit/AppKit.h>

#include <PandaUI/Elements/Color/ColorPicker.hpp>

namespace Panda {

void installSystemColorSampler() {
    PandaUI::ColorPicker::setSystemColorSampler([](std::function<void(PandaUI::Color)> done) {
        @autoreleasepool {
            NSColorSampler *sampler = [[NSColorSampler alloc] init];
            // The handler arrives on the main thread while events are pumped; nil — cancellation (Esc).
            [sampler showSamplerWithSelectionHandler:^(NSColor *selectedColor) {
                if (selectedColor != nil) {
                    NSColor *rgb = [selectedColor colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
                    if (rgb != nil) {
                        done(PandaUI::Color(
                            static_cast<float>(rgb.redComponent), static_cast<float>(rgb.greenComponent),
                            static_cast<float>(rgb.blueComponent), 1.f
                        ));
                    }
                }
                [sampler release];
            }];
        }
    });
}

} // namespace Panda

#endif
