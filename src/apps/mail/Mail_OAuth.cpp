#include "apps/mail/Mail_OAuth.hpp"

#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "crypto/Secret_File.hpp"
#include "util/Url.hpp"
#include "Arduino.h"
#include "system/I18n_Functions.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    bool Reached(uint32_t now, uint32_t at){
        return (int32_t)(now - at) >= 0;
    }

    // 更新用トークンの暗号化の用途(行ごとに "mail-oauth-0", "mail-oauth-1", …)
    constexpr const char* kTokenPurpose = "mail-oauth";

    constexpr const char* kDeviceGrant = "urn:ietf:params:oauth:grant-type:device_code";
}

// ---------------------------------------------------------------------------
// 設定とトークン
// ---------------------------------------------------------------------------

void MailOAuth::configure(const Config& given){
    Config c = given;
    if(c.tenant.empty()) c.tenant.assign(kDefaultTenant);
    if(c.authority.empty()) c.authority.assign(kDefaultAuthority);
    //末尾の / は付けない("https://host" + "/tenant/...")
    while(c.authority.length() > 0 && c.authority.c_str()[c.authority.length() - 1] == '/'){
        c.authority.assign(c.authority.c_str(), c.authority.length() - 1);
    }
    const bool changed = c.client_id != cfg_.client_id || c.tenant != cfg_.tenant || c.authority != cfg_.authority;
    cfg_ = c;
    if(changed){
        cancel();
        forget();
    }
}

bool MailOAuth::setRefreshToken(const char* token){
    const size_t n = token ? strlen(token) : 0;
    if(n >= kRefreshTokenCap){
        refresh_len_ = 0;
        refresh_[0] = '\0';
        return false;
    }
    memcpy(refresh_, token ? token : "", n + 1);
    refresh_len_ = n;
    return true;
}

void MailOAuth::forget(){
    access_len_ = 0;
    access_[0] = '\0';
    access_expires_sec_ = 0;
    refresh_len_ = 0;
    refresh_[0] = '\0';
}

bool MailOAuth::hasAccessToken() const {
    if(access_len_ == 0) return false;
    const uint32_t life = access_expires_sec_ > kExpiryMarginSec ? access_expires_sec_ - kExpiryMarginSec : 0;
    return accessTokenAgeMs() < life * 1000u;
}

void MailOAuth::dropAccessToken(){
    access_len_ = 0;
    access_[0] = '\0';
}

uint32_t MailOAuth::accessTokenAgeMs() const {
    return millis() - access_got_ms_;
}

uint32_t MailOAuth::secondsLeft() const {
    if(phase_ != Phase::WaitUser) return 0;
    const uint32_t passed = (millis() - code_started_ms_) / 1000u;
    return passed >= code_expires_sec_ ? 0 : code_expires_sec_ - passed;
}

