#include "UI/Panels/UpdateIndicator.hpp"

#include "App/EditorUpdater.hpp"

namespace Editor {

using namespace PandaUI;

UpdateIndicatorModel updateIndicatorModel(const Panda::EditorUpdater &updater) {
    using Kind = Panda::UpdateState::Kind;
    using Shown = UpdateIndicatorKind;
    UpdateIndicatorModel model;
    const Panda::UpdateState &state = updater.state();
    if (updater.isDismissed()) { return model; }
    switch (state.kind) {
        case Kind::Idle: break;
        case Kind::Checking:
            model.kind = Shown::Checking;
            model.text = "Checking for updates…";
            break;
        case Kind::Downloading:
            model.kind = Shown::Downloading;
            model.progress = state.progress;
            model.text = state.progress >= 0.f ? "Downloading update " + std::to_string(int(state.progress * 100.f)) + "%"
                                               : "Downloading update…";
            break;
        case Kind::Installing:
            model.kind = Shown::Installing;
            model.text = "Installing update…";
            break;
        case Kind::Ready:
            model.kind = Shown::Ready;
            model.text = "Update ready · Restart";
            model.clickable = true;
            break;
        case Kind::UpToDate:
            model.kind = Shown::UpToDate;
            model.text = "Panda is up to date";
            break;
        case Kind::Updated:
            model.kind = Shown::Updated;
            model.text = "Updated to " + state.version + " · Release notes";
            model.clickable = true;
            break;
        case Kind::Error:
            model.kind = Shown::Error;
            model.text = "Update failed · Details";
            model.clickable = true;
            break;
    }
    return model;
}

UpdateIndicatorView::UpdateIndicatorView(UpdateIndicatorOutput *output)
    : m_output(output) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setGap(2.f);

    auto column = std::make_shared<Panel>();
    column->setBackgroundColor(Color(0x00000000));
    column->layout().setFlexDirection(FlexDirection::Column);
    column->layout().setAlignItems(Align::Stretch);
    column->layout().setGap(2.f);

    m_text = std::make_shared<Button>("");
    m_text->setSize(ButtonSize::Small);
    m_text->setStyle(ButtonStyle::Link);
    m_text->setOnClick([this](Button &) {
        if (m_output && m_model.clickable) { m_output->updateIndicatorDidActivate(); }
    });
    column->addSubview(m_text);

    m_progress = std::make_shared<ProgressBar>();
    m_progress->layout().setHeight(Length::points(3.f));
    m_progress->layout().setMargin(Edge::Horizontal, 8.f);
    m_progress->setHidden(true);
    column->addSubview(m_progress);
    addSubview(column);

    m_dismiss = std::make_shared<Button>("×");
    m_dismiss->setSize(ButtonSize::Small);
    m_dismiss->setStyle(ButtonStyle::Link);
    m_dismiss->setOnClick([this](Button &) {
        if (m_output) { m_output->updateIndicatorDidDismiss(); }
    });
    addSubview(m_dismiss);

    applyModel();
}

void UpdateIndicatorView::setModel(const UpdateIndicatorModel &model) {
    if (model == m_model) { return; }
    m_model = model;
    applyModel();
}

void UpdateIndicatorView::applyModel() {
    const bool hidden = m_model.kind == UpdateIndicatorKind::Hidden;
    setHidden(hidden);
    if (hidden) { return; }
    m_text->setText(m_model.text);
    m_text->setEnabled(m_model.clickable);
    // The ready update is the one state that asks for a click: it stands out.
    m_text->setStyle(m_model.kind == UpdateIndicatorKind::Ready ? ButtonStyle::AccentOutline : ButtonStyle::Link);
    const bool downloading = m_model.kind == UpdateIndicatorKind::Downloading;
    m_progress->setHidden(!downloading);
    if (downloading) {
        if (m_model.progress >= 0.f) {
            m_progress->setProgress(m_model.progress);
        } else {
            m_progress->setIndeterminate(true);
        }
    }
    // A state in progress cannot be dismissed: it would come back with the next percent anyway.
    m_dismiss->setHidden(m_model.kind == UpdateIndicatorKind::Checking || downloading ||
                         m_model.kind == UpdateIndicatorKind::Installing);
}

} // namespace Editor
