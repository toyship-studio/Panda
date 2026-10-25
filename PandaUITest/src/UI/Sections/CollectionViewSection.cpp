#include "UI/Sections/CollectionViewSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace Demo {

using namespace PandaUI;

namespace {

    constexpr float LIST_ROW_HEIGHT = 32.f;
    constexpr int LIST_COUNT = 10000;
    constexpr int GRID_COUNT = 2000;
    constexpr float BOX_HEIGHT = 360.f;

    // Palette of the grid cells (0xRRGGBBAA) — cycled by index so that the boundaries and scrolling are visible.
    constexpr uint32_t kGridPalette[] = {
        0x4F8FE0FF, 0x57B894FF, 0xE0A24FFF, 0xD16C8AFF, 0x8B7FD1FF, 0x4FB0C0FF,
    };

    // Content of a list row: a label on the left, vertically centered. It is reused — bind() sets everything.
    class ListCell final : public Panel {
    public:
        ListCell() {
            setBackgroundColor(Color(0x00000000));
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setPadding(Edge::Horizontal, 12.f);
            m_label = std::make_shared<Label>("");
            m_label->setFont(Font(17.f));
            addSubview(m_label);
        }
        void bind(int index) {
            m_label->setText("Item " + std::to_string(index));
        }

    private:
        std::shared_ptr<Label> m_label;
    };

    // Content of a grid cell: a colored tile with a number in the center.
    class GridCell final : public Panel {
    public:
        GridCell() {
            layout().setFlexDirection(FlexDirection::Column);
            layout().setJustifyContent(Justify::Center);
            layout().setAlignItems(Align::Center);
            surface().setCornerRadius(Radius::md);
            m_label = std::make_shared<Label>("");
            m_label->setFont(Font(17.f));
            addSubview(m_label);
        }
        void bind(int index) {
            setBackgroundColor(Color(kGridPalette[index % (sizeof(kGridPalette) / sizeof(kGridPalette[0]))]));
            m_label->setText(std::to_string(index));
        }

    private:
        std::shared_ptr<Label> m_label;
    };

    // Example wrapper: a vertical stack [fixed-height CollectionView | selection status].
    std::shared_ptr<View> makeExampleBox(const std::shared_ptr<CollectionView> &collection, std::shared_ptr<Label> status) {
        collection->layout().setFlexGrow(1.f); // fill the card width
        collection->layout().setHeight(Length::points(BOX_HEIGHT));
        collection->surface().setCornerRadius(Radius::md);
        collection->surface().setBorderWidth(1.f);
        collection->surface().setBorderColor(theme().border);
        collection->setBackgroundColor(theme().surfaceVariant);

        auto box = StackView::vertical(8.f);
        box->layout().setFlexGrow(1.f);
        box->addSubview(collection);
        box->addSubview(std::move(status));
        return box;
    }

} // namespace

CollectionViewSection::CollectionViewSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void CollectionViewSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Collection View",
        "Virtualization and cell reuse: only the visible cells (+buffer) actually exist."
    ));

    // --- List of 10,000 rows ---
    {
        auto example = std::make_shared<DemoExample>(
            "List — 10,000 rows", "Scroll: only ~a dozen cells live in the tree. Click selects (selection survives scrolling)."
        );

        auto list = std::make_shared<CollectionView>(std::make_shared<ListCollectionLayout>(LIST_ROW_HEIGHT));
        list->setSelectionMode(CollectionSelectionMode::Single);

        auto status = std::make_shared<Label>("10,000 rows. Selected: —");
        status->setFont(Font(15.f));
        status->setTextRole(TextRole::Secondary);

        CollectionViewDataSource dataSource;
        dataSource.numberOfItems = [] { return LIST_COUNT; };
        dataSource.cellForReuse = [] { return std::make_shared<ListCell>(); };
        dataSource.configureCell = [](View &cell, int index) { static_cast<ListCell &>(cell).bind(index); };
        list->setDataSource(dataSource);

        Label *statusPtr = status.get();
        list->setOnSelectionChanged([statusPtr](CollectionView &cv) {
            const auto &sel = cv.selectedIndices();
            statusPtr->setText(
                sel.empty() ? "10,000 rows. Selected: —" : "10,000 rows. Selected: Item " + std::to_string(sel.front())
            );
        });

        example->addContent(makeExampleBox(list, status));
        addSubview(example);
    }

    // --- Grid of 2,000 cells ---
    {
        auto example = std::make_shared<DemoExample>(
            "Grid — 2,000 cells", "Wraps by width, reflows on window resize. Virtualized as well."
        );

        auto grid = std::make_shared<CollectionView>(
            std::make_shared<GridCollectionLayout>(Size(88.f, 64.f), 10.f, 12.f)
        );
        grid->setSelectionMode(CollectionSelectionMode::Single);

        auto status = std::make_shared<Label>("2,000 cells. Selected: —");
        status->setFont(Font(15.f));
        status->setTextRole(TextRole::Secondary);

        CollectionViewDataSource dataSource;
        dataSource.numberOfItems = [] { return GRID_COUNT; };
        dataSource.cellForReuse = [] { return std::make_shared<GridCell>(); };
        dataSource.configureCell = [](View &cell, int index) { static_cast<GridCell &>(cell).bind(index); };
        grid->setDataSource(dataSource);

        Label *statusPtr = status.get();
        grid->setOnSelectionChanged([statusPtr](CollectionView &cv) {
            const auto &sel = cv.selectedIndices();
            statusPtr->setText(
                sel.empty() ? "2,000 cells. Selected: —" : "2,000 cells. Selected: #" + std::to_string(sel.front())
            );
        });

        example->addContent(makeExampleBox(grid, status));
        addSubview(example);
    }
}

void CollectionViewSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void CollectionViewSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
