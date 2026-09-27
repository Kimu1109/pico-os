#include "gui/scenes/SettingsScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "OS_Data.hpp"
#include "storage/SD_Path.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"

#include <cstdio>
#include <cstring>

namespace {
    // タイムゾーンのプリセット。POSIX TZ文字列そのものを項目名として使う
    // (このOSのユーザーは開発者自身が想定なので、生のTZ表記でも実害が無い)
    constexpr const char* kTimezonePresets[] = {
        "JST-9", "UTC0", "EST5EDT", "CST6CDT", "MST7MDT", "PST8PDT"
    };
    constexpr int kTimezonePresetCount = sizeof(kTimezonePresets) / sizeof(kTimezonePresets[0]);
}

void SettingsScene::loadTimezoneItems(const FixedString<PICO_STR_M>& current_tz){
    this->tz_item_count     = 0;
    this->tz_selected_index = -1;

    for(int i = 0; i < kTimezonePresetCount && this->tz_item_count < kMaxTzItems; i++){
        this->tz_items[this->tz_item_count].assign(kTimezonePresets[i]);
        if(current_tz == kTimezonePresets[i]) this->tz_selected_index = this->tz_item_count;
        this->tz_item_count++;
    }

    // プリセットに無い値は、独自設定を黒く上書きしないよう末尾に追加して選ぶ
    if(this->tz_selected_index < 0 && !current_tz.empty() && this->tz_item_count < kMaxTzItems){
        this->tz_items[this->tz_item_count].assign(current_tz);
        this->tz_selected_index = this->tz_item_count;
        this->tz_item_count++;
    }

    if(this->tz_selected_index < 0) this->tz_selected_index = 0; // 空/上限溢れ時はJST-9(先頭)を既定に
}

void SettingsScene::loadValues(){
    this->ssid_value.clear();
    this->has_password = false;
    this->ntp1_value.assign(NetworkFunctions::ntpServer1.c_str());
    this->ntp2_value.assign(NetworkFunctions::ntpServer2.c_str());
    this->home_value.clear();

    FixedString<PICO_STR_M> tz_value;
    tz_value.assign("JST-9");

    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_NETWORK_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "wifi-ssid") == 0){
                this->ssid_value.assign(value);
            }else if(strcmp(key, "wifi-password") == 0){
                this->has_password = (value[0] != '\0');
            }else if(strcmp(key, "ntp-server-1") == 0){
                this->ntp1_value.assign(value);
            }else if(strcmp(key, "ntp-server-2") == 0){
                this->ntp2_value.assign(value);
            }else if(strcmp(key, "browser-home") == 0){
                this->home_value.assign(value);
            }else if(strcmp(key, "timezone") == 0){
                tz_value.assign(value);
            }
        }
    );

    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_USER_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "run-test") == 0){
                bool run_test = false;
                if(PICO_Config::ConfigValue::AsBool(value, run_test) && this->run_test_checkbox){
                    this->run_test_checkbox->setIsChecked(run_test);
                }
            }
        }
    );

    this->loadTimezoneItems(tz_value);
}

void SettingsScene::refreshSsidLabel(){
    if(!this->ssid_label) return;
    char buf[PICO_STR_L];
    snprintf(buf, sizeof(buf), "SSID: %s", this->ssid_value.empty() ? "(未設定)" : this->ssid_value.c_str());
    this->ssid_label->setText(buf);
}

void SettingsScene::refreshPasswordLabel(){
    if(!this->password_label) return;
    this->password_label->setText(this->has_password ? "パスワード: 設定済み" : "パスワード: 未設定");
}

void SettingsScene::refreshNtp1Label(){
    if(!this->ntp1_label) return;
    char buf[PICO_STR_L];
    snprintf(buf, sizeof(buf), "NTPサーバー1: %s", this->ntp1_value.c_str());
    this->ntp1_label->setText(buf);
}

void SettingsScene::refreshNtp2Label(){
    if(!this->ntp2_label) return;
    char buf[PICO_STR_L];
    snprintf(buf, sizeof(buf), "NTPサーバー2: %s", this->ntp2_value.c_str());
    this->ntp2_label->setText(buf);
}

void SettingsScene::refreshHomeLabel(){
    if(!this->home_label) return;
    char buf[PICO_STR_L];
    snprintf(buf, sizeof(buf), "ホーム: %s", this->home_value.empty() ? "(未設定)" : this->home_value.c_str());
    this->home_label->setText(buf);
}

