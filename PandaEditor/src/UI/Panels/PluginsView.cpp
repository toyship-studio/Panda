#include "UI/Panels/PluginsView.hpp"

#include "Project/Plugins/PluginStore.hpp"
#include "Project/System/SystemTools.hpp"
#include "UI/EditorDialogs.hpp"

#include <Panda/Base/Version.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>
#include <PandaUI/Core/Clipboard.hpp>
#include <PandaUI/Runtime/Context.hpp>

#include <algorithm>
#include <cctype>

namespace Editor {

using namespace PandaUI;

namespace {

    // The categories of a plugin manifest (context/plugins.md), the chips under the search.
    const char *const kCategories[] = {"themes", "world", "rendering", "physics", "assets", "editor", "scripting", "ui", "tools", "localization", "samples"};

    // Nothing on this page is set below 15 px: the cards are read, not scanned.
    constexpr float kChipFont = 15.f;
    constexpr float kMetaFont = 16.f;
    constexpr float kIconSize = 56.f;
    constexpr float kCardRadius = 12.f;

    std::string titleCase(const std::string &word) {
        if (word == "ui") { return "UI"; }
        if (word.empty()) { return word; }
        std::string result = word;
        result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
        return result;
    }

    std::string formatCount(uint64_t value) {
        std::string digits = std::to_string(value);
        std::string result;
        int count = 0;
        for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
            if (count > 0 && count % 3 == 0) { result.insert(result.begin(), ','); }
            result.insert(result.begin(), *it);
            ++count;
        }
        return result;
    }

    std::string dateOf(const std::string &iso) {
        return iso.size() >= 10 ? iso.substr(0, 10) : iso;
    }

    std::string join(const std::vector<std::string> &items, const char *separator) {
        std::string result;
        for (const std::string &item : items) {
            if (!result.empty()) { result += separator; }
            result += item;
        }
        return result;
    }

    std::string lower(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    bool contains(const std::string &haystack, const std::string &needle) {
        return lower(haystack).find(needle) != std::string::npos;
    }

    std::shared_ptr<Label> makeLabel(const std::string &text, Font font, TextRole role, int lines) {
        auto label = std::make_shared<Label>(text);
        label->setFont(font);
        label->setTextRole(role);
        label->setNumberOfLines(lines);
        if (lines == 1) { label->setTextOverflowMode(TextOverflowMode::TailEllipsis); }
        label->layout().setFlexShrink(1.f);
        label->layout().setMinWidth(Length::points(0.f));
        return label;
    }

    std::shared_ptr<Panel> makeRow(float gap) {
        auto row = std::make_shared<Panel>();
        row->layout().setFlexDirection(FlexDirection::Row);
        row->layout().setAlignItems(Align::Center);
        row->layout().setGap(gap);
        row->layout().setFlexShrink(1.f);
        row->layout().setMinWidth(Length::points(0.f));
        return row;
    }

    std::shared_ptr<Panel> makeColumn(float gap) {
        auto column = std::make_shared<Panel>();
        column->layout().setFlexDirection(FlexDirection::Column);
        column->layout().setAlignItems(Align::Stretch);
        column->layout().setGap(gap);
        return column;
    }

    // A static tag: what a plugin provides, its category, a tag, its license.
    std::shared_ptr<Panel> makeTag(const std::string &text, bool accent) {
        auto tag = std::make_shared<Panel>();
        tag->layout().setFlexDirection(FlexDirection::Row);
        tag->layout().setAlignItems(Align::Center);
        tag->layout().setPadding(Edge::Horizontal, 10.f);
        tag->layout().setPadding(Edge::Vertical, 4.f);
        tag->surface().setCornerRadius(7.f);
        tag->setBackgroundColor(accent ? theme().accentSoft : theme().control);
        auto label = std::make_shared<Label>(text);
        label->setFont(Font(kChipFont, FontWeight::Medium));
        label->setTextColor(accent ? theme().accentText : theme().textSecondary);
        tag->addSubview(label);
        return tag;
    }

    std::shared_ptr<Button> makeButton(const std::string &title, ButtonStyle style, std::function<void()> onClick) {
        auto button = std::make_shared<Button>(title);
        button->setStyle(style);
        button->setOnClick([onClick = std::move(onClick)](Button &) {
            if (onClick) { onClick(); }
        });
        return button;
    }

    std::shared_ptr<Button> makeLinkButton(const std::string &title, std::function<void()> onClick) {
        auto button = makeButton(title, ButtonStyle::Link, std::move(onClick));
        button->layout().setPadding(Edge::Horizontal, 0.f);
        button->layout().setPadding(Edge::Vertical, 2.f);
        return button;
    }

    const char *operationTitle(PluginCardModel::Status status) {
        switch (status) {
            case PluginCardModel::Status::Installing: return "Installing…";
            case PluginCardModel::Status::Updating: return "Updating…";
            case PluginCardModel::Status::Removing: return "Removing…";
            default: return "";
        }
    }

} // namespace

/* ------------------------------ PluginCard ------------------------------ */

PluginCard::PluginCard(const PluginCardActions *actions)
    : m_actions(actions) {
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Start);
    layout().setGap(16.f);
    layout().setPadding(16.f);
    surface().setCornerRadius(kCardRadius);
    surface().setBorderWidth(1.f);
    applyStyle();
}

