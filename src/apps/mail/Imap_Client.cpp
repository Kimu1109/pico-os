#include "apps/mail/Imap_Client.hpp"

#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "crypto/Secret_Cipher.hpp"
#include "util/Url.hpp"
#include "util/Base64.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    // millis() の巻き戻り(約49日)を跨いでも正しく比べる
    bool Reached(uint32_t now, uint32_t at){
        return (int32_t)(now - at) >= 0;
    }

    constexpr const char* kPasswordPurpose = "mail-password";
    constexpr const char* kDefaultMailbox = "INBOX";

    bool ContainsNoCase(const char* s, size_t len, const char* word){
        const size_t wl = strlen(word);
        for(size_t i = 0; i + wl <= len; i++){
            size_t k = 0;
            while(k < wl){
                char a = s[i + k], b = word[k];
                if(a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
                if(b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
                if(a != b) break;
                k++;
            }
            if(k == wl) return true;
        }
        return false;
    }

    // XOAUTH2 の中身(user=..^Aauth=Bearer ..^A^A)を base64 にしながら少しずつ送る(トークンは数KBあるので組み立てない)
    class Base64Writer {
        public:
            explicit Base64Writer(HttpTransport& net) : net_(net) {}
            void put(const char* s, size_t n){
                for(size_t i = 0; i < n; i++){
                    carry_[carry_len_++] = (uint8_t)s[i];
                    if(carry_len_ == 3){
                        emit(carry_, 3);
                        carry_len_ = 0;
                    }
                }
            }
            void put(const char* s){ put(s, strlen(s)); }
            // 端数を出して CRLF を付ける
            bool finish(){
                if(carry_len_ > 0) emit(carry_, carry_len_);
                carry_len_ = 0;
                if(len_ + 2 > sizeof(out_)) flush();
                out_[len_++] = '\r';
                out_[len_++] = '\n';
                flush();
                return ok_;
            }
        private:
            HttpTransport& net_;
            uint8_t carry_[3] = {};
            size_t carry_len_ = 0;
            char out_[256];
            size_t len_ = 0;
            bool ok_ = true;
            void emit(const uint8_t* b, size_t n){
                if(len_ + 4 > sizeof(out_)) flush();
                len_ += Base64::Encode(b, n, out_ + len_);
            }
            void flush(){
                if(len_ == 0) return;
                if(ok_ && net_.write((const uint8_t*)out_, len_) != len_) ok_ = false;
                len_ = 0;
            }
    };

    bool IsMicrosoftHost(const char* host){
        return strcmp(host, MailOAuth::kImapHost) == 0 || strcmp(host, "imap-mail.outlook.com") == 0
            || strcmp(host, "outlook.office.com") == 0;
    }

    // サーバの説明("[AUTHENTICATIONFAILED] Invalid credentials")を理由の後ろへ付ける
    void WithServerText(FixedString<PICO_STR_LL>& out, const char* why, const char* text){
        out.assign(why);
        if(text && *text){
            out.append(": ");
            out.append(text);
        }
    }
}

// ---------------------------------------------------------------------------
// 設定
// ---------------------------------------------------------------------------

bool ImapClient::configure(const Config& given){
    disconnect();
    configured_ = false;
    Config c = given;
    if(c.mailbox.empty()) c.mailbox.assign(kDefaultMailbox);
    //前のアカウントの一覧と本文は捨てる(別のサーバのUIDと混ざらないように)
    const bool account_changed = c.host != cfg_.host || c.port != cfg_.port || c.user != cfg_.user
                              || c.mailbox != cfg_.mailbox || c.auth != cfg_.auth;
    if(account_changed){
        count_ = 0;
        loaded_ = false;
        list_rev_++;
        exists_ = 0;
        uidvalidity_ = 0;
        want_body_ = 0;
        body_uid_ = 0;
        body_len_ = 0;
        body_[0] = '\0';
        body_rev_++;
    }
    if(c.auth == Auth::Microsoft && c.host.empty()) c.host.assign(MailOAuth::kImapHost);
    //別のユーザーのトークンで入らない
    if(account_changed) oauth_.forget();
    cfg_ = c;
    oauth_.configure(cfg_.oauth);
    if(cfg_.auth != Auth::Microsoft){
        oauth_.cancel();
        oauth_op_ = OAuthOp::None;
    }

    const bool need_password = cfg_.auth == Auth::Password;
    if(cfg_.host.empty() || cfg_.user.empty() || (need_password && cfg_.password.empty())){
        setStatus(State::NotConfigured, "メールのアカウントがありません。[設定] から入れてください");
        return false;
    }
    if(cfg_.auth == Auth::Microsoft && cfg_.oauth.client_id.empty()){
        setStatus(State::NotConfigured, "mail.cfg に oauth-client-id がありません。[設定] から入れてください");
        return false;
    }
    //LOGIN へそのまま入れるので、quoted string にできるか先に確かめる
    char q[PICO_STR_256B];
    if(!Imap::Quote(cfg_.user.c_str(), q, sizeof(q))){
        setStatus(State::NotConfigured, "ユーザー名に使えない文字があります(半角英数字と記号だけ)");
        return false;
    }
    if(need_password && !Imap::Quote(cfg_.password.c_str(), q, sizeof(q))){
        setStatus(State::NotConfigured, "パスワードに使えない文字があります(半角英数字と記号だけ)");
        return false;
    }
    if(!Imap::Quote(cfg_.mailbox.c_str(), q, sizeof(q))){
        setStatus(State::NotConfigured, "mail.cfg の mailbox に使えない文字があります");
        return false;
    }

    configured_ = true;
    wait_manual_ = false;
    backoff_ms_ = 0;
    next_poll_ms_ = millis();
    listed_exists_ = 0xFFFFFFFFu;
    setStatus(State::Ok, "");
    return true;
}

bool ImapClient::loadConfig(){
    if(!OSData::SD_usable){
        disconnect();
        configured_ = false;
        setStatus(State::NotConfigured, "SDカードがありません");
        return false;
    }

    Config c;
    char password[PICO_Config::kConfigMaxValueLen] = "";
    bool password_plain = false;
    const bool opened = PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_MAIL_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "auth") == 0){
                c.auth = (strcmp(value, "microsoft") == 0 || strcmp(value, "outlook") == 0) ? Auth::Microsoft : Auth::Password;
            }else if(strcmp(key, "oauth-client-id") == 0){
                c.oauth.client_id.assign(value);
            }else if(strcmp(key, "oauth-tenant") == 0){
                c.oauth.tenant.assign(value);
            }else if(strcmp(key, "oauth-authority") == 0){
                c.oauth.authority.assign(value);
            }else if(strcmp(key, "host") == 0){
                c.host.assign(value);
            }else if(strcmp(key, "port") == 0){
                int v = 0;
                if(PICO_Config::ConfigValue::AsInt(value, v) && v > 0 && v <= 65535) c.port = (uint16_t)v;
            }else if(strcmp(key, "tls") == 0){
                bool b = true;
                if(PICO_Config::ConfigValue::AsBool(value, b)) c.tls = b;
            }else if(strcmp(key, "user") == 0){
                c.user.assign(value);
            }else if(strcmp(key, "password") == 0){
                password_plain = PICO_Secret::NeedsUpgrade(value);
                if(!PICO_Secret::Decrypt(kPasswordPurpose, value, password, sizeof(password))) password[0] = '\0';
            }else if(strcmp(key, "mailbox") == 0){
                c.mailbox.assign(value);
            }else if(strcmp(key, "mark-read") == 0){
                bool b = true;
                if(PICO_Config::ConfigValue::AsBool(value, b)) c.mark_read = b;
            }
        });
    if(!opened){
        disconnect();
        configured_ = false;
        cfg_ = Config{};
        setStatus(State::NotConfigured, "メールのアカウントがありません。[設定] から入れてください");
        return false;
    }
    c.password.assign(password);
    //auth を書いていなくても Outlook のサーバなら Microsoft のサインインで入る
    if(IsMicrosoftHost(c.host.c_str()) && !c.oauth.client_id.empty()) c.auth = Auth::Microsoft;
    if(!configure(c)) return false;

    if(cfg_.auth == Auth::Microsoft && !oauth_.hasRefreshToken()){
        char token[MailOAuth::kRefreshTokenCap];
        bool rewrite = false;
        if(MailOAuth::LoadRefreshToken(token, sizeof(token), &rewrite)){
            oauth_.setRefreshToken(token);
            oauth_saved_refresh_rev_ = oauth_.refreshRevision();
            if(rewrite && MailOAuth::SaveRefreshToken(token)) LOG_APP_MSG("Mail: サインインを今の鍵で暗号化し直しました");
        }
    }

    //母艦で平文のまま書かれていたら、暗号化して書き直す(SDだけを落としたときに読まれないように)
    if(password_plain && password[0]){
        if(SaveAccount(nullptr, 0, nullptr, password)) LOG_APP_MSG("Mail: パスワードを暗号化して保存し直しました");
    }
    return true;
}

