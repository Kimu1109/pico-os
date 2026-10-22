#include "apps/calculator/Calc_Currency.hpp"
#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "util/Url.hpp"
#include "OS_Data.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace {
    // 一覧の先頭に並べる通貨(この順)。残りはアルファベット順
    const char* const kPreferred[] = { "JPY", "USD", "EUR", "GBP", "CNY", "KRW", "TWD", "HKD", "AUD", "CAD", "CHF" };

    struct NameEntry { const char* code; const char* name; };
    const NameEntry kNames[] = {
        { "JPY", "日本円" },         { "USD", "米ドル" },         { "EUR", "ユーロ" },
        { "GBP", "英ポンド" },       { "CNY", "人民元" },         { "KRW", "韓国ウォン" },
        { "TWD", "台湾ドル" },       { "HKD", "香港ドル" },       { "AUD", "豪ドル" },
        { "CAD", "カナダドル" },     { "CHF", "スイスフラン" },   { "SGD", "シンガポールドル" },
        { "THB", "タイバーツ" },     { "INR", "インドルピー" },   { "IDR", "インドネシアルピア" },
        { "MYR", "マレーシアリンギット" }, { "PHP", "フィリピンペソ" }, { "VND", "ベトナムドン" },
        { "NZD", "NZドル" },         { "SEK", "スウェーデンクローナ" }, { "NOK", "ノルウェークローネ" },
        { "DKK", "デンマーククローネ" }, { "PLN", "ポーランドズロチ" }, { "CZK", "チェココルナ" },
        { "HUF", "ハンガリーフォリント" }, { "RON", "ルーマニアレウ" }, { "ISK", "アイスランドクローナ" },
        { "TRY", "トルコリラ" },     { "ILS", "イスラエルシェケル" }, { "MXN", "メキシコペソ" },
        { "BRL", "ブラジルレアル" }, { "ZAR", "南アフリカランド" }, { "RUB", "ロシアルーブル" },
        { "AED", "UAEディルハム" },  { "SAR", "サウジリヤル" },
    };

    int PreferredRank(const char* code){
        const int n = (int)(sizeof(kPreferred) / sizeof(kPreferred[0]));
        for(int i = 0; i < n; i++) if(strcmp(kPreferred[i], code) == 0) return i;
        return n;
    }

    bool Before(const CurrencyRates::Rate& a, const CurrencyRates::Rate& b){
        const int ra = PreferredRank(a.code), rb = PreferredRank(b.code);
        if(ra != rb) return ra < rb;
        return strcmp(a.code, b.code) < 0;
    }

    const char* PathOr(const char* path){
        return path ? path : PICO_Path::FILE::CFG::SYS_CURRENCY_CFG;
    }
}

bool CurrencyRates::IsCode(const char* s){
    if(!s) return false;
    for(int i = 0; i < 3; i++) if(s[i] < 'A' || s[i] > 'Z') return false;
    return s[3] == '\0';
}

int CurrencyRates::find(const char* code) const {
    if(!code) return -1;
    for(int i = 0; i < count_; i++) if(strcmp(rates_[i].code, code) == 0) return i;
    return -1;
}

const char* CurrencyRates::NameOf(const char* code){
    if(!code) return nullptr;
    for(const NameEntry& e : kNames) if(strcmp(e.code, code) == 0) return e.name;
    return nullptr;
}

bool CurrencyRates::convert(int from, int to, double v, double& out) const {
    if(from < 0 || from >= count_ || to < 0 || to >= count_) return false;
    if(from == to){ out = v; return true; }
    const double a = rates_[from].per_base;
    const double b = rates_[to].per_base;
    if(!(a > 0.0) || !(b > 0.0)) return false;
    out = v / a * b;
    return std::isfinite(out);
}

// ---------------------------------------------------------------------------
// JSONの読み取り
// ---------------------------------------------------------------------------

void CurrencyRates::beginParse(){
    staging_count_ = 0;
    in_rates_ = false;
    staging_date_.clear();
    staging_base_.clear();
    body_bytes_ = 0;
    json_.reset(this);
}

