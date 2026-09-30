#include "gui/scenes/SshScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "ssh/Ssh_Util.hpp"
#include "storage/SD_Path.hpp"
#include "util/Utf8Byte.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>

// ---------------------------------------------------------------- 設定

void SshScene::loadConfig(){
    if(!OSData::SD_usable || !OSData::SD.exists(PICO_Path::FILE::CFG::SYS_SSH_CFG)) return;
    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_SSH_CFG, [this](const char* key, const char* value){
        if(strcmp(key, "target") == 0) this->target_default.assign(value);
        else if(strcmp(key, "font") == 0) this->small_font = (strcmp(value, "small") == 0);
    });
}

// ---------------------------------------------------------------- 表示

void SshScene::refreshButtons(){
    if(this->conn_button){
        const bool active = this->isActive();
        this->conn_button->setText(active ? "切断" : "接続");
    }
    if(this->font_button) this->font_button->setText(this->small_font ? "文字:小" : "文字:大");
}

void SshScene::layout(){
    if(!this->view || !this->keybar) return;
    //据え置きのキーボードが出ていれば、その上端までに縮める
    const int bottom = KeyboardFunctions::IsDocked() ? KeyboardFunctions::VisibleTop() : SCREEN_HEIGHT;
    if(bottom == this->last_bottom && this->small_font == this->last_small) return;
    this->last_bottom = bottom;
    this->last_small = this->small_font;

    const int bar_y = bottom - kKeyBarH;
    if(this->keybar->getLocalRect().y != bar_y){
        this->keybar->markdirty(this->keybar->getScreenRect());
        this->keybar->setY(bar_y);
    }
    const int top = this->view->getLocalRect().y;
    this->view->setSmallFont(this->small_font);
    this->view->setH(bar_y - top);

    const int cols = this->view->fitCols();
    const int rows = this->view->fitRows();
    this->term.resize(cols, rows);
    this->view->needsRender();
    if(this->client){
        this->client->setTerminalSize(cols, rows, cols * this->view->cellW(), rows * this->view->cellH());
    }
}

// ---------------------------------------------------------------- 接続の流れ

void SshScene::startPrompt(Prompt p, const char* text, bool echo){
    this->prompt = p;
    this->line.clear();
    this->line_echo = echo;
    this->say(text);
}

void SshScene::startTargetPrompt(){
    char buf[PICO_STR_LL];
    if(this->target_default.empty()){
        snprintf(buf, sizeof(buf), "\r\n接続先 (ユーザー@ホスト[:ポート] [-J 踏み台]): ");
    }else{
        snprintf(buf, sizeof(buf), "\r\n接続先 [%s]: ", this->target_default.c_str());
    }
    this->startPrompt(Prompt::Target, buf, true);
}

bool SshScene::ParseEndpoint(const char* s, size_t n, FixedString<PICO_STR_M>& user,
                             FixedString<PICO_STR_M>& host, uint16_t& port){
    while(n > 0 && s[0] == ' '){ s++; n--; }
    while(n > 0 && s[n - 1] == ' ') n--;
    const char* at = (const char*)memchr(s, '@', n);
    if(!at || at == s || at + 1 >= s + n) return false;
    user.assign(s, (size_t)(at - s));
    const char* h = at + 1;
    const size_t hn = (size_t)(s + n - h);
    //最後の':'より後が数字だけならポート
    const char* colon = nullptr;
    for(size_t i = 0; i < hn; i++) if(h[i] == ':') colon = h + i;
    port = 22;
    size_t host_n = hn;
    if(colon){
        long p = 0;
        const char* q = colon + 1;
        if(q >= h + hn) return false;
        for(; q < h + hn; q++){
            if(*q < '0' || *q > '9') return false;
            p = p * 10 + (*q - '0');
            if(p > 65535) return false;
        }
        if(p <= 0) return false;
        port = (uint16_t)p;
        host_n = (size_t)(colon - h);
    }
    host.assign(h, host_n);
    return !host.empty() && memchr(host.c_str(), ' ', host.length()) == nullptr &&
           memchr(user.c_str(), ' ', user.length()) == nullptr;
}

bool SshScene::parseTarget(const char* s){
    // "user@host[:port]" または "user@host[:port] -J user@jump[:port]"
    const char* j = strstr(s, " -J ");
    if(!j){
        this->use_jump = false;
        return ParseEndpoint(s, strlen(s), this->user, this->host, this->port);
    }
    this->use_jump = true;
    return ParseEndpoint(s, (size_t)(j - s), this->user, this->host, this->port) &&
           ParseEndpoint(j + 4, strlen(j + 4), this->jump_user, this->jump_host, this->jump_port);
}