bool ImapClient::SaveAccount(const char* host, uint16_t port, const char* user, const char* password){
    const char* path = PICO_Path::FILE::CFG::SYS_MAIL_CFG;
    if(host && *host){
        if(!PICO_Config::SetValue(path, "auth", "password")) return false;
        if(!PICO_Config::SetValue(path, "host", host)) return false;
        char p[8];
        snprintf(p, sizeof(p), "%u", (unsigned)(port ? port : kDefaultPort));
        if(!PICO_Config::SetValue(path, "port", p)) return false;
    }
    if(user && *user){
        if(!PICO_Config::SetValue(path, "user", user)) return false;
    }
    if(password && *password){
        char enc[PICO_Config::kConfigMaxValueLen];
        if(!PICO_Secret::Encrypt(kPasswordPurpose, password, enc, sizeof(enc))){
            LOG_APP_WARN("Mail: パスワードを暗号化できませんでした(長すぎます)");
            return false;
        }
        if(!PICO_Config::SetValue(path, "password", enc)) return false;
    }
    return true;
}

bool ImapClient::SaveMicrosoftAccount(const char* user, const char* client_id){
    const char* path = PICO_Path::FILE::CFG::SYS_MAIL_CFG;
    //ユーザーが変わるなら、前のアカウントのサインインは消す
    char old_user[PICO_Config::kConfigMaxValueLen] = "";
    PICO_Config::ParseFile(path, [&](const char* key, const char* value){
        if(strcmp(key, "user") == 0) snprintf(old_user, sizeof(old_user), "%s", value);
    });
    if(!user || strcmp(old_user, user) != 0) MailOAuth::ClearRefreshToken();

    char p[8];
    snprintf(p, sizeof(p), "%u", (unsigned)kDefaultPort);
    return PICO_Config::SetValue(path, "auth", "microsoft")
        && PICO_Config::SetValue(path, "host", MailOAuth::kImapHost)
        && PICO_Config::SetValue(path, "port", p)
        && PICO_Config::SetValue(path, "tls", "true")
        && (!user || !*user || PICO_Config::SetValue(path, "user", user))
        && (!client_id || !*client_id || PICO_Config::SetValue(path, "oauth-client-id", client_id));
}

