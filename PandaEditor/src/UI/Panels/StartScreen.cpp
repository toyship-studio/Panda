#include "UI/Panels/StartScreen.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <cstddef>
#include <functional>
#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {

    constexpr float SidebarWidth = 236.f;

    class ProjectRow final : public Control {
    public:
        explicit ProjectRow(std::function<void()> onOpen)
            : m_onOpen(std::move(onOpen)) {
            setCursor(Cursor::PointingHand);
            setBackgroundColor(Color(0x00000000));
            surface().setCornerRadius(10.f);
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setGap(12.f);
            layout().setPadding(Edge::Horizontal, 10.f);
            layout().setPadding(Edge::Vertical, 9.f);
            layout().setWidth(Length::percent(100.f));
        }

    private:
        void performPrimaryAction() override {
            if (m_onOpen) { m_onOpen(); }
        }

        void controlStateChanged(ControlState, ControlState) override {
            const Theme &t = theme();
            if (hasControlState(ControlState::Highlighted)) {
                setBackgroundColor(t.controlPressed);
            } else if (hasControlState(ControlState::Hovered)) {
                setBackgroundColor(t.rowHover);
            } else {
                setBackgroundColor(Color(0x00000000));
            }
        }

        void themeChanged() override {
            controlStateChanged(ControlState::Normal, controlState());
        }

        std::function<void()> m_onOpen;
    };

} // namespace

/* ------------------------------- View -------------------------------- */

StartScreenView::StartScreenView(TextureHandle logoTexture, StartScreenOutput *output)
    : m_logoTexture(logoTexture)
    , m_output(output) {
    addSubviews();
    setupLayout();
    setupStyle();
    rebuildContent();
}

void StartScreenView::setModel(const StartScreenModel &model) {
    const bool contentChanged = model.mode != m_model.mode || model.recentProjects != m_model.recentProjects;
    const bool locationChanged = model.newProjectLocation != m_model.newProjectLocation;
    m_model = model;
    if (m_statusLabel) { m_statusLabel->setText(m_model.statusText); }
    if (m_update) { m_update->setModel(m_model.update); }
    if (contentChanged) {
        rebuildContent();
        return;
    }
    // Targeted form update: choosing a folder in the picker does not rebuild the content —
    // a rebuild would erase the project name the user has already typed.
    if (locationChanged && m_locationField) { m_locationField->setText(m_model.newProjectLocation); }
}

void StartScreenView::addSubviews() {
    m_sidebar = std::make_shared<Panel>();
    m_sidebar->layout().setWidth(Length::points(SidebarWidth));
    m_sidebar->layout().setFlexShrink(0.f);
    m_sidebar->layout().setFlexDirection(FlexDirection::Column);
    m_sidebar->layout().setPadding(20.f);
    m_sidebar->layout().setGap(12.f);
    addSubview(m_sidebar);

    auto identity = std::make_shared<Panel>();
    identity->setBackgroundColor(Color(0x00000000));
    identity->layout().setFlexDirection(FlexDirection::Column);
    identity->layout().setAlignItems(Align::Center);
    identity->layout().setGap(10.f);
    identity->layout().setMargin(Edge::Bottom, 14.f);

    if (m_logoTexture) {
        auto logo = std::make_shared<ImageView>(m_logoTexture);
        logo->layout().setSize({128.f, 128.f});
        logo->setContentMode(ImageContentMode::Fit);
        identity->addSubview(logo);
    } else {
        auto mark = std::make_shared<Panel>();
        mark->layout().setSize({128.f, 128.f});
        mark->surface().setCornerRadius(30.f);
        mark->surface().setBorderWidth(1.f);
        identity->addSubview(mark);
    }

    identity->addSubview(makeLabel("Panda v 0.1", 17.f, FontWeight::Bold));
    identity->addSubview(makeLabel("Create, open, continue", 13.f, FontWeight::Medium, TextRole::Secondary));
    m_sidebar->addSubview(identity);

    auto newProject = makeSidebarButton("New Project", ButtonStyle::Accent);
    newProject->setOnClick([this](Button &) {
        if (m_output) { m_output->startScreenDidRequestNewProject(); }
    });
    m_sidebar->addSubview(newProject);

    auto openProject = makeSidebarButton("Open Project", ButtonStyle::Neutral);
    openProject->setOnClick([this](Button &) {
        if (m_output) { m_output->startScreenDidRequestOpenProjectDialog(); }
    });
    m_sidebar->addSubview(openProject);

    auto spacer = std::make_shared<Panel>();
    spacer->setBackgroundColor(Color(0x00000000));
    spacer->layout().setFlexGrow(1.f);
    m_sidebar->addSubview(spacer);

    // The editor updater: a line above the status, hidden until there is something to say.
    m_update = std::make_shared<UpdateIndicatorView>(this);
    m_update->setModel(m_model.update);
    m_sidebar->addSubview(m_update);

    m_statusLabel = makeLabel(m_model.statusText, 13.f, FontWeight::Medium, TextRole::Muted);
    m_statusLabel->setNumberOfLines(0);
    m_statusLabel->layout().setWidth(Length::percent(100.f));
    m_sidebar->addSubview(m_statusLabel);

    m_content = std::make_shared<Panel>();
    m_content->layout().setFlexDirection(FlexDirection::Column);
    m_content->layout().setFlexGrow(1.f);
    m_content->layout().setPadding(28.f);
    m_content->layout().setGap(18.f);
    addSubview(m_content);
}

