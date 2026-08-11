#include "widgets/Icon.hpp"
#include "functions/GFX_Functions.hpp"

void Icon::render() {
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    if(this->prev_rect != this->rect)
        PICO_GFX::markDirty(this->prev_rect);

    PICO_GFX::markDirty(this->rect);
    IconRender::DrawIcon(this->iconId, this->iconSize, this->rect.x, this->rect.y, this->color);

    this->prev_rect.copy(this->rect);
    this->needs_redraw = false;
}