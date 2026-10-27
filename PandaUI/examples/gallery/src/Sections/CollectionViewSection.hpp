#pragma once

#include "Demo/DemoComponent.hpp"

namespace PandaUI::Gallery {

// Collection View section: virtualization + cell reuse on large data sets —
// a list of 10,000 rows and a grid of 2,000 cells, both with selection.
class CollectionViewSection final : public DemoComponent {
public:
    CollectionViewSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace PandaUI::Gallery
