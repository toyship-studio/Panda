#pragma once

#include "PandaUI/Core/Appearance.hpp"

#include "PandaUI/Elements/Button.hpp"
#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Elements/Modal.hpp"
#include "PandaUI/Elements/Panel.hpp"

#include <functional>
#include <memory>
#include <string>

namespace PandaUI {

enum class DialogActionRole {
    Cancel,      // neutral; also triggered by Escape
    Default,     // neutral
    Primary,     // accent; also triggered by Enter
    Destructive, // accent for a dangerous action (deletion); also by Enter
};

// Standard modal dialog: a card "title / message / body / buttons".
// The body is any view (a form of PropertyRow, a grid of options, a list). Any footer button
// first calls its own handler, then closes the dialog; form validation is expressed through
// setEnabled() of the primary button (addAction returns it), not by refusing to close.
//
//   auto dialog = std::make_shared<Dialog>("Resize Terrain");
//   dialog->setBody(form);
//   dialog->addAction("Cancel", DialogActionRole::Cancel);
//   dialog->addAction("Apply", DialogActionRole::Primary, [state, onApply] { onApply(*state); });
//   context->modals().present(dialog);
// The look of a dialog: the card (the radius and the shadow are the modal elevation), its width and
// paddings, the fonts and colors of the title and the message.
struct DialogAppearance {
    SurfaceStyle card{ThemeColor::surface, ThemeColor::border};
    float width = 420.f;
    float padding = Spacing::xl;
    float gap = Spacing::lg;
    Font titleFont = Font(18.f, FontWeight::Bold);
    ThemeColor titleColor = ThemeColor::textPrimary;
    Font messageFont = Font(15.f);
    ThemeColor messageColor = ThemeColor::textSecondary;
    float actionGap = 8.f;
    float actionsTopMargin = 4.f;
};

class Dialog : public Modal {
public:
    explicit Dialog(std::string title);

    // The look of the dialogs created from now on (Appearance.hpp).
    static DialogAppearance &defaultAppearance();
    void setAppearance(const DialogAppearance &appearance);
    const DialogAppearance &appearance() const {
        return m_appearance;
    }

    void setMessage(std::string message); // explanation under the title (wraps to the width)
    void setBody(std::shared_ptr<View> body);
    void setContentWidth(float width); // card width; 420 by default

    std::shared_ptr<Button>
    addAction(std::string title, DialogActionRole role, std::function<void()> handler = nullptr);

private:
    bool keyDown(KeyEvent &event) override;
    void applyAppearance();

    DialogAppearance m_appearance;
    std::shared_ptr<Panel> m_card;
    std::shared_ptr<Label> m_titleLabel;
    std::shared_ptr<Label> m_messageLabel;
    std::shared_ptr<Panel> m_bodyHolder;
    std::shared_ptr<Panel> m_actionRow;
    std::shared_ptr<Button> m_enterButton; // Primary/Destructive — triggered by Enter
    std::function<void()> m_enterAction;
};

} // namespace PandaUI
