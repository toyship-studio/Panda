#pragma once

#include <PandaUI/PandaUI.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Editor {

struct StartScreenRecentProject {
    std::string name;
    std::string path;

    bool operator==(const StartScreenRecentProject &other) const {
        return name == other.name && path == other.path;
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
};

// Intents of the start screen controller going outward, to the application host.
class StartScreenControllerOutput {
public:
    virtual ~StartScreenControllerOutput() = default;

    virtual void startScreenDidRequestOpenProjectDialog() = 0;
    virtual void startScreenDidRequestOpenRecentProject(size_t index) = 0;
    virtual void startScreenDidRequestRemoveRecentProject(size_t index) = 0;
    virtual void startScreenDidRequestCreateProject(const std::string &name, const std::string &location) = 0;
    virtual void startScreenDidRequestChooseProjectLocation() = 0;
};

// VIEW: passive retained UI. Receives StartScreenModel through setModel(), sends out
// only intents through StartScreenOutput.
class StartScreenView final : public PandaUI::Panel {
public:
    StartScreenView(PandaUI::TextureHandle logoTexture, StartScreenOutput *output);

    void setModel(const StartScreenModel &model);

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
    // Creation form fields: live in m_content, updated in a targeted way (location from the
    // picker) — see setModel.
    std::shared_ptr<PandaUI::TextField> m_nameField;
    std::shared_ptr<PandaUI::TextField> m_locationField;
};

// CONTROLLER: owns the start screen model, implements the View's intents. Changes the UI state (mode,
// status) itself; forwards domain actions (open/create/delete a project) to the output (the host).
class StartScreenController final : public StartScreenOutput {
public:
    StartScreenController(PandaUI::TextureHandle logoTexture, StartScreenControllerOutput *output);

    std::shared_ptr<PandaUI::View> rootView() const {
        return m_view;
    }

    void setRecentProjects(std::vector<StartScreenRecentProject> projects);
    void setStatus(std::string text);
    void setNewProjectDefaults(std::string name, std::string location);
    void setNewProjectLocation(std::string location);
    void showRecentProjects();

    void startScreenDidRequestNewProject() override;
    void startScreenDidRequestOpenProjectDialog() override;
    void startScreenDidRequestOpenRecentProject(size_t index) override;
    void startScreenDidRequestRemoveRecentProject(size_t index) override;
    void startScreenDidRequestCreateProject(const std::string &name, const std::string &location) override;
    void startScreenDidCancelProjectCreation() override;
    void startScreenDidRequestChooseProjectLocation() override;

private:
    void syncView();

    StartScreenControllerOutput *m_output = nullptr;
    StartScreenModel m_model;
    std::shared_ptr<StartScreenView> m_view;
};

} // namespace Editor
