#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "OS_Data.hpp"
#include <cstdio>

Button* PickerDialog::makeButton(const char* text, bool is_ok) {
    Button* b = new Button(0, 0, text);
    b->setOnPressStart([this, is_ok]() { this->finish(is_ok); });
    b->setParent(this);
    return b;
}

void PickerDialog::finish(bool is_ok) {
    if (closed_) return;
    closed_ = true;
    // 数字入力のキーボードを出しっぱなしにしない(入力欄がこの後消える)
    if (mode_ == Mode::Number) KeyboardFunctions::HideAll();
    this->causeOnClosed(is_ok);
    this->setVisible(false);
}

PickerDialog::PickerDialog(Mode mode, const char* title_text, const char* ok_text, const char* cancel_text)
    : mode_(mode) {
    this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

    title = new Label<PICO_STR_L>(0, 0, title_text ? title_text : "");
    title->setParent(this);
    children_.push_back(title);

    // 決定ボタンが要るのは入力系だけ(Choiceは項目のタップで閉じ、Progressは閉じない)
    const bool wants_ok = (mode == Mode::Date || mode == Mode::Time || mode == Mode::Number);
    if (wants_ok && DialogLayout::HasText(ok_text)) ok_button = makeButton(ok_text, true);
    if (DialogLayout::HasText(cancel_text)) cancel_button = makeButton(cancel_text, false);
    // 閉じる手段が無くなる組み合わせ(Progressは除く)は、キャンセルを必ず出す
    if (mode != Mode::Progress && !ok_button && !cancel_button) {
        if (wants_ok) ok_button = makeButton("決定", true);
        else cancel_button = makeButton("キャンセル", false);
    }

    switch (mode) {
        case Mode::Choice:
            list = new ScrollList(0, 0, kDialogWidth - MARGIN * 2, 60);
            list->setParent(this);
            list->setOnSelectItem([this](int index, bool) {
                if (index < 0) return;
                this->selected_ = index;
                this->finish(true);
            });
            children_.push_back(list);
            break;
        case Mode::Date:
            prev_button = new Button(0, 0, "<");
            prev_button->setOnPressStart([this]() {
                if (--month_ < 1) { month_ = 12; year_--; }
                this->refreshMonth();
            });
            prev_button->setParent(this);
            next_button = new Button(0, 0, ">");
            next_button->setOnPressStart([this]() {
                if (++month_ > 12) { month_ = 1; year_++; }
                this->refreshMonth();
            });
            next_button->setParent(this);
            month_label = new Label<PICO_STR_S>(0, 0, "");
            month_label->setParent(this);
            grid = new MonthGrid(0, 0, kDialogWidth - MARGIN * 2, 150);
            grid->setParent(this);
            grid->setOnSelectDay([this](int d) { this->day_ = d; });
            children_.push_back(prev_button);
            children_.push_back(next_button);
            children_.push_back(month_label);
            children_.push_back(grid);
            this->setDate(2026, 1, 1);
            break;
        case Mode::Time:
            time_picker = new DurationPicker(0, 0, kDialogWidth - MARGIN * 2, 60);
            time_picker->setParent(this);
            children_.push_back(time_picker);
            break;
        case Mode::Number:
            number = new NumberInput(0, 0, kDialogWidth - MARGIN * 2);
            number->setParent(this);
            children_.push_back(number);
            break;
        case Mode::Progress:
            bar = new ProgressBar(0, 0, kDialogWidth - MARGIN * 2, 16);
            bar->setParent(this);
            children_.push_back(bar);
            break;
    }

    if (ok_button) children_.push_back(ok_button);
    if (cancel_button) children_.push_back(cancel_button);

    this->layout();
    this->visible = false;
}

PickerDialog::~PickerDialog() {
    for (Widget* c : children_) delete c;
    children_.clear();
}

void PickerDialog::addChoice(const char* text) {
    if (!list) return;
    ScrollListTools::Item item;
    item.text.assign(text);
    list->add(item);
}

void PickerDialog::setDate(int year, int month, int day) {
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    year_ = year;
    month_ = month;
    day_ = day < 1 ? 1 : day;
    this->refreshMonth();
}

void PickerDialog::refreshMonth() {
    if (!grid) return;
    grid->setMonth(year_, month_);
    if (day_ > grid->getDaysInMonth()) day_ = grid->getDaysInMonth();
    if (day_ < 1) day_ = 1;
    grid->setSelected(day_);
    char buf[PICO_STR_S];
    snprintf(buf, sizeof(buf), "%d年%d月", year_, month_);
    month_label->setText(buf);
    this->layout(); // 月ラベルの幅が変わるので中央へ置き直す
}

