#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/TextView.hpp"
#include "gui/widgets/ImageView.hpp"
#include "gui/widgets/apps/MarkdownView.hpp"
#include "gui/widgets/apps/SeekBar.hpp"
#include "gui/widgets/apps/MediaButtons.hpp"
#include "hal/Sound_Functions.hpp"
#include "util/FixedString.hpp"

// ファイルビューワー。ファイル1つの中身を見るだけの画面。
//
// ファイルを探すのはファイルアプリ(FileExplorerScene)の仕事で、そこでファイルを
// 2回タップするとこのシーンがPush()される(開くファイルのパスをコンストラクタで受け取る)。
// 「戻る」でファイルアプリへ戻る。拡張子で表示を変える:
//   .md / .markdown … MarkdownView(Markdownブラウザと同じ描画)
//   .pimg           … ImageView(開いたときに1回だけ解いて、以降はメモリから描く)
//   .pvid           … 動画アプリ(VideoScene)へ置き換わる(シークバー・再生/一時停止・±10秒つき)
//   .txt(Shift_JIS)… 読書アプリ(ReaderScene)へ置き換わる(青空文庫の配布物。ここでは文字化けするため)
//   .wav            … 再生(下にシークバーと再生・一時停止/10秒戻る・進むのアイコンボタン(MediaButtons)。ミュージックアプリと同じ音の出し方)
//   .mml            … 中身をテキストで見せ(TextView)、同じ操作の欄で再生する。MMLは途中へ飛べないので
//                      シークバーは進み具合を見せるだけで、±10秒は押せない
//   それ以外        … TextView(見えている行だけを描くプレーンテキストの表示欄)
// テキスト系(.md含む、画像以外)は右上の「編集」でテキストエディタ(TextEditorScene)をその
// ファイルを開いた状態でPush()する。.txt は「読む」で読書アプリ(ReaderScene)をPush()する。エディタの「戻る」で戻ると、ここはonEnter()から作り直される
// ので、編集した内容が反映された表示になる(画像の編集エディタは今のところ無い)
// 表示に使う部品は開いたファイルの種類の1つだけを作る(MarkdownViewは約40KBあるため)。
class FileViewerScene : public Scene {
    private:
        // プレーンテキストとして読む上限。超えたぶんは読まず、その旨を上に出す
        static constexpr size_t kMaxTextBytes = PICO_STR_16KiB;

        static constexpr int MARGIN = 3;

        FixedString<PICO_PATH_LEN> path;

        Button* back_button = nullptr;
        Button* edit_button = nullptr;
        Button* read_button = nullptr;
        Label<PICO_STR_L>* status_label = nullptr;
        MarkdownView* md_view = nullptr;
        TextView* text_view = nullptr;
        ImageView* image_view = nullptr;

        // TextViewが指す本文。シーンのメンバなので、表示中は必ず生きている
        FixedString<kMaxTextBytes> text_buf;

        // ---- 音の再生(.wav / .mml) ----
        enum class Media : uint8_t { None, Wav, Mml };
        static constexpr uint32_t kSkipMs = 10000;
        Media media = Media::None;
        bool media_started = false;     // 鳴らし始めた(終わるまで・止めるまで)
        bool media_ended = false;
        int last_shown_sec = -1;
        bool last_playing_label = false;
        int media_h = 0;                // 下の操作の欄が占める高さ(本文の高さから引く)
        SeekBar* seek_bar = nullptr;
        MediaButtons* media_buttons = nullptr;
        Label<PICO_STR_S>* time_label = nullptr;

        void buildMediaControls(const Rect& content);
        bool startMedia();
        void stopMedia();
        void toggleMedia();
        void skipMedia(int32_t delta_ms);
        void seekMedia(uint32_t ms);
        bool mediaActive() const;
        bool mediaPaused() const;
        uint32_t mediaPosMs() const;
        uint32_t mediaTotalMs(bool* loops) const;
        void refreshMedia(bool force);

        Rect bodyRect(int top_row_h) const;
        void showText(const Rect& body, const char* message);
        bool loadPlainText(const Rect& body, bool& truncated);

    public:
        explicit FileViewerScene(const char* path) { this->path.assign(path ? path : ""); }

        const char* getName() const override { return "FileViewer"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;
        // WAVは44.1kHzのまま鳴らす(ミュージックアプリと同じ)
        uint32_t preferredSampleRate() const override;
};