void ImapClient::setStatus(State s, const char* text){
    const bool changed = (s != state_) || strcmp(status_.c_str(), text ? text : "") != 0;
    state_ = s;
    status_.assign(text ? text : "");
    if(changed) status_rev_++;
}

void ImapClient::failPendingBody(const char* why){
    if(!want_body_) return;
    body_req_uid_ = want_body_;
    want_body_ = 0;
    finishBody(false, why);
}

void ImapClient::failLater(const char* why){
    LOG_APP_WARN("Mail: %s", why ? why : "");
    setStatus(State::Error, why);
    backoff_ms_ = backoff_ms_ ? backoff_ms_ * 2 : kMinBackoffMs;
    if(backoff_ms_ > kMaxBackoffMs) backoff_ms_ = kMaxBackoffMs;
    next_poll_ms_ = millis() + backoff_ms_;
    want_list_ = false;
    want_poll_ = false;
    if(cmd_ == Cmd::Body) finishBody(false, why);
    if(want_body_){
        body_req_uid_ = want_body_;
        want_body_ = 0;
        finishBody(false, why);
    }
}

// ---------------------------------------------------------------------------
// 操作
// ---------------------------------------------------------------------------

void ImapClient::refreshNow(){
    wait_manual_ = false;
    backoff_ms_ = 0;
    next_poll_ms_ = millis();
    want_list_ = true;
}

void ImapClient::openMessage(uint32_t uid){
    if(uid == 0) return;
    want_body_ = uid;
}

void ImapClient::closeMessage(){
    want_body_ = 0;
}

bool ImapClient::startSignIn(){
    if(!configured_ || cfg_.auth != Auth::Microsoft) return false;
    disconnect();
    oauth_.forget();
    oauth_op_ = OAuthOp::SignIn;
    wait_manual_ = true;   //サインインが終わるまで繋ぎに行かない
    if(!oauth_.startSignIn()){
        //すぐに失敗した(client_id が無い等)。理由は updateOAuth() で出す
        return false;
    }
    setStatus(State::SigningIn, "サインインを始めています…");
    return true;
}

