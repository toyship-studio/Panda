#pragma once

#include "PandaUI/Core/TextureHandle.hpp"
#include "PandaUI/Core/View.hpp"

namespace PandaUI {

enum class ImageContentMode {
    Stretch,
    Fit,
    Fill,
    Center,
};

class ImageView : public View {
public:
    ImageView();
    explicit ImageView(TextureHandle texture);

    // measure-leaf (intrinsic size → Yoga measure function): children are not allowed — forbidden
    // at compile time (otherwise Yoga crashes at runtime). Accessories/icons go into a wrapper
    // container.
    void addSubview(std::shared_ptr<View>) = delete;

    void setTexture(TextureHandle texture);
    TextureHandle getTexture() const {
        return m_texture;
    }
    void setTintColor(Color color);
    const Color &getTintColor() const {
        return m_tintColor;
    }
    void setContentMode(ImageContentMode contentMode);
    ImageContentMode getContentMode() const {
        return m_contentMode;
    }
    // Vertical UV flip: render-to-texture (FBO) usually arrives flipped in Y — the scene viewport
    // sets true so that the frame is "right side up".
    void setFlipVertical(bool flip);
    bool isFlippedVertical() const {
        return m_flipVertical;
    }

protected:
    Size intrinsicContentSize(Size availableSize) const override;
    void draw(float offsetX, float offsetY) override;

private:
    TextureHandle m_texture;
    Color m_tintColor;
    ImageContentMode m_contentMode;
    bool m_flipVertical = false;
};

} // namespace PandaUI