void PickerDialog::setTime(int hour, int minute, int second) {
    if (!time_picker) return;
    if (hour < 0) hour = 0;
    if (minute < 0) minute = 0;
    if (second < 0) second = 0;
    time_picker->setTotalMs(((uint32_t)hour * 3600u + (uint32_t)minute * 60u + (uint32_t)second) * 1000u);
}

void PickerDialog::setNumber(const char* text) {
    if (number) number->setNum(text ? text : "");
}

void PickerDialog::setProgress(float value) {
    progress_ = value;
    if (bar) bar->setValue(value);
}

void PickerDialog::setMessage(const char* text) {
    if (!title) return;
    title->setText(text ? text : "");
    this->layout();
}

FixedString<PICO_STR_S> PickerDialog::getResultText() const {
    FixedString<PICO_STR_S> out;
    char buf[PICO_STR_S];
    switch (mode_) {
        case Mode::Date:
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year_, month_, day_);
            out.assign(buf);
            break;
        case Mode::Time: {
            const uint32_t sec = time_picker ? time_picker->getTotalSeconds() : 0;
            snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60), (unsigned)(sec % 60));
            out.assign(buf);
            break;
        }
        case Mode::Number:
            if (number) out.assign(number->getNum()->c_str());
            break;
        default:
            break;
    }
    return out;
}

void PickerDialog::layout() {
    title->setMaxWidth(kDialogWidth - MARGIN * 2);
    const int title_h = title->getH();

    // 相対位置(ダイアログの左上から)を積んでいき、最後に全体の高さからダイアログの置き場を決める
    int y = MARGIN;
    const int title_y = y;
    y += title_h + MARGIN;

    int list_y = 0, list_h = 0, nav_y = 0, grid_y = 0, picker_y = 0, input_y = 0, bar_y = 0;
    switch (mode_) {
        case Mode::Choice: {
            const int count = list->getItemCount();
            const int item_h = FontFn::GetFontSize(list->getFontSize()) + 2 + 2;
            list_h = std::min(kMaxListHeight, std::max(1, count) * item_h + 4);
            list_y = y;
            y += list_h + MARGIN;
            break;
        }
        case Mode::Date:
            nav_y = y;
            y += DialogLayout::kButtonHeight + MARGIN;
            grid_y = y;
            y += grid->getH() + MARGIN;
            break;
        case Mode::Time:
            picker_y = y;
            y += time_picker->getH() + MARGIN;
            break;
        case Mode::Number:
            input_y = y;
            y += number->getH() + MARGIN;
            break;
        case Mode::Progress:
            bar_y = y;
            y += bar->getH() + MARGIN;
            break;
    }
    const int buttons_h = this->buttonCount() ? DialogLayout::ButtonAreaHeight(this->buttonCount()) : 0;
    // ボタンの領域は自分の上下の余白を持つので、直前の余白とは重ねる
    const int total_h = buttons_h ? (y - MARGIN + buttons_h) : y;

    this->dlg = DialogLayout::Place(kDialogWidth, total_h);
    const int ox = dlg.x;
    const int oy = dlg.y;
    const int inner_w = kDialogWidth - MARGIN * 2;

    title->setX(ox + MARGIN);
    title->setY(oy + title_y);

    switch (mode_) {
        case Mode::Choice:
            list->setX(ox + MARGIN);
            list->setY(oy + list_y);
            list->setW(inner_w);
            list->setH(list_h);
            break;
        case Mode::Date: {
            prev_button->setX(ox + MARGIN);
            prev_button->setY(oy + nav_y);
            next_button->setX(ox + kDialogWidth - MARGIN - next_button->getW() - 2);
            next_button->setY(oy + nav_y);
            month_label->setX(ox + (kDialogWidth - month_label->getW()) / 2);
            month_label->setY(oy + nav_y + (DialogLayout::kButtonHeight - month_label->getH()) / 2);
            grid->setX(ox + MARGIN);
            grid->setY(oy + grid_y);
            break;
        }
        case Mode::Time:
            time_picker->setX(ox + MARGIN);
            time_picker->setY(oy + picker_y);
            break;
        case Mode::Number:
            number->setX(ox + MARGIN);
            number->setY(oy + input_y);
            break;
        case Mode::Progress:
            bar->setX(ox + MARGIN);
            bar->setY(oy + bar_y);
            break;
    }

    Button* buttons[] = { ok_button, cancel_button };
    DialogLayout::StackButtons(buttons, 2, ox + MARGIN, kDialogWidth - MARGIN * 2, oy + total_h);
    this->needsRender();
}

void PickerDialog::render() {
    if (!needs_redraw) return;
    if (!visible) return;

    markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();

    OSData::frame->fillRect(dlg.x, dlg.y, dlg.w, dlg.h, this->background_color);
    OSData::frame->drawRect(dlg.x, dlg.y, dlg.w, dlg.h, PICO_BLACK);

    needs_redraw = false;
}