void CurrencyRates::stageRate(const char* code, double per_base){
    if(!IsCode(code) || !(per_base > 0.0) || !std::isfinite(per_base)) return;
    for(int i = 0; i < staging_count_; i++){
        if(strcmp(staging_[i].code, code) == 0){ staging_[i].per_base = per_base; return; }
    }
    int slot = staging_count_;
    if(staging_count_ >= kMaxRates){
        //入りきらない。open.er-apiは約160通貨をアルファベット順に返すので、捨てるだけだと
        //後ろのJPY/USDが入らない。よく使う通貨なら、よく使わない通貨を1つ追い出して入れる
        const int n_pref = (int)(sizeof(kPreferred) / sizeof(kPreferred[0]));
        if(PreferredRank(code) >= n_pref) return;
        slot = -1;
        for(int i = staging_count_ - 1; i >= 0; i--){
            if(PreferredRank(staging_[i].code) >= n_pref){ slot = i; break; }
        }
        if(slot < 0) return;
    }
    memcpy(staging_[slot].code, code, 4);
    staging_[slot].per_base = per_base;
    if(slot == staging_count_) staging_count_++;
}

void CurrencyRates::onBegin(const char* key, bool is_array, int depth){
    if(depth == 2) in_rates_ = (!is_array && key && strcmp(key, "rates") == 0);
}

void CurrencyRates::onEnd(bool is_array, int depth){
    (void)is_array;
    if(depth == 2) in_rates_ = false; // depthは閉じた入れ物の深さ
}

void CurrencyRates::onValue(const char* key, JsonReader::Type type, const char* text, size_t len,
                            bool truncated, int depth){
    (void)len;
    if(!key || truncated) return;
    if(depth == 2 && in_rates_ && type == JsonReader::Type::Number){
        char* end = nullptr;
        const double v = strtod(text, &end);
        if(end && end != text) this->stageRate(key, v);
        return;
    }
    if(depth != 1 || type != JsonReader::Type::String) return;
    if(strcmp(key, "base") == 0 || strcmp(key, "base_code") == 0){
        staging_base_.assign(text);
    }else if(strcmp(key, "date") == 0){
        staging_date_.assign(text);
    }else if(strcmp(key, "time_last_update_utc") == 0 && staging_date_.empty()){
        //"Wed, 21 Oct 2026 00:02:32 +0000" の先頭の日付だけ
        staging_date_.assign(text, strlen(text) > 16 ? 16 : strlen(text));
    }
}

void CurrencyRates::sortStaging(){
    //48件なので挿入ソートで足りる
    for(int i = 1; i < staging_count_; i++){
        const Rate r = staging_[i];
        int j = i - 1;
        while(j >= 0 && Before(r, staging_[j])){ staging_[j + 1] = staging_[j]; j--; }
        staging_[j + 1] = r;
    }
}

bool CurrencyRates::commitParse(){
    if(!json_.complete() || staging_base_.length() != 3 || !IsCode(staging_base_.c_str())) return false;
    //基準通貨そのものは "rates" に入らない取得元(Frankfurter)があるので、1として足す
    this->stageRate(staging_base_.c_str(), 1.0);
    if(staging_count_ < 2) return false;

    this->sortStaging();
    memcpy(rates_, staging_, sizeof(Rate) * (size_t)staging_count_);
    count_ = staging_count_;
    date_ = staging_date_;
    base_ = staging_base_;
    revision_++;
    return true;
}

bool CurrencyRates::parse(const char* json, size_t len){
    this->beginParse();
    if(!json_.feed(json, len)) return false;
    return this->commitParse();
}

bool CurrencyRates::write(const void* data, size_t len){
    body_bytes_ += len;
    if(body_bytes_ > kMaxBodyBytes) return false;
    return json_.feed(data, len);
}

// ---------------------------------------------------------------------------
// SDの控え
// ---------------------------------------------------------------------------