void PluginCard::setModel(const PluginCardModel &model) {
    if (m_built && model == m_model) { return; }
    m_model = model;
    m_built = true;
    m_needsRebuild = true;
}

void PluginCard::update(double) {
    if (m_needsRebuild) {
        m_needsRebuild = false;
        rebuild();
    }
}

void PluginCard::themeChanged() {
    applyStyle();
    if (m_built) { m_needsRebuild = true; } // the custom colors (tags, the error line) follow the theme
}

void PluginCard::applyStyle() {
    setBackgroundColor(theme().surface);
    surface().setBorderColor(theme().borderMed);
}

void PluginCard::rebuild() {
    using Status = PluginCardModel::Status;
    removeAllSubviews();
    const PluginCardModel &m = m_model;
    const std::string id = m.id;

    // The icon: the image of the plugin, or its initial on a tile.
    auto iconBox = std::make_shared<Panel>();
    iconBox->layout().setSize(Size(kIconSize, kIconSize));
    iconBox->layout().setFlexShrink(0.f);
    iconBox->layout().setJustifyContent(Justify::Center);
    iconBox->layout().setAlignItems(Align::Center);
    iconBox->surface().setCornerRadius(10.f);
    iconBox->setBackgroundColor(theme().control);
    iconBox->setClipsToBounds(true);
    if (m.icon) {
        auto image = std::make_shared<ImageView>(m.icon);
        image->setContentMode(ImageContentMode::Fit);
        image->layout().setSize(Size(kIconSize, kIconSize));
        iconBox->addSubview(image);
    } else {
        std::string initial = m.name.empty() ? "?" : m.name.substr(0, 1);
        initial[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(initial[0])));
        auto letter = std::make_shared<Label>(initial);
        letter->setFont(Font(26.f, FontWeight::Bold));
        letter->setTextRole(TextRole::Muted);
        iconBox->addSubview(letter);
    }
    addSubview(iconBox);

    // The body: name and versions, who and how many, the description, the tags, the themes, the links.
    auto body = makeColumn(8.f);
    body->layout().setFlexGrow(1.f);
    body->layout().setFlexShrink(1.f);
    body->layout().setMinWidth(Length::points(0.f));

    auto header = makeRow(10.f);
    header->layout().setFlexWrap(Wrap::Wrap);
    header->addSubview(makeLabel(m.name.empty() ? m.id : m.name, Typo::DialogTitle, TextRole::Primary, 1));
    const bool updateAvailable = m.status == Status::UpdateAvailable || m.status == Status::Updating;
    if (!m.version.empty()) {
        auto version = makeLabel("v" + m.version, Font::monospaced(15.f), TextRole::Muted, 1);
        header->addSubview(version);
    }
    if (!m.installedVersion.empty() && (updateAvailable || m.installedVersion != m.version)) {
        auto installed = makeLabel("installed v" + m.installedVersion, Font::monospaced(15.f), TextRole::Dim, 1);
        header->addSubview(installed);
    }
    if (m.status == Status::BuiltIn) { header->addSubview(makeTag("Built in", false)); }
    if (m.status == Status::Dev) { header->addSubview(makeTag("Dev", true)); }
    if (m.status == Status::OverriddenByDev) { header->addSubview(makeTag("Overridden by a dev plugin", false)); }
    if (updateAvailable) { header->addSubview(makeTag("Update available", true)); }
    body->addSubview(header);

    std::vector<std::string> meta;
    if (!m.authors.empty()) { meta.push_back("by " + m.authors); }
    if (m.fromCatalog) {
        meta.push_back(formatCount(m.downloads) + (m.downloads == 1 ? " download" : " downloads"));
        if (!m.updatedAt.empty()) { meta.push_back("updated " + m.updatedAt); }
    }
    if (!meta.empty()) { body->addSubview(makeLabel(join(meta, "  ·  "), Font(kMetaFont), TextRole::Muted, 2)); }

    if (!m.description.empty()) { body->addSubview(makeLabel(m.description, Typo::Body, TextRole::Secondary, 3)); }

    auto tags = makeRow(8.f);
    tags->layout().setFlexWrap(Wrap::Wrap);
    for (const std::string &provided : m.provides) { tags->addSubview(makeTag(titleCase(provided), true)); }
    if (!m.category.empty()) {
        const bool sameAsProvides = std::find(m.provides.begin(), m.provides.end(), m.category) != m.provides.end();
        if (!sameAsProvides) { tags->addSubview(makeTag(titleCase(m.category), false)); }
    }
    for (const std::string &tag : m.tags) {
        if (tag == m.category) { continue; }
        tags->addSubview(makeTag(tag, false));
    }
    if (!m.license.empty()) { tags->addSubview(makeTag(m.license, false)); }
    if (!m.engineRange.empty()) { tags->addSubview(makeTag("Panda " + m.engineRange, false)); }
    if (!m.id.empty()) { tags->addSubview(makeTag(m.id, false)); }
    if (!tags->getSubviews().empty()) { body->addSubview(tags); }

    if (!m.themes.empty()) {
        auto themes = makeRow(8.f);
        themes->layout().setFlexWrap(Wrap::Wrap);
        themes->addSubview(makeLabel("Themes", Font(kMetaFont, FontWeight::Medium), TextRole::Muted, 1));
        for (const auto &[themeId, themeName] : m.themes) {
            const bool active = themeId == m.activeTheme;
            auto button = makeButton(active ? "✓ " + themeName : themeName, active ? ButtonStyle::AccentOutline : ButtonStyle::Neutral, [this, themeId] {
                if (m_actions && m_actions->selectTheme) { m_actions->selectTheme(themeId); }
            });
            button->setSize(ButtonSize::Small);
            button->setFont(Font(kChipFont));
            themes->addSubview(button);
        }
        body->addSubview(themes);
    }

    const bool downloading = m.status == Status::Installing || m.status == Status::Updating;
    if (downloading) {
        auto progress = std::make_shared<ProgressBar>();
        progress->layout().setHeight(Length::points(4.f));
        if (m.progress >= 0.f) {
            progress->setProgress(m.progress);
        } else {
            progress->setIndeterminate(true);
        }
        body->addSubview(progress);
    }

    if (!m.error.empty()) {
        auto error = makeLabel(m.error, Font(kMetaFont), TextRole::Primary, 3);
        error->setTextColor(theme().danger);
        body->addSubview(error);
    }

    auto links = makeRow(18.f);
    links->layout().setFlexWrap(Wrap::Wrap);
    if (!m.portalUrl.empty()) {
        const std::string url = m.portalUrl;
        links->addSubview(makeLinkButton("Open on pandaengine.dev", [this, url] {
            if (m_actions && m_actions->openUrl) { m_actions->openUrl(url); }
        }));
    }
    if (!m.repositoryUrl.empty()) {
        const std::string url = m.repositoryUrl;
        links->addSubview(makeLinkButton("Repository", [this, url] {
            if (m_actions && m_actions->openUrl) { m_actions->openUrl(url); }
        }));
    }
    links->addSubview(makeLinkButton("Copy ID", [id] { setClipboardText(id); }));
    body->addSubview(links);
    addSubview(body);

    // The actions: one column on the right, the main one on top.
    auto actions = makeColumn(8.f);
    actions->layout().setAlignItems(Align::End);
    actions->layout().setFlexShrink(0.f);
    auto act = [this](std::function<void(const std::string &)> PluginCardActions::*member, std::string pluginId) {
        return [this, member, pluginId] {
            if (m_actions && (m_actions->*member)) { (m_actions->*member)(pluginId); }
        };
    };
    switch (m.status) {
        case Status::NotInstalled:
            actions->addSubview(makeButton("Install", ButtonStyle::Accent, act(&PluginCardActions::install, id)));
            break;
        case Status::OverriddenByDev: {
            auto install = makeButton("Install", ButtonStyle::Accent, {});
            install->setEnabled(false);
            actions->addSubview(install);
            break;
        }
        case Status::Installing:
        case Status::Updating:
        case Status::Removing: {
            auto busy = makeButton(operationTitle(m.status), ButtonStyle::Neutral, {});
            busy->setEnabled(false);
            actions->addSubview(busy);
            break;
        }
        case Status::Installed:
            actions->addSubview(makeButton("Uninstall", ButtonStyle::Neutral, act(&PluginCardActions::uninstall, id)));
            break;
        case Status::UpdateAvailable:
            actions->addSubview(makeButton("Update to v" + m.version, ButtonStyle::Accent, act(&PluginCardActions::update, id)));
            actions->addSubview(makeButton("Uninstall", ButtonStyle::Neutral, act(&PluginCardActions::uninstall, id)));
            break;
        case Status::BuiltIn:
            break;
        case Status::Dev:
            actions->addSubview(makeButton("Reload", ButtonStyle::AccentOutline, act(&PluginCardActions::reload, id)));
            actions->addSubview(makeButton("Uninstall", ButtonStyle::Neutral, act(&PluginCardActions::uninstall, id)));
            break;
    }
    addSubview(actions);
    setNeedsLayout();
}