bool MailOAuth::AppendFormValue(FixedString<kBodyCap>& out, const char* value){
    static const char* kHex = "0123456789ABCDEF";
    for(const char* p = value; *p; p++){
        const char c = *p;
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                        || c == '-' || c == '.' || c == '_' || c == '~';
        if(plain){
            if(!out.append(c)) return false;
        }else{
            const uint8_t b = (uint8_t)c;
            const char esc[4] = {'%', kHex[b >> 4], kHex[b & 0x0F], '\0'};
            if(!out.append(esc)) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 保存(/sys/mail_token.cfg)
// ---------------------------------------------------------------------------

bool MailOAuth::LoadRefreshToken(char* out, size_t cap, bool* needs_rewrite){
    return PICO_SecretFile::Load(PICO_Path::FILE::CFG::SYS_MAIL_TOKEN, kTokenPurpose, out, cap, needs_rewrite);
}

bool MailOAuth::SaveRefreshToken(const char* token){
    if(!token || strlen(token) >= kRefreshTokenCap) return false;
    return PICO_SecretFile::Save(PICO_Path::FILE::CFG::SYS_MAIL_TOKEN, kTokenPurpose,
                                 "# メールの Microsoft のサインイン(暗号化した更新用トークン)", token);
}

void MailOAuth::ClearRefreshToken(){
    PICO_SecretFile::Remove(PICO_Path::FILE::CFG::SYS_MAIL_TOKEN);
}

// ---------------------------------------------------------------------------
// 要求
// ---------------------------------------------------------------------------

void MailOAuth::resetParsed(){
    access_len_ = 0;
    access_[0] = '\0';
    access_truncated_ = false;
    new_refresh_len_ = 0;
    new_refresh_[0] = '\0';
    new_refresh_truncated_ = false;
    got_expires_in_ = 0;
    got_interval_ = 0;
    got_error_.clear();
    got_error_desc_.clear();
}

bool MailOAuth::post(const char* path){
    Url url;
    FixedString<PICO_STR_LL> full(cfg_.authority.c_str());
    full.append('/');
    full.append(cfg_.tenant);
    full.append(path);
    if(!UrlTools::Parse(url, full.c_str())){
        finishWith(Result::Failed, TR("サインインのサーバのURLが読めません"));
        return false;
    }
    json_.reset(this);
    json_ok_ = true;
    in_flight_ = true;
    if(!req_.begin(url, HttpRequest::Method::POST, this, body_.c_str(), body_.length(),
                   "application/x-www-form-urlencoded")){
        in_flight_ = false;
        finishWith(Result::Failed, TR("サインインのサーバへ送れません"));
        return false;
    }
    return true;
}

bool MailOAuth::startSignIn(){
    cancel();
    if(cfg_.client_id.empty()){
        finishWith(Result::Failed, TR("mail.cfg に oauth-client-id がありません"));
        return false;
    }
    body_.assign("client_id=");
    bool ok = AppendFormValue(body_, cfg_.client_id.c_str());
    ok = ok && body_.append("&scope=") && AppendFormValue(body_, kScope);
    if(!ok){
        finishWith(Result::Failed, TR("送る内容が長すぎます"));
        return false;
    }
    device_code_[0] = '\0';
    user_code_.clear();
    verification_uri_.clear();
    resetParsed();
    phase_ = Phase::RequestCode;
    return post("/oauth2/v2.0/devicecode");
}

bool MailOAuth::startRefresh(){
    cancel();
    if(cfg_.client_id.empty()){
        finishWith(Result::Failed, TR("mail.cfg に oauth-client-id がありません"));
        return false;
    }
    if(refresh_len_ == 0){
        finishWith(Result::NeedSignIn, TR("Microsoft のサインインが必要です"));
        return false;
    }
    body_.assign("grant_type=refresh_token&client_id=");
    bool ok = AppendFormValue(body_, cfg_.client_id.c_str());
    ok = ok && body_.append("&refresh_token=") && AppendFormValue(body_, refresh_);
    ok = ok && body_.append("&scope=") && AppendFormValue(body_, kScope);
    if(!ok){
        finishWith(Result::Failed, TR("送る内容が長すぎます"));
        return false;
    }
    resetParsed();
    phase_ = Phase::Refresh;
    return post("/oauth2/v2.0/token");
}

void MailOAuth::beginTokenPoll(){
    body_.assign("grant_type=");
    bool ok = AppendFormValue(body_, kDeviceGrant);
    ok = ok && body_.append("&client_id=") && AppendFormValue(body_, cfg_.client_id.c_str());
    ok = ok && body_.append("&device_code=") && AppendFormValue(body_, device_code_);
    if(!ok){
        finishWith(Result::Failed, TR("送る内容が長すぎます"));
        return;
    }
    resetParsed();
    post("/oauth2/v2.0/token");
}

void MailOAuth::cancel(){
    if(in_flight_) req_.cancel();
    req_.closeConnection();
    in_flight_ = false;
    phase_ = Phase::Idle;
}

void MailOAuth::finishWith(Result r, const char* why){
    req_.closeConnection();
    in_flight_ = false;
    phase_ = Phase::Idle;
    result_ = r;
    error_text_.assign(why ? why : "");
    if(r != Result::Ok) LOG_APP_WARN("Mail: サインイン: %s", why ? why : "");
    rev_++;
}

void MailOAuth::update(){
    if(phase_ == Phase::Idle) return;
    if(!in_flight_){
        //WaitUser の問い合わせの合間
        if(phase_ != Phase::WaitUser) return;
        if(secondsLeft() == 0){
            finishWith(Result::Denied, TR("コードの期限が切れました。もう一度サインインしてください"));
            return;
        }
        if(Reached(millis(), next_poll_ms_)) beginTokenPoll();
        return;
    }
    req_.update();
    if(req_.getStatus() != TaskTools::PROCESSING) finish();
}

void MailOAuth::finish(){
    in_flight_ = false;
    if(req_.getStatus() != TaskTools::SUCCESS){
        FixedString<PICO_STR_LL> why(TR("サインインのサーバと通信できません: "));
        why.append(req_.failureToStr());
        if(phase_ == Phase::WaitUser){
            //待っている間の一時的な失敗は、期限まで問い合わせ続ける
            LOG_APP_WARN("Mail: %s", why.c_str());
            next_poll_ms_ = millis() + interval_sec_ * 1000u;
            return;
        }
        finishWith(Result::Failed, why.c_str());
        return;
    }
    const int code = req_.response().statusCode();
    const bool http_ok = code >= 200 && code < 300;
    if(!json_ok_ || (!json_.complete() && http_ok)){
        FixedString<PICO_STR_LL> why;
        why.appendFormat(TR("サインインのサーバの応答が読めません (HTTP %d)"), code);
        if(phase_ == Phase::WaitUser){
            next_poll_ms_ = millis() + interval_sec_ * 1000u;
            return;
        }
        finishWith(Result::Failed, why.c_str());
        return;
    }

    FixedString<PICO_STR_LL> why;
    switch(phase_){
        case Phase::RequestCode:
            if(!http_ok || device_code_[0] == '\0' || user_code_.empty()){
                if(!got_error_desc_.empty()) why.appendFormat(TR("サインインを始められません: %s"), got_error_desc_.c_str());
                else why.appendFormat(TR("サインインを始められません (HTTP %d)"), code);
                //client_id が違う等。何度やっても同じ
                finishWith(Result::Denied, why.c_str());
                return;
            }
            if(verification_uri_.empty()) verification_uri_.assign("https://microsoft.com/devicelogin");
            code_started_ms_ = millis();
            code_expires_sec_ = got_expires_in_ ? got_expires_in_ : 900;
            interval_sec_ = got_interval_ ? got_interval_ : 5;
            next_poll_ms_ = millis() + interval_sec_ * 1000u;
            phase_ = Phase::WaitUser;
            result_ = Result::None;
            error_text_.clear();
            rev_++;
            return;

        case Phase::WaitUser:
        case Phase::Refresh:
            break;
        case Phase::Idle:
            return;
    }

    if(http_ok && access_len_ > 0 && !access_truncated_){
        access_got_ms_ = millis();
        access_expires_sec_ = got_expires_in_ ? got_expires_in_ : 3600;
        if(new_refresh_len_ > 0 && !new_refresh_truncated_){
            memcpy(refresh_, new_refresh_, new_refresh_len_ + 1);
            refresh_len_ = new_refresh_len_;
            refresh_rev_++;
        }
        device_code_[0] = '\0';
        finishWith(Result::Ok, "");
        return;
    }
    if(http_ok && access_truncated_){
        access_len_ = 0;
        access_[0] = '\0';
        finishWith(Result::Failed, TR("アクセストークンが長すぎます"));
        return;
    }

    //誤り(RFC 6749 5.2 / RFC 8628 3.5)
    const char* err = got_error_.c_str();
    if(phase_ == Phase::WaitUser){
        if(strcmp(err, "authorization_pending") == 0){
            next_poll_ms_ = millis() + interval_sec_ * 1000u;
            return;
        }
        if(strcmp(err, "slow_down") == 0){
            interval_sec_ += 5;
            next_poll_ms_ = millis() + interval_sec_ * 1000u;
            return;
        }
        device_code_[0] = '\0';
        if(strcmp(err, "authorization_declined") == 0 || strcmp(err, "access_denied") == 0){
            finishWith(Result::Denied, TR("サインインが断られました"));
        }else if(strcmp(err, "expired_token") == 0 || strcmp(err, "code_expired") == 0){
            finishWith(Result::Denied, TR("コードの期限が切れました。もう一度サインインしてください"));
        }else{
            if(!got_error_desc_.empty()) why.appendFormat(TR("サインインできません: %s"), got_error_desc_.c_str());
            else why.appendFormat(TR("サインインできません: %s (HTTP %d)"), *err ? err : "?", code);
            finishWith(Result::Denied, why.c_str());
        }
        return;
    }

    //Refresh
    if(strcmp(err, "invalid_grant") == 0 || strcmp(err, "interaction_required") == 0
       || strcmp(err, "invalid_client") == 0 || strcmp(err, "unauthorized_client") == 0){
        //更新用トークンが取り消された/期限切れ(パスワードの変更等)。サインインし直すしかない
        refresh_len_ = 0;
        refresh_[0] = '\0';
        refresh_rev_++;
        finishWith(Result::NeedSignIn, TR("Microsoft のサインインが切れました。[更新] を押してサインインし直してください"));
        return;
    }
    if(!got_error_desc_.empty()) why.appendFormat(TR("アクセストークンを取れません: %s"), got_error_desc_.c_str());
    else why.appendFormat(TR("アクセストークンを取れません (HTTP %d)"), code);
    finishWith(Result::Failed, why.c_str());
}

// ---------------------------------------------------------------------------
// 応答のJSON
// ---------------------------------------------------------------------------

bool MailOAuth::write(const void* data, size_t len){
    if(!json_ok_) return true; //読めなくなったら残りは捨てる(誤りは finish() で)
    if(!json_.feed(data, len)) json_ok_ = false;
    return true;
}

char* MailOAuth::stringBuffer(const char* key, int depth, size_t& cap){
    if(depth != 1 || !key) return nullptr;
    if(strcmp(key, "access_token") == 0){
        cap = kAccessTokenCap;
        return access_;
    }
    if(strcmp(key, "refresh_token") == 0){
        cap = kRefreshTokenCap;
        return new_refresh_;
    }
    if(strcmp(key, "device_code") == 0){
        cap = kDeviceCodeCap;
        return device_code_;
    }
    return nullptr;
}

void MailOAuth::onValue(const char* key, JsonReader::Type type, const char* text, size_t len,
                        bool truncated, int depth){
    if(depth != 1 || !key) return;
    if(type == JsonReader::Type::String){
        if(strcmp(key, "access_token") == 0){
            access_len_ = len;
            access_truncated_ = truncated;
        }else if(strcmp(key, "refresh_token") == 0){
            new_refresh_len_ = len;
            new_refresh_truncated_ = truncated;
        }else if(strcmp(key, "device_code") == 0){
            if(truncated) device_code_[0] = '\0';
        }else if(strcmp(key, "user_code") == 0){
            user_code_.assign(text);
        }else if(strcmp(key, "verification_uri") == 0){
            verification_uri_.assign(text);
        }else if(strcmp(key, "error") == 0){
            got_error_.assign(text);
        }else if(strcmp(key, "error_description") == 0){
            //"AADSTS70016: ... Trace ID: ..." の1行目だけ
            const char* nl = strpbrk(text, "\r\n");
            got_error_desc_.assign(text, nl ? (size_t)(nl - text) : len);
        }
        //expires_in を文字列で返すサーバもある
        if(strcmp(key, "expires_in") == 0) got_expires_in_ = (uint32_t)strtoul(text, nullptr, 10);
        else if(strcmp(key, "interval") == 0) got_interval_ = (uint32_t)strtoul(text, nullptr, 10);
        return;
    }
    if(type == JsonReader::Type::Number){
        if(strcmp(key, "expires_in") == 0) got_expires_in_ = (uint32_t)strtoul(text, nullptr, 10);
        else if(strcmp(key, "interval") == 0) got_interval_ = (uint32_t)strtoul(text, nullptr, 10);
    }
}
