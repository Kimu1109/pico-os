#pragma once

#include <cstddef>
#include <cstdint>

#include "util/FixedString.hpp"
#include "consts.hpp"

// 音のファイルのタグ(曲名・アーティスト・アルバム・曲番号)を読む。ミュージックの索引(apps/music/Music_Library)が使う。
// docs/design/sound.md「曲のタグと索引」。
//
// - .mp3: 頭の ID3v2(v2.2 / v2.3 / v2.4)の TIT2・TPE1・TALB・TRCK(v2.2 は TT2・TP1・TAL・TRK)。
//   無い項目は終わりの ID3v1(128バイト。v1.1 の曲番号も)で埋める
// - 文字コード: ID3v2 の 0=ISO-8859-1 / 1=UTF-16(BOM付き) / 2=UTF-16BE / 3=UTF-8。
//   0 と ID3v1 は、日本のソフトが Shift_JIS をそのまま入れていることが多いので、Shift_JIS として正しく読めて
//   2バイト文字を含めば Shift_JIS、そうでなければ ISO-8859-1 として読む(MailCharset で UTF-8 へ)
// - **ファイル全体は読まない**。フレームの見出し(10バイト)を順に読み、要る4つの中身(各256バイトまで)だけを読んで、
//   ほかのフレーム(アルバムアート等)は飛ばす。4つそろえばそこで終える
// - 読めないもの(圧縮・暗号化したフレーム、v2.3 のタグ全体の非同期化)は読まずに ID3v1 へ回す
// - .flac: VORBIS_COMMENT の TITLE・ARTIST・ALBUM・TRACKNUMBER(キーは大小を区別しない。UTF-8)。頭の ID3v2 は飛ばす。
//   アルバムアート(PICTURE)等のブロックは中身を読まずに飛ばす
// - .m4a: iTunes形式(moov → udta → meta → ilst)の ©nam・©ART・©alb・trkn。それぞれの data atom(UTF-8)を読む。
//   アルバムアート(covr)等は読まない。moov が mdat の後ろ(末尾)にあっても見つける
// - .wav / .mml はタグを読まない(false。表示はファイル名)
struct AudioTags {
    FixedString<PICO_STR_L> title;
    FixedString<PICO_STR_L> artist;
    FixedString<PICO_STR_L> album;
    uint16_t track = 0;     // 曲番号(「3/12」なら3)。無ければ0

    bool empty() const { return title.empty() && artist.empty() && album.empty() && track == 0; }
};

namespace AudioTagReader {
    // SDの path からタグを読む。何か1つでも読めたら true(out は読めた項目だけ埋まる)
    bool Read(const char* path, AudioTags& out);

    // ---- 中身(ホストテスト用に出してある) ----
    // ID3v2 のテキストの中身(先頭の1バイトが文字コード)を UTF-8 へ直す。前後の空白と終端の0は落とす
    void DecodeText(const uint8_t* p, size_t n, FixedString<PICO_STR_L>& out);
    // 文字コードの印を付けない8bitの文字列(ID3v1・ID3v2の0)。Shift_JIS か ISO-8859-1 かを見分けて UTF-8 へ
    void DecodeLegacy(const uint8_t* p, size_t n, FixedString<PICO_STR_L>& out);
    // FLAC の VORBIS_COMMENT の1件(「TITLE=…」の形、終端の0は無し)を out へ。知らないキーは捨てる
    void DecodeVorbisComment(const uint8_t* p, size_t n, AudioTags& out);
    // 「3/12」→ 3。読めなければ 0
    uint16_t ParseTrackNumber(const char* s);
}