void ImapClient::cancelSignIn(){
    if(oauth_op_ != OAuthOp::SignIn) return;
    oauth_.cancel();
    oauth_op_ = OAuthOp::None;
    oauth_seen_rev_ = oauth_.revision();
    setStatus(State::NeedSignIn, "サインインをやめました。[更新] でサインインし直せます");
}

bool ImapClient::ensureAccessToken(){
    if(oauth_.hasAccessToken()) return true;
    if(oauth_op_ != OAuthOp::None) return false;
    if(!oauth_.hasRefreshToken()){
        wait_manual_ = true;
        failPendingBody("Microsoft のサインインが必要です");
        setStatus(State::NeedSignIn, "Microsoft のサインインが必要です。[更新] を押してサインインしてください");
        return false;
    }
    oauth_op_ = OAuthOp::Refresh;
    oauth_.startRefresh();
    return false;
}

void ImapClient::updateOAuth(){
    oauth_.update();
    if(oauth_op_ == OAuthOp::SignIn && oauth_.phase() == MailOAuth::Phase::WaitUser){
        //案内(コードと残り時間)を状態の行へ
        FixedString<PICO_STR_LL> t;
        t.appendFormat("%s でコード %s を入れてください", oauth_.verificationUri(), oauth_.userCode());
        if(state_ != State::SigningIn || strcmp(status_.c_str(), t.c_str()) != 0) setStatus(State::SigningIn, t.c_str());
    }
    if(oauth_.revision() == oauth_seen_rev_) return;
    oauth_seen_rev_ = oauth_.revision();
    if(oauth_.busy()) return;   //途中の知らせ(コードが出た等)
    const OAuthOp op = oauth_op_;
    oauth_op_ = OAuthOp::None;
    if(op == OAuthOp::None) return;

    //更新用トークンが変わったら保存し直す(Microsoft は取り直すたびに新しいものを返す)
    if(oauth_.refreshRevision() != oauth_saved_refresh_rev_){
        oauth_saved_refresh_rev_ = oauth_.refreshRevision();
        if(oauth_.hasRefreshToken()){
            if(!MailOAuth::SaveRefreshToken(oauth_.refreshToken())) LOG_APP_WARN("Mail: サインインを保存できませんでした");
        }else{
            MailOAuth::ClearRefreshToken();
        }
    }

    switch(oauth_.result()){
        case MailOAuth::Result::Ok:
            if(op == OAuthOp::SignIn){
                LOG_APP_OK("Mail: Microsoft にサインインしました");
                auth_retried_ = false;
                setStatus(State::Ok, "");
                refreshNow();
            }
            return;
        case MailOAuth::Result::NeedSignIn:
        case MailOAuth::Result::Denied:
            wait_manual_ = true;
            want_list_ = false;
            want_poll_ = false;
            failPendingBody(oauth_.errorText());
            setStatus(State::NeedSignIn, oauth_.errorText());
            return;
        case MailOAuth::Result::Failed:
        case MailOAuth::Result::None:
        default:
            if(op == OAuthOp::SignIn){
                wait_manual_ = true;
                setStatus(State::NeedSignIn, oauth_.errorText());
            }else{
                failLater(oauth_.errorText());
            }
            return;
    }
}

const Imap::Summary* ImapClient::findMessage(uint32_t uid) const {
    for(int i = 0; i < count_; i++) if(list_[i].uid == uid) return &list_[i];
    return nullptr;
}

int ImapClient::unseenCount() const {
    int n = 0;
    for(int i = 0; i < count_; i++) if(!list_[i].seen) n++;
    return n;
}

// ---------------------------------------------------------------------------
// 接続
// ---------------------------------------------------------------------------

bool ImapClient::connectNow(){
    Url url;
    url.host = cfg_.host;
    url.port = cfg_.port;
    url.secure = cfg_.tls;
    reader_.reset();
    if(!net_.connect(url, kConnectTimeoutMs)){
        FixedString<PICO_STR_LL> why;
        why.assign(net_.errorText());
        why.appendFormat("(%s:%u)", cfg_.host.c_str(), (unsigned)cfg_.port);
        failLater(why.c_str());
        return false;
    }
    phase_ = Phase::Greeting;
    cmd_ = Cmd::None;
    cmd_started_ms_ = millis();
    return true;
}

