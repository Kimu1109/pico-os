// キャプティブポータル(net/Captive_Portal)のうち、通信を伴わない中身のテスト。
//
// 判定(Judge: 204/中身の無い200 = 外へ出られる、3xx/中身のある200 = ポータル、失敗 = 出られない)と、
// HTMLを流しながら最初の<form>を読むFormScanner(<script>/<style>/コメントの中の偽のフォームを飛ばす・
// 文字参照・隠し項目/チェックボックス/選ばれたラジオ/最初の送信ボタン・<meta refresh>)、
// 送る本文と送り先(BuildBody/BuildTarget)を確かめる。1バイトずつ食わせても同じ結果になることも見る。
// 実際のHTTPの往復はPCビルドで偽のポータル(Pythonの小さなサーバ)に対して確かめた(docs/design/security-wifi.md)。
#include "net/Captive_Portal.hpp"
#include "system/Log_Functions.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// ログはここでは見ない
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eqStr(const char* actual, const char* expected, const char* label){
    const bool ok = (strcmp(actual, expected) == 0);
    printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", label);
    if(!ok){
        printf("       実測: [%s]\n       期待: [%s]\n", actual, expected);
        failures++;
    }
}

using namespace CaptivePortal;

static void Scan(Form& f, const std::string& html, bool byte_by_byte){
    FormScanner s(f);
    s.reset();
    if(byte_by_byte){
        for(char c : html) s.write(&c, 1);
    }else{
        s.write(html.data(), html.size());
    }
}

static const char* kPage =
    "<!DOCTYPE html><html><head>"
    "<script>var s = \"<form action='/evil'><input name=x>\"; if (a < b && c > d) {}</SCRIPT>"
    "<style>.x > .y { content: '<form>' }</style><!-- <form action=\"/comment\"> -- -->"
    "</head><body><h1>Welcome &amp; enjoy</h1>"
    "<FORM Method='post' action=\"login?a=1&amp;b=2\">"
    "<input type=\"hidden\" name=\"token\" value=\"ab&amp;c=&#49;&#x41;\">"
    "<input name=room placeholder='Room number'>"
    "<input type=password name=\"pw\" value=\"\">"
    "<label><input type=checkbox name=\"agree\"> I agree</label>"
    "<input type=\"radio\" name=\"plan\" value=\"free\" checked><input type=\"radio\" name=\"plan\" value=\"paid\">"
    "<input type=\"submit\" name=\"go\" value=\"Connect > now\">"
    "<input type=\"submit\" name=\"other\" value=\"No\">"
    "<input type=\"reset\" name=\"r\"><input type=\"text\" value=\"no name\">"
    "<button>plain</button>"
    "</form><form action=\"/second\"><input name=\"q\"></form></body></html>";

