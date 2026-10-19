//
// Created by Admin on 17.02.2022.
//

#pragma once

#import "Fern/Window/Window.hpp"

class UIKitWindow : public Fern::Window {
public:
    UIKitWindow();
    ~UIKitWindow() override;
    void setTitle(const char *title) override {}
    Fern::WindowState getState() override {
        return Fern::WindowState::WindowStateFullScreen;
    }
    void setState(Fern::WindowState state) override {}
    void setModifiedState(bool isUnsavedChanges) override {}
    bool getModifiedState() override {
        return false;
    }
    void setFlags(Fern::WindowFlags flags) override {}
    void setSize(Fern::Size size) override {}
    Fern::Size getSize() override {
        return m_size;
    }
    Fern::Size getDpi() override {
        return m_dpi;
    }
    Fern::EdgeInsets getSafeAreaInsets() override;
    Fern::Vec2 getCursorPosition() override {}
    bool setProgressBar(float progress) override {
        return false;
    }
    bool endProgressBar() override {
        return false;
    }
    void setMenuBar(const Fern::MenuBar &menuBar) override {}
    Fern::GraphicsContext *getDrawingContext() override {
        return m_context;
    }
    void refreshMetricsFromView();

private:
    Fern::Size m_size;
    Fern::Size m_dpi;
    Fern::EdgeInsets m_safeAreaInsets;
    Fern::GraphicsContext *m_context;
};

UIKitWindow *FernIOSRefreshMainWindowMetrics();
