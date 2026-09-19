#pragma once

#include <cstdint>
#include "util/FixedString.hpp"
#include "consts.hpp"

class Widget;

// ウィジェットの値をWidgetType非依存の共通口から読み書きするための橋渡し。
// WidgetFactory::Create()と対になる「生成したウィジェットの中身を読み書きする」側で、
// Lua等の外部からは型ごとに専用バインディングを書かず、この1組の関数だけを通す
// (CLAUDE.md「Lua着手前の受け皿の状態」の「プロパティのget/set共通口」に対応)。
//
// Widget基底の仮想関数にしなかった理由: setW()/setH()はWidget基底に存在せず
// (LayoutContainer/GridContainerのコメント参照)、ウィジェットごとにシグネチャも
// 意味も異なる。無理に共通の仮想I/Fへ揃えると基底が汚れるため、WidgetFactoryと
// 同じ「WidgetType→switch」の形にしてある。
//
// 対応範囲: WidgetFactory::IsCreatable()がtrueを返す汎用部品15種のみ。
// widgets/apps・systems・dialogsの専用ウィジェットは対象外(そちらはOS内部の
// C++コードが直接メンバ関数を呼べるので、共通口を必要としない)。
namespace WidgetProperty {

    // プロパティの種類。ウィジェットをまたいで意味が同じものは1つのIdを共有する
    // (例: Text はButton/Label/Textbox/Checkboxで共通)。
    enum class Id : uint16_t {
        // Widget基底が持つ共通プロパティ(get/setとも常に対応。setW/setHは
        // 実際にsetW/setHを持つ型のみ)
        X, Y, W, H, Visible, BackgroundColor,

        // テキスト系
        Text, Placeholder, FontSize, TextColor, BorderColor,
        MaxWidth, MaxHeight, TextAlign, IsSingleLine,

        // 値系
        Value, MinValue, MaxValue, Checked, SelectedIndex, DecimalPlaces, VisibleNum,

        // Icon
        IconId, IconSize, IconOpaque, Color,

        // Image
        Path,

        // CanvasRaster
        BrushRadius, CanvasMode,

        // LayoutContainer / GridContainer
        Gap, Padding, Direction, CrossAlign, Cols, HAlign, VAlign,

        // TabBar
        TabSelected, TabCount,

        // ScrollList
        EnableIcon,

        Count
    };

    enum class Type : uint8_t { Int, Float, Bool, Str };

    // プロパティの値そのもの。ヒープを使わない固定長(FixedStringはbuf_[N]のみ)。
    // 文字列はPICO_STR_L(96B)まで。Image::Pathのように長いパスを渡す場合、
    // PICO_PATH_LEN(255B)より長い文字列はFixedStringの切り詰めルールに従って切られる。
    struct Value {
        Type type = Type::Int;
        int32_t i = 0;
        float f = 0.0f;
        bool b = false;
        // Image::Pathのような長いパスも切り詰めずに扱えるよう、PICO_STR_Lではなく
        // PICO_PATH_LEN(255B)を容量にしてある(他の短いテキストプロパティにとっても
        // 上位互換の器になるだけで問題は無い)。
        FixedString<PICO_PATH_LEN> s;

        static Value MakeInt(int32_t v)   { Value r; r.type = Type::Int;   r.i = v; return r; }
        static Value MakeFloat(float v)   { Value r; r.type = Type::Float; r.f = v; return r; }
        static Value MakeBool(bool v)     { Value r; r.type = Type::Bool;  r.b = v; return r; }
        static Value MakeStr(const char* v) { Value r; r.type = Type::Str; r.s.assign(v); return r; }
    };

    // widgetがnullptr、idが対象のwidgetTypeで非対応、のいずれかならfalseを返す(outは書き換えない)。
    bool Get(Widget* widget, Id id, Value& out);

    // widgetがnullptr、idが非対応、valueのtypeが期待と不一致、のいずれかならfalseを返す。
    bool Set(Widget* widget, Id id, const Value& value);
};