void SshScene::hostOf(SshClient* c, const char*& h, uint16_t& p, const char*& u) const {
    if(c && c == this->jump){
        h = this->jump_host.c_str(); p = this->jump_port; u = this->jump_user.c_str();
    }else{
        h = this->host.c_str(); p = this->port; u = this->user.c_str();
    }
}

void SshScene::submitLine(){
    const Prompt p = this->prompt;
    this->prompt = Prompt::None;

    switch(p){
        case Prompt::Target: {
            FixedString<PICO_STR_L> t = this->line;
            if(t.empty()) t = this->target_default;
            if(t.empty()){
                this->startTargetPrompt();
                return;
            }
            if(!this->parseTarget(t.c_str())){
                this->say("\x1b[91m「ユーザー名@ホスト[:ポート]」の形で入力してください。"
                          "踏み台を通すときは後ろに「 -J ユーザー名@踏み台[:ポート]」\x1b[0m");
                this->startTargetPrompt();
                return;
            }
            if(t != this->target_default){
                this->target_default = t;
                if(OSData::SD_usable) PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SSH_CFG, "target", t.c_str());
            }
            char buf[PICO_STR_LL];
            if(this->use_jump){
                snprintf(buf, sizeof(buf), "踏み台 %s:%u へ接続しています...\r\n",
                         this->jump_host.c_str(), (unsigned)this->jump_port);
            }else{
                snprintf(buf, sizeof(buf), "%s:%u へ接続しています...\r\n", this->host.c_str(), (unsigned)this->port);
            }
            this->say(buf);
            this->connect_wait_frames = 2;
            return;
        }
        case Prompt::HostKey: {
            SshClient* c = this->prompt_client;
            if(!c) return;
            const char* h; uint16_t pt; const char* u;
            this->hostOf(c, h, pt, u);
            if(this->line == "yes" || this->line == "y" || this->line == "YES"){
                if(SshUtil::AddKnownHost(PICO_Path::FILE::SSH_KNOWN_HOSTS, h, pt, c->hostKey())){
                    this->say("known_hosts へ追加しました\r\n");
                }else{
                    this->say("\x1b[93m(known_hosts へ書き込めませんでした。次回も確認します)\x1b[0m\r\n");
                }
                c->acceptHostKey(true);
            }else{
                c->acceptHostKey(false);
            }
            //次に見えた状態は(同じ状態へ戻ってきた場合も)必ず onStateChanged() へ通す
            if(c == this->jump) this->last_jump_state = SshClient::State::Idle;
            else this->last_state = SshClient::State::Idle;
            return;
        }
        case Prompt::Secret: {
            SshClient* c = this->prompt_client;
            if(c) c->providePassword(this->line.c_str());
            //パスワードが違うと同じ NeedPassword へすぐ戻ってくるので、状態の変化として拾えるようにする
            if(c == this->jump) this->last_jump_state = SshClient::State::Idle;
            else this->last_state = SshClient::State::Idle;
            //打った内容を残さない
            memset((void*)this->line.c_str(), 0, this->line.length());
            this->line.clear();
            return;
        }
        case Prompt::None:
            return;
    }
}

void SshScene::OnOutput(void* ctx, const uint8_t* data, size_t len){
    static_cast<SshScene*>(ctx)->term.write(data, len);
}

void SshScene::dropClient(){
    //中(client)は通り道(tunnel)を、通り道は踏み台(jump)を指しているので、この順に片付ける
    if(this->client){
        this->client->disconnect();
        delete this->client;
        this->client = nullptr;
    }
    delete this->tunnel;
    this->tunnel = nullptr;
    if(this->jump){
        this->jump->disconnect();
        delete this->jump;
        this->jump = nullptr;
    }
    this->prompt_client = nullptr;
    this->last_state = SshClient::State::Idle;
    this->last_jump_state = SshClient::State::Idle;
}

