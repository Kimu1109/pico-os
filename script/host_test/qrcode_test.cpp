// QRCodeView(QRコードの表示部品)と、それが使う lib/qrcodegen を確かめるテスト。
//
//   - 升目の数(版)が文字列の長さ・誤り訂正の強さに応じて決まる。255バイトは誤り訂正Highでも入る
//   - 形式情報(左上の切り出し記号の周りの15ビット)を読み戻すと、BCH符号として正しく、
//     誤り訂正の段が頼んだもの以上(升目が増えない範囲で強める)になっている
//   - 3隅の切り出し記号・タイミングパターンが規格どおり
//   - render()がframeへ升目どおりに描く(余白・中央寄せ・升の大きさ)。パレットで明るい方を地にする
//   - 長すぎる・途中にNULのある文字列は切らずに空にしてfalse
//   - プロパティの共通口(pico.set/get の "text" / "ecc" / "color" / "qr_size")
//
// 実際に読み取れることは、PCビルドの画面写真をOpenCVで読んで確かめた(docs/design/gui.md「QRコード」)。
#include "gui/widgets/QRCodeView.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "OS_Data.hpp"
#include <cstdio>
#include <cstring>
#include <string>

// ---- モック(widget_property_test.cppと同じ) ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::Show(ITextInputTarget*, KeyboardFunctions::Layout, bool){}
void KeyboardFunctions::OnPanelShown(KeyboardPanel*){}
void KeyboardFunctions::OnPanelHidden(KeyboardPanel*){}
void KeyboardFunctions::OnPanelResized(KeyboardPanel*){}
void KeyboardFunctions::OnPanelChanged(KeyboardPanel*, bool){}
void KeyboardFunctions::HideAll(){}

namespace WP = WidgetProperty;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// 形式情報を左上から読む(規格 7.9)。マスク 0x5412 を外した15ビットを返す。
// 縦の列 x=8 の y=0..5, 7, 8 がビット0..7、横の行 y=8 の x=7, 5..0 がビット8..14
static int ReadFormatBits(const QRCodeView& q){
    int bits = 0, i = 0;
    for(int y = 0; y <= 5; y++) bits |= q.getModule(8, y) << i++;
    bits |= q.getModule(8, 7) << i++;
    bits |= q.getModule(8, 8) << i++;
    bits |= q.getModule(7, 8) << i++;
    for(int x = 5; x >= 0; x--) bits |= q.getModule(x, 8) << i++;
    return bits ^ 0x5412;
}