void ImapClient::disconnect(){
    net_.close();
    reader_.reset();
    phase_ = Phase::Disconnected;
    cmd_ = Cmd::None;
    streaming_body_ = false;
}

void ImapClient::stop(){
    if(phase_ == Phase::Ready && cmd_ == Cmd::None){
        //行儀よく切る(返事は待たない)
        char line[32];
        const int n = snprintf(line, sizeof(line), "a%u LOGOUT\r\n", (unsigned)++tag_);
        net_.write((const uint8_t*)line, (size_t)n);
    }
    disconnect();
    want_body_ = 0;
    want_poll_ = false;
    //アクセストークンを取り直している途中ならやめる(サインインの途中もやめる。コードは出し直せる)
    if(oauth_op_ != OAuthOp::None){
        oauth_.cancel();
        if(oauth_op_ == OAuthOp::SignIn) setStatus(State::NeedSignIn, "サインインをやめました。[更新] でサインインし直せます");
        oauth_op_ = OAuthOp::None;
        oauth_seen_rev_ = oauth_.revision();
    }
}

bool ImapClient::send(Cmd cmd, const char* args){
    char line[512];
    const int n = snprintf(line, sizeof(line), "a%u %s\r\n", (unsigned)++tag_, args);
    if(n <= 0 || (size_t)n >= sizeof(line)){
        failLater("コマンドが長すぎます");
        disconnect();
        return false;
    }
    if(net_.write((const uint8_t*)line, (size_t)n) != (size_t)n){
        failLater("送れません(接続が切れました)");
        disconnect();
        return false;
    }
    cmd_ = cmd;
    cmd_tag_ = tag_;
    cmd_started_ms_ = millis();
    return true;
}

void ImapClient::startNext(){
    if(want_body_){
        body_req_uid_ = want_body_;
        want_body_ = 0;
        body_got_ = false;
        body_has_flags_ = false;
        body_seen_ = false;
        body_literal_size_ = 0;
        char args[96];
        snprintf(args, sizeof(args), "UID FETCH %u (UID FLAGS %s<0.%u>)", (unsigned)body_req_uid_,
                 cfg_.mark_read ? "BODY[]" : "BODY.PEEK[]", (unsigned)kBodyFetchBytes);
        send(Cmd::Body, args);
        return;
    }
    if(!wait_manual_ && Reached(millis(), next_poll_ms_)) want_poll_ = true;
    if(want_list_ || want_poll_) send(Cmd::Noop, "NOOP");
}

void ImapClient::update(bool network_up){
    if(!configured_) return;
    if(!network_up){
        if(phase_ != Phase::Disconnected) disconnect();
        if(oauth_op_ == OAuthOp::Refresh){
            oauth_.cancel();
            oauth_op_ = OAuthOp::None;
            oauth_seen_rev_ = oauth_.revision();
        }
        if(want_body_){
            body_req_uid_ = want_body_;
            want_body_ = 0;
            finishBody(false, "Wi-Fiに繋がっていません");
        }
        if(state_ != State::Offline) setStatus(State::Offline, "Wi-Fiに繋がっていません");
        return;
    }
    if(state_ == State::Offline){
        //繋がったら待たずに取り直す
        setStatus(State::Ok, "");
        refreshNow();
    }
    if(cfg_.auth == Auth::Microsoft){
        updateOAuth();
        if(oauth_op_ == OAuthOp::SignIn) return;
    }

    const uint32_t now = millis();
    if(phase_ == Phase::Disconnected){
        if(want_body_ && wait_manual_){
            //ログインできない間は繋ぎに行かない(何度も間違ったパスワードを送らない)
            body_req_uid_ = want_body_;
            want_body_ = 0;
            finishBody(false, status_.c_str());
            return;
        }
        const bool need = want_body_ != 0 || (!wait_manual_ && (want_list_ || Reached(now, next_poll_ms_)));
        if(need && (cfg_.auth != Auth::Microsoft || ensureAccessToken())) connectNow();
        return;
    }

    //受け取った分だけ読む(1フレームで読みすぎない)
    uint8_t buf[512];
    for(int budget = 0; budget < 16 && phase_ != Phase::Disconnected; budget++){
        if(net_.available() <= 0) break;
        const int n = net_.read(buf, sizeof(buf));
        if(n <= 0) break;
        reader_.feed(buf, (size_t)n, *this);
    }
    if(phase_ == Phase::Disconnected) return;

    if(!net_.connected() && net_.available() <= 0){
        const bool active = cmd_ != Cmd::None || phase_ != Phase::Ready;
        if(active) failLater("接続が切れました");
        disconnect();
        return;
    }
    if((cmd_ != Cmd::None || phase_ == Phase::Greeting) && Reached(now, cmd_started_ms_ + kResponseTimeoutMs)){
        failLater("サーバが応答しません");
        disconnect();
        return;
    }

    if(phase_ == Phase::Ready && cmd_ == Cmd::None) startNext();
}

