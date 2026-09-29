#include "gui/widgets/dialogs/KeyboardDialog.hpp"
#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "functions/GFX_Functions.hpp"

KeyboardDialog::KeyboardDialog(){
    this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

    this->preview = new Label<PICO_STR_LL>(MARGIN, MARGIN, "");
    this->preview->setMaxWidth(SCREEN_WIDTH - MARGIN * 2);
    this->preview->setCursorVisible(true);
    this->preview->setCursorBlink(true);
    this->preview->setBackgroundColor(PICO_WHITE);
    this->preview->setBorderColor(PICO_BLACK);
    this->preview->setBorderWidth(1);
    this->preview->setParent(this);
    children_.push_back(this->preview);

    this->setVisible(false);
}

void KeyboardDialog::attach(KeyboardPanel* panel){
    const int top = panel->getPanelTop();
    if(this->visible && top > this->panel_top) this->redraw_below_frames = 2;
    this->panel_top = top;

    this->preview->setMaxHeight(top - MARGIN * 2);
    this->needsRender();
}

void KeyboardDialog::refresh(ITextInputWidget* keyboard){
    const FixedString<PICO_STR_LL> text = keyboard->getText();
    size_t comp_start = 0, comp_len = 0;
    keyboard->getComposition(comp_start, comp_len);

    if(comp_len == 0){
        this->preview->setText(text);
        this->preview->setCursorToByteOffset(keyboard->getCursorByteOffset());
        return;
    }

    //変換中の読みを`~`(波線のマークアップ。記号自体は描かれない)で囲んで見せる。
    //読みが無いときに囲みを出さないのは、空の`~~`が取り消し線の開始として解釈されるため
    FixedString<PICO_STR_LL + 2> display;
    display.assign(text.c_str(), comp_start);
    display.append("~");
    display.append(text.c_str() + comp_start, comp_len);
    display.append("~");
    display.append(text.c_str() + comp_start + comp_len);

    this->preview->setText(display);
    this->preview->setCursorToByteOffset(comp_start + 1 + comp_len);
}

void KeyboardDialog::render(){
    //合成(FlushDirty)の外、つまりUpdateAll()から毎フレーム呼ばれる回でだけ数える
    if(!PICO_GFX::isDirtyDeactivates && this->redraw_below_frames > 0) this->redraw_below_frames--;

    if(!this->visible) return;
    if(!this->needs_redraw) return;

    this->markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();

    this->needs_redraw = false;
}