Button* SettingsScene::makeEditButton(int16_t y){
    Button* b = new Button("編集");
    b->setFontSize(FontFn::Small);
    b->setAllowTextSpacing(false);
    b->setW(EDIT_BTN_W);
    b->setH(EDIT_BTN_H);

    // 全行で同じ右端に揃えるので、xは最初の1回だけ実測して覚えておく
    if(this->edit_btn_x == 0){
        const Rect content = Scene::contentRect();
        const Rect box = b->getLocalRect();
        this->edit_btn_x = content.x + content.w - MARGIN - box.w;
    }
    b->setX(this->edit_btn_x);
    b->setY(y);
    return b;
}

void SettingsScene::openEditDialog(EditField field, const char* label_text, const char* prefill){
    // MarkdownSceneの検索入力と同じ形: 押されるたびにnewし、閉じたらDestroyLater()で破棄する
    // (設定項目ごとに専用ダイアログを持つより省メモリで、既存の流儀にも合う)
    auto* dialog = new InputDialog(label_text, true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setInput(prefill ? prefill : "");
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog, field](bool is_submit){
        if(is_submit){
            this->commitEdit(field, dialog->getInput());
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void SettingsScene::commitEdit(EditField field, const FixedString<PICO_STR_LL>& input){
    switch(field){
        case EditField::Ssid: {
            if(input.empty()) break; // 空欄なら変更しない(SSIDは消せない)
            PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "wifi-ssid", input.c_str());
            this->ssid_value.assign(input.c_str());
            this->refreshSsidLabel();
            // 既知のパスワード(NetworkFunctionsが再接続用に保持している)があれば、
            // その場でSSIDの変更を反映する。無ければ次にパスワードを入れた時に繋がる
            if(!NetworkFunctions::currentPassword.empty()){
                NetworkFunctions::ConnectWiFiAsync(this->ssid_value.c_str(), NetworkFunctions::currentPassword.c_str());
            }
            break;
        }
        case EditField::Password: {
            if(input.empty()) break; // 空欄なら変更しない(既存のパスワードを保つ)
            PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "wifi-password", input.c_str());
            this->has_password = true;
            this->refreshPasswordLabel();
            if(!this->ssid_value.empty()){
                NetworkFunctions::ConnectWiFiAsync(this->ssid_value.c_str(), input.c_str());
            }
            break;
        }
        case EditField::Ntp1: {
            if(input.empty()) break;
            PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "ntp-server-1", input.c_str());
            this->ntp1_value.assign(input.c_str());
            NetworkFunctions::ntpServer1.assign(input.c_str());
            this->refreshNtp1Label();
            break;
        }
        case EditField::Ntp2: {
            if(input.empty()) break;
            PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "ntp-server-2", input.c_str());
            this->ntp2_value.assign(input.c_str());
            NetworkFunctions::ntpServer2.assign(input.c_str());
            this->refreshNtp2Label();
            break;
        }
        case EditField::BrowserHome: {
            // 空欄も許可する(空にすると同梱サンプル文書に戻る。MarkdownScene::readHome()参照)
            PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "browser-home", input.c_str());
            this->home_value.assign(input.c_str());
            this->refreshHomeLabel();
            break;
        }
    }
}