void StartScreenView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Row);
    layout().setFlexGrow(1.f); // fill the window (parent — the Column root), do not shrink to the content
}

void StartScreenView::setupStyle() {
    const Theme &t = theme();
    setBackgroundColor(t.windowBackground);
    if (m_sidebar) {
        m_sidebar->setBackgroundColor(t.surface);
        m_sidebar->surface().setBorderWidth(1.f);
        m_sidebar->surface().setBorderColor(t.border);
    }
    if (m_content) { m_content->setBackgroundColor(t.windowBackground); }
}

void StartScreenView::themeChanged() {
    setupStyle();
    rebuildContent();
}

void StartScreenView::rebuildContent() {
    if (!m_content) { return; }
    m_nameField = nullptr; // the form fields live in m_content — they die together with it
    m_locationField = nullptr;
    m_content->removeAllSubviews();
    if (m_model.mode == StartScreenMode::NewProject) {
        buildNewProjectForm();
    } else {
        buildRecentProjects();
    }
    setNeedsLayout();
}

void StartScreenView::buildRecentProjects() {
    m_content->addSubview(makeLabel("Recent Projects", 26.f, FontWeight::Bold));

    auto hint = makeLabel(
        "Open a recent project, choose a project folder, or create a new project.",
        15.f,
        FontWeight::Regular,
        TextRole::Secondary
    );
    hint->setNumberOfLines(0);
    hint->layout().setWidth(Length::percent(100.f));
    m_content->addSubview(hint);

    auto card = makeSectionCard();
    if (m_model.recentProjects.empty()) {
        auto emptyTitle = makeLabel("No recent projects", 18.f, FontWeight::Bold);
        auto emptyText = makeLabel("Create a new project or open an existing project folder.", 15.f, FontWeight::Regular, TextRole::Secondary);
        emptyText->setNumberOfLines(0);
        card->addSubview(emptyTitle);
        card->addSubview(emptyText);
    } else {
        for (size_t i = 0; i < m_model.recentProjects.size(); ++i) {
            if (i > 0) { card->addSubview(makeDivider()); }
            card->addSubview(makeProjectRow(i));
        }
    }
    m_content->addSubview(card);
}