bool CurrencyRates::load(const char* path){
    const char* p = PathOr(path);
    if(!OSData::SD.exists(p)) return false;

    staging_count_ = 0;
    staging_date_.clear();
    staging_base_.clear();
    url_.clear();
    PICO_Config::ParseFile(p, [this](const char* key, const char* value){
        if(strcmp(key, "url") == 0)       url_.assign(value);
        else if(strcmp(key, "date") == 0) staging_date_.assign(value);
        else if(strcmp(key, "base") == 0) staging_base_.assign(value);
        else if(IsCode(key)){
            char* end = nullptr;
            const double v = strtod(value, &end);
            if(end && end != value) this->stageRate(key, v);
        }
    });
    if(staging_count_ < 2 || !IsCode(staging_base_.c_str())) return false;

    this->sortStaging();
    memcpy(rates_, staging_, sizeof(Rate) * (size_t)staging_count_);
    count_ = staging_count_;
    date_ = staging_date_;
    base_ = staging_base_;
    revision_++;
    return true;
}

bool CurrencyRates::save(const char* path) const {
    const char* p = PathOr(path);
    OSData::SD.mkdir(PICO_Path::DIR::SYS);
    FsFile f = OSData::SD.open(p, O_WRONLY | O_CREAT | O_TRUNC);
    if(!f) return false;

    bool ok = true;
    const char* head = "# 電卓の通貨レート(取得するたびに書き直す。url を書けば取得元を変えられる)\n";
    ok = ok && f.write(head, strlen(head)) == strlen(head);
    if(!url_.empty()) ok = ok && PICO_Config::Detail::WriteEntry(f, "url", url_.c_str());
    ok = ok && PICO_Config::Detail::WriteEntry(f, "date", date_.c_str());
    ok = ok && PICO_Config::Detail::WriteEntry(f, "base", base_.c_str());
    for(int i = 0; i < count_ && ok; i++){
        char num[32];
        snprintf(num, sizeof(num), "%.10g", rates_[i].per_base);
        ok = PICO_Config::Detail::WriteEntry(f, rates_[i].code, num);
    }
    f.close();
    return ok;
}

// ---------------------------------------------------------------------------
// 取得
// ---------------------------------------------------------------------------

bool CurrencyRates::beginFetch(){
    if(state_ == State::Fetching) return false;

    Url u;
    if(!UrlTools::Parse(u, this->url())){
        state_ = State::Failed;
        message_.assign("取得元のURLを解釈できません");
        return false;
    }
    this->beginParse();
    if(!http_.begin(u, this)){
        state_ = State::Failed;
        message_.assign("取得できません: ");
        message_.append(http_.failureToStr());
        LOG_APP_WARN("電卓: レートの取得を始められません (%s)", http_.failureToStr());
        return false;
    }
    state_ = State::Fetching;
    message_.assign("レートを取得中…");
    LOG_APP_MSG("電卓: レートを取得します (%s)", this->url());
    return true;
}

void CurrencyRates::update(){
    if(state_ != State::Fetching) return;
    http_.update();
    if(http_.getStatus() == TaskTools::PROCESSING) return;

    const int code = http_.response().statusCode();
    if(http_.getStatus() == TaskTools::FAILED){
        state_ = State::Failed;
        if(code >= 400){
            message_.clear();
            message_.appendFormat("取得できません(サーバが %d を返しました)", code);
        }else if(json_.failed() || body_bytes_ > kMaxBodyBytes){
            message_.assign("レートの形式が違います");
        }else{
            message_.assign("取得できません: ");
            message_.append(http_.failureToStr());
        }
        LOG_APP_WARN("電卓: %s", message_.c_str());
        return;
    }

    if(!this->commitParse()){
        state_ = State::Failed;
        message_.assign("レートの形式が違います");
        LOG_APP_WARN("電卓: レートを読めませんでした (%u B)", (unsigned)body_bytes_);
        return;
    }
    state_ = State::Done;
    message_.clear();
    message_.appendFormat("%s のレートに更新しました", date_.c_str());
    if(!this->save()) LOG_APP_WARN("電卓: レートをSDへ保存できませんでした");
    LOG_APP_OK("電卓: レートを更新しました (%s, %d通貨)", date_.c_str(), count_);
}

void CurrencyRates::cancel(){
    if(state_ == State::Fetching){
        http_.cancel();
        state_ = State::Idle;
    }
}