SshClient* SshScene::newClient(){
    SshClient* c = new SshClient();
    c->setOutput(&SshScene::OnOutput, this);

    //公開鍵(あれば)。踏み台と相手の両方で同じ鍵を使う
    uint8_t secret[64], pub[32];
    switch(SshUtil::LoadPrivateKey(PICO_Path::FILE::SSH_ID_ED25519, secret, pub)){
        case SshUtil::KeyResult::Ok:
            c->setIdentity(secret, pub);
            break;
        case SshUtil::KeyResult::NotFound:
            break;
        case SshUtil::KeyResult::Encrypted:
            this->say("\x1b[93m鍵がパスフレーズ付きのため使えません(パスワードで続けます)\x1b[0m\r\n");
            break;
        case SshUtil::KeyResult::Unsupported:
            this->say("\x1b[93m鍵の種類が ssh-ed25519 ではないため使えません\x1b[0m\r\n");
            break;
        case SshUtil::KeyResult::Broken:
            this->say("\x1b[93m鍵ファイルを読めませんでした\x1b[0m\r\n");
            break;
    }
    memset(secret, 0, sizeof(secret));
    return c;
}

void SshScene::beginConnect(){
    this->dropClient();

    if(!NetworkFunctions::IsConnected()){
        this->say("\x1b[91mWi-Fiに繋がっていません\x1b[0m\r\n");
        this->startTargetPrompt();
        return;
    }

    SshClient* first = this->newClient();
    const char* h; uint16_t pt; const char* u;
    if(this->use_jump){
        this->jump = first;
        this->tunnel = new SshTunnel();
        this->tunnel->attach(this->jump);
        this->jump->setForward(this->host.c_str(), this->port);
        h = this->jump_host.c_str(); pt = this->jump_port; u = this->jump_user.c_str();
    }else{
        this->client = first;
        if(this->view){
            this->client->setTerminalSize(this->term.cols(), this->term.rows(),
                                          this->term.cols() * this->view->cellW(), this->term.rows() * this->view->cellH());
        }
        h = this->host.c_str(); pt = this->port; u = this->user.c_str();
    }

    LOG_APP_MSG("SSH: %s@%s:%u へ接続", u, h, (unsigned)pt);
    if(!first->connect(h, pt, u)){
        char buf[PICO_STR_LL];
        snprintf(buf, sizeof(buf), "\x1b[91m%s\x1b[0m\r\n", first->errorText());
        this->say(buf);
        this->dropClient();
        this->startTargetPrompt();
    }
    this->refreshButtons();
}

void SshScene::onJumpOpen(){
    //踏み台の通り道が開いた。その上で相手とSSHする
    char buf[PICO_STR_LL];
    snprintf(buf, sizeof(buf), "踏み台経由で %s:%u へ接続しています...\r\n", this->host.c_str(), (unsigned)this->port);
    this->say(buf);
    this->client = this->newClient();
    if(this->view){
        this->client->setTerminalSize(this->term.cols(), this->term.rows(),
                                      this->term.cols() * this->view->cellW(), this->term.rows() * this->view->cellH());
    }
    this->client->connectVia(this->tunnel, this->user.c_str());
    this->last_state = SshClient::State::Idle;
}

void SshScene::onStateChanged(SshClient* c, SshClient::State s){
    char buf[PICO_STR_512B];
    const char* h; uint16_t pt; const char* u;
    this->hostOf(c, h, pt, u);
    const bool is_jump = (c == this->jump);

    switch(s){
        case SshClient::State::HostKeyCheck: {
            const SshUtil::HostStatus st = SshUtil::CheckKnownHost(PICO_Path::FILE::SSH_KNOWN_HOSTS, h, pt, c->hostKey());
            char fp[80];
            SshUtil::Fingerprint(c->hostKey(), fp, sizeof(fp));
            if(st == SshUtil::HostStatus::Match){
                c->acceptHostKey(true);
                return;
            }
            if(st == SshUtil::HostStatus::Mismatch){
                snprintf(buf, sizeof(buf),
                    "\x1b[91m警告: %s のホスト鍵が以前と違います!\r\n"
                    "なりすましの可能性があります。心当たりがあれば\r\n"
                    "%s から該当する行を消してください。\r\n"
                    "今の鍵: %s\x1b[0m\r\n",
                    h, PICO_Path::FILE::SSH_KNOWN_HOSTS, fp);
                this->say(buf);
                c->acceptHostKey(false);
                return;
            }
            snprintf(buf, sizeof(buf),
                "%s%s は初めて接続するホストです。\r\nED25519の鍵の指紋:\r\n%s\r\n", is_jump ? "踏み台 " : "", h, fp);
            this->say(buf);
            this->prompt_client = c;
            this->startPrompt(Prompt::HostKey, "このホストを信頼して接続しますか? (yes/no): ", true);
            return;
        }
        case SshClient::State::NeedPassword: {
            snprintf(buf, sizeof(buf), "%s@%s の%s", u, h, c->promptText());
            this->prompt_client = c;
            this->startPrompt(Prompt::Secret, buf, c->promptEcho());
            return;
        }
        case SshClient::State::Open:
            this->prompt = Prompt::None;
            if(is_jump) this->onJumpOpen();
            return;
        case SshClient::State::Closed: {
            this->prompt = Prompt::None;
            //踏み台が先に切れた場合は、中の「接続が切れました」より踏み台の理由の方が役に立つ
            const char* why = c->errorText();
            if(!is_jump && this->jump && this->jump->state() == SshClient::State::Closed &&
               strcmp(why, "接続が切れました") == 0){
                why = this->jump->errorText();
            }
            snprintf(buf, sizeof(buf), "\r\n\x1b[0m\x1b[93m%s%s\x1b[0m\r\n", is_jump ? "踏み台: " : "", why);
            //代替画面(vim等)の途中で切れた場合に備えて、普通の画面へ戻す
            //(スクロール範囲の指定を戻すとカーソルが左上へ飛ぶので、保存/復元で挟む)
            this->say("\x1b[?1049l\x1b[?25h\x1b" "7\x1b[r\x1b" "8");
            this->say(buf);
            this->dropClient();
            this->startTargetPrompt();
            this->refreshButtons();
            return;
        }
        default:
            return;
    }
}