void StartScreenView::buildNewProjectForm() {
    m_content->addSubview(makeLabel("New Project", 26.f, FontWeight::Bold));

    auto hint = makeLabel("Choose a project name and the parent folder where it should be created.", 15.f, FontWeight::Regular, TextRole::Secondary);
    hint->setNumberOfLines(0);
    m_content->addSubview(hint);

    auto card = makeSectionCard();

    card->addSubview(makeLabel("Project Name", 14.f, FontWeight::Medium, TextRole::Secondary));

    m_nameField = std::make_shared<TextField>();
    m_nameField->setText(m_model.newProjectName.empty() ? "UntitledGame" : m_model.newProjectName);
    m_nameField->layout().setWidth(Length::percent(100.f));
    card->addSubview(m_nameField);

    auto locationLabel = makeLabel("Location", 14.f, FontWeight::Medium, TextRole::Secondary);
    locationLabel->layout().setMargin(Edge::Top, 6.f);
    card->addSubview(locationLabel);

    auto locationRow = std::make_shared<Panel>();
    locationRow->setBackgroundColor(Color(0x00000000));
    locationRow->layout().setFlexDirection(FlexDirection::Row);
    locationRow->layout().setAlignItems(Align::Center);
    locationRow->layout().setGap(10.f);
    locationRow->layout().setWidth(Length::percent(100.f));

    m_locationField = std::make_shared<TextField>();
    m_locationField->setText(m_model.newProjectLocation);
    m_locationField->layout().setFlexGrow(1.f);
    locationRow->addSubview(m_locationField);

    auto chooseLocation = std::make_shared<IconButton>(Ic::Folder, 16.f, 30.f);
    chooseLocation->setIconColor(Color(0xC9A45FFF)); // folder yellow (like folders)
    chooseLocation->setTooltip("Choose folder");
    chooseLocation->setOnClick([this] {
        if (m_output) { m_output->startScreenDidRequestChooseProjectLocation(); }
    });
    locationRow->addSubview(chooseLocation);
    card->addSubview(locationRow);

    auto actions = std::make_shared<Panel>();
    actions->setBackgroundColor(Color(0x00000000));
    actions->layout().setFlexDirection(FlexDirection::Row);
    actions->layout().setJustifyContent(Justify::End);
    actions->layout().setGap(10.f);
    actions->layout().setMargin(Edge::Top, 8.f);

    auto cancel = std::make_shared<Button>("Cancel");
    cancel->setOnClick([this](Button &) {
        if (m_output) { m_output->startScreenDidCancelProjectCreation(); }
    });
    actions->addSubview(cancel);

    auto create = std::make_shared<Button>("Create Project");
    create->setStyle(ButtonStyle::Accent);
    create->setOnClick([this](Button &) {
        if (m_output && m_nameField && m_locationField) {
            m_output->startScreenDidRequestCreateProject(m_nameField->getText(), m_locationField->getText());
        }
    });
    actions->addSubview(create);

    card->addSubview(actions);
    m_content->addSubview(card);
}

std::shared_ptr<Label>
StartScreenView::makeLabel(const std::string &text, float fontSize, FontWeight weight, TextRole role) {
    auto label = std::make_shared<Label>(text);
    label->setFont(Font(fontSize));
    label->setFont(label->getFont().withWeight(weight));
    label->setTextRole(role);
    return label;
}

std::shared_ptr<Panel> StartScreenView::makeSectionCard() {
    auto card = std::make_shared<Panel>();
    card->setBackgroundColor(theme().surface);
    card->surface().setCornerRadius(12.f);
    card->surface().setBorderWidth(1.f);
    card->surface().setBorderColor(theme().border);
    // No shadow: by design only popup layers have shadows; the card is flat, separated by a border.
    card->layout().setFlexDirection(FlexDirection::Column);
    card->layout().setPadding(16.f);
    card->layout().setGap(12.f);
    card->layout().setMaxWidth(Length::points(760.f));
    return card;
}

std::shared_ptr<View> StartScreenView::makeProjectRow(size_t index) {
    const StartScreenRecentProject &project = m_model.recentProjects[index];

    auto row = std::make_shared<ProjectRow>([this, index] {
        if (m_output) { m_output->startScreenDidRequestOpenRecentProject(index); }
    });

    // Project glyph on the left (folder, warm yellow — like folders in the Content Browser).
    row->addSubview(makeIcon(Ic::Folder, 22.f, Color(0xC9A45FFF)));

    auto textColumn = std::make_shared<Panel>();
    textColumn->setUserInteractionEnabled(false);
    textColumn->setBackgroundColor(Color(0x00000000));
    textColumn->layout().setFlexDirection(FlexDirection::Column);
    textColumn->layout().setGap(3.f);
    textColumn->layout().setFlexGrow(1.f);

    auto title = makeLabel(project.name, 17.f, FontWeight::Bold);
    auto path = makeLabel(project.path, 13.f, FontWeight::Regular, TextRole::Muted);
    path->setFont(Font::monospaced(13.f)); // the path is monospaced (design)
    path->setNumberOfLines(1);
    path->setTextOverflowMode(TextOverflowMode::TailEllipsis);
    textColumn->addSubview(title);
    textColumn->addSubview(path);
    row->addSubview(textColumn);

    auto remove = std::make_shared<IconButton>(Ic::Trash, 15.f, 26.f);
    remove->setCornerRadius(6.f);
    remove->setTooltip("Remove from recent");
    remove->setOnClick([this, index] {
        if (m_output) { m_output->startScreenDidRequestRemoveRecentProject(index); }
    });
    row->addSubview(remove);

    return row;
}

