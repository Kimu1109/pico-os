#pragma once

#include "gui/widgets/interfaces/ITextInputTarget.hpp"

class KeyboardPanel;

// オンスクリーンキーボード(OS常駐のオーバーレイ)の窓口。
//
// キーボードは「キー盤」(KeyboardPanel派生の3種。OSData::keyboard_jpn/eng/num)と
// 「ダイアログ枠」(KeyboardDialog。背景の斜線と上部の入力欄)に分かれている。
//   - Show(target)               … 従来どおりダイアログとして開く(Textbox等)
//   - Show(target, layout, true) … キー盤だけを画面下へ据え置く(テキストエディタ等。
//                                    入力先はonDisplayChanged()で自分の画面へ反映する)
namespace KeyboardFunctions {
    enum class Layout { Japanese, English, Number };

    void Setup();

    // targetへ入力するキーボードを開く。開いているキーボードがあれば先に閉じる
    void Show(ITextInputTarget* target, Layout layout = Layout::Japanese, bool docked = false);

    void RegisterInputTarget(ITextInputTarget *target);
    void UnregisterInputTarget(ITextInputTarget *target);

    // 表示中のキーボードを全て閉じる。
    // キーボードはオーバーレイ常駐なのでシーン遷移では破棄されないが、
    // 入力対象(シーン内のTextbox等)が破棄される前にonHideを届けておく必要がある
    void HideAll();

    bool IsVisible();
    // 据え置き表示で開いているか
    bool IsDocked();
    // 表示中のキー盤(無ければnullptr)。物理キーボードの打鍵の届け先
    KeyboardPanel* VisiblePanel();
    // 表示中のキー盤の上端のy座標(表示していなければSCREEN_HEIGHT)
    int VisibleTop();

    // 日本語⇔英字の切り替え。onShow/onHideを挟まずにテキストとカーソルを引き継ぐ
    void SwitchPanel(KeyboardPanel* from, KeyboardPanel* to);

    // ---- KeyboardPanelから呼ばれる ----
    void OnPanelShown(KeyboardPanel* panel);
    void OnPanelHidden(KeyboardPanel* panel);
    void OnPanelResized(KeyboardPanel* panel);
    void OnPanelChanged(KeyboardPanel* panel, bool text_changed);
}
