#pragma once

#include "gui/core/Scene.hpp"
#include "gb/Gb_Emu.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

class GameBoyView;
class GameBoyPad;
class FileSelectDialog;
class PickerDialog;
class GbRomFlash;

// Game Boy(DMG)エミュのアプリ。SUMMARY.md #9 の第1段(ROMを丸ごとRAMへ読む)。
//
//   ステータスバー(20px)
//   ゲーム画面 240x216(1.5倍、GameBoyView)
//   操作パッド 240x84(GameBoyPad)
//
// 開くとまずROMの選択(FileSelectDialog、最初は /gb/)を出す。キャンセルならランチャへ戻る。
// パッドの「MENU」(コントローラーのZL)で途中保存/途中から再開/別のROM、「戻る」でランチャへ戻る。
//
// ---- 途中保存(GbEmu::saveState) ----
//   - 置き場は /gb/states/<ROMの名前>.st0 / .st1(ROMの選択に並ばないよう分けた)
//   - 手動の枠(.st1): MENUの「途中保存」「途中から再開」
//   - 自動の枠(.st0): 画面を離れるとき(戻る・HOME・電源メニューの再起動)・ROMを替えるとき・
//     電源メニューでスリープへ入ったとき・電池が少ない間は kLowBatteryAutoSaveMs ごとに書く。
//     ROMを開いたとき自動の枠があれば「前回の続きから / 最初から」を聞く
//   - ゲームの中のセーブ(カートリッジRAM、.sav)は、書き込みが落ち着いたら遊んでいる途中でも書き出す
//     (GbEmu::flushSaveIfIdle。以前は「戻る」を押すまで書かず、電池切れで消えた)
//
// ---- ROMの置き場 ----
// 256KBまでのROMは今までどおりRAMへ丸ごと読む(GbEmu::load)。それより大きいROM(とRAMが足りなかったとき)は、
// 内蔵フラッシュ(ファームの更新のLittleFSの領域を間借り。gb/Gb_Rom_Flash)へ書いてそこから読む。
// 書くのは初回だけで、同じROMなら次から書かずに始まる。書いている間は画面に進み具合を出し、エミュは止めておく
// (消す/書く間は2コア目が止まるので、先に音を止める)。/sys/gb.cfg の rom-in-flash で変えられる:
//   auto(既定) … 上のとおり / always … 小さいROMも内蔵フラッシュから読む(RAMを使わない)/ never … 使わない
//
// ---- 寿命 ----
// エミュ(ROM・セーブ込み、最大で約310KB)はシーンが表に出ている間だけ持ち、
// onExit()で自動の枠と .sav を書き出してから全部返す。選んだROMのパスだけはメンバに残るので、
// 上へ別のシーンが載って戻ってきた場合は同じROMを開き直し、自動の枠から続ける。
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
        PickerDialog* menu = nullptr;

        // 選んだROM(空 = まだ選んでいない)
        FixedString<PICO_PATH_LEN> rom_path;

        // ROM選択の結果。ダイアログのコールバックの中では読み込まず、次のonUpdate()で処理する
        // (閉じたダイアログの跡を1回描き直してから、重い読み込みに入るため)
        // OpenPicker: 開いた直後のROM選択。シーンの切り替えと同じフレームで半透明のダイアログを
        // 出すと、下(ランチャ)の描き直しが済む前の絵がダイアログの下に残るので1フレーム遅らせる
        enum class Pending : uint8_t { None, OpenPicker, Load, Cancelled,
                                       AskResume, ResumeAuto, SaveManual, LoadManual };
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

        // 上端の1行(途中保存の結果)を消す時刻。0 = 出していない
        uint32_t banner_until_ms = 0;
        bool was_sleeping = false;
        uint32_t last_low_battery_save_ms = 0;
        // 戻ってきたとき(Push()から)は聞かずに自動の枠から続ける
        bool resume_silently = false;

        // 内蔵フラッシュへROMを書いている間だけ new(約4.4KB)
        GbRomFlash* flash_job = nullptr;
        FixedString<PICO_PATH_LEN> flash_path;
        bool flash_resume_silently = false;
        uint32_t flash_last_percent = 0;
        enum class RomMode : uint8_t { Auto, Always, Never };
        RomMode romMode() const;
        bool startFlash(const char* path);
        void stepFlash();
        void stopFlash();
        void afterLoad(const char* path);

        void openPicker();
        void openMenu();
        void askResume();
        void loadRom(const char* path);
        void autoSave(const char* why);
        void showBanner(const char* text);

    public:
        constexpr static uint32_t kBannerMs = 1800;
        constexpr static uint32_t kLowBatteryAutoSaveMs = 60000;
        constexpr static int kLowBatteryPercent = 10;
        // 内蔵フラッシュへ書くとき、1フレームに使う時間(64KBの消去は1回でこれを超える)
        constexpr static uint32_t kFlashStepMs = 40;

        ~GameBoyScene() override;

        const char* getName() const override { return "GameBoy"; }
        // 画面の上部(ゲームの画面)をトーストで隠さない
        bool quietNotifications() const override { return true; }
        // 離れるとROMを閉じる(途中の状態は自動の枠へ保存する)
        bool keepForeground() const override { return true; }
        bool usesPad() const override { return true; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
};