/* ------------------------------ PluginsView ------------------------------ */

PluginsView::PluginsView(PluginsHost host)
    : m_host(std::move(host)) {
    Panda::PluginStore *store = m_host.store;
    m_actions.install = [store](const std::string &id) {
        if (!store) { return; }
        store->clearError(id);
        store->install(id);
    };
    m_actions.update = [store](const std::string &id) {
        if (!store) { return; }
        store->clearError(id);
        store->update(id);
    };
    m_actions.uninstall = [store](const std::string &id) {
        if (!store) { return; }
        store->clearError(id);
        store->remove(id);
    };
    m_actions.reload = [store](const std::string &) {
        if (store) { store->rescan(); }
    };
    m_actions.retry = [store](const std::string &id) {
        if (store) { store->clearError(id); }
    };
    m_actions.selectTheme = [this](const std::string &themeId) {
        if (m_host.selectTheme) { m_host.selectTheme(themeId); }
        m_listDirty = true; // the active mark moves
    };
    m_actions.openUrl = [](const std::string &url) { Panda::SystemTools::openUrl(url); };
    addSubviews();
    setupLayout();
    setupStyle();
    requestCatalog();
    rebuildChips();
    rebuildList();
    m_chipsDirty = false;
    m_listDirty = false;
}

void PluginsView::addSubviews() {
    // The header: the title and Install Dev Plugin, the Zed layout.
    auto header = makeRow(16.f);
    header->layout().setJustifyContent(Justify::SpaceBetween);
    m_title = makeLabel("Plugins", Typo::WindowTitle, TextRole::Primary, 1);
    header->addSubview(m_title);
    m_devButton = makeButton("Install Dev Plugin", ButtonStyle::AccentOutline, [this] {
        if (m_host.installDevPlugin) { m_host.installDevPlugin(); }
    });
    m_devButton->setTooltip("Link a folder with plugin.json and data/ as an installed plugin; Reload rereads it");
    header->addSubview(m_devButton);
    addSubview(header);

    // The search and the filter.
    auto searchRow = makeRow(12.f);
    m_searchField = std::make_shared<TextField>();
    m_searchField->setPlaceholder("Search plugins…");
    m_searchField->setFont(Font(16.f));
    m_searchField->layout().setFlexGrow(1.f);
    m_searchField->layout().setHeight(Length::points(32.f));
    m_searchField->setOnTextChanged([this](TextField &) {
        m_searchPending = true;
        m_searchTimer = 0.0;
    });
    m_searchField->setOnSubmit([this](TextField &field) {
        m_searchPending = false;
        m_search = field.getText();
        m_listDirty = true;
        requestCatalog();
    });
    searchRow->addSubview(m_searchField);
    m_filterControl = std::make_shared<SegmentedControl>();
    m_filterControl->setSegments({"All", "Installed", "Not Installed"});
    m_filterControl->setSelectedIndex(0);
    m_filterControl->layout().setWidth(Length::points(360.f));
    m_filterControl->layout().setFlexShrink(0.f);
    m_filterControl->setOnSelectionChanged([this](SegmentedControl &control) {
        switch (control.selectedIndex()) {
            case 1: m_filter = Filter::Installed; break;
            case 2: m_filter = Filter::NotInstalled; break;
            default: m_filter = Filter::All; break;
        }
        m_listDirty = true;
    });
    searchRow->addSubview(m_filterControl);
    addSubview(searchRow);

    // The categories.
    m_chips = makeRow(8.f);
    m_chips->layout().setFlexWrap(Wrap::Wrap);
    addSubview(m_chips);

    m_notice = makeLabel("", Font(kMetaFont), TextRole::Primary, 2);
    m_notice->setTextColor(theme().warning);
    m_notice->setHidden(true);
    addSubview(m_notice);

    // The list.
    m_list = std::make_shared<ScrollView>();
    m_list->setScrollDirection(ScrollDirection::Vertical);
    m_list->layout().setFlexGrow(1.f);
    m_list->layout().setFlexShrink(1.f);
    m_list->layout().setMinHeight(Length::points(0.f));
    m_list->getContentView()->layout().setFlexDirection(FlexDirection::Column);
    m_list->getContentView()->layout().setAlignItems(Align::Stretch);
    m_list->getContentView()->layout().setGap(12.f);
    m_list->getContentView()->layout().setPadding(Edge::Bottom, 24.f);
    m_list->getContentView()->layout().setPadding(Edge::Right, 4.f);
    addSubview(m_list);

    m_cards = makeColumn(12.f);
    m_list->addContentSubview(m_cards);

    m_empty = makeLabel("", Typo::Body, TextRole::Muted, 3);
    m_empty->setTextAlignment(TextAlignment::Center);
    m_empty->layout().setPadding(Edge::Vertical, 24.f);
    m_empty->setHidden(true);
    m_list->addContentSubview(m_empty);

    m_footer = makeRow(16.f);
    m_footer->layout().setJustifyContent(Justify::Center);
    m_footer->layout().setPadding(Edge::Vertical, 8.f);
    m_footerText = makeLabel("", Font(kMetaFont), TextRole::Muted, 1);
    m_footer->addSubview(m_footerText);
    m_loadMore = makeButton("Load more", ButtonStyle::AccentOutline, [this] {
        if (m_host.store) { m_host.store->fetchMore(); }
    });
    m_footer->addSubview(m_loadMore);
    m_retry = makeButton("Try again", ButtonStyle::AccentOutline, [this] {
        if (m_host.store) { m_host.store->fetchCatalog(Panda::CatalogQuery{m_search, m_category}, true); }
    });
    m_footer->addSubview(m_retry);
    m_footer->setHidden(true);
    m_list->addContentSubview(m_footer);
}