void SettingsScene::onEnter(){
    const Rect content = Scene::contentRect();

    // ---- 上部: [戻る] ----
    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    const Rect back_box = this->back_button->getLocalRect();
    this->top_row_h = back_box.h;

    const int16_t body_y = (int16_t)(content.y + MARGIN + this->top_row_h + MARGIN);
    auto rowY = [&](int i) -> int16_t { return (int16_t)(body_y + i * ROW_H); };

    // 起動時セルフチェックはloadValues()がチェック状態を直接流し込むので、
    // 読み込みより前に生成しておく(見た目の並び順は後段のNTP/ホームより下で変わらない)
    this->run_test_checkbox = new Checkbox(content.x + MARGIN, rowY(7), "起動時に自己診断を実行");
    this->run_test_checkbox->setFontSize(FontFn::Small);
    this->run_test_checkbox->setOnChangeChecked([this](){
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_USER_CFG, "run-test",
            PICO_Config::ConfigValue::FromBool(this->run_test_checkbox->getIsChecked()));
    });

    this->loadValues();

    // ---- SSID ----
    this->ssid_edit_button = this->makeEditButton(rowY(0));
    this->ssid_edit_button->setOnPressEnd([this](){
        this->openEditDialog(EditField::Ssid, "Wi-Fi SSID", this->ssid_value.c_str());
    });
    WidgetFunctions::Add(this->ssid_edit_button);

    // 編集ボタンの実測が済んだので、以降の行のラベル幅はこれで揃える
    const int label_w = this->edit_btn_x - content.x - MARGIN * 2;

    this->ssid_label = new Label<PICO_STR_L>(content.x + MARGIN, rowY(0), "");
    this->ssid_label->setFontSize(FontFn::Small);
    this->ssid_label->setMaxWidth(label_w);
    this->ssid_label->setMaxHeight(Label<PICO_STR_L>::GetLineHeight(FontFn::Small));
    WidgetFunctions::Add(this->ssid_label);
    this->refreshSsidLabel();

    // ---- Wi-Fiパスワード ----
    this->password_edit_button = this->makeEditButton(rowY(1));
    this->password_edit_button->setOnPressEnd([this](){
        // 空欄のまま決定すると既存のパスワードを変更しない(commitEdit()参照)。
        // プレースホルダはInputDialog側の固定文言("ここに入力...")のままになる
        this->openEditDialog(EditField::Password, "Wi-Fiパスワード", "");
    });
    WidgetFunctions::Add(this->password_edit_button);

    this->password_label = new Label<PICO_STR_M>(content.x + MARGIN, rowY(1), "");
    this->password_label->setFontSize(FontFn::Small);
    this->password_label->setMaxWidth(label_w);
    this->password_label->setMaxHeight(Label<PICO_STR_M>::GetLineHeight(FontFn::Small));
    WidgetFunctions::Add(this->password_label);
    this->refreshPasswordLabel();

    // ---- NTPサーバー1/2 ----
    this->ntp1_edit_button = this->makeEditButton(rowY(3));
    this->ntp1_edit_button->setOnPressEnd([this](){
        this->openEditDialog(EditField::Ntp1, "NTPサーバー1", this->ntp1_value.c_str());
    });
    WidgetFunctions::Add(this->ntp1_edit_button);

    this->ntp1_label = new Label<PICO_STR_L>(content.x + MARGIN, rowY(3), "");
    this->ntp1_label->setFontSize(FontFn::Small);
    this->ntp1_label->setMaxWidth(label_w);
    this->ntp1_label->setMaxHeight(Label<PICO_STR_L>::GetLineHeight(FontFn::Small));
    WidgetFunctions::Add(this->ntp1_label);
    this->refreshNtp1Label();

    this->ntp2_edit_button = this->makeEditButton(rowY(4));
    this->ntp2_edit_button->setOnPressEnd([this](){
        this->openEditDialog(EditField::Ntp2, "NTPサーバー2", this->ntp2_value.c_str());
    });
    WidgetFunctions::Add(this->ntp2_edit_button);

    this->ntp2_label = new Label<PICO_STR_L>(content.x + MARGIN, rowY(4), "");
    this->ntp2_label->setFontSize(FontFn::Small);
    this->ntp2_label->setMaxWidth(label_w);
    this->ntp2_label->setMaxHeight(Label<PICO_STR_L>::GetLineHeight(FontFn::Small));
    WidgetFunctions::Add(this->ntp2_label);
    this->refreshNtp2Label();

    // ---- ブラウザのホーム ----
    this->home_edit_button = this->makeEditButton(rowY(5));
    this->home_edit_button->setOnPressEnd([this](){
        // 空欄で決定すると同梱サンプル文書に戻る(commitEdit()参照)
        this->openEditDialog(EditField::BrowserHome, "ブラウザのホームURL", this->home_value.c_str());
    });
    WidgetFunctions::Add(this->home_edit_button);

    this->home_label = new Label<PICO_STR_L>(content.x + MARGIN, rowY(5), "");
    this->home_label->setFontSize(FontFn::Small);
    this->home_label->setMaxWidth(label_w);
    this->home_label->setMaxHeight(Label<PICO_STR_L>::GetLineHeight(FontFn::Small));
    WidgetFunctions::Add(this->home_label);
    this->refreshHomeLabel();

    // ---- 音量(sound.cfgの volume) ----
    // 現在値はSoundFunctionsが起動時にsound.cfgから読んだもの(=今鳴っている音量)を出す
    this->volume_title = new Label<PICO_STR_S>(content.x + MARGIN, (int16_t)(rowY(6) + 2), "音量");
    this->volume_title->setFontSize(FontFn::Small);
    WidgetFunctions::Add(this->volume_title);

    constexpr int16_t kVolumeTitleW = 40;
    const int16_t slider_x = (int16_t)(content.x + MARGIN + kVolumeTitleW);
    this->volume_slider = new NumberSlider(slider_x, rowY(6), (int16_t)(content.x + content.w - MARGIN - slider_x));
    this->volume_slider->setMinValue(0);
    this->volume_slider->setMaxValue(100);
    this->volume_slider->setDecimalPlacesNum(0);
    this->volume_applied = SoundFunctions::GetVolume();
    this->volume_dirty   = false;
    this->volume_slider->setValue((float)this->volume_applied);
    WidgetFunctions::Add(this->volume_slider);

    // ---- 起動時セルフチェック(本体はloadValues()より前で生成済み) ----
    WidgetFunctions::Add(this->run_test_checkbox);

    this->run_test_note = new Label<PICO_STR_M>(content.x + MARGIN, (int16_t)(rowY(7) + this->run_test_checkbox->getH() + 2), "次回の起動から反映されます");
    this->run_test_note->setFontSize(FontFn::Small);
    this->run_test_note->setTextColor(PICO_DARKGREY);
    WidgetFunctions::Add(this->run_test_note);

    // ---- タイムゾーン ----
    // 開いた時にドロップダウンの一覧が下の行(NTP等)へ重なるため、当たり判定・描画の
    // 両方で最前面に来るよう他の行より後にAdd()する(追加順=描画順、後が上に乗る)。
    // 見た目の並び順(2行目の下)とAdd()の順序は無関係なので、位置はここで決めてよい
    this->timezone_title = new Label<PICO_STR_M>(content.x + MARGIN, rowY(2), "タイムゾーン");
    this->timezone_title->setFontSize(FontFn::Small);

    constexpr int16_t kTzDropdownW = 110;
    const int16_t tz_x = (int16_t)(content.x + content.w - MARGIN - kTzDropdownW);
    this->timezone_dropdown = new DropdownMenu(tz_x, rowY(2), kTzDropdownW);
    for(int i = 0; i < this->tz_item_count; i++){
        this->timezone_dropdown->add(this->tz_items[i].c_str());
    }
    this->timezone_dropdown->setSelectedIndex(this->tz_selected_index);

    WidgetFunctions::Add(this->timezone_title);
    WidgetFunctions::Add(this->timezone_dropdown);
}

