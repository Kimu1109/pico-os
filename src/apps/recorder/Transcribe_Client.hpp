#pragma once

#include "apps/recorder/Transcribe_Proto.hpp"
#include "net/Http_Request.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <SdFat.h>
#include <cstdint>

// 録音(WAV)を文字起こしのAPIへ送り、返ってきた文章を WAV の隣の .txt へ置く係。
// RecorderScene が値メンバとして持ち、毎フレーム update() を呼ぶ。オンラインが前提。
//
// - 送り先は OpenAI 互換の `POST <api>/audio/transcriptions`(multipart/form-data)。
//   /sys/transcribe.cfg の api を変えれば OpenAI・Groq・自前の whisper.cpp サーバ等へ向けられる
//   (既定は https://api.openai.com/v1 と gpt-4o-mini-transcribe)
// - APIキーは /sys/transcribe_key.cfg に暗号化して置く(PICO_SecretFile。OpenAIのキーは約164文字あり、
//   設定ファイルの1つの値には入らないため)。母艦で平文の1行を置いてもよく、読んだときに暗号化して書き直す。
//   キーの無い送り先(手元のサーバ)なら無くてもよい
// - **WAVはRAMへ載せない**: multipart の前後だけを作り、間はSDから少しずつ流す(HttpRequest::beginStream)
// - 返事は response_format=text で受け、200ならそのまま <名前>.txt.part へ書き、終わったら .txt へ入れ替える
//   (途中で失敗しても前の .txt は壊れない)。200以外は先頭の512Bだけ取って理由を拾う
// - 同時に1本だけ。繋いでいる間はTLSの約40KBを持ち、終わったら返す
class TranscribeClient : public IHttpSink, public IHttpBodySource {
    public:
        static constexpr const char* kDefaultApi = "https://api.openai.com/v1";
        static constexpr const char* kDefaultModel = "gpt-4o-mini-transcribe";
        static constexpr const char* kDefaultLanguage = "ja";
        // OpenAIの上限(25MB)。16kHzのモノラルで約13分、48kHzで約4分
        static constexpr uint32_t kMaxUploadBytes = 25UL * 1024UL * 1024UL;
        // 送り終えてから返事が来るまで待つ時間(長い録音は相手が考え込む)
        static constexpr unsigned long kResponseTimeoutMs = 180000;
        static constexpr size_t kMaxKeyBytes = 512;

        enum class State : uint8_t {
            NotConfigured,  // キーが無い・設定が読めない
            Ready,          // 送れる
            Sending,        // 録音を送っている(progressPercent())
            Waiting,        // 送り終えて、文字起こしの返事を待っている
            Done,           // 終わった(textPath() に置いた)
            Failed,         // 失敗した(message())
        };

        struct Config {
            FixedString<PICO_STR_LL> api;
            FixedString<PICO_STR_L> model;
            FixedString<PICO_STR_S> language;
            FixedString<PICO_STR_LL> prompt;
        };

        TranscribeClient() = default;
        TranscribeClient(const TranscribeClient&) = delete;
        TranscribeClient& operator=(const TranscribeClient&) = delete;

        // /sys/transcribe.cfg と /sys/transcribe_key.cfg を読む。送れる設定が揃えばtrue
        bool loadConfig();
        // 設定を直接与える(ホストテスト用。loadConfig() と同じ検査をする)
        bool configure(const Config& cfg, const char* key);
        // キーを暗号化して /sys/transcribe_key.cfg へ書く(画面から)
        static bool SaveKey(const char* key);

        // wav_path(/recordings/rec_0001.wav 等)を送る。始められなければ false(理由は message())
        bool start(const char* wav_path);
        void update(bool network_up);
        // やめる(書きかけの .part は消す)
        void cancel();

        State state() const { return state_; }
        bool busy() const { return state_ == State::Sending || state_ == State::Waiting; }
        // 送った割合(0〜100)
        int progressPercent() const;
        // 状態の説明(失敗の理由・案内)
        const char* message() const { return message_.c_str(); }
        // 文章を置いた(置く)ファイル
        const char* textPath() const { return text_path_.c_str(); }
        // 送っている WAV
        const char* wavPath() const { return wav_path_.c_str(); }
        const Config& config() const { return cfg_; }

        // rec_0001.wav → rec_0001.txt(.wav で終わらなければ false)
        static bool TextPathFor(const char* wav_path, char* out, size_t cap);

        // ---- IHttpSink(返事の本文) ----
        bool write(const void* data, size_t len) override;
        // ---- IHttpBodySource(送る本文 = head + WAV + tail) ----
        size_t size() const override;
        int read(uint8_t* buf, size_t cap) override;
        bool rewind() override;

    private:
        HttpRequest req_;
        Config cfg_;
        Url endpoint_;
        bool configured_ = false;
        State state_ = State::NotConfigured;
        FixedString<PICO_STR_512B> message_;

        FixedString<PICO_PATH_LEN> wav_path_;
        FixedString<PICO_PATH_LEN> text_path_;
        FsFile wav_;
        FsFile out_;
        bool out_failed_ = false;

        Transcribe::Head head_;
        Transcribe::Tail tail_;
        uint32_t wav_size_ = 0;
        size_t pos_ = 0;            // 送る本文のどこまで読んだか

        // 200以外の返事の先頭(理由を拾う)
        char err_body_[512] = {};
        size_t err_len_ = 0;

        void fail(const char* text);
        void finish();
        void closeFiles(bool keep_text);
        void partPath(char* out, size_t cap) const;
};
