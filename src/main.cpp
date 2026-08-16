#include <Arduino.h>

#include "functions/SD_Functions.hpp"
#include "functions/Touch_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/IME_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"

#include "widgets/Label.hpp"
#include "widgets/Button.hpp"
#include "widgets/ScrollList.hpp"
#include "widgets/Icon.hpp"
#include "widgets/Checkbox.hpp"
#include "widgets/Image.hpp"
#include "widgets/Textbox.hpp"
#include "widgets/NumberSlider.hpp"

#include "OS_Data.hpp"
#include <SPI.h>

//デバッグ用
static Label* sd_label;
static Button* hi_button;
static ScrollList* scroll;
static Icon* iconTest16;
static Icon* iconTest24;
static Icon* iconTest32;
static Icon* iconTest48;
static Icon* iconTest64;
static Checkbox* checkedCheckbox;
static Image* dolphin;
static Textbox* textbox;
static NumberSlider* slider_w;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    PICO_GFX::Setup();
    PICO_Touch::Setup();

    hi_button = new Button(160, 100, "HI!");
    hi_button->SetFontSize(FontFn::FontSize::Big);
    hi_button->SetBorderColor(PICO_PURPLE);

    sd_label = new Label(0, 50, "sd-failed");
    scroll = new ScrollList(10, 80, 150, 100, 2);
    scroll->Add("こんにちは");
    scroll->Add("Hello!");
    scroll->Add("グーデンターク");
    scroll->Add("コンギョサムニダ");
    scroll->Add("ニーハオ");
    scroll->Add("チマチョゴリ");
    scroll->Add("グーパンダック");
    scroll->Add("こんちくは!");
    scroll->SetFontSize(FontFn::FontSize::Small);
    scroll->SetBorderColor(PICO_RED);
    scroll->BackgroundColor(PICO_BLUE);
    scroll->SetTextColor(PICO_GREEN);

    iconTest16 = new Icon(0, 200, IconID::AppBox, IconSize::Px16);
    iconTest24 = new Icon(16 + 4, 200, IconID::AppBox, IconSize::Px24);
    iconTest32 = new Icon(16 + 24 + 4 * 2, 200, IconID::AppBox, IconSize::Px32);
    iconTest48 = new Icon(16 + 24 + 32 + 4 * 3, 200, IconID::AppBox, IconSize::Px48);
    iconTest64 = new Icon(16 + 24 + 32 + 48 + 4 * 4, 200, IconID::AppBox, IconSize::Px64);

    checkedCheckbox = new Checkbox(0, 270, "二重確認した?");
    checkedCheckbox->SetFontSize(FontFn::FontSize::Small);
    textbox = new Textbox("hi!", 130, 10, 100, 64, false);
    textbox->SetFontSize(FontFn::FontSize::Small);
    textbox->BackgroundColor(PICO_DARKGREEN);
    textbox->SetBorderColor(PICO_DARKCYAN);

    slider_w = new NumberSlider(10, 320 - 30, 120);

    WidgetFunctions::add(hi_button);
    WidgetFunctions::add(sd_label);
    WidgetFunctions::add(scroll);
    WidgetFunctions::add(iconTest16);
    WidgetFunctions::add(iconTest24);
    WidgetFunctions::add(iconTest32);
    WidgetFunctions::add(iconTest48);
    WidgetFunctions::add(iconTest64);
    WidgetFunctions::add(checkedCheckbox);
    WidgetFunctions::add(textbox);
    WidgetFunctions::add(slider_w);

    if(PICO_SD::Setup()){
        sd_label->Text(PICO_SD::ReadTextFileFast("/test.txt"));

        dolphin = new Image("dolphin.pimg", 0, 0, true);
        WidgetFunctions::add(dolphin);
    }

    hi_button->onPressStart([]() {
        int iconIdInt = static_cast<int>(iconTest16->GetIconId());
        iconIdInt++;
        if(iconIdInt >= static_cast<int>(IconID::IconCount)) iconIdInt = 0;

        const IconID iconId = static_cast<IconID>(iconIdInt);
        iconTest16->SetIconId(iconId);
        iconTest24->SetIconId(iconId);
        iconTest32->SetIconId(iconId);
        iconTest48->SetIconId(iconId);
        iconTest64->SetIconId(iconId);
    });

    KeyboardFunctions::Setup();
    IME_Functions::setup();

    pinMode(LED_BUILTIN, HIGH);
}

void loop() {
    PICO_Touch::Update();
    
    WidgetFunctions::updateAll();

    PICO_GFX::flushDirty();
}