#pragma once

#include <cstddef>
#include <cstdint>

#include "util/FixedString.hpp"
#include "consts.hpp"

// Webに置いたファームの版の一覧(index.tsv、docs/FIRMWARE_UPDATE.md「Webから取る」)を読む係。
// 作る側は script/build_fw_index.py(CIが GitHub Pages の /firmware/ へ置く)。値を変えるときは両方を揃えること。
//
//   # pico-os-fw-index 1
//   <通し番号>\t<git>\t<日時(UTC)>\t<stable|nightly>\t<構成>\t<署名 1|0>\t<バイト数>\t<SHA-256>\t<ファイル名>
//
// 一覧そのものには署名しない。書き込む前に、落とした更新ファイルの署名(焼き込みの鍵)を
// Fw_Update がいつも通り確かめるため。偽の一覧でできるのは古い署名済みの版を勧めることくらいで、
// それは「古い版は本体で確認する」で止まる。SHA-256 は落としたものが壊れていないかを書き込む前に見るためのもの。
//
// 列が増えても読めるように、10列目より後ろは読み飛ばす。読めない行・知らない種類の行も飛ばす(数だけ数える)。
// 通信もSDも持たないので、ホストテスト(fw_index_test)で全部の経路を確かめられる。
namespace FwIndex {
    constexpr const char* kMagic = "# pico-os-fw-index 1";
    constexpr const char* kFileName = "index.tsv";
    constexpr int kMaxEntries = 16;
    // 一覧の大きさの上限(1行は200バイトほど。16行で約3KB)
    constexpr size_t kMaxBytes = 8192;
    // 1行の上限(超える行は読めない行として飛ばす)
    constexpr size_t kMaxLineBytes = 320;

    enum class Channel : uint8_t { Stable, Nightly };
    // 一覧の選択肢に出す短い名前("安定" / "夜間"。夜間 = ナイトリービルド)
    const char* ChannelText(Channel c);

    struct Entry {
        uint32_t serial = 0;
        FixedString<PICO_STR_S> git;   // 短いコミットのハッシュ(最大16文字)
        FixedString<PICO_STR_S> date;  // "2026-10-16 12:34"(UTC)
        Channel channel = Channel::Nightly;
        FixedString<PICO_STR_M> env;   // ビルドの構成(platformio.ini の env)
        bool signed_flag = false;
        uint32_t bytes = 0;            // 更新ファイル(.pfw)の大きさ
        uint8_t sha256[32] = {};       // 更新ファイル全体のSHA-256
        FixedString<PICO_STR_L> file;  // 一覧と同じ場所に置いたファイルの名前
    };

    // どの行を残すか
    struct Filter {
        const char* env = "";        // この構成のものだけ
        bool allow_unsigned = false; // 署名なしも残す(/sys/debug.cfg の fw-allow-unsigned)
    };

    struct Result {
        int count = 0;            // 残した行
        int hidden_env = 0;       // 構成が違うので外した行
        int hidden_unsigned = 0;  // 署名なしなので外した行
        int bad_lines = 0;        // 読めない行(列が足りない・値がおかしい・知らない種類)
        int overflow = 0;         // 残せる数(cap)を超えて捨てた行
    };

    // text(len バイト、NUL終端でなくてよい)を読み、残す行を out[0..cap) へ並び順のまま入れる。
    // 1行目が kMagic でなければ false(一覧ではない・形式の版が違う)
    bool Parse(const char* text, size_t len, const Filter& filter, Entry* out, int cap, Result& result);

    // 落として置いてよいファイル名か(英数字と . _ - だけ、.pfw で終わる、. で始まらない、PICO_STR_L 未満)
    bool SafeFileName(const char* name);
}
