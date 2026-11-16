#pragma once

#include "UI/Panels/UpdateIndicator.hpp"

#include <PandaUI/PandaUI.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Panda {
class ProjectLoader;
}

namespace Editor {

struct StartScreenRecentProject {
    std::string name;
    std::string path;
    bool missing = false; // the folder is not there any more: the row says so instead of failing on a click

    bool operator==(const StartScreenRecentProject &other) const {
        return name == other.name && path == other.path && missing == other.missing;
    }
};

enum class StartScreenMode {
    RecentProjects,
    NewProject,
};

struct StartScreenModel {
    StartScreenMode mode = StartScreenMode::RecentProjects;
    std::vector<StartScreenRecentProject> recentProjects;
    std::string statusText = "Select a project or create a new one.";
    std::string newProjectName = "UntitledGame";
    std::string newProjectLocation;
    UpdateIndicatorModel update; // the editor updater, shown in the sidebar
};

// Start screen intents. Implemented by the coordinator; the View does not change the model directly.
class StartScreenOutput {
public:
    virtual ~StartScreenOutput() = default;

    virtual void startScreenDidRequestNewProject() = 0;
    virtual void startScreenDidRequestOpenProjectDialog() = 0;
    virtual void startScreenDidRequestOpenRecentProject(size_t index) = 0;
    virtual void startScreenDidRequestRemoveRecentProject(size_t index) = 0;
    virtual void startScreenDidRequestCreateProject(const std::string &name, const std::string &location) = 0;
    virtual void startScreenDidCancelProjectCreation() = 0;
    virtual void startScreenDidRequestChooseProjectLocation() = 0;
    virtual void startScreenDidActivateUpdate() = 0;
    virtual void startScreenDidDismissUpdate() = 0;
};

// VIEW: passive retained UI. Receives StartScreenModel through setModel(), sends out
// only intents through StartScreenOutput.
class StartScreenView final : public PandaUI::Panel, public UpdateIndicatorOutput {
public:
    StartScreenView(PandaUI::TextureHandle logoTexture, StartScreenOutput *output);

    void setModel(const StartScreenModel &model);

    void updateIndicatorDidActivate() override;
    void updateIndicatorDidDismiss() override;

private:
    void addSubviews();
    void setupLayout();
    void setupStyle();
    void themeChanged() override;

    void rebuildContent();
    void buildRecentProjects();
    void buildNewProjectForm();

    std::shared_ptr<PandaUI::Label> makeLabel(
        const std::string &text, float fontSize, PandaUI::FontWeight weight = PandaUI::FontWeight::Regular,
        PandaUI::TextRole role = PandaUI::TextRole::Primary
    );
    std::shared_ptr<PandaUI::Panel> makeSectionCard();
    std::shared_ptr<PandaUI::View> makeProjectRow(size_t index);
    std::shared_ptr<PandaUI::Panel> makeDivider();
    std::shared_ptr<PandaUI::Button> makeSidebarButton(const std::string &text, PandaUI::ButtonStyle style);

    PandaUI::TextureHandle m_logoTexture;
    StartScreenOutput *m_output = nullptr;
    StartScreenModel m_model;
    std::shared_ptr<PandaUI::Panel> m_sidebar;
    std::shared_ptr<PandaUI::Panel> m_content;
    std::shared_ptr<PandaUI::Label> m_statusLabel;
    std::shared_ptr<UpdateIndicatorView> m_update;
    // Creation form fields: live in m_content, updated in a targeted way (location from the
    // picker) — see setModel.
    std::shared_ptr<PandaUI::TextField> m_nameField;
    std::shared_ptr<PandaUI::TextField> m_locationField;
};

// CONTROLLER: owns the start screen model and implements the intents of the View. The start
// screen is where a project is chosen: it opens, creates and forgets projects through the loader
// itself and says what came of it in its status line. The update indicator it shows is the
// application's: a click on it goes to `updates`.
class StartScreenController final : public StartScreenOutput {
public:
    StartScreenController(PandaUI::TextureHandle logoTexture, Panda::ProjectLoader &loader, UpdateIndicatorOutput *updates);

    std::shared_ptr<PandaUI::View> rootView() const {
        return m_view;
    }

    // The screen is about to be shown: the list is the recent projects of the loader as they are
    // now (a folder may be gone since).
    void refreshRecentProjects();
    void setStatus(std::string text);
    void setUpdateStatus(const UpdateIndicatorModel &model);
    void showRecentProjects();

    void startScreenDidRequestNewProject() override;
    void startScreenDidRequestOpenProjectDialog() override;
    void startScreenDidRequestOpenRecentProject(size_t index) override;
    void startScreenDidRequestRemoveRecentProject(size_t index) override;
    void startScreenDidRequestCreateProject(const std::string &name, const std::string &location) override;
    void startScreenDidCancelProjectCreation() override;
    void startScreenDidRequestChooseProjectLocation() override;
    void startScreenDidActivateUpdate() override;
    void startScreenDidDismissUpdate() override;

private:
    void syncView();

    Panda::ProjectLoader &m_loader;
    UpdateIndicatorOutput *m_updates = nullptr;
    StartScreenModel m_model;
    std::shared_ptr<StartScreenView> m_view;
};

} // namespace Editor