void SettingsScene::updateVolume(){
    if(!this->volume_slider) return;

    const int v = (int)(this->volume_slider->getValue() + 0.5f);
    if(v != this->volume_applied){
        this->volume_applied = v;
        SoundFunctions::SetVolume(v);
        this->volume_dirty = true;
    }

    // 指を離したら(スライダーの外で離した場合も含む)保存して、その音量で確認音を1回鳴らす
    if(this->volume_dirty && !OSData::isTouched){
        this->volume_dirty = false;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", this->volume_applied);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SOUND_CFG, "volume", buf);
        SoundFunctions::Beep(880, 120);
    }
}

void SettingsScene::onUpdate(){
    this->updateVolume();

    if(!this->timezone_dropdown) return;

    const int idx = this->timezone_dropdown->getSelectedIndex();
    if(idx < 0 || idx == this->tz_selected_index) return;
    if(idx >= this->tz_item_count) return;

    this->tz_selected_index = idx;

    PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "timezone", this->tz_items[idx].c_str());
    TimeFunctions::ApplyTimezone(this->tz_items[idx].c_str());
}

void SettingsScene::onExit(){
    // 離す前に画面を抜けた(Popされた)場合も、変えた音量は保存しておく
    if(this->volume_dirty){
        this->volume_dirty = false;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", this->volume_applied);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SOUND_CFG, "volume", buf);
    }

    this->back_button = nullptr;

    this->ssid_label           = nullptr;
    this->ssid_edit_button     = nullptr;
    this->password_label       = nullptr;
    this->password_edit_button = nullptr;
    this->timezone_title       = nullptr;
    this->timezone_dropdown    = nullptr;
    this->ntp1_label           = nullptr;
    this->ntp1_edit_button     = nullptr;
    this->ntp2_label           = nullptr;
    this->ntp2_edit_button     = nullptr;
    this->home_label           = nullptr;
    this->home_edit_button     = nullptr;
    this->volume_title         = nullptr;
    this->volume_slider        = nullptr;
    this->run_test_checkbox    = nullptr;
    this->run_test_note        = nullptr;

    this->edit_btn_x = 0;
}