void PluginsView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setAlignItems(Align::Stretch);
    layout().setFlexGrow(1.f);
    layout().setGap(16.f);
    layout().setPadding(Edge::Horizontal, 24.f);
    layout().setPadding(Edge::Top, 20.f);
}

void PluginsView::setupStyle() {
    setBackgroundColor(theme().windowBackground);
    if (m_notice) { m_notice->setTextColor(theme().warning); }
    m_chipsDirty = true; // the selected chip is a solid accent: the colors follow the theme
}

std::shared_ptr<Button> PluginsView::makeChip(const std::string &title, bool selected, std::function<void()> onClick) {
    auto chip = makeButton(title, selected ? ButtonStyle::Accent : ButtonStyle::Neutral, std::move(onClick));
    chip->setSize(ButtonSize::Small);
    chip->setFont(Font(kChipFont, FontWeight::Medium));
    chip->layout().setPadding(Edge::Horizontal, 14.f);
    chip->layout().setPadding(Edge::Vertical, 6.f);
    return chip;
}

void PluginsView::rebuildChips() {
    if (!m_chips) { return; }
    m_chips->removeAllSubviews();
    m_chips->addSubview(makeChip("All", m_category.empty(), [this] {
        m_category.clear();
        m_chipsDirty = true;
        m_listDirty = true;
        requestCatalog();
    }));
    for (const char *category : kCategories) {
        const std::string value = category;
        m_chips->addSubview(makeChip(titleCase(value), m_category == value, [this, value] {
            m_category = value;
            m_chipsDirty = true;
            m_listDirty = true;
            requestCatalog();
        }));
    }
    m_chips->setNeedsLayout();
}

void PluginsView::refresh() {
    m_listDirty = true;
    requestCatalog();
}

void PluginsView::requestCatalog() {
    if (!m_host.store) { return; }
    m_host.store->fetchCatalog(Panda::CatalogQuery{m_search, m_category});
}

void PluginsView::update(double deltaTime) {
    if (!m_host.store) { return; }
    Panda::PluginStore &store = *m_host.store;
    if (m_searchPending) {
        m_searchTimer += deltaTime;
        if (m_searchTimer >= kSearchDebounceSeconds) {
            m_searchPending = false;
            m_search = m_searchField->getText();
            m_listDirty = true;
            requestCatalog();
        }
    }
    if (m_chipsDirty) {
        m_chipsDirty = false;
        rebuildChips();
    }
    if (m_listDirty || store.revision() != m_shownRevision) {
        m_listDirty = false;
        m_shownRevision = store.revision();
        rebuildList();
    }
    const Panda::PluginNotice &notice = store.notice();
    if (notice.revision != m_shownNotice) {
        m_shownNotice = notice.revision;
        if (!notice.title.empty()) { showMessageDialog(getContext(), notice.title, notice.message); }
    }
    // The next page comes when the list is scrolled near its end.
    const Panda::CatalogState &catalog = store.catalog();
    if (m_filter != Filter::Installed && !catalog.loading && !catalog.failed && !catalog.fromCache && catalog.hasMore()) {
        const float bottom = m_list->getContentOffset().y + m_list->getFrame().size.height;
        if (m_list->getContentSize().height > 0.f && bottom >= m_list->getContentSize().height - 240.f) { store.fetchMore(); }
    }
}