// ---------------------------------------------------------------------------
// 応答
// ---------------------------------------------------------------------------

bool ImapClient::onLiteralStart(const char* line, size_t len, uint32_t size){
    if(cmd_ != Cmd::Body) return false;
    //本文(BODY[]<0>)だけを流す。BODY[HEADER...] 等は貯める
    if(!ContainsNoCase(line, len, "BODY[]")) return false;
    mime_.begin(body_, kBodyCap);
    streaming_body_ = true;
    body_got_ = true;
    body_literal_size_ = size;
    return true;
}

void ImapClient::onLiteralData(const uint8_t* data, size_t n){
    if(streaming_body_) mime_.feed(data, n);
}

void ImapClient::onLiteralEnd(){
    if(!streaming_body_) return;
    mime_.finish();
    streaming_body_ = false;
}

void ImapClient::onResponse(const char* text, size_t len, bool overflow){
    if(overflow) LOG_APP_WARN("Mail: 応答が長すぎるので途中まで読みます (%u B)", (unsigned)len);
    Imap::Response r;
    Imap::ParseResponse(text, r);
    switch(r.kind){
        case Imap::Kind::Tagged:
            handleTagged(r);
            break;
        case Imap::Kind::Untagged:
            handleUntagged(r, text, len);
            break;
        case Imap::Kind::Continuation:
            //AUTHENTICATE XOAUTH2 の続き。1回目はトークンを送り、2回目(断られたときの理由のJSON)には空行を返す
            if(cmd_ == Cmd::Auth){
                cmd_started_ms_ = millis();
                if(!auth_sent_){
                    auth_sent_ = true;
                    if(!sendXoauth2()){
                        failLater("送れません(接続が切れました)");
                        disconnect();
                    }
                }else{
                    net_.write((const uint8_t*)"\r\n", 2);
                }
            }
            break;
        default:
            break;
    }
}

void ImapClient::handleUntagged(const Imap::Response& r, const char* text, size_t len){
    if(r.status == Imap::Status::Bye){
        if(phase_ == Phase::Greeting || cmd_ != Cmd::None){
            FixedString<PICO_STR_LL> why;
            WithServerText(why, "サーバに切られました", r.text);
            failLater(why.c_str());
        }
        disconnect();
        return;
    }

    if(phase_ == Phase::Greeting){
        char args[PICO_STR_512B];
        if(r.status == Imap::Status::Ok && cfg_.auth == Auth::Microsoft){
            //SASL-IR は使わず、"+" を待ってから送る(どのサーバでも通る形)
            auth_sent_ = false;
            phase_ = Phase::Login;
            send(Cmd::Auth, "AUTHENTICATE XOAUTH2");
        }else if(r.status == Imap::Status::Ok){
            char u[PICO_STR_256B], p[PICO_STR_256B];
            Imap::Quote(cfg_.user.c_str(), u, sizeof(u));
            Imap::Quote(cfg_.password.c_str(), p, sizeof(p));
            snprintf(args, sizeof(args), "LOGIN %s %s", u, p);
            phase_ = Phase::Login;
            send(Cmd::Login, args);
        }else if(r.status == Imap::Status::PreAuth){
            char m[PICO_STR_256B];
            Imap::Quote(cfg_.mailbox.c_str(), m, sizeof(m));
            snprintf(args, sizeof(args), "SELECT %s", m);
            phase_ = Phase::Select;
            send(Cmd::Select, args);
        }else{
            failLater("IMAPのサーバではないようです");
            disconnect();
        }
        return;
    }

    if(r.has_number){
        if(Imap::KeywordIs(r, "EXISTS")){
            exists_ = r.number;
            return;
        }
        if(Imap::KeywordIs(r, "EXPUNGE")){
            if(exists_ > 0) exists_--;
            listed_exists_ = 0xFFFFFFFFu; //番号がずれたので、次の確認で取り直す
            return;
        }
        if(Imap::KeywordIs(r, "FETCH")){
            Imap::Fetch f;
            if(!Imap::ParseFetch(text, len, f)) return;
            if(cmd_ == Cmd::List && f.seq >= fetch_lo_ && f.seq <= exists_){
                int slot = -1;
                for(int i = 0; i < new_count_; i++) if(new_list_[i].seq == f.seq){ slot = i; break; }
                if(slot < 0 && new_count_ < kMaxMessages){
                    slot = new_count_++;
                    new_list_[slot] = Imap::Summary{};
                }
                if(slot >= 0) Imap::ApplyFetch(f, new_list_[slot]);
                return;
            }
            if(cmd_ == Cmd::Body && f.has_flags){
                body_has_flags_ = true;
                body_seen_ = f.seen;
            }
            //既読の変化等。一覧にあれば写す
            if(f.has_flags){
                for(int i = 0; i < count_; i++){
                    Imap::Summary& s = list_[i];
                    const bool match = f.has_uid ? (s.uid == f.uid) : (s.seq == f.seq);
                    if(!match) continue;
                    if(s.seen != f.seen || s.flagged != f.flagged || s.answered != f.answered){
                        s.seen = f.seen;
                        s.flagged = f.flagged;
                        s.answered = f.answered;
                        list_rev_++;
                    }
                    break;
                }
            }
            return;
        }
        return;
    }

    //* OK [UIDVALIDITY n] … メールボックスが作り直されたらUIDが変わるので一覧を取り直す
    if(r.status == Imap::Status::Ok && strncmp(r.text, "[UIDVALIDITY ", 13) == 0){
        const uint32_t v = (uint32_t)strtoul(r.text + 13, nullptr, 10);
        if(v != uidvalidity_){
            uidvalidity_ = v;
            listed_exists_ = 0xFFFFFFFFu;
        }
    }
}

