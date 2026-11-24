#include "UI/Documents/AssetEditorView.hpp"

#include "UI/Documents/AssetPreviewView.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "UI/EditorModelHelper.hpp"

#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <Panda/Assets/Base/AssetRef.hpp>
#include <Panda/Assets/ImportedMeshAsset.hpp>
#include <Panda/Assets/TextureAsset.hpp>
#include <Panda/GameLogic/GameContext.hpp>

#include <cstdio>
#include <string>

namespace Editor {

using namespace PandaUI;

namespace {


    // A count a person reads at a glance: "103 330".
    std::string formatCount(size_t value) {
        std::string digits = std::to_string(value);
        for (int position = static_cast<int>(digits.size()) - 3; position > 0; position -= 3) {
            digits.insert(static_cast<size_t>(position), " ");
        }
        return digits;
    }

    // File size for the asset header: "512 B" / "12.4 KB" / "1.2 MB".
    std::string formatByteSize(uint64_t bytes) {
        if (bytes < 1024) { return std::to_string(bytes) + " B"; }
        const char *units[] = {"KB", "MB", "GB"};
        double value = static_cast<double>(bytes);
        int unit = -1;
        while (value >= 1024.0 && unit < 2) {
            value /= 1024.0;
            unit++;
        }
        char buffer[32];
        snprintf(buffer, sizeof(buffer), value < 10.0 ? "%.1f %s" : "%.0f %s", value, units[unit]);
        return buffer;
    }

} // namespace

AssetEditorView::AssetEditorView() {
    // A form takes the focus as a whole: its tab, when chosen, puts no caret into a field.
    setFocusable(true);
    addSubviews();
    setupLayout();
    setupStyle();
}

void AssetEditorView::addSubviews() {
    m_body = std::make_shared<ScrollView>();
    m_body->setScrollDirection(ScrollDirection::Vertical);
    m_body->layout().setFlexGrow(1.f);
    m_body->getContentView()->layout().setFlexDirection(FlexDirection::Column);
    m_body->getContentView()->layout().setAlignItems(Align::Center);
    m_body->getContentView()->layout().setPadding(Edge::Horizontal, 12.f);
    m_body->getContentView()->layout().setPadding(Edge::Vertical, 12.f);
    addSubview(m_body);

    // The form is a column of the width a form reads at, in the middle of a tab of any width (the
    // settings editor of VS Code): a field stretched over a wide tab is hard to follow to its label.
    auto column = std::make_shared<Panel>();
    column->setBackgroundColor(Color(0x00000000));
    column->layout().setFlexDirection(FlexDirection::Column);
    column->layout().setWidth(Length::percent(100.f));
    column->layout().setMaxWidth(Length::points(760.f));
    column->layout().setGap(6.f);
    m_body->addContentSubview(column);

    m_name = std::make_shared<Label>("");
    m_name->setFont(Typo::Body.withWeight(FontWeight::Medium));
    column->addSubview(m_name);

    m_subtitle = std::make_shared<Label>("");
    m_subtitle->setFont(Typo::ValueSmall);
    m_subtitle->setTextRole(TextRole::Muted);
    m_subtitle->setNumberOfLines(0);
    column->addSubview(m_subtitle);

    m_preview = std::make_shared<AssetPreviewView>();
    column->addSubview(m_preview);

    m_props = std::make_shared<Panel>();
    m_props->setBackgroundColor(Color(0x00000000));
    m_props->layout().setFlexDirection(FlexDirection::Column);
    m_props->layout().setGap(6.f);
    column->addSubview(m_props);
}

void AssetEditorView::setAsset(Panda::AssetId asset) {
    m_asset = asset;
    // The editor of the kind lives as long as the form shows THIS asset.
    if (m_editor && asset != m_editorAsset) {
        m_editor.reset();
        m_editorAsset = 0;
    }
    // The large preview belongs to the form: every tab has its own.
    m_preview->setAsset(asset);
    bind();
}

void AssetEditorView::rebuild() {
    bind();
}

void AssetEditorView::bind() {
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (m_asset && handler) {
        const auto &registry = handler->getRegistry();
        auto it = registry.find(m_asset);
        if (it != registry.end()) {
            m_name->setText(handler->getAssetName(it->first));
            // Subtitle: type · size of the files in the project (in the future — also the size in
            // the runtime Asset Package: important for the game size).
            const uint64_t sizeOnDisk = handler->assetSizeOnDisk(m_asset);
            const std::string type = assetTypeString(it->second.type);
            std::string subtitle = sizeOnDisk > 0 ? type + " · " + formatByteSize(sizeOnDisk) : type;
            // What the file holds, where the engine knows it: the pixels of a texture, the
            // triangles of a model.
            if (it->second.type == Panda::AssetType::TEXTURE) {
                Panda::AssetRef<Panda::TextureAsset> texture{it->first};
                if (texture && texture->getSize().width > 0) {
                    subtitle += " · " + std::to_string(static_cast<int>(texture->getSize().width)) + " × " +
                                std::to_string(static_cast<int>(texture->getSize().height));
                    // What it takes in video memory: a block-compressed texture a quarter of RGBA8.
                    const Miren::TextureFormat format = texture->getFormat();
                    const bool compressed = Miren::TextureCreate::isCompressedFormat(format);
                    const auto &settings = std::get<Panda::AssetInfo::TextureSettings>(it->second.settings);
                    const double texels = static_cast<double>(texture->getSize().width) * texture->getSize().height;
                    const double bytes = texels * (compressed ? 1.0 : format == Miren::RGBA16F ? 8.0 : 4.0) *
                                         (settings.numMips > 0 ? 4.0 / 3.0 : 1.0);
                    subtitle += std::string(" · ") + (compressed ? "BC7" : format == Miren::RGBA16F ? "RGBA16F" : "RGBA8") +
                                " · " + formatByteSize(static_cast<uint64_t>(bytes)) + " of video memory";
                }
            } else if (it->second.type == Panda::AssetType::IMPORTED_MESH) {
                Panda::AssetRef<Panda::ImportedMeshAsset> mesh{it->first};
                if (mesh) {
                    size_t triangles = 0;
                    for (size_t i = 0; i < mesh->submeshCount(); ++i) {
                        for (const Panda::ImportedPrimitive &primitive : mesh->submeshAt(i).primitives) {
                            triangles += primitive.indicesCount / 3;
                        }
                    }
                    subtitle += " · " + std::to_string(mesh->submeshCount()) + (mesh->submeshCount() == 1 ? " part · " : " parts · ") +
                                formatCount(triangles) + " triangles";
                }
            }
            m_subtitle->setText(subtitle);
            rebuildProperties();
            return;
        }
    }
    m_name->setText("Asset");
    m_subtitle->setText("-");
    m_props->removeAllSubviews();
    setNeedsLayout();
}

void AssetEditorView::rebuildProperties() {
    m_props->removeAllSubviews();
    Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
    if (!handler || !m_asset) { return; }
    const Panda::AssetId id = m_asset;
    const auto &registry = handler->getRegistry();
    auto it = registry.find(id);
    if (it == registry.end()) { return; }
    const std::string kind = assetTypeString(it->second.type);
    const AssetKindUi *ui = editorExtensions().assetKinds.find(kind);
    if (ui == nullptr || ui->createEditor == nullptr) {
        // A kind without an editor (a shader, a file of the modules that are off): the header says
        // what it is. A file the editor has no form for is text it does not edit (it has no text
        // editor): the form hands it to the external editor of the settings, as a double click in
        // the Content Browser does — a shader opens with both files of its pair.
        auto label = std::make_shared<Label>(kind);
        label->setFont(Typo::Item);
        label->setTextRole(TextRole::Secondary);
        auto section = makeFormSection("ASSET");
        section->addContent(std::make_shared<PropertyRow>("Type", label, false));
        const Panda::path_t file = handler->getProjectPath() / handler->assetPath(id);
        section->addContent(makeFormButtonRow({makeFormButton("Open in External Editor", ButtonStyle::Neutral, [file] {
            editorHost().openInExternalEditor(file);
        })}));
        section->addContent(makeFormHint("The external editor is set in Settings → External Tools."));
        m_props->addSubview(section);
        setNeedsLayout();
        return;
    }
    if (!m_editor || m_editorAsset != id) {
        AssetEditorContext context;
        context.asset = id;
        context.rebuild = [this] { requestRebuild(); };
        m_editor = ui->createEditor(context);
        m_editorAsset = id;
    }
    m_editor->build(*m_props);
    setNeedsLayout();
}

void AssetEditorView::requestRebuild() {
    Context *context = getContext();
    if (!context) { return; }
    // The form may not live until the next tick (the tab was closed) — a lifetime token instead
    // of this.
    context->postDeferred([this, alive = std::weak_ptr<bool>(m_alive)] {
        if (alive.lock()) { rebuildProperties(); }
    });
}

void AssetEditorView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void AssetEditorView::setupStyle() {
    setBackgroundColor(ThemeColor::windowBackground);
}

} // namespace Editor