std::vector<PluginCardModel> PluginsView::buildModels() const {
    using Status = PluginCardModel::Status;
    std::vector<PluginCardModel> models;
    if (!m_host.store) { return models; }
    Panda::PluginStore &store = *m_host.store;
    const Panda::CatalogState &catalog = store.catalog();
    const std::string activeTheme = m_host.currentTheme ? m_host.currentTheme() : std::string();
    const std::string needle = lower(m_search);

    auto catalogEntry = [&catalog](const std::string &id) -> const Panda::CatalogPlugin * {
        for (const Panda::CatalogPlugin &item : catalog.items) {
            if (item.id == id) { return &item; }
        }
        return nullptr;
    };
    auto fillState = [&store](PluginCardModel &model) {
        if (const Panda::PluginOperation *operation = store.operationFor(model.id)) {
            switch (operation->kind) {
                case Panda::PluginOperation::Kind::Install: model.status = Status::Installing; break;
                case Panda::PluginOperation::Kind::Update: model.status = Status::Updating; break;
                case Panda::PluginOperation::Kind::Remove: model.status = Status::Removing; break;
            }
            model.progress = operation->progress;
            if (!operation->version.empty()) { model.version = operation->version; }
        }
        if (const std::string *error = store.errorFor(model.id)) { model.error = *error; }
    };
    auto fromInstalled = [&](const Panda::InstalledPlugin &plugin, const Panda::CatalogPlugin *entry) {
        PluginCardModel model;
        const Panda::PluginManifest &manifest = plugin.manifest;
        const Panda::PluginUpdate *update = store.updateFor(manifest.id);
        model.id = manifest.id;
        model.name = manifest.name.empty() ? manifest.id : manifest.name;
        model.installedVersion = manifest.version;
        model.version = update ? update->version : manifest.version;
        model.description = manifest.description.empty() && entry ? entry->description : manifest.description;
        model.authors = join(manifest.authors, ", ");
        if (model.authors.empty() && entry) { model.authors = entry->owner.name.empty() ? entry->owner.githubLogin : entry->owner.name; }
        model.category = manifest.category.empty() && entry ? entry->category : manifest.category;
        model.license = manifest.license.empty() && entry ? entry->license : manifest.license;
        model.engineRange = manifest.engine;
        model.tags = manifest.tags.empty() && entry ? entry->tags : manifest.tags;
        model.provides = plugin.provides();
        if (model.provides.empty() && entry) { model.provides = entry->provides; }
        if (entry) {
            model.downloads = entry->downloadsTotal;
            model.updatedAt = dateOf(entry->updatedAt);
            model.fromCatalog = true;
        }
        if (entry || (!plugin.builtIn && !plugin.dev)) { model.portalUrl = Panda::PluginStore::portalPageUrl(manifest.id); }
        if (const auto it = manifest.links.find("repository"); it != manifest.links.end()) { model.repositoryUrl = it->second; }
        for (const Panda::EditorTheme &theme : plugin.themes) { model.themes.emplace_back(theme.id, theme.name); }
        model.activeTheme = activeTheme;
        model.icon = store.icon(manifest.id, entry ? entry->iconUrl : std::string(), plugin.directory / "icon.png");
        model.status = plugin.builtIn ? Status::BuiltIn : plugin.dev ? Status::Dev : update ? Status::UpdateAvailable : Status::Installed;
        fillState(model);
        return model;
    };
    auto fromCatalog = [&](const Panda::CatalogPlugin &entry) {
        PluginCardModel model;
        model.id = entry.id;
        model.name = entry.name.empty() ? entry.id : entry.name;
        model.version = entry.latestVersion;
        model.description = entry.description;
        model.authors = entry.owner.name.empty() ? entry.owner.githubLogin : entry.owner.name;
        model.category = entry.category;
        model.license = entry.license;
        model.tags = entry.tags;
        model.provides = entry.provides;
        model.downloads = entry.downloadsTotal;
        model.updatedAt = dateOf(entry.updatedAt);
        model.fromCatalog = true;
        model.portalUrl = Panda::PluginStore::portalPageUrl(entry.id);
        model.icon = store.icon(entry.id, entry.iconUrl, {});
        model.status = Status::NotInstalled;
        fillState(model);
        return model;
    };
    auto matchesLocal = [&](const Panda::InstalledPlugin &plugin) {
        const Panda::PluginManifest &manifest = plugin.manifest;
        if (!m_category.empty() && manifest.category != m_category) {
            const std::vector<std::string> provides = plugin.provides();
            if (std::find(provides.begin(), provides.end(), m_category) == provides.end()) { return false; }
        }
        if (needle.empty()) { return true; }
        if (contains(manifest.name, needle) || contains(manifest.id, needle) || contains(manifest.description, needle)) { return true; }
        for (const std::string &tag : manifest.tags) {
            if (contains(tag, needle)) { return true; }
        }
        return false;
    };

    if (m_filter == Filter::Installed) {
        for (const Panda::InstalledPlugin &plugin : store.installed()) {
            if (matchesLocal(plugin)) { models.push_back(fromInstalled(plugin, catalogEntry(plugin.manifest.id))); }
        }
        return models;
    }
    if (m_filter == Filter::All) {
        // Dev plugins first, like the dev extensions of Zed, then the installed ones the catalog does
        // not list (built-in, or the registry is unreachable).
        for (const Panda::InstalledPlugin &plugin : store.installed()) {
            if (plugin.dev && matchesLocal(plugin)) { models.push_back(fromInstalled(plugin, catalogEntry(plugin.manifest.id))); }
        }
        for (const Panda::InstalledPlugin &plugin : store.installed()) {
            if (!plugin.dev && !catalogEntry(plugin.manifest.id) && matchesLocal(plugin)) { models.push_back(fromInstalled(plugin, nullptr)); }
        }
    }
    for (const Panda::CatalogPlugin &entry : catalog.items) {
        const Panda::InstalledPlugin *installed = store.findInstalled(entry.id);
        if (installed && installed->dev) {
            if (m_filter == Filter::NotInstalled) { continue; }
            PluginCardModel model = fromCatalog(entry);
            model.status = Status::OverriddenByDev;
            models.push_back(std::move(model));
            continue;
        }
        if (installed) {
            if (m_filter == Filter::NotInstalled) { continue; }
            models.push_back(fromInstalled(*installed, &entry));
            continue;
        }
        models.push_back(fromCatalog(entry));
    }
    return models;
}