// 5ビットのデータに対するBCH(15,5)の10ビットの検査符号
static int Bch(int data){
    int rem = data;
    for(int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    return (data << 10) | (rem & 0x3FF);
}

// 形式情報の上2ビット → 誤り訂正の段(L=01, M=00, Q=11, H=10)
static int EccFromFormat(int bits){
    switch((bits >> 13) & 3){
        case 1: return 0;
        case 0: return 1;
        case 3: return 2;
        default: return 3;
    }
}

static bool FinderAt(const QRCodeView& q, int ox, int oy){
    for(int y = 0; y < 7; y++){
        for(int x = 0; x < 7; x++){
            const int d = std::max(std::abs(x - 3), std::abs(y - 3));
            const bool want = d != 2;   // 外枠(3)・中の白(2)・芯(0,1)
            if(q.getModule(ox + x, oy + y) != want) return false;
        }
    }
    return true;
}

int main(){
    // ---- 符号化の基本 ----
    {
        QRCodeView q(0, 0, 100);
        check(q.getModuleCount() == 0, "空なら升目は0");
        check(q.setText("https://example.com/"), "URLを符号化できる");
        const int n = q.getModuleCount();
        check(n == 25, "20バイト・誤り訂正Mは版2(25x25)");
        check(FinderAt(q, 0, 0) && FinderAt(q, n - 7, 0) && FinderAt(q, 0, n - 7), "3隅の切り出し記号");
        bool timing = true;
        for(int i = 8; i < n - 8; i++){
            if(q.getModule(i, 6) != (i % 2 == 0) || q.getModule(6, i) != (i % 2 == 0)) timing = false;
        }
        check(timing, "タイミングパターン(6行目・6列目の白黒交互)");
        const int f = ReadFormatBits(q);
        check(Bch(f >> 10) == f, "形式情報はBCH符号として正しい");
        check(EccFromFormat(f) >= 1, "誤り訂正は頼んだM以上");
        check(!q.getModule(-1, 0) && !q.getModule(n, 0), "範囲外の升目はfalse");

        check(q.setText(""), "空文字はtrue");
        check(q.getModuleCount() == 0, "空文字で升目は0に戻る");
    }

    // ---- 数字だけは詰めた形になる(同じ長さでもバイト列より升目が少ない) ----
    {
        QRCodeView digits(0, 0, 100), bytes(0, 0, 100);
        digits.setText("12345678901234567890123456789012345678901234567890");
        bytes.setText("abcdefghijabcdefghijabcdefghijabcdefghijabcdefghij");
        check(digits.getModuleCount() < bytes.getModuleCount(), "数字だけの50文字はバイト列より小さい版");
    }

    // ---- 誤り訂正を強めると升目が増える。形式情報に段が載る ----
    {
        QRCodeView q(0, 0, 100);
        const char* text = "WIFI:T:WPA;S:picoos-setup;P:12345678;;";
        q.setText(text);
        const int n_m = q.getModuleCount();
        check(q.setEcc(QRCodeView::Ecc::High), "誤り訂正Highへ");
        check(q.getEcc() == QRCodeView::Ecc::High, "getEccは頼んだ段");
        check(q.getModuleCount() > n_m, "Highは升目が増える");
        check(EccFromFormat(ReadFormatBits(q)) == 3, "形式情報はH");
        check(std::strcmp(q.getText(), text) == 0, "文字列は保たれる");
    }

    // ---- 255バイトまでは誤り訂正Highでも入る。超えたら空にしてfalse ----
    {
        QRCodeView q(0, 0, 100);
        q.setEcc(QRCodeView::Ecc::High);
        std::string s(255, 'x');
        s[0] = '\xE3'; s[1] = '\x81'; s[2] = '\x82';   // 「あ」(バイト列になる)
        check(q.setText(s.c_str(), s.size()), "255バイトは誤り訂正Highで入る");
        check(q.getModuleCount() == 4 * QRCodeView::kMaxVersion + 17, "そのときが最大の版");
        std::string over(256, 'x');
        check(!q.setText(over.c_str(), over.size()), "256バイトはfalse");
        check(q.getModuleCount() == 0 && q.getText()[0] == '\0', "失敗したら切らずに空");
        const char with_nul[] = {'a', '\0', 'b'};
        check(!q.setText(with_nul, 3), "途中のNULはfalse");
        check(q.getModuleCount() == 0, "途中のNULでも空");
    }

    // ---- 描画: 升目どおり・中央・升を大きく ----
    {
        OSData::frame->createSprite(240, 320);
        OSData::frame->clear(3);
        QRCodeView q(10, 20, 120);
        q.setH(100);
        q.setText("HELLO");
        const int n = q.getModuleCount();   // 21
        q.render();
        // 短い辺100 / (21+余白1升x2) = 4px(余白は2升まで取れる)。中央寄せ: x = 10 + (120-84)/2
        const int scale = 100 / (n + 2);
        const int ox = 10 + (120 - n * scale) / 2, oy = 20 + (100 - n * scale) / 2;
        bool same = true;
        for(int y = 0; y < n; y++){
            for(int x = 0; x < n; x++){
                const int want = q.getModule(x, y) ? PICO_BLACK : PICO_WHITE;
                for(int dy = 0; dy < scale; dy++)
                    for(int dx = 0; dx < scale; dx++)
                        if((int)OSData::frame->readPixelValue(ox + x * scale + dx, oy + y * scale + dy) != want) same = false;
            }
        }
        check(scale == 4, "升1つは4px");
        check(same, "升目どおりに描く");
        check(OSData::frame->readPixelValue(10, 20) == PICO_WHITE && OSData::frame->readPixelValue(129, 119) == PICO_WHITE,
              "矩形の隅は地の色(余白)");
        check(OSData::frame->readPixelValue(9, 20) == 3 && OSData::frame->readPixelValue(130, 20) == 3, "矩形の外は描かない");

        // 小さい矩形では余白を削る(21升+余白2升x2=25px → 1px)
        QRCodeView small(0, 0, 25);
        small.setText("HELLO");
        OSData::frame->clear(3);
        small.render();
        check(OSData::frame->readPixelValue(2, 2) == PICO_BLACK && OSData::frame->readPixelValue(1, 2) == PICO_WHITE,
              "25pxなら余白2升で左上の切り出し記号が(2,2)から");
        // 規格どおり4升の余白が取れるなら取る(21+8=29px → 1px・余白4升)
        QRCodeView roomy(0, 0, 29);
        roomy.setText("HELLO");
        OSData::frame->clear(3);
        roomy.render();
        check(OSData::frame->readPixelValue(4, 4) == PICO_BLACK && OSData::frame->readPixelValue(3, 4) == PICO_WHITE,
              "29pxなら余白4升");

        // ダークテーマ(0が明るく15が暗い)でも明るい地に暗い升目
        for(int i = 0; i < 16; i++) PICO_GFX::COLORS[i] = PICO_GFX::DARK_COLORS[i];
        OSData::frame->clear(3);
        q.renderForce();
        q.render();
        check(OSData::frame->readPixelValue(10, 20) == PICO_BLACK, "ダークテーマでは地に0(明るい方)");
        check(OSData::frame->readPixelValue(ox, oy) == PICO_WHITE, "ダークテーマでは升目に15(暗い方)");
        for(int i = 0; i < 16; i++) PICO_GFX::COLORS[i] = PICO_GFX::DEFAULT_COLORS[i];
        OSData::frame->deleteSprite();
    }

    // ---- プロパティの共通口(Luaの pico.set / pico.get) ----
    {
        WidgetType t;
        check(WidgetFactory::TypeFromName("QRCode", t) && t == WidgetType::QRCodeView, "Luaの名前はQRCode");
        Widget* w = WidgetFactory::Create(WidgetType::QRCodeView);
        check(w != nullptr && w->getHitTransparent(), "生成できて、タップは素通り");
        WP::Value v;
        check(WP::Get(w, WP::Id::QrSize, v) && v.i == 0, "qr_sizeは空なら0");
        check(WP::Set(w, WP::Id::Text, WP::Value::MakeStr("pico-os")), "text set");
        check(WP::Get(w, WP::Id::Text, v) && std::strcmp(v.s.c_str(), "pico-os") == 0, "text get");
        check(WP::Get(w, WP::Id::QrSize, v) && v.i == 21, "qr_size = 21");
        check(WP::Set(w, WP::Id::Ecc, WP::Value::MakeInt(3)), "ecc set");
        check(WP::Get(w, WP::Id::Ecc, v) && v.i == 3, "ecc get");
        check(!WP::Set(w, WP::Id::Ecc, WP::Value::MakeInt(4)), "eccの範囲外はfalse");
        check(!WP::Set(w, WP::Id::Text, WP::Value::MakeInt(1)), "textの型不一致はfalse");
        check(WP::Set(w, WP::Id::Color, WP::Value::MakeInt(PICO_NAVY)) && WP::Get(w, WP::Id::Color, v) && v.i == PICO_NAVY, "color");
        check(WP::Set(w, WP::Id::W, WP::Value::MakeInt(150)) && w->getW() == 150, "W set");
        check(WP::Get(w, WP::Id::BackgroundColor, v) && v.i == PICO_WHITE, "地の色の既定は白");
        WP::Id id;
        check(WP::IdFromName("qr_size", id) && id == WP::Id::QrSize && WP::IdFromName("ecc", id) && id == WP::Id::Ecc,
              "プロパティ名 qr_size / ecc");
        delete w;
    }

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
