#pragma once

#include <PandaUI/PandaUI.hpp>

#include <memory>
#include <string>

namespace Editor {

// What the update indicator shows (context/infrastructure.md, section 5): the state of the
// editor updater as one line. The same model drives the indicator of the toolbar and of the start
// screen; the application layer builds it from the updater and hides it after a dismiss.
enum class UpdateIndicatorKind {
    Hidden,
    Checking,
    Downloading,
    Installing,
    Ready,    // downloaded and installed: a restart runs the new version
    Updated,  // the first start of a new version: the release notes are one click away
    UpToDate, // a manual check found nothing (goes away by itself)
    Error,
};

struct UpdateIndicatorModel {
    UpdateIndicatorKind kind = UpdateIndicatorKind::Hidden;
    std::string text;
    float progress = -1.f; // Downloading: 0..1, -1 unknown
    bool clickable = false;

    bool operator==(const UpdateIndicatorModel &other) const {
        return kind == other.kind && text == other.text && progress == other.progress && clickable == other.clickable;
    }
    bool operator!=(const UpdateIndicatorModel &other) const {
        return !(*this == other);
    }
};

class UpdateIndicatorOutput {
public:
    virtual ~UpdateIndicatorOutput() = default;
    virtual void updateIndicatorDidActivate() = 0; // the text was clicked: restart, details, release notes
    virtual void updateIndicatorDidDismiss() = 0;  // the × was clicked: hidden until the state changes
};

// VIEW: a pill with the state text (a button where a click does something) and a × to dismiss it.
class UpdateIndicatorView final : public PandaUI::Panel {
public:
    explicit UpdateIndicatorView(UpdateIndicatorOutput *output);

    void setModel(const UpdateIndicatorModel &model);

private:
    void applyModel();

    UpdateIndicatorOutput *m_output;
    UpdateIndicatorModel m_model;
    std::shared_ptr<PandaUI::Button> m_text;
    std::shared_ptr<PandaUI::ProgressBar> m_progress;
    std::shared_ptr<PandaUI::Button> m_dismiss;
};

} // namespace Editor
