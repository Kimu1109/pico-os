#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/NumberInput.hpp"
#include "gui/widgets/ProgressBar.hpp"
#include "gui/widgets/apps/MonthGrid.hpp"
#include "gui/widgets/apps/DurationPicker.hpp"
#include "gui/widgets/dialogs/DialogLayout.hpp"

// 「選ぶ・入れる」系のダイアログを1つにまとめたもの(Luaの pico.show_choice / show_date /
// show_time / show_number / show_progress が使う)。種類(Mode)ごとに中身が違うだけで、
// 枠・題名・ボタンの並べ方・閉じ方はMsgDialog/InputDialogと同じ。
//
//   Choice   … 題名 + 選択肢の一覧(1回タップで選んで閉じる) + キャンセル
//   Date     … 題名 + [<] 年月 [>] + 月の格子(MonthGrid) + 決定/キャンセル。結果は"YYYY-MM-DD"
//   Time     … 題名 + 時:分:秒(DurationPicker) + 決定/キャンセル。結果は"HH:MM:SS"
//   Number   … 題名 + 数字入力欄(NumberInput) + 決定/キャンセル。結果は入力した文字列
//   Progress … 題名 + 進捗バー + (任意で)キャンセル。閉じるのはキャンセルかスクリプト側から
//
// 子ウィジェットは種類に必要なものだけnewする(固定長の配列は持たない)。
class PickerDialog : public Widget {
    public:
        enum class Mode : uint8_t { Choice, Date, Time, Number, Progress };

        constexpr static int kDialogWidth = 220;
        constexpr static int kMaxListHeight = 160;

    private:
        constexpr static int MARGIN = DialogLayout::kMargin;

        Mode mode_;
        std::vector<Widget*> children_;

        Rect dlg = DialogLayout::Place(kDialogWidth, 120);

        Label<PICO_STR_L>* title = nullptr;
        Button* ok_button = nullptr;
        Button* cancel_button = nullptr;
        ScrollList* list = nullptr;
        Button* prev_button = nullptr;
        Button* next_button = nullptr;
        Label<PICO_STR_S>* month_label = nullptr;
        MonthGrid* grid = nullptr;
        DurationPicker* time_picker = nullptr;
        NumberInput* number = nullptr;
        ProgressBar* bar = nullptr;

        int year_ = 2026, month_ = 1, day_ = 1;
        int selected_ = -1;       // Choice: 選んだ番号
        float progress_ = 0.0f;
        bool closed_ = false;

        std::function<void(bool is_ok)> on_closed = nullptr;

        int buttonCount() const { return (ok_button ? 1 : 0) + (cancel_button ? 1 : 0); }
        Button* makeButton(const char* text, bool is_ok);
        void refreshMonth();
        void finish(bool is_ok);

    public:
        PickerDialog(Mode mode, const char* title_text, const char* ok_text, const char* cancel_text);
        ~PickerDialog() override;

        Mode getMode() const { return mode_; }

        // ---- Choice ----
        void addChoice(const char* text);
        int getChoiceCount() const { return list ? list->getItemCount() : 0; }
        int getSelectedIndex() const { return selected_; }

        // ---- Date ----
        void setDate(int year, int month, int day);
        int getYear() const { return year_; }
        int getMonth() const { return month_; }
        int getDay() const { return day_; }

        // ---- Time ----
        void setTime(int hour, int minute, int second);

        // ---- Number ----
        void setNumber(const char* text);

        // ---- Progress ----
        void setProgress(float value);
        float getProgress() const { return progress_; }
        void setMessage(const char* text);

        // 種類に応じた結果の文字列。Date="YYYY-MM-DD" / Time="HH:MM:SS" / Number=入力した文字列 / 他は空
        FixedString<PICO_STR_S> getResultText() const;

        // 中身が出そろったあとで呼ぶ(addChoice等のあと)。大きさと位置を決め直す
        void layout();

        void setOnClosed(std::function<void(bool is_ok)> callback){ this->on_closed = callback; }
        void clearOnClosed(){ this->on_closed = nullptr; }
        void causeOnClosed(bool is_ok){ if(this->on_closed) this->on_closed(is_ok); }
        // スクリプト側から閉じる(is_ok=falseとして通知はしない。pico.destroyと同じ後片付けのために使う)
        void close(){ this->setVisible(false); }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::PickerDialog; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        const std::vector<Widget*>& getChildren() const override { return children_; }
};
