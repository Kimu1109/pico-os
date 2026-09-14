#include "net/Doc_Fetch.hpp"

#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"

#include <cstring>
#include <ctime>

bool DocFetch::begin(const Url& target, bool bypass_cache){
    this->cancel();

    url = target;

    //キャッシュのキーはポートまで含めた形。ポートが違えば別のサーバとして扱う
    if(!UrlTools::HostHeader(host, url)){
        finish(State::Failed, Source::None, "URLが長すぎます");
        return false;
    }
    //ディレクトリを指すURL("http://host/" や "http://host/docs/")には開くべき
    //文書が無い。PROTOCOL.mdは既定の文書名(index.md等)を決めていないので、
    //こちらで勝手に補わずに断る。PathFor()と同じ理由で弾かれるが、
    //「パスが長すぎます」と出ると原因が分からなくなるため先に見ている
    if(strcmp(url.path.c_str(), "/") == 0){
        finish(State::Failed, Source::None, "文書を指していないURLです");
        return false;
    }
    if(!PICO_DocCache::PathFor(cache_path, host.c_str(), url.path.c_str())){
        finish(State::Failed, Source::None, "パスが長すぎます");
        return false;
    }

    //手元にあるなら検証子を添えて条件付きGETにする。変更が無ければ304で済む。
    //取り直し(リロード)のときだけは検証子を送らず、必ず本文を貰う
    PICO_DocCache::Entry entry;
    const bool cached = !bypass_cache
        && PICO_DocCache::Lookup(host.c_str(), url.path.c_str(), entry);

    if(!writer.begin(host.c_str(), url.path.c_str())){
        //書き込み先を用意できない(SDが無い等)。取得しても置き場所が無いので、
        //キャッシュがあればそれを開く
        return fallbackToCache("保存先を用意できません");
    }
    sink.writer = &writer;

    if(!http.begin(url, &sink, cached ? entry.validator.c_str() : nullptr)){
        writer.abort();
        return fallbackToCache(http.failureToStr());
    }

    state_ = State::Fetching;
    source_ = Source::None;
    message_ = "";
    return true;
}

void DocFetch::update(){
    if(state_ != State::Fetching) return;

    http.update();

    const TaskTools::Status s = http.getStatus();
    if(s == TaskTools::PROCESSING) return;

    if(s == TaskTools::FAILED){
        writer.abort();
        //404なら理由を具体的に出す
        const int code = http.response().statusCode();
        if(code >= 400){
            static char buf[48];
            snprintf(buf, sizeof(buf), "サーバが %d を返しました", code);
            fallbackToCache(buf);
        }else{
            fallbackToCache(http.failureToStr());
        }
        return;
    }

    // ---- 成功 ----
    if(http.isNotModified()){
        //本文は来ていないので、書きかけを捨てて既存のキャッシュを使う
        writer.abort();

        if(OSData::SD.exists(cache_path.c_str())){
            finish(State::Ready, Source::NotModified, "");
        }else{
            //304なのに手元に無い = 目録と本体がずれている。次回は取り直しになる
            PICO_DocCache::RemoveEntry(host.c_str(), url.path.c_str());
            finish(State::Failed, Source::None, "キャッシュが見つかりません");
        }
        return;
    }

    //取得時刻は参考値(NTP同期前の時計は当てにならない)。
    //鮮度の判断はvalidator側で行う
    const uint32_t epoch = (uint32_t)time(nullptr);

    if(!writer.commit(http.response().validator().c_str(), epoch)){
        fallbackToCache("保存に失敗しました");
        return;
    }

    finish(State::Ready, Source::Network, "");
}

bool DocFetch::fallbackToCache(const char* why){
    if(!cache_path.empty() && OSData::SD.exists(cache_path.c_str())){
        LOG_SYS_WARN("DocFetch: %s (古いキャッシュを表示します)", why ? why : "");
        finish(State::Ready, Source::CacheAfterError, why);
        return true;
    }

    LOG_SYS_WARN("DocFetch: %s", why ? why : "");
    finish(State::Failed, Source::None, why);
    return false;
}

void DocFetch::finish(State s, Source src, const char* msg){
    state_ = s;
    source_ = src;
    message_ = msg ? msg : "";
}

void DocFetch::cancel(){
    http.cancel();
    writer.abort();
    sink.writer = nullptr;

    state_ = State::Idle;
    source_ = Source::None;
    message_ = "";
    cache_path.clear();
}
