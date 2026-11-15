#include "PandaEditorKit/Widgets/Picker.hpp"

#include "PandaEditorKit/Widgets/Icons.hpp"
#include "PandaEditorKit/Widgets/Typography.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

namespace Editor {

using namespace PandaUI;

namespace {

    constexpr float PICKER_ROW_HEIGHT = 28.f;
    constexpr float PALETTE_TOP = 72.f; // a palette hangs this far under the top edge of the window
    constexpr int PICKER_VISIBLE_ROWS = 9;

    bool containsIgnoringCase(const std::string &text, const std::string &query) {
        if (query.empty()) { return true; }
        const auto it = std::search(text.begin(), text.end(), query.begin(), query.end(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
        return it != text.end();
    }

    bool startsWithIgnoringCase(const std::string &text, const std::string &query) {
        if (query.size() > text.size()) { return false; }
        return std::equal(query.begin(), query.end(), text.begin(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
    }

    // The card of the picker. The search field keeps the keyboard; the arrows it does not use
    // come up here and walk the list.
    class PickerCard final : public Panel {
    public:
        PickerCard(std::string placeholder, std::vector<PickerItem> items, std::function<void(int)> onPick, float width)
            : m_items(std::move(items))
            , m_onPick(std::move(onPick)) {
            applyElevation(*this, Elevation::Popover);
            layout().setFlexDirection(FlexDirection::Column);
            layout().setPadding(Spacing::md);
            layout().setGap(Spacing::md);
            layout().setWidth(Length::points(width));

            m_search = std::make_shared<TextField>();
            m_search->setPlaceholder(std::move(placeholder));
            m_search->layout().setWidth(Length::percent(100.f));
            addSubview(m_search);

            m_list = std::make_shared<ListView>();
            ListViewAppearance appearance = m_list->appearance();
            appearance.rowHeight = PICKER_ROW_HEIGHT;
            appearance.rowRadius = Radius::sm;
            m_list->setAppearance(appearance);
            m_list->setSelectionMode(ListSelectionMode::Single);
            m_list->layout().setWidth(Length::percent(100.f));
            addSubview(m_list);

            m_empty = std::make_shared<Label>("Nothing found");
            m_empty->setFont(Typo::Caption);
            m_empty->setTextRole(TextRole::Dim);
            m_empty->setTextAlignment(TextAlignment::Center);
            m_empty->layout().setMargin(Edge::Vertical, Spacing::md);
            addSubview(m_empty);

            m_search->setOnTextChanged([this](TextField &field) { rebuild(field.getText()); });
            m_search->setOnSubmit([this](TextField &) { m_list->activateSelected(); });
            m_list->setOnItemActivated([this](ListView &, int row) { pick(row); });
            rebuild({});
        }

        void setPopup(Popup *popup) {
            m_popup = popup;
        }
        TextField *searchField() const {
            return m_search.get();
        }

        bool keyDown(KeyEvent &event) override {
            if (event.key == Key::Up || event.key == Key::Down) {
                m_list->moveSelection(event.key == Key::Up ? -1 : 1);
                return true;
            }
            return Panel::keyDown(event);
        }

    private:
        void rebuild(const std::string &query) {
            m_list->clearItems();
            m_rows.clear();
            // The best match first: a title that starts with the text, then a title that has it,
            // then a row found by its detail. Within a rank the order of the items stays.
            std::vector<int> ranked[3];
            for (int index = 0; index < static_cast<int>(m_items.size()); ++index) {
                const PickerItem &item = m_items[index];
                if (query.empty()) {
                    ranked[0].push_back(index);
                } else if (startsWithIgnoringCase(item.title, query)) {
                    ranked[0].push_back(index);
                } else if (containsIgnoringCase(item.title, query)) {
                    ranked[1].push_back(index);
                } else if (containsIgnoringCase(item.detail, query)) {
                    ranked[2].push_back(index);
                }
            }
            for (const std::vector<int> &rank : ranked) {
                for (int index : rank) {
                    m_list->addRow(makeRow(m_items[static_cast<size_t>(index)]));
                    m_rows.push_back(index);
                }
            }
            const int shown = std::min(static_cast<int>(m_rows.size()), PICKER_VISIBLE_ROWS);
            m_list->layout().setHeight(Length::points(PICKER_ROW_HEIGHT * static_cast<float>(shown)));
            m_list->layout().setDisplay(m_rows.empty() ? Display::None : Display::Flex);
            m_list->setHidden(m_rows.empty());
            m_empty->layout().setDisplay(m_rows.empty() ? Display::Flex : Display::None);
            m_empty->setHidden(!m_rows.empty());
            if (!m_rows.empty()) { m_list->setSelectedIndex(0); } // Enter takes the best match
            m_list->setContentOffset(Point(0.f, 0.f));
            setNeedsLayout();
        }

        std::shared_ptr<View> makeRow(const PickerItem &item) {
            auto row = std::make_shared<Panel>();
            row->setBackgroundColor(Color(0x00000000));
            row->setUserInteractionEnabled(false); // the list row under it takes the click
            row->layout().setFlexDirection(FlexDirection::Row);
            row->layout().setAlignItems(Align::Center);
            row->layout().setGap(Spacing::md);
            row->layout().setFlexGrow(1.f);
            row->layout().setFlexShrink(1.f);
            if (!item.icon.empty()) {
                auto icon = makeIcon(item.icon, Ic::RowSize);
                icon->layout().setFlexShrink(0.f);
                row->addSubview(icon);
            }
            auto title = std::make_shared<Label>(item.title);
            title->setFont(Typo::Item);
            title->layout().setFlexShrink(1.f);
            row->addSubview(title);
            if (!item.detail.empty()) {
                auto detail = std::make_shared<Label>(item.detail);
                detail->setFont(Typo::Caption);
                detail->setTextRole(TextRole::Dim);
                detail->layout().setFlexShrink(4.f); // the detail gives way first
                row->addSubview(detail);
            }
            if (!item.hint.empty()) {
                auto spacer = std::make_shared<Panel>();
                spacer->setBackgroundColor(Color(0x00000000));
                spacer->layout().setFlexGrow(1.f);
                row->addSubview(spacer);
                auto hint = std::make_shared<Label>(item.hint);
                hint->setFont(Typo::ValueSmall);
                hint->setTextRole(TextRole::Muted);
                hint->layout().setFlexShrink(0.f);
                row->addSubview(hint);
            }
            return row;
        }

        void pick(int row) {
            if (row < 0 || row >= static_cast<int>(m_rows.size())) { return; }
            const int index = m_rows[row];
            // The choice runs before the popover goes (the overlay is removed after the frame),
            // and from copies: what it does may rebuild the form the picker was opened from.
            const std::function<void(int)> onPick = m_onPick;
            Popup *popup = m_popup;
            if (onPick) { onPick(index); }
            if (popup != nullptr) { popup->dismiss(); }
        }

        std::vector<PickerItem> m_items;
        std::vector<int> m_rows; // row of the list → index in m_items
        std::function<void(int)> m_onPick;
        std::shared_ptr<TextField> m_search;
        std::shared_ptr<ListView> m_list;
        std::shared_ptr<Label> m_empty;
        Popup *m_popup = nullptr;
    };

} // namespace

void showPalette(
    Context *context, const std::string &placeholder, std::vector<PickerItem> items, std::function<void(int)> onPick
) {
    if (context == nullptr) { return; }
    const float left = std::max(0.f, (context->getViewportSize().width - PICKER_PALETTE_WIDTH) * 0.5f);
    showPicker(context, Point(left, PALETTE_TOP), placeholder, std::move(items), std::move(onPick), PICKER_PALETTE_WIDTH);
}

void showPicker(
    Context *context, Point at, const std::string &placeholder, std::vector<PickerItem> items,
    std::function<void(int)> onPick, float width
) {
    if (context == nullptr) { return; }
    auto card = std::make_shared<PickerCard>(placeholder, std::move(items), std::move(onPick), width);
    auto popup = std::make_shared<Popup>(card, Rect(at.x, at.y, 0.f, 0.f));
    card->setPopup(popup.get());
    context->addOverlay(popup);
    context->setFocusedView(card->searchField()); // typing filters right away
}

} // namespace Editor
