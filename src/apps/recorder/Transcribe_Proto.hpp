#pragma once

#include "util/Json_Reader.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstddef>
#include <cstdint>

// 文字起こしのAPI(OpenAI互換の POST <api>/audio/transcriptions)の送り方と返事の読み方。
// 通信はしない(TranscribeClient が使う。ホストテストで全部を確かめられるように分けてある)。
//
// 送るのは multipart/form-data:
//   model / language(空なら送らない=自動判定)/ prompt(空なら送らない)/ response_format=text / file
// file の中身(WAV)はここでは扱わない。前(head)と後ろ(tail)だけを作り、間にファイルを流すのは呼ぶ側。
// response_format=text にするのは、成功の返事が本文そのもの(JSONではない)になり、長い文章でも
// そのままSDへ流せるため(JSONの文字列を丸ごとRAMへ載せずに済む)。
namespace Transcribe {
    // multipart の前の部分の置き場。model/prompt 等の値を含むので余裕を持たせる
    using Head = FixedString<PICO_STR_1KiB>;
    using Tail = FixedString<PICO_STR_M>;

    struct Fields {
        const char* model = "";
        const char* language = "";  // "ja" 等。空なら送らない(相手が自動で判定する)
        const char* prompt = "";    // 言葉づかい・固有名詞のヒント。空なら送らない
    };

    // 区切り(boundary)を作る。seed は毎回変わる値(millis() 等)。out は24バイト以上
    void MakeBoundary(char* out, size_t cap, uint32_t seed);

    // 送る値として使えるか(改行・制御文字・" を含まない)。名前のヘッダや区切りを壊さないため
    bool SafeFieldValue(const char* s);

    // file の手前までを作る。filename は "rec_0001.wav" 等(パスは含めない)。
    // 値が使えない/入りきらなければ false
    bool BuildHead(Head& out, const char* boundary, const Fields& f, const char* filename);
    // file の後ろ(終わりの区切り)
    bool BuildTail(Tail& out, const char* boundary);

    // 失敗の返事(JSON)から理由を拾う。
    //   OpenAI/Groq: {"error":{"message":"…"}}  whisper.cpp: {"error":"…"}  その他: {"detail":"…"} / {"message":"…"}
    // JSONでなければ本文の1行目を理由にする
    class ErrorParser : public JsonReader::Handler {
        public:
            void begin();
            void feed(const void* data, size_t len);
            // 拾えた理由(無ければ空)
            const char* message() const;

            void onValue(const char* key, JsonReader::Type type, const char* text, size_t len,
                         bool truncated, int depth) override;
            void onBegin(const char* key, bool is_array, int depth) override;
            void onEnd(bool is_array, int depth) override;

        private:
            JsonReader reader_;
            FixedString<PICO_STR_256B> msg_;
            FixedString<PICO_STR_256B> first_line_;
            bool first_line_done_ = false;
            bool in_error_ = false;     // "error": { … } の中
            uint8_t rank_ = 0;          // 拾った理由の確からしさ(高いものを残す)
    };

    // HTTPの状態番号と拾った理由から、画面に出す短い日本語を作る
    void DescribeFailure(char* out, size_t cap, int status, const char* reason);
}
