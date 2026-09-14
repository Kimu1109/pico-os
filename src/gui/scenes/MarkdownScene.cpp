#include "gui/scenes/MarkdownScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "storage/Doc_Cache.hpp"
#include "util/Md_Scan.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>

namespace {
    //browser-homeが無いときに開く、同梱のサンプル文書
    constexpr const char* kFallbackHome = "/tmp/doc.md";

    //network.cfgからホーム(browser-home)を読む。無ければ空のまま
    void readHome(FixedString<PICO_STR_LL>& out){
        out.clear();
        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_NETWORK_CFG,
            [&out](const char* key, const char* value){
                if(strcmp(key, "browser-home") == 0) out.assign(value);
            }
        );
    }
}

MarkdownScene::MarkdownScene(const char* location){
    if(location && location[0] != '\0'){
        this->pushHistory(location);
        return;
    }

    FixedString<PICO_STR_LL> home;
    readHome(home);
    this->pushHistory(home.empty() ? kFallbackHome : home.c_str());
}

void MarkdownScene::onEnter(){
    const Rect content = Scene::contentRect();

    const int header_y = content.y + (HEADER_H - BUTTON_H) / 2 - 2;
    const int view_y   = content.y + HEADER_H;
    const int view_h   = content.h - HEADER_H - FOOTER_H;

    // ---- ヘッダ ----
    //「<」「>」はHomeSceneのページ送りと同じ流儀(Buttonはアイコンを持てないため、
    // フォントに確実に含まれるASCIIで済ませている)
    this->back_button = new Button(MARGIN, header_y, "<");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setAllowTextSpacing(false);
    this->back_button->setW(NAV_BUTTON_W);
    this->back_button->setH(BUTTON_H);
    this->back_button->setOnPressEnd([this](){ this->goBack(); });
    WidgetFunctions::Add(this->back_button);

    this->forward_button = new Button(MARGIN + NAV_BUTTON_W + 8, header_y, ">");
    this->forward_button->setFontSize(FontFn::Small);
    this->forward_button->setAllowTextSpacing(false);
    this->forward_button->setW(NAV_BUTTON_W);
    this->forward_button->setH(BUTTON_H);
    this->forward_button->setOnPressEnd([this](){ this->goForward(); });
    WidgetFunctions::Add(this->forward_button);

    this->exit_button = new Button(content.w - MARGIN - EXIT_BUTTON_W, header_y, "終了");
    this->exit_button->setFontSize(FontFn::Small);
    this->exit_button->setAllowTextSpacing(false);
    this->exit_button->setW(EXIT_BUTTON_W);
    this->exit_button->setH(BUTTON_H);
    this->exit_button->setOnPressEnd([](){
        //ここでのPopはアプリ終了(ランチャへ戻る)。文書の履歴とは別物
        SceneFunctions::Pop();
    });
    WidgetFunctions::Add(this->exit_button);

    // ---- 本文 ----
    this->view = new MarkdownView(0, view_y, content.w, view_h);
    this->view->setOnLinkTap([this](FixedString<PICO_PATH_LEN> ref){
        this->onLinkTap(ref);
    });
    WidgetFunctions::Add(this->view);

    // ---- フッタ ----
    //幅いっぱいに1行だけ出す。折り返させたうえで高さを1行に絞ることで、
    //長い場所は右側が表示されなくなる(ブラウザのURL欄と同じ見え方)
    this->status_label = new Label<PICO_PATH_LEN>(MARGIN, content.y + content.h - FOOTER_H, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    this->status_label->setMaxHeight(Label<PICO_PATH_LEN>::GetLineHeight(FontFn::Small));
    WidgetFunctions::Add(this->status_label);

    //Pop()で戻ってきた場合もここを通るので、履歴の現在位置を開き直す
    this->openCurrent();
}

void MarkdownScene::onExit(){
    //取得中に抜けると、書きかけの一時ファイルが残ったままになる
    this->fetch.cancel();
    this->phase = Phase::Idle;

    this->rememberScroll();

    this->view = nullptr;
    this->back_button = nullptr;
    this->forward_button = nullptr;
    this->exit_button = nullptr;
    this->status_label = nullptr;
}

void MarkdownScene::onUpdate(){
    if(phase == Phase::Idle) return;

    fetch.update();
    if(fetch.state() == DocFetch::State::Fetching) return;

    if(phase == Phase::Document){
        this->onFetchFinished();
        return;
    }

    // ---- 画像を1枚取り終えた ----
    if(fetch.state() != DocFetch::State::Ready){
        //1枚落ちたということは相手へ届いていない。残りも同じなので打ち切る。
        //ここで諦めないと、接続待ち(最大3秒)を画像の枚数ぶん繰り返すことになる
        LOG_SYS_WARN("Markdown: 画像を取得できないため残りを諦めます (%s)", fetch.message());
        this->showDocument();
        return;
    }

    pending_index++;
    if(pending_index >= pending_count){
        this->showDocument();
        return;
    }

    if(!this->startNextImage()) this->showDocument();
}

// ---------- 履歴 ----------

bool MarkdownScene::pushHistory(const char* location){
    FixedString<PICO_STR_LL> stored;
    //切り詰まった場所を積むと別の文書を指してしまうので、積まずに失敗を返す
    if(!stored.assign(location)) return false;

    //同じ場所を続けて開いた場合は積み直さない
    if(history_pos >= 0 && history[history_pos].location == stored) return true;

    //前方履歴を捨てる(戻ってから別のリンクを踏んだ場合)
    history_count = history_pos + 1;

    if(history_count >= kMaxHistory){
        for(int i = 0; i < kMaxHistory - 1; i++){
            history[i] = history[i + 1];
        }
        history_count = kMaxHistory - 1;
    }

    history[history_count].location = stored;
    history[history_count].scroll_y = 0;
    history_count++;
    history_pos = history_count - 1;
    return true;
}

void MarkdownScene::rememberScroll(){
    if(!view || history_pos < 0) return;
    history[history_pos].scroll_y = view->getScrollY();
}

bool MarkdownScene::currentAsUrl(Url& out) const {
    if(history_pos < 0) return false;
    //URLとして解釈できればリモート、できなければSD上のパス。
    //「どちらなのか」を判定するのはここ1箇所だけにしてある
    return UrlTools::Parse(out, history[history_pos].location.c_str());
}

bool MarkdownScene::openCurrent(){
    if(!view || history_pos < 0) return false;

    Url url;
    if(currentAsUrl(url)){
        //リモート: まず文書を取りに行く。結果はonUpdate()経由で処理する
        this->showStatus("読み込み中...", PICO_DARKGREY);
        this->refreshChrome();

        doc_url = url;
        doc_cache_path.clear();
        pending_count = 0;
        pending_index = 0;

        phase = Phase::Document;
        if(!fetch.begin(url)){
            this->onFetchFinished(); //begin()の時点で確定した失敗を表示する
            return false;
        }
        return true;
    }

    //ローカル: そのまま開く
    const HistoryEntry& entry = history[history_pos];
    if(!view->load(entry.location.c_str())){
        LOG_SYS_WARN("Markdown: 読み込みに失敗しました (%s)", entry.location.c_str());
        this->showStatus("開けませんでした", PICO_RED);
        return false;
    }

    view->setScrollY(entry.scroll_y);
    this->refreshChrome();
    return true;
}

void MarkdownScene::onFetchFinished(){
    phase = Phase::Idle;

    if(!view || history_pos < 0) return;

    if(fetch.state() != DocFetch::State::Ready){
        this->showStatus(fetch.message()[0] ? fetch.message() : "取得できませんでした", PICO_RED);
        return;
    }

    //本文の置き場所を控える(この後fetchは画像の取得に使い回すため)
    if(!doc_cache_path.assign(fetch.path())){
        this->showStatus("パスが長すぎます", PICO_RED);
        return;
    }

    //取得できず古いキャッシュで代用した場合は、画像も取りに行かない
    //(同じ相手へ繋がらないため)
    if(fetch.source() == DocFetch::Source::CacheAfterError){
        this->showDocument();
        this->showStatus("オフライン表示(保存済み)", PICO_OLIVE);
        return;
    }

    this->collectMissingImages();

    if(pending_count == 0){
        this->showDocument();
        return;
    }

    phase = Phase::Images;
    if(!this->startNextImage()) this->showDocument();
}

// ---------- 画像 ----------

void MarkdownScene::collectMissingImages(){
    pending_count = 0;
    pending_index = 0;

    FixedString<PICO_STR_M> host;
    if(!UrlTools::HostHeader(host, doc_url)) return;

    FsFile f = OSData::SD.open(doc_cache_path.c_str());
    if(!f) return;

    //画像ブロックの判定は行単位なので、行ごとに読めば足りる。
    //文書全体をRAMへ載せないで済む(MarkdownViewのdoc_textとは別物なので、
    // 8KiBのバッファをもう1つ抱えたくない)
    char line[PICO_STR_512B];

    while(pending_count < kMaxPrefetchImages && f.fgets(line, sizeof(line)) > 0){
        size_t n = strlen(line);
        while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';

        const char* ref = nullptr;
        size_t refLen = 0;
        if(!MdScan::ImageRefInLine(line, n, ref, refLen)) continue;

        FixedString<PICO_STR_L> value;
        //収まらない参照は取りに行けない(表示側も同じ理由で開けない)
        if(!value.assign(ref, refLen)) continue;

        //既にキャッシュにあるものは取りに行かない(2回目の訪問は取得ゼロ)
        Url image_url;
        if(!UrlTools::Resolve(image_url, doc_url, value.c_str())) continue;
        if(PICO_DocCache::Exists(host.c_str(), image_url.path.c_str())) continue;

        //同じ画像が何度も出てくる文書で枠を無駄にしない
        bool duplicated = false;
        for(int i = 0; i < pending_count; i++){
            if(pending_images[i] == value){ duplicated = true; break; }
        }
        if(duplicated) continue;

        pending_images[pending_count++] = value;
    }

    f.close();
}

bool MarkdownScene::startNextImage(){
    if(pending_index < 0 || pending_index >= pending_count) return false;

    Url image_url;
    if(!UrlTools::Resolve(image_url, doc_url, pending_images[pending_index].c_str())) return false;

    char buf[48];
    snprintf(buf, sizeof(buf), "画像を取得中 %d/%d", pending_index + 1, pending_count);
    this->showStatus(buf, PICO_DARKGREY);

    return fetch.begin(image_url);
}

void MarkdownScene::showDocument(){
    phase = Phase::Idle;

    if(!view || history_pos < 0 || doc_cache_path.empty()) return;

    if(!view->load(doc_cache_path.c_str())){
        this->showStatus("開けませんでした", PICO_RED);
        return;
    }
    view->setScrollY(history[history_pos].scroll_y);
    this->refreshChrome();
}

void MarkdownScene::goBack(){
    if(!canGoBack()) return;
    this->rememberScroll();
    fetch.cancel();
    phase = Phase::Idle;
    history_pos--;
    this->openCurrent();
}

void MarkdownScene::goForward(){
    if(!canGoForward()) return;
    this->rememberScroll();
    fetch.cancel();
    phase = Phase::Idle;
    history_pos++;
    this->openCurrent();
}

// ---------- リンク ----------

void MarkdownScene::onLinkTap(const FixedString<PICO_PATH_LEN>& ref){
    FixedString<PICO_STR_LL> next;

    Url base;
    if(currentAsUrl(base)){
        //リモートからの参照。絶対URLでも相対でもUrlTools側が面倒を見る
        Url target;
        if(!UrlTools::Resolve(target, base, ref.c_str())){
            this->showStatus("リンクを解決できません", PICO_RED);
            return;
        }
        if(target.secure){
            this->showStatus("httpsはまだ開けません", PICO_RED);
            return;
        }
        if(!UrlTools::FormatFull(next, target)){
            this->showStatus("URLが長すぎます", PICO_RED);
            return;
        }
    }else{
        //ローカルからの参照。絶対URLならそのままリモートへ移る
        Url absolute;
        if(UrlTools::Parse(absolute, ref.c_str())){
            if(absolute.secure){
                this->showStatus("httpsはまだ開けません", PICO_RED);
                return;
            }
            if(!next.assign(ref.c_str())){
                this->showStatus("URLが長すぎます", PICO_RED);
                return;
            }
        }else{
            const char* here = history[history_pos].location.c_str();

            FixedString<PICO_PATH_LEN> resolved;
            if(!PICO_IO::resolve(resolved, here, ref.c_str())){
                this->showStatus("パスを解決できません", PICO_RED);
                return;
            }
            //存在しないリンクで履歴を汚さないよう、積む前に確かめる
            //(リモートは取ってみるまで分からないので、この確認はローカルだけ)
            if(!OSData::SD.exists(resolved.c_str())){
                this->showStatus("見つかりません", PICO_RED);
                return;
            }
            if(!next.assign(resolved.c_str())){
                this->showStatus("パスが長すぎます", PICO_RED);
                return;
            }
        }
    }

    //進む前に今の位置を控える(戻ってきたときに復元される)
    this->rememberScroll();

    if(!this->pushHistory(next.c_str())){
        this->showStatus("場所が長すぎます", PICO_RED);
        return;
    }

    this->openCurrent();
}

// ---------- 見た目の更新 ----------

void MarkdownScene::refreshChrome(){
    //辿れない方向のボタンは灰色にする(押しても無視される)
    if(back_button){
        back_button->setTextColor(canGoBack() ? PICO_BLACK : PICO_LIGHTGREY);
    }
    if(forward_button){
        forward_button->setTextColor(canGoForward() ? PICO_BLACK : PICO_LIGHTGREY);
    }

    if(status_label && history_pos >= 0){
        status_label->setTextColor(PICO_DARKGREY);
        status_label->setText(history[history_pos].location.c_str());
    }
}

void MarkdownScene::showStatus(const char* message, int8_t color){
    if(!status_label) return;
    status_label->setTextColor(color);
    status_label->setText(message);
}
