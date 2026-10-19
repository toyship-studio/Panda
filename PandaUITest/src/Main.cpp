#include "UITestLayer.hpp"

#include <Foundation/PlatformDetection.hpp>
#include <Fern/Fern.hpp>
#include <Miren/Miren.hpp>
#include <Panda.hpp>

int main(int argc, const char **argv) {
    Fern::initialize();
    Fern::Rect rect(0, 0, 1280, 820);
    Fern::Window *window = Fern::createWindow("Panda UI Test", rect, Fern::WindowState::WindowStateNormal);
    Miren::initialize(window->getDrawingContext());

    auto *application = new Panda::Application();
    application->setMainWindow(window);
    application->pushLayer(new UITestLayer());
    application->loop();
    delete application;

    Miren::terminate();
    Fern::disposeWindow(window);
    Fern::terminate();
    return 0;
}
