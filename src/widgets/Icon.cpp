#include "widgets/Icon.hpp"
#include "functions/GFX_Functions.hpp"

void Icon::render() {
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    if(this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();

    markdirty(g_rect);
    IconRender::DrawIcon(this->iconId, this->iconSize, g_rect.x, g_rect.y, this->color);

    this->prev_l_rect.copy(this->l_rect);
    this->needs_redraw = false;
}