std::shared_ptr<Panel> StartScreenView::makeDivider() {
    auto divider = std::make_shared<Panel>();
    divider->setBackgroundColor(theme().borderFaint);
    divider->layout().setHeight(Length::points(1.f));
    divider->layout().setWidth(Length::percent(100.f));
    return divider;
}

std::shared_ptr<Button> StartScreenView::makeSidebarButton(const std::string &text, ButtonStyle style) {
    auto button = std::make_shared<Button>(text);
    button->setStyle(style);
    button->setFont(Font(15.f));
    button->layout().setWidth(Length::percent(100.f));
    return button;
}

/* ---------------------------- Controller ----------------------------- */

StartScreenController::StartScreenController(TextureHandle logoTexture, StartScreenControllerOutput *output)
    : m_output(output)
    , m_view(std::make_shared<StartScreenView>(logoTexture, this)) {
    syncView();
}

void StartScreenController::setRecentProjects(std::vector<StartScreenRecentProject> projects) {
    if (m_model.recentProjects == projects) { return; }
    m_model.recentProjects = std::move(projects);
    syncView();
}

void StartScreenView::updateIndicatorDidActivate() {
    if (m_output) { m_output->startScreenDidActivateUpdate(); }
}

void StartScreenView::updateIndicatorDidDismiss() {
    if (m_output) { m_output->startScreenDidDismissUpdate(); }
}

void StartScreenController::setUpdateStatus(const UpdateIndicatorModel &model) {
    if (m_model.update == model) { return; }
    m_model.update = model;
    syncView();
}

void StartScreenController::startScreenDidActivateUpdate() {
    if (m_output) { m_output->startScreenDidActivateUpdate(); }
}

void StartScreenController::startScreenDidDismissUpdate() {
    if (m_output) { m_output->startScreenDidDismissUpdate(); }
}

void StartScreenController::setStatus(std::string text) {
    m_model.statusText = std::move(text);
    syncView();
}

void StartScreenController::setNewProjectDefaults(std::string name, std::string location) {
    m_model.newProjectName = std::move(name);
    m_model.newProjectLocation = std::move(location);
    syncView();
}

void StartScreenController::setNewProjectLocation(std::string location) {
    m_model.newProjectLocation = std::move(location);
    syncView();
}

void StartScreenController::showRecentProjects() {
    m_model.mode = StartScreenMode::RecentProjects;
    syncView();
}

void StartScreenController::startScreenDidRequestNewProject() {
    m_model.mode = StartScreenMode::NewProject;
    syncView();
}

void StartScreenController::startScreenDidRequestOpenProjectDialog() {
    m_model.mode = StartScreenMode::RecentProjects;
    syncView();
    m_output->startScreenDidRequestOpenProjectDialog();
}

void StartScreenController::startScreenDidRequestOpenRecentProject(size_t index) {
    if (index >= m_model.recentProjects.size()) { return; }
    m_output->startScreenDidRequestOpenRecentProject(index);
}

void StartScreenController::startScreenDidRequestRemoveRecentProject(size_t index) {
    if (index >= m_model.recentProjects.size()) { return; }
    m_output->startScreenDidRequestRemoveRecentProject(index);
}

void StartScreenController::startScreenDidRequestCreateProject(const std::string &name, const std::string &location) {
    m_output->startScreenDidRequestCreateProject(name, location);
}

void StartScreenController::startScreenDidCancelProjectCreation() {
    m_model.mode = StartScreenMode::RecentProjects;
    setStatus("Project creation cancelled");
}

void StartScreenController::startScreenDidRequestChooseProjectLocation() {
    m_output->startScreenDidRequestChooseProjectLocation();
}

void StartScreenController::syncView() {
    m_view->setModel(m_model);
}

} // namespace Editor