// ---------------------------------------------------------------- 入力

void SshScene::sendToShell(const char* s, size_t n){
    if(!this->client || !this->client->isOpen()) return;
    this->client->send((const uint8_t*)s, n);
    if(this->view) this->view->scrollToBottom();
}

void SshScene::inputText(const char* s, size_t n){
    if(n == 0) return;
    if(this->client && this->client->isOpen()){
        //Ctrlが点いていれば、最初の1文字を制御文字にする
        if(this->keybar && this->keybar->getCtrl() && (uint8_t)s[0] < 0x80){
            char c = s[0];
            if(c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            char code;
            if(c >= '@' && c <= '_') code = (char)(c & 0x1F);
            else if(c == '?') code = 0x7F;
            else if(c == ' ') code = 0;
            else code = c;
            this->keybar->setCtrl(false);
            this->sendToShell(&code, 1);
            s++;
            n--;
            if(n == 0) return;
        }
        this->sendToShell(s, n);
        return;
    }
    if(this->prompt == Prompt::None) return;

    //端末の中で打っている行(改行・制御文字は入れない)
    for(size_t i = 0; i < n; ){
        const int len = Utf8CharBytesFromLeadByte((uint8_t)s[i]);
        if(i + (size_t)len > n) break;
        if(len == 1 && (uint8_t)s[i] < 0x20){ i++; continue; }
        if(!this->line.append(s + i, (size_t)len)) break;
        if(this->line_echo) this->term.write((const uint8_t*)(s + i), (size_t)len);
        i += (size_t)len;
    }
}

void SshScene::inputEnter(){
    if(this->client && this->client->isOpen()){
        this->sendToShell("\r", 1);
        return;
    }
    if(this->prompt == Prompt::None) return;
    this->say("\r\n");
    this->submitLine();
}

void SshScene::inputBackspace(){
    if(this->client && this->client->isOpen()){
        this->sendToShell("\x7f", 1);
        return;
    }
    if(this->prompt == Prompt::None || this->line.empty()) return;
    const FixedString<5> last = this->line.lastChar();
    this->line.removeLastChar();
    if(!this->line_echo) return;
    //全角は2セルぶん戻す
    uint32_t cp = 0;
    const int n = (int)last.length();
    if(n == 1) cp = (uint8_t)last[0];
    else if(n == 2) cp = ((uint8_t)last[0] & 0x1F) << 6 | ((uint8_t)last[1] & 0x3F);
    else if(n == 3) cp = ((uint8_t)last[0] & 0x0F) << 12 | ((uint8_t)last[1] & 0x3F) << 6 | ((uint8_t)last[2] & 0x3F);
    else cp = 0x20000;
    if(VtTerminal::CharWidth(cp) == 2) this->say("\b\b  \b\b");
    else this->say("\b \b");
}

void SshScene::inputArrow(char dir){
    if(!this->client || !this->client->isOpen()) return;
    char seq[4] = { '\x1b', this->term.appCursorKeys() ? 'O' : '[', dir, 0 };
    this->sendToShell(seq, 3);
}

void SshScene::inputInterrupt(){
    if(this->client && this->client->isOpen()){
        this->sendToShell("\x03", 1);
        return;
    }
    //接続の途中(ホスト鍵の確認・パスワード)なら中止、接続先の入力なら打ち直し
    this->say("^C\r\n");
    if(this->client || this->jump){
        this->prompt = Prompt::None;
        //次のonUpdate()でClosedとして後始末される
        if(this->client) this->client->disconnect();
        else this->jump->disconnect();
        return;
    }
    this->connect_wait_frames = -1;
    this->startTargetPrompt();
}

void SshScene::onKeyBar(TermKeyBar::Key key){
    switch(key){
        case TermKeyBar::Esc:   this->sendToShell("\x1b", 1); break;
        case TermKeyBar::Tab:   this->sendToShell("\t", 1); break;
        case TermKeyBar::Ctrl:  break; // 点灯の切り替えはバー自身が行う
        case TermKeyBar::Up:    this->inputArrow('A'); break;
        case TermKeyBar::Down:  this->inputArrow('B'); break;
        case TermKeyBar::Left:  this->inputArrow('D'); break;
        case TermKeyBar::Right: this->inputArrow('C'); break;
        case TermKeyBar::CtrlC: this->inputInterrupt(); break;
        default: break;
    }
}

void SshScene::openKeyboard(){
    KeyboardFunctions::Show(this, KeyboardFunctions::Layout::English, true);
}

// ---------------------------------------------------------------- ITextInputTarget

void SshScene::onShow(ITextInputWidget* keyboard){
    this->syncing = true;
    keyboard->setText(FixedString<PICO_STR_LL>(""));
    this->syncing = false;
}

void SshScene::onHide(ITextInputWidget*){
    if(this->view) this->view->setPreedit("");
}

void SshScene::onDisplayChanged(ITextInputWidget* keyboard){
    if(this->syncing) return;
    const FixedString<PICO_STR_LL> text = keyboard->getText();

    //日本語の変換中は送らず、読みを端末に重ねて見せるだけ
    size_t comp_start = 0, comp_len = 0;
    keyboard->getComposition(comp_start, comp_len);
    if(comp_len > 0){
        if(this->view) this->view->setPreedit(text.c_str());
        return;
    }
    if(this->view) this->view->setPreedit("");
    if(text.empty()) return;

    //先に入力欄を空へ戻す(送る途中で再入しないように)
    this->syncing = true;
    keyboard->setText(FixedString<PICO_STR_LL>(""));
    this->syncing = false;

    const char* s = text.c_str();
    const size_t n = text.length();
    size_t start = 0;
    for(size_t i = 0; i < n; i++){
        if(s[i] == '\n'){
            this->inputText(s + start, i - start);
            this->inputEnter();
            start = i + 1;
        }
    }
    this->inputText(s + start, n - start);
}

bool SshScene::onBackspaceAtStart(ITextInputWidget*){
    this->inputBackspace();
    return true;
}

bool SshScene::onCursorAtEdge(ITextInputWidget*, int dir){
    this->inputArrow(dir < 0 ? 'D' : 'C');
    return true;
}

// ---------------------------------------------------------------- シーン

SshScene::~SshScene(){
    KeyboardFunctions::UnregisterInputTarget(this);
    this->dropClient();
}

void SshScene::onEnter(){
    const Rect content = Scene::contentRect();

    auto make_button = [&](const char* label, int x, int y){
        Button* b = new Button(x, y, label);
        b->setFontSize(FontFn::Small);
        b->setH(20);
        WidgetFunctions::Add(b);
        return b;
    };

    if(!this->started) this->loadConfig();

    // 上の段: 戻る 接続/切断 文字の大きさ … キーボードの出し入れ(右端)
    int x = content.x + MARGIN;
    const int y1 = content.y + MARGIN;
    this->back_button = make_button("戻る", x, y1);
    x += this->back_button->getLocalRect().w + MARGIN;
    this->conn_button = make_button("接続", x, y1);
    this->conn_button->setW(this->conn_button->getLocalRect().w); //文字が変わっても幅を変えない
    x += this->conn_button->getLocalRect().w + MARGIN;
    this->font_button = make_button("文字:大", x, y1);
    this->font_button->setW(this->font_button->getLocalRect().w);

    const int row_h = this->back_button->getLocalRect().h;
    this->kb_button = new Button(0, y1, "");
    this->kb_button->setIcon(IconID::Keyboard, IconSize::Px16);
    this->kb_button->setW(28);
    this->kb_button->setH(20);
    this->kb_button->setX(content.x + content.w - MARGIN - this->kb_button->getLocalRect().w);
    WidgetFunctions::Add(this->kb_button);

    const int view_y = y1 + row_h + MARGIN;
    this->view = new TerminalView(content.x, view_y, content.w, SCREEN_HEIGHT - kKeyBarH - view_y, &this->term);
    this->view->setOnTap([this](){
        if(!KeyboardFunctions::IsDocked()) this->openKeyboard();
    });
    WidgetFunctions::Add(this->view);

    this->keybar = new TermKeyBar(content.x, SCREEN_HEIGHT - kKeyBarH, content.w, kKeyBarH);
    this->keybar->setOnKey([this](TermKeyBar::Key k){ this->onKeyBar(k); });
    WidgetFunctions::Add(this->keybar);

    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    this->conn_button->setOnPressEnd([this](){
        if(this->isActive()){
            this->prompt = Prompt::None;
            if(this->client) this->client->disconnect();
            else this->jump->disconnect();
            return;
        }
        //接続先の入力中ならEnterと同じ(何も打っていなければ前回の接続先へ繋ぐ)
        if(this->prompt == Prompt::Target) this->inputEnter();
    });
    this->font_button->setOnPressEnd([this](){
        this->small_font = !this->small_font;
        if(OSData::SD_usable){
            PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SSH_CFG, "font", this->small_font ? "small" : "large");
        }
        this->refreshButtons();
        this->layout();
    });
    this->kb_button->setOnPressEnd([this](){
        if(KeyboardFunctions::IsDocked()) KeyboardFunctions::HideAll();
        else this->openKeyboard();
    });

    this->last_bottom = -1;
    this->layout();
    this->refreshButtons();

    if(!this->started){
        this->started = true;
        this->say("\x1b[1mpico-os SSH\x1b[0m\r\n");
        if(!OSData::SD.exists(PICO_Path::FILE::SSH_ID_ED25519)){
            this->say("(公開鍵認証を使うには /sys/ssh/id_ed25519 に鍵を置きます)\r\n");
        }
        this->startTargetPrompt();
    }
    this->openKeyboard();
}