int main(){
    // ---- 判定 ----
    {
        Url probe;
        UrlTools::Parse(probe, kDefaultProbeUrl);
        FixedString<PICO_STR_LL> portal;
        check(Judge(true, 204, 0, "", probe, portal) == Verdict::Online && portal.empty(), "204は外へ出られる");
        check(Judge(true, 200, 0, "", probe, portal) == Verdict::Online, "中身の無い200も外へ出られる");
        check(Judge(false, 0, 0, "", probe, portal) == Verdict::NoInternet, "繋がらなければ出られない");
        check(Judge(true, 302, 0, "/login?x=1", probe, portal) == Verdict::Portal, "3xxはポータル");
        eqStr(portal.c_str(), "http://connectivitycheck.gstatic.com/login?x=1", "相対の転送先はプローブから解決");
        check(Judge(true, 302, 0, "https://portal.example/p", probe, portal) == Verdict::Portal, "絶対の転送先");
        eqStr(portal.c_str(), "https://portal.example/p", "絶対の転送先はそのまま");
        check(Judge(true, 200, 512, "", probe, portal) == Verdict::Portal, "中身のある200(横取り)はポータル");
        eqStr(portal.c_str(), "http://connectivitycheck.gstatic.com/generate_204", "横取りならプローブ自身を開く");
    }

    // ---- フォームの読み取り(まとめて/1バイトずつ) ----
    for(int pass = 0; pass < 2; pass++){
        const bool bytewise = (pass == 1);
        printf("-- %s --\n", bytewise ? "1バイトずつ" : "まとめて");
        Form f;
        Scan(f, kPage, bytewise);
        check(f.found && f.post, "最初のフォーム(POST)を見つける");
        eqStr(f.action.c_str(), "login?a=1&b=2", "actionの文字参照を戻す");
        check(f.has_script, "<script>があったことを覚える");
        check(f.count == 6, "送る項目は6つ(名前の無いもの・reset・選ばれていないラジオ・2つ目の送信ボタンは除く)");
        if(f.count == 6){
            eqStr(f.fields[0].name.c_str(), "token", "隠し項目");
            eqStr(f.fields[0].value.c_str(), "ab&c=1A", "&amp; &#49; &#x41; を戻す");
            check(f.fields[1].kind == FieldKind::Text && f.fields[1].value.empty(), "type無しはテキストの欄");
            eqStr(f.fields[1].label.c_str(), "Room number", "placeholderを見出しに");
            check(f.fields[2].kind == FieldKind::Password, "パスワードの欄");
            eqStr(f.fields[2].label.c_str(), "pw", "placeholderが無ければname");
            check(f.fields[3].kind == FieldKind::Check, "チェックボックス");
            eqStr(f.fields[3].value.c_str(), "on", "値の無いチェックボックスは on");
            eqStr(f.fields[4].value.c_str(), "free", "選ばれたラジオだけ");
            check(f.fields[5].kind == FieldKind::Submit, "送信ボタン");
            eqStr(f.fields[5].value.c_str(), "Connect > now", "引用符の中の > でタグを終えない");
        }
        check(f.missingInputs() == 2, "埋める欄は2つ");
    }

    // ---- 送る本文と送り先 ----
    {
        Form f;
        Scan(f, kPage, false);
        f.fields[1].value.assign("12 03");
        f.fields[2].value.assign("p@ss");
        check(f.missingInputs() == 0, "埋めたら0");
        FixedString<PICO_STR_1KiB> body;
        check(BuildBody(f, body), "本文を作れる");
        eqStr(body.c_str(), "token=ab%26c%3D1A&room=12%2003&pw=p%40ss&agree=on&plan=free&go=Connect%20%3E%20now",
              "application/x-www-form-urlencoded");
        Url base, target;
        UrlTools::Parse(base, "http://10.0.0.1/portal/start?x=9");
        check(BuildTarget(f, base, body, target), "送り先を作れる");
        eqStr(target.path.c_str(), "/portal/login", "actionをページから解決");
        eqStr(target.query.c_str(), "a=1&b=2", "POSTはactionのクエリをそのまま");

        f.post = false;
        f.action.clear();
        f.count = 1;
        BuildBody(f, body);
        check(BuildTarget(f, base, body, target), "GETの送り先");
        eqStr(target.path.c_str(), "/portal/start", "actionが空ならページ自身");
        eqStr(target.query.c_str(), "token=ab%26c%3D1A", "GETは本文をクエリに");
    }

    // ---- フォームが無いページ ----
    {
        Form f;
        Scan(f, "<html><head><meta http-equiv=\"Refresh\" content=\"0; URL='/portal/real'\"></head><body>wait</body></html>", false);
        check(!f.found, "フォームは無い");
        eqStr(f.refresh.c_str(), "/portal/real", "<meta refresh>の行き先(引用符を落とす)");

        Form g;
        Scan(g, "<form action=/a><input type=hidden name=k value=v><input type=hidden name=k2 value=v2>"
                "<input type=hidden name=k3><input type=hidden name=k4><input type=hidden name=k5>"
                "<input type=hidden name=k6><input type=hidden name=k7><input type=hidden name=k8>"
                "<input type=hidden name=k9><input type=hidden name=k10><input type=hidden name=k11>"
                "<input type=hidden name=k12><input type=hidden name=k13></form>", false);
        check(g.count == kMaxFields && g.overflow, "項目が多すぎれば上限で止めて印を付ける");
        eqStr(g.action.c_str(), "/a", "引用符の無い属性値");

        // 溜め場所(512B)を超える長いタグは捨てるが、その後は読み続ける
        std::string longtag = "<div data-x=\"" + std::string(800, 'a') + "\"><form method=post><input name=n value=1></form>";
        Form h;
        Scan(h, longtag, true);
        check(h.found && h.count == 1, "長すぎるタグの後も読み続ける");
    }

    printf("\n%s (失敗 %d 件)\n", failures == 0 ? "全て成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