void ImapClient::handleTagged(const Imap::Response& r){
    if(r.tag != cmd_tag_ || cmd_ == Cmd::None) return;
    const Cmd cmd = cmd_;
    cmd_ = Cmd::None;
    const bool ok = r.status == Imap::Status::Ok;
    FixedString<PICO_STR_LL> why;

    switch(cmd){
        case Cmd::Login:
            if(!ok){
                WithServerText(why, "ログインできません", r.text);
                LOG_APP_WARN("Mail: %s", why.c_str());
                //間違ったパスワードを繰り返し送らない(サーバに締め出される)。[更新] で再開する
                wait_manual_ = true;
                want_list_ = false;
                want_poll_ = false;
                if(want_body_){
                    body_req_uid_ = want_body_;
                    want_body_ = 0;
                    finishBody(false, why.c_str());
                }
                setStatus(State::AuthError, why.c_str());
                disconnect();
                return;
            }
            loginSucceeded();
            return;

        case Cmd::Auth:
            if(!ok){
                //トークンが期限より前に無効になった(取り消し等)かもしれない。取ってから時間が経っていれば1回だけ取り直す
                if(!auth_retried_ && oauth_.accessTokenAgeMs() > 60000u){
                    LOG_APP_WARN("Mail: XOAUTH2 を断られたのでアクセストークンを取り直します: %s", r.text);
                    auth_retried_ = true;
                    oauth_.dropAccessToken();
                    disconnect();
                    want_list_ = true;
                    next_poll_ms_ = millis();
                    return;
                }
                WithServerText(why, "ログインできません", r.text);
                why.append("(Outlook.com の設定で IMAP を有効にしているか確かめてください)");
                LOG_APP_WARN("Mail: %s", why.c_str());
                wait_manual_ = true;
                want_list_ = false;
                want_poll_ = false;
                oauth_.dropAccessToken();
                failPendingBody(why.c_str());
                setStatus(State::AuthError, why.c_str());
                disconnect();
                return;
            }
            auth_retried_ = false;
            loginSucceeded();
            return;

        case Cmd::Select:
            if(!ok){
                why.assign("メールボックス「");
                why.append(cfg_.mailbox);
                why.append("」を開けません");
                if(*r.text){ why.append(": "); why.append(r.text); }
                failLater(why.c_str());
                disconnect();
                return;
            }
            phase_ = Phase::Ready;
            //繋ぎ直したら一覧は必ず取り直す(切れている間に変わったかもしれない)
            want_list_ = true;
            return;

        case Cmd::Noop:
            if(!ok){
                WithServerText(why, "新着を確かめられません", r.text);
                failLater(why.c_str());
                disconnect();
                return;
            }
            {
                const bool fetch = want_list_ || exists_ != listed_exists_;
                want_list_ = false;
                want_poll_ = false;
                backoff_ms_ = 0;
                next_poll_ms_ = millis() + kPollMs;
                if(state_ != State::Ok || status_.length() > 0) setStatus(State::Ok, "");
                if(!fetch) return;
                if(exists_ == 0){
                    count_ = 0;
                    loaded_ = true;
                    listed_exists_ = 0;
                    list_rev_++;
                    return;
                }
                fetch_lo_ = exists_ > (uint32_t)kMaxMessages ? exists_ - kMaxMessages + 1 : 1;
                new_count_ = 0;
                char args[160];
                snprintf(args, sizeof(args),
                         "FETCH %u:%u (UID FLAGS INTERNALDATE RFC822.SIZE BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])",
                         (unsigned)fetch_lo_, (unsigned)exists_);
                listed_exists_ = exists_;
                send(Cmd::List, args);
            }
            return;

        case Cmd::List:
            if(!ok){
                listed_exists_ = 0xFFFFFFFFu;
                WithServerText(why, "一覧を取れません", r.text);
                failLater(why.c_str());
                disconnect();
                return;
            }
            //新しい順(番号の大きい順)に並べる。件数が少ないので挿入ソートで足りる
            for(int i = 1; i < new_count_; i++){
                Imap::Summary tmp = new_list_[i];
                int j = i - 1;
                while(j >= 0 && new_list_[j].seq < tmp.seq){
                    new_list_[j + 1] = new_list_[j];
                    j--;
                }
                new_list_[j + 1] = tmp;
            }
            for(int i = 0; i < new_count_; i++) list_[i] = new_list_[i];
            count_ = new_count_;
            loaded_ = true;
            list_rev_++;
            setStatus(State::Ok, "");
            return;

        case Cmd::Body:
            if(!ok){
                WithServerText(why, "メールを取れません", r.text);
                finishBody(false, why.c_str());
                return;
            }
            if(!body_got_){
                finishBody(false, "メールが見つかりません(削除されたかもしれません)");
                return;
            }
            finishBody(true, "");
            return;

        case Cmd::Logout:
        case Cmd::None:
            return;
    }
}