void SshScene::onExit(){
    //キーボードはSceneFunctionsが先に閉じている(onHide済み)
    KeyboardFunctions::UnregisterInputTarget(this);
    this->back_button = nullptr;
    this->conn_button = nullptr;
    this->font_button = nullptr;
    this->kb_button = nullptr;
    this->view = nullptr;
    this->keybar = nullptr;
    //画面を離れたら切る(受信を進める者がいなくなるため)
    if(this->client || this->jump){
        this->dropClient();
        this->say("\r\n\x1b[93m切断しました\x1b[0m\r\n");
        this->startTargetPrompt();
    }
    this->connect_wait_frames = -1;
}

void SshScene::onUpdate(){
    this->layout();

    if(this->connect_wait_frames >= 0){
        if(this->connect_wait_frames == 0){
            this->connect_wait_frames = -1;
            this->beginConnect();
        }else{
            this->connect_wait_frames--;
        }
    }

    //踏み台を先に回す(届いたデータを通り道に溜めてから、中が読む)
    if(this->jump){
        this->jump->update();
        const SshClient::State s = this->jump->state();
        if(s != this->last_jump_state){
            this->last_jump_state = s;
            //中が居る(=開通した後)なら、踏み台が切れたことは中が「接続が切れました」として拾う
            if(!(s == SshClient::State::Closed && this->client)) this->onStateChanged(this->jump, s);
            this->refreshButtons();
        }
    }
    if(this->client){
        this->client->update();
        //端末の問い合わせ(カーソル位置等)への返事
        if(this->term.replyLength() > 0){
            if(this->client->isOpen()) this->client->send((const uint8_t*)this->term.reply(), this->term.replyLength());
            this->term.clearReply();
        }
        const SshClient::State s = this->client->state();
        if(s != this->last_state){
            this->last_state = s;
            this->onStateChanged(this->client, s);
            this->refreshButtons();
        }
    }else{
        this->term.clearReply();
    }

    if(this->view) this->view->onFrame();
}