std::string PluginsView::emptyMessage() const {
    const Panda::CatalogState &catalog = m_host.store->catalog();
    const bool hasSearch = !m_search.empty() || !m_category.empty();
    if (m_filter != Filter::Installed) {
        if (catalog.loading && catalog.items.empty()) { return "Loading plugins…"; }
        if (catalog.failed) { return catalog.error; }
    }
    switch (m_filter) {
        case Filter::All: return hasSearch ? "No plugins match your search." : "No plugins.";
        case Filter::Installed: return hasSearch ? "No installed plugins match your search." : "No installed plugins.";
        case Filter::NotInstalled: return hasSearch ? "Every plugin that matches your search is installed." : "Every plugin is installed.";
    }
    return {};
}

void PluginsView::rebuildList() {
    if (!m_host.store) { return; }
    const Panda::CatalogState &catalog = m_host.store->catalog();
    const std::vector<PluginCardModel> models = buildModels();

    // The cards keep their identity by plugin id: only changed models rebuild a card.
    std::unordered_map<std::string, std::shared_ptr<PluginCard>> kept;
    m_cards->removeAllSubviews();
    for (const PluginCardModel &model : models) {
        std::shared_ptr<PluginCard> card;
        if (auto it = m_cardViews.find(model.id); it != m_cardViews.end()) { card = it->second; }
        if (!card) { card = std::make_shared<PluginCard>(&m_actions); }
        card->setModel(model);
        kept[model.id] = card;
        m_cards->addSubview(card);
    }
    m_cardViews = std::move(kept);

    const bool empty = models.empty();
    m_empty->setHidden(!empty);
    if (empty) { m_empty->setText(emptyMessage()); }

    // The cached catalog and a page that did not load are said above the list.
    const bool notice = m_filter != Filter::Installed && !catalog.failed && !catalog.error.empty();
    m_notice->setHidden(!notice);
    if (notice) { m_notice->setText(catalog.error); }

    // The footer: how much of the catalog is on screen, and the way to the rest.
    const bool catalogShown = m_filter != Filter::Installed;
    const bool showFooter = catalogShown && (catalog.failed || (!catalog.items.empty() && (catalog.hasMore() || catalog.loading)));
    m_footer->setHidden(!showFooter);
    if (showFooter) {
        if (catalog.failed) {
            m_footerText->setText("");
            m_footerText->setHidden(true);
            m_loadMore->setHidden(true);
            m_retry->setHidden(false);
        } else {
            m_footerText->setHidden(false);
            m_footerText->setText(
                catalog.loading ? "Loading more…"
                                : "Showing " + std::to_string(catalog.items.size()) + " of " + std::to_string(catalog.total) + " plugins"
            );
            m_loadMore->setHidden(catalog.loading || !catalog.hasMore());
            m_retry->setHidden(true);
        }
    }
    setNeedsLayout();
}

} // namespace Editor