void ImapClient::loginSucceeded(){
    char m[PICO_STR_256B];
    char args[PICO_STR_256B + 16];
    Imap::Quote(cfg_.mailbox.c_str(), m, sizeof(m));
    snprintf(args, sizeof(args), "SELECT %s", m);
    phase_ = Phase::Select;
    send(Cmd::Select, args);
}

bool ImapClient::sendXoauth2(){
    //RFC にはないが Google/Microsoft の決まり: base64("user=" user ^A "auth=Bearer " token ^A ^A)
    Base64Writer w(net_);
    w.put("user=");
    w.put(cfg_.user.c_str());
    w.put("\x01" "auth=Bearer ");
    w.put(oauth_.accessToken(), oauth_.accessTokenLength());
    w.put("\x01\x01");
    return w.finish();
}

void ImapClient::finishBody(bool ok, const char* why){
    if(cmd_ == Cmd::Body) cmd_ = Cmd::None;
    if(streaming_body_){
        mime_.finish();
        streaming_body_ = false;
    }
    body_ok_ = ok;
    body_err_.assign(ok ? "" : (why ? why : ""));
    body_uid_ = body_req_uid_;
    if(ok){
        body_len_ = mime_.length();
        body_html_ = mime_.fromHtml();
        const Imap::Summary* s = findMessage(body_uid_);
        const bool cut_by_fetch = s ? (s->size > kBodyFetchBytes) : (body_literal_size_ >= kBodyFetchBytes);
        body_truncated_ = mime_.truncated() || cut_by_fetch;
        //既読にした(BODY[] で取るとサーバが \Seen を付ける)。応答に FLAGS があればそちらを信じる
        const bool seen = body_has_flags_ ? body_seen_ : cfg_.mark_read;
        for(int i = 0; i < count_; i++){
            if(list_[i].uid != body_uid_) continue;
            if(seen && !list_[i].seen){
                list_[i].seen = true;
                list_rev_++;
            }
            break;
        }
    }else{
        body_len_ = 0;
        body_[0] = '\0';
        body_html_ = false;
        body_truncated_ = false;
    }
    body_rev_++;
}
