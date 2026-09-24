#pragma once

#include "gui/scenes/Scene.hpp"
#include "gb/Gb_Emu.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

class GameBoyView;
class GameBoyPad;
class FileSelectDialog;

// Game Boy(DMG)エミュのアプリ。SUMMARY.md #9 の第1段(ROMを丸ごとRAMへ読む)。
//
//   ステータスバー(20px)
//   ゲーム画面 240x216(1.5倍、GameBoyView)
//   操作パッド 240x84(GameBoyPad)
//
// 開くとまずROMの選択(FileSelectDialog、最初は /gb/)を出す。キャンセルならランチャへ戻る。
// パッドの「ROM」で別のROMに差し替え、「戻る」でランチャへ戻る。
//
// ---- 寿命 ----
// エミュ(ROM・セーブ込み、最大で約310KB)はシーンが表に出ている間だけ持ち、
// onExit()で .sav を書き出してから全部返す。選んだROMのパスだけはメンバに残るので、
// 上へ別のシーンが載って戻ってきた場合は同じROMを最初から起動し直す
// (エミュの途中状態を退避する仕組みはまだ無い)。
//
// ---- 速さ ----
// ゲームボーイは約59.7フレーム/秒。onUpdate()でmillis()の差を積み、溜まったぶんだけ
// 進める(1回に最大2フレーム)。追いつけないぶんは捨てるので、重いときは遅く動くだけで
// 早送りで取り返そうとはしない。
class GameBoyScene : public Scene {
    private:
        GbEmu emu;

        GameBoyView* view = nullptr;
        GameBoyPad* pad = nullptr;
        FileSelectDialog* picker = nullptr;

        // 選んだROM(空 = まだ選んでいない)
        FixedString<PICO_PATH_LEN> rom_path;

        // ROM選択の結果。ダイアログのコールバックの中では読み込まず、次のonUpdate()で処理する
        // (閉じたダイアログの跡を1回描き直してから、重い読み込みに入るため)
        // OpenPicker: 開いた直後のROM選択。シーンの切り替えと同じフレームで半透明のダイアログを
        // 出すと、下(ランチャ)の描き直しが済む前の絵がダイアログの下に残るので1フレーム遅らせる
        enum class Pending : uint8_t { None, OpenPicker, Load, Cancelled };
        Pending pending = Pending::None;
        FixedString<PICO_PATH_LEN> pending_path;
        uint8_t pending_wait_frames = 0;

        uint32_t last_ms = 0;
        uint32_t acc_us = 0;
        bool crash_reported = false;

        // 速さの計測(実機で「フルスピードで動いているか」を見るため)。
        // 5秒ごとに、進めたフレーム数と追いつけずに捨てたフレーム数をログへ出す
        uint32_t stat_start_ms = 0;
        uint32_t stat_frames = 0;
        uint32_t stat_dropped = 0;

        void openPicker();
        void loadRom(const char* path);

    public:
        const char* getName() const override { return "GameBoy"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
};
