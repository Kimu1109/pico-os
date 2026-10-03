#pragma once

#include "lua/LuaDebugger.hpp"

// Luaデバッガの画面(LuaDebugger::Frontendの実機/PC版)。
//
// 止まっている間はOSのloop()に戻れない(Luaのフックの中にいる)ので、ウィジェットは使わず
// 液晶(OSData::lcd)へ直接描き、タッチ・コントローラー・物理キーボード・シリアルを自分で読む
// 小さなループを回す。続けるときは止まる前の画面(OSData::frame)を液晶へ戻し、次のフレームで
// 全体を描き直してもらう(LuaDebugger::RequestRedraw())。
//
//   ┌ 停止: ブレークポイント ────────┐ 赤い帯(エラーのときはメッセージも)
//   │ main.lua:42  update               │ 場所と関数名
//   │  40  local x = 1                  │ ソース(SDのファイルから前後2行。止まった行は黄色)
//   │  41  ...                          │
//   │ スタック(タップで段を選ぶ)       │
//   │ 変数(タップで次のページ)         │ ローカル変数と上位値(^付き)
//   │ [続行][1行][次へ][抜ける][停止]   │
//   └───────────────────────────────┘
// キー: c/Enter=続行 s=1行 n=次へ o=抜ける q=停止。コントローラー: A=続行 下=1行 右=次へ 上=抜ける HOME=停止。
// シリアル: dbg c / s / n / o / q / bt / locals。
class LuaDebugScreen : public LuaDebugger::Frontend {
    public:
        LuaDebugger::Command onPause(LuaDebugger& dbg, lua_State* L, LuaDebugger::PauseInfo& info) override;

    private:
        static constexpr int kSourceLines = 5;
        FixedString<PICO_STR_L> source_[kSourceLines];
        int source_first_ = 0;
        int source_count_ = 0;
        int var_page_ = 0;

        void draw(const LuaDebugger::PauseInfo& info, int selected_frame);
};
