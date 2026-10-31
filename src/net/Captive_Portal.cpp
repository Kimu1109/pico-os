#include "net/Captive_Portal.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "system/I18n_Functions.hpp"

namespace {
    using namespace CaptivePortal;

    char Lower(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
    bool IsSpace(char c){ return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; }

    bool EqualsNoCase(const char* a, size_t alen, const char* b){
        const size_t blen = strlen(b);
        if(alen != blen) return false;
        for(size_t i = 0; i < alen; i++) if(Lower(a[i]) != b[i]) return false;
        return true;
    }

    // 文字参照のうちポータルで見かけるものだけ戻す(&amp; &lt; &gt; &quot; &apos; &#NN; &#xHH;)
    template <size_t N>
    void AppendDecoded(FixedString<N>& out, const char* s, size_t len){
        size_t i = 0;
        while(i < len){
            if(s[i] == '&'){
                size_t j = i + 1;
                while(j < len && j - i < 10 && s[j] != ';') j++;
                if(j < len && s[j] == ';'){
                    const char* e = s + i + 1;
                    const size_t elen = j - i - 1;
                    long code = -1;
                    if(EqualsNoCase(e, elen, "amp")) code = '&';
                    else if(EqualsNoCase(e, elen, "lt")) code = '<';
                    else if(EqualsNoCase(e, elen, "gt")) code = '>';
                    else if(EqualsNoCase(e, elen, "quot")) code = '"';
                    else if(EqualsNoCase(e, elen, "apos")) code = '\'';
                    else if(elen >= 2 && e[0] == '#'){
                        char num[12] = {0};
                        const bool hex = (e[1] == 'x' || e[1] == 'X');
                        const size_t start = hex ? 2 : 1;
                        if(elen - start < sizeof(num)){
                            memcpy(num, e + start, elen - start);
                            char* end = nullptr;
                            code = strtol(num, &end, hex ? 16 : 10);
                            if(!end || *end != '\0' || code <= 0 || code > 0x10FFFF) code = -1;
                        }
                    }
                    if(code >= 0){
                        // UTF-8で書く
                        char u[5] = {0};
                        if(code < 0x80){ u[0] = (char)code; }
                        else if(code < 0x800){ u[0] = (char)(0xC0 | (code >> 6)); u[1] = (char)(0x80 | (code & 0x3F)); }
                        else if(code < 0x10000){ u[0] = (char)(0xE0 | (code >> 12)); u[1] = (char)(0x80 | ((code >> 6) & 0x3F)); u[2] = (char)(0x80 | (code & 0x3F)); }
                        else { u[0] = (char)(0xF0 | (code >> 18)); u[1] = (char)(0x80 | ((code >> 12) & 0x3F)); u[2] = (char)(0x80 | ((code >> 6) & 0x3F)); u[3] = (char)(0x80 | (code & 0x3F)); }
                        out.append(u);
                        i = j + 1;
                        continue;
                    }
                }
            }
            out.append(s[i]);
            i++;
        }
    }

    // タグの中身("input type=hidden name='a' value=\"b\"")から属性を1つ探す。
    // 値の無い属性(checked 等)は present だけ真で、out は空
    template <size_t N>
    bool GetAttr(const char* tag, const char* want, FixedString<N>* out){
        const char* p = tag;
        while(*p && !IsSpace(*p) && *p != '/') p++; // タグ名を飛ばす
        while(*p){
            while(IsSpace(*p) || *p == '/') p++;
            if(!*p) break;
            const char* name = p;
            while(*p && !IsSpace(*p) && *p != '=' && *p != '/') p++;
            const size_t name_len = (size_t)(p - name);
            while(IsSpace(*p)) p++;
            const char* val = nullptr;
            size_t val_len = 0;
            if(*p == '='){
                p++;
                while(IsSpace(*p)) p++;
                if(*p == '"' || *p == '\''){
                    const char q = *p++;
                    val = p;
                    while(*p && *p != q) p++;
                    val_len = (size_t)(p - val);
                    if(*p) p++;
                }else{
                    val = p;
                    while(*p && !IsSpace(*p)) p++;
                    val_len = (size_t)(p - val);
                }
            }
            if(name_len > 0 && EqualsNoCase(name, name_len, want)){
                if(out){
                    out->clear();
                    if(val) AppendDecoded(*out, val, val_len);
                }
                return true;
            }
        }
        return false;
    }

    // タグ名(先頭の語)を小文字で
    void TagName(const char* tag, char* out, size_t cap){
        size_t n = 0;
        while(tag[n] && !IsSpace(tag[n]) && tag[n] != '/' && n + 1 < cap){ out[n] = Lower(tag[n]); n++; }
        // "</form" の / は残す
        out[n] = '\0';
        if(tag[0] == '/'){
            size_t k = 1;
            while(tag[k] && !IsSpace(tag[k]) && k + 1 < cap){ out[k] = Lower(tag[k]); k++; }
            out[0] = '/';
            out[k] = '\0';
        }
    }

    template <size_t N>
    bool LowerEquals(const FixedString<N>& s, const char* b){ return EqualsNoCase(s.c_str(), s.length(), b); }
}

// ============================================================================ 判定

CaptivePortal::Verdict CaptivePortal::Judge(bool request_ok, int status, size_t body_bytes, const char* location,
                                             const Url& probe, FixedString<PICO_STR_LL>& out_portal){
    out_portal.clear();
    if(!request_ok) return Verdict::NoInternet;
    // 204が本来の答え。中身の無い200もAndroidと同じく「外へ出られる」とみなす
    if(status == 204 || (status == 200 && body_bytes == 0)) return Verdict::Online;
    if(status >= 300 && status < 400 && location && location[0]){
        Url next;
        if(UrlTools::Resolve(next, probe, location) && UrlTools::FormatFull(out_portal, next)) return Verdict::Portal;
    }
    // 中身のあるページが返った(横取りされた)、または転送先が読めない: プローブのURL自身を開けばポータルが出る
    UrlTools::FormatFull(out_portal, probe);
    return Verdict::Portal;
}

// ============================================================================ フォーム

int CaptivePortal::Form::missingInputs() const {
    int n = 0;
    for(int i = 0; i < count; i++){
        if((fields[i].kind == FieldKind::Text || fields[i].kind == FieldKind::Password) && fields[i].value.empty()) n++;
    }
    return n;
}

void CaptivePortal::Form::clear(){
    found = post = overflow = has_script = false;
    action.clear();
    refresh.clear();
    for(int i = 0; i < kMaxFields; i++){
        fields[i].name.clear();
        fields[i].value.clear();
        fields[i].label.clear();
        fields[i].kind = FieldKind::Hidden;
    }
    count = 0;
}

void CaptivePortal::FormScanner::reset(){
    state_ = State::Text;
    tag_len_ = 0;
    tag_overflow_ = false;
    quote_ = 0;
    match_ = 0;
    in_form_ = false;
    form_done_ = false;
    form_.clear();
}

bool CaptivePortal::FormScanner::write(const void* data, size_t len){
    const char* p = (const char*)data;
    for(size_t i = 0; i < len; i++) feed(p[i]);
    return true;
}

void CaptivePortal::FormScanner::feed(char c){
    switch(state_){
        case State::Text:
            if(c == '<'){
                state_ = State::Tag;
                tag_len_ = 0;
                tag_overflow_ = false;
                quote_ = 0;
            }
            return;

        case State::Tag:
            if(quote_){
                if(c == quote_) quote_ = 0;
            }else if(c == '"' || c == '\''){
                // 属性値の中の > でタグを終えないように引用符を追う(タグ名の直後のものは除く)
                if(tag_len_ > 0) quote_ = c;
            }else if(c == '>'){
                tag_[tag_len_ < sizeof(tag_) ? tag_len_ : sizeof(tag_) - 1] = '\0';
                state_ = State::Text;
                if(!tag_overflow_) endTag();
                return;
            }
            if(tag_len_ + 1 < sizeof(tag_)) tag_[tag_len_++] = c;
            else tag_overflow_ = true;
            // "<!--" はコメント。中の > や < では終わらない
            if(tag_len_ == 3 && tag_[0] == '!' && tag_[1] == '-' && tag_[2] == '-'){
                state_ = State::Comment;
                match_ = 0;
            }
            return;

        case State::Comment:
            // "-->" を探す
            if(c == '-') { if(match_ < 2) match_++; }
            else if(c == '>' && match_ >= 2){ state_ = State::Text; match_ = 0; }
            else match_ = 0;
            return;

        case State::Raw: {
            // "</script" か "</style" を探す(大文字小文字は問わない)。見つけたら残りはタグとして読む
            const char* end = in_raw_style_ ? "</style" : "</script";
            const char lc = Lower(c);
            if(lc == end[match_]){
                match_++;
                if(end[match_] == '\0'){
                    state_ = State::Tag;
                    // 終わりのタグの名前をそのまま溜めた形にする(endTag()は何もしない)
                    snprintf(tag_, sizeof(tag_), "%s", end + 1);
                    tag_len_ = strlen(tag_);
                    tag_overflow_ = false;
                    quote_ = 0;
                    match_ = 0;
                }
            }else{
                match_ = (lc == '<') ? 1 : 0;
            }
            return;
        }
    }
}

void CaptivePortal::FormScanner::endTag(){
    char name[16];
    TagName(tag_, name, sizeof(name));
    const bool self_closing = tag_len_ > 0 && tag_[tag_len_ - 1] == '/';

    if(strcmp(name, "script") == 0 || strcmp(name, "style") == 0){
        if(name[1] == 'c') form_.has_script = true;
        if(!self_closing){
            state_ = State::Raw;
            in_raw_style_ = (name[1] == 't');
            match_ = 0;
        }
        return;
    }

    if(strcmp(name, "meta") == 0){
        FixedString<PICO_STR_S> equiv;
        FixedString<PICO_STR_LL> content;
        if(GetAttr(tag_, "http-equiv", &equiv) && LowerEquals(equiv, "refresh") && GetAttr(tag_, "content", &content)){
            // "0; url=https://..." の url= から後ろ
            const char* s = content.c_str();
            for(const char* q = s; *q; q++){
                if(Lower(q[0]) == 'u' && Lower(q[1]) == 'r' && Lower(q[2]) == 'l' && q[3] == '='){
                    const char* u = q + 4;
                    while(*u == '\'' || *u == '"' || IsSpace(*u)) u++;
                    form_.refresh.assign(u);
                    // 末尾の引用符を落とす
                    while(!form_.refresh.empty()){
                        const char last = form_.refresh.c_str()[form_.refresh.length() - 1];
                        if(last != '\'' && last != '"' && !IsSpace(last)) break;
                        FixedString<PICO_STR_LL> t;
                        t.assign(form_.refresh.c_str(), form_.refresh.length() - 1);
                        form_.refresh.assign(t.c_str());
                    }
                    break;
                }
            }
        }
        return;
    }

    if(strcmp(name, "form") == 0){
        if(form_done_ || form_.found) return; // 最初のフォームだけ
        form_.found = true;
        in_form_ = true;
        FixedString<PICO_STR_S> method;
        if(GetAttr(tag_, "method", &method)) form_.post = LowerEquals(method, "post");
        GetAttr(tag_, "action", &form_.action);
        return;
    }
    if(strcmp(name, "/form") == 0){
        if(in_form_){ in_form_ = false; form_done_ = true; }
        return;
    }
    if(!in_form_) return;

    const bool is_input = strcmp(name, "input") == 0;
    const bool is_button = strcmp(name, "button") == 0;
    if(!is_input && !is_button) return;

    FixedString<PICO_STR_S> type;
    if(!GetAttr(tag_, "type", &type) || type.empty()) type.assign(is_button ? "submit" : "text");
    Field f;
    if(!GetAttr(tag_, "name", &f.name) || f.name.empty()){
        return; // 名前の無い項目は送られない
    }
    GetAttr(tag_, "value", &f.value);

    if(LowerEquals(type, "hidden")){
        f.kind = FieldKind::Hidden;
    }else if(LowerEquals(type, "password")){
        f.kind = FieldKind::Password;
    }else if(LowerEquals(type, "checkbox")){
        f.kind = FieldKind::Check;  // 同意の印はチェックして送る
        if(f.value.empty()) f.value.assign("on");
    }else if(LowerEquals(type, "radio")){
        if(!GetAttr<PICO_STR_S>(tag_, "checked", nullptr)) return; // 選ばれているものだけ
        f.kind = FieldKind::Check;
        if(f.value.empty()) f.value.assign("on");
    }else if(LowerEquals(type, "submit") || LowerEquals(type, "image")){
        for(int i = 0; i < form_.count; i++) if(form_.fields[i].kind == FieldKind::Submit) return; // 最初の1つ
        f.kind = FieldKind::Submit;
    }else if(LowerEquals(type, "button") || LowerEquals(type, "reset") || LowerEquals(type, "file")){
        return;
    }else{
        f.kind = FieldKind::Text; // text / email / tel / number / url 等
    }
    if(!GetAttr(tag_, "placeholder", &f.label) || f.label.empty()) f.label.assign(f.name.c_str());

    if(form_.count >= kMaxFields){
        form_.overflow = true;
        return;
    }
    Field& dst = form_.fields[form_.count++];
    dst.kind = f.kind;
    dst.name.assign(f.name.c_str());
    dst.value.assign(f.value.c_str());
    dst.label.assign(f.label.c_str());
}

bool CaptivePortal::BuildBody(const Form& form, FixedString<PICO_STR_1KiB>& out){
    out.clear();
    FixedString<PICO_STR_LL> enc_name;
    FixedString<PICO_STR_512B> enc_value;
    for(int i = 0; i < form.count; i++){
        const Field& f = form.fields[i];
        if(!UrlTools::EncodeComponent(enc_name, f.name.c_str())) return false;
        if(!UrlTools::EncodeComponent(enc_value, f.value.c_str())) return false;
        if(!out.empty() && !out.append("&")) return false;
        if(!out.append(enc_name) || !out.append("=") || !out.append(enc_value)) return false;
    }
    return true;
}

bool CaptivePortal::BuildTarget(const Form& form, const Url& base, const FixedString<PICO_STR_1KiB>& body, Url& out){
    if(form.action.empty()){
        out = base;
    }else if(!UrlTools::Resolve(out, base, form.action.c_str())){
        return false;
    }
    if(!form.post){
        if(!out.query.assign(body.c_str())) return false; // GETはクエリに載せる(入りきらなければ送れない)
    }
    return true;
}

// ============================================================================ 判定のTask

bool CaptivePortal::CaptiveProbe::begin(const char* probe_url){
    status = TaskTools::PROCESSING;
    verdict_ = Verdict::NoInternet;
    portal_.clear();
    sink_.bytes = 0;
    if(!UrlTools::Parse(probe_, probe_url)){
        status = TaskTools::FAILED;
        return false;
    }
    req_.setFollowRedirects(false);
    if(!req_.begin(probe_, HttpRequest::Method::GET, &sink_)){
        status = TaskTools::FAILED;
        return false;
    }
    return true;
}

void CaptivePortal::CaptiveProbe::update(){
    if(status != TaskTools::PROCESSING) return;
    req_.update();
    const TaskTools::Status s = req_.getStatus();
    if(s == TaskTools::PROCESSING) return;
    const HttpResponse& r = req_.response();
    verdict_ = Judge(s == TaskTools::SUCCESS, r.statusCode(), sink_.bytes, r.location().c_str(), probe_, portal_);
    status = TaskTools::SUCCESS;
}

// ============================================================================ ログインのTask

bool CaptivePortal::CaptiveLogin::fetch(const Url& url){
    page_ = url;
    scanner_.reset();
    phase_ = Phase::Fetching;
    return req_.begin(url, HttpRequest::Method::GET, &scanner_);
}

void CaptivePortal::CaptiveLogin::fail(const char* text){
    fail_.assign(text);
    phase_ = Phase::Failed;
    status = TaskTools::FAILED;
}

bool CaptivePortal::CaptiveLogin::begin(const char* portal_url){
    status = TaskTools::PROCESSING;
    refresh_hops_ = 0;
    fail_.clear();
    Url url;
    if(!UrlTools::Parse(url, portal_url)){
        fail(TR("ポータルのURLを読めません"));
        return false;
    }
    if(!fetch(url)){
        fail(TR("ポータルへ繋げません"));
        return false;
    }
    return true;
}

int CaptivePortal::CaptiveLogin::missingIndex(int i) const {
    int n = 0;
    for(int k = 0; k < form_.count; k++){
        const Field& f = form_.fields[k];
        if((f.kind == FieldKind::Text || f.kind == FieldKind::Password) && f.value.empty()){
            if(n == i) return k;
            n++;
        }
    }
    return -1;
}

void CaptivePortal::CaptiveLogin::setValue(int field_index, const char* value){
    if(field_index < 0 || field_index >= form_.count) return;
    form_.fields[field_index].value.assign(value ? value : "");
    if(phase_ == Phase::NeedsInput && form_.missingInputs() == 0) phase_ = Phase::Ready;
}

bool CaptivePortal::CaptiveLogin::submit(){
    if(phase_ != Phase::Ready && phase_ != Phase::NeedsInput) return false;
    if(!BuildBody(form_, body_)){
        fail(TR("送る値が長すぎます"));
        return false;
    }
    Url target;
    if(!BuildTarget(form_, page_, body_, target)){
        fail(TR("送り先のURLを作れません"));
        return false;
    }
    phase_ = Phase::Submitting;
    const bool ok = form_.post
        ? req_.begin(target, HttpRequest::Method::POST, &discard_, body_.c_str(), body_.length(),
                     "application/x-www-form-urlencoded")
        : req_.begin(target, HttpRequest::Method::GET, &discard_);
    if(!ok){
        fail(TR("ポータルへ送れません"));
        return false;
    }
    return true;
}

void CaptivePortal::CaptiveLogin::update(){
    if(status != TaskTools::PROCESSING) return;
    if(phase_ != Phase::Fetching && phase_ != Phase::Submitting) return;
    req_.update();
    const TaskTools::Status s = req_.getStatus();
    if(s == TaskTools::PROCESSING) return;

    if(phase_ == Phase::Submitting){
        // 送れたら(応答の中身は問わない)終わり。通ったかは呼び出し側がもう一度判定して確かめる
        if(s == TaskTools::SUCCESS){
            phase_ = Phase::Done;
            status = TaskTools::SUCCESS;
        }else{
            FixedString<PICO_STR_L> t;
            t.assign(TR("送れませんでした: "));
            t.append(req_.failureToStr());
            fail(t.c_str());
        }
        return;
    }

    // Fetching
    if(s != TaskTools::SUCCESS){
        FixedString<PICO_STR_L> t;
        t.assign(TR("ページを取れませんでした: "));
        t.append(req_.failureToStr());
        fail(t.c_str());
        return;
    }
    page_ = req_.currentUrl(); // 転送された先を基準にactionを解決する
    if(!form_.found){
        // <meta refresh> で次のページへ送るだけのポータルがある(2回まで追う)
        if(!form_.refresh.empty() && refresh_hops_ < 2){
            Url next;
            if(UrlTools::Resolve(next, page_, form_.refresh.c_str())){
                refresh_hops_++;
                if(fetch(next)) return;
            }
        }
        fail(form_.has_script ? TR("フォームが無いページです(JavaScriptで動くポータルは使えません)")
                              : TR("ページにフォームがありません"));
        return;
    }
    phase_ = form_.missingInputs() > 0 ? Phase::NeedsInput : Phase::Ready;
}
