// PC / Web(WebAssembly)ビルドのエントリポイント。
//
// 実機ではArduinoフレームワークが setup()/loop() を回すが、ここではSDLパネルが
// その役を担う。src/main.cpp の setup()/loop() をそのまま呼ぶ(src/には手を入れない)。
//
// **ネイティブとWebでループの回し方だけが違う。**
//
//   ネイティブ … Panel_sdl::main() がSDLのイベントループを持ち、渡した関数を
//                別スレッドで回してくれる。そこで setup()→loop() を回す。
//   Web        … ブラウザのメインスレッドは止められない(止めた分だけ描画も入力も
//                丸ごと止まる)ので、スレッドを使わず emscripten_set_main_loop() に
//                「1フレーム分」を渡して requestAnimationFrame で刻んでもらう。
//                Panel_sdl は setup()/loop()/close() が公開されているので、
//                Panel_sdl::main() を使わずに自前で回せる。
//
// 使い方(ネイティブ):
//   picoos_pc                       通常起動(ウィンドウが開く)
//   picoos_pc --shot out.ppm [N]    Nフレーム回してから画面をPPMへ書き出して終了
//                                   (既定60フレーム。CIやヘッドレスでの確認用)
//   picoos_pc --tap X,Y@F[:H]       Fフレーム目に(X,Y)をHフレーム押す(既定H=3)
//                                   ヘッドレスではSDLへマウスが来ないので、
//                                   撮りたい画面まで操作を進めるために使う。
//                                   複数回指定できる(最大16件)
//
// 使い方(Web): index.html を開くだけ。環境変数の代わりにURLのクエリで状態を指定する
//   index.html?wifi=disconnected&rssi=-85
//   ※--shot / --tap はネイティブ専用(ブラウザではDevToolsと画面のPNG保存を使う)
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#include <SDL2/SDL.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <thread>
#include <vector>

#if defined(__EMSCRIPTEN__)
    #include <emscripten.h>
    #include <emscripten/html5.h>  // emscripten_get_canvas_element_size
#endif
#include <Arduino.h>               // PicoPcSerial(picoos_serial_push / SDLのキーボード)

#include "consts.hpp"
#include "OS_Data.hpp"
#include <config/LGFX_Config_PC.hpp>         // PICOOS_PC_SCALE(画面の拡大率)
#include <functions/Touch_Functions_PC.hpp>  // PicoOsTouchScript

// src/main.cpp が提供する
void setup(void);
void loop(void);
// 2コア目(実機ではarduino-picoが別のコアで回す)。音声専用。
// ネイティブは別スレッド、Webはフレームごとに1回呼ぶ
void setup1(void);
void loop1(void);

//---- ここからネイティブ専用(--shot とユーザコード用スレッド) ----
#if !defined(__EMSCRIPTEN__)

namespace {
    const char* g_shot_path = nullptr;
    int         g_shot_frames = 60;

    // 画面をPPM(P6)として書き出す。
    //
    // readRectにrgb888_tのバッファを渡してLovyanGFX側で変換させる。
    // uint16_tで受けてRGB565を自前で展開すると、パネル内部のバイト順の都合で
    // 赤と青が入れ替わる(0xF800が0x00F8として読める)。白黒だけ見ていると
    // 左右対称なビット列なので気づけない
    bool writeScreenshot(const char* path)
    {
        if (!OSData::lcd) return false;

        std::vector<lgfx::rgb888_t> pixels((size_t)SCREEN_WIDTH * SCREEN_HEIGHT);
        OSData::lcd->readRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, pixels.data());

        FILE* fp = fopen(path, "wb");
        if (!fp) {
            printf("[PC] スクリーンショットを書き出せません: %s\n", path);
            return false;
        }

        // バックライトは液晶のピクセルの中身ではないのでreadRect()には写らない。
        // 実際の画面(Panel_sdl_SpiWait::setBrightness()がテクスチャへ掛ける率)と同じ
        // 百分率を掛けて、明るさの設定が--shotでも見えるようにする
        const int backlight = OSData::lcd->getBrightness() >= 100 ? 100 : OSData::lcd->getBrightness();

        fprintf(fp, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
        for (const auto& c : pixels) {
            fputc(c.R8() * backlight / 100, fp);
            fputc(c.G8() * backlight / 100, fp);
            fputc(c.B8() * backlight / 100, fp);
        }
        fclose(fp);

        printf("[PC] スクリーンショットを書き出しました: %s (%dx%d)\n",
               path, SCREEN_WIDTH, SCREEN_HEIGHT);
        return true;
    }

    // Panel_sdl::main() は「全ウィンドウが閉じるまで」SDLのイベントループを回すので、
    // この関数からreturnしてもプロセスは終わらない。SDL_QUITを流し込んで畳ませる
    void requestQuit()
    {
        SDL_Event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = SDL_QUIT;
        SDL_PushEvent(&ev);
    }

    // ---- PCのキーボード → 物理キーボードの打鍵 ----
    // ウィンドウでのキー操作を、実機のUSBシリアルと同じ "key M CODE" の行にして
    // Serialの受信口へ入れる(KeyInput_Functions.hpp)。pico-os側は実機と同じ経路で読む。
    // 文字はSDL_TEXTINPUT(Shift・キー配列・母艦のIMEを反映済みのUTF-8)、名前のあるキーと
    // Ctrl/Alt付きの文字はSDL_KEYDOWNから作る(Ctrl+CではTEXTINPUTが来ないため)。
    // PICOOS_PC_KEYBOARD=off で無効。
    //
    // イベントの監視(SDL_AddEventWatch)はSDLのイベントを取り込むスレッドで呼ばれる。
    // 受信口はロックを持つので、ループのスレッドとぶつからない
    bool g_pc_keyboard = true;

    uint8_t pcKeyMods(Uint16 mod)
    {
        uint8_t m = 0;
        if (mod & KMOD_CTRL)  m |= 1;
        if (mod & KMOD_LALT)  m |= 2;   //右AltはAltGr(記号の入力)のことがあるので数えない
        if (mod & KMOD_SHIFT) m |= 4;
        return m;
    }

    void pcPushKey(uint8_t mods, const char* code)
    {
        char line[48];
        snprintf(line, sizeof(line), "key %x %s\n", mods, code);
        PicoPcSerial::PushLine(line);
    }

    int pcKeyboardWatch(void*, SDL_Event* ev)
    {
        if (ev->type == SDL_TEXTINPUT) {
            //Ctrl/Alt付きはKEYDOWN側で送る(環境によってはTEXTINPUTも来るため二重にしない)
            if (SDL_GetModState() & (KMOD_CTRL | KMOD_LALT)) return 0;
            const unsigned char* s = (const unsigned char*)ev->text.text;
            while (*s) {
                uint32_t cp; int n;
                if (s[0] < 0x80)                { cp = s[0];        n = 1; }
                else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; n = 2; }
                else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; n = 3; }
                else if ((s[0] & 0xF8) == 0xF0) { cp = s[0] & 0x07; n = 4; }
                else { s++; continue; }
                int i = 1;
                for (; i < n && (s[i] & 0xC0) == 0x80; i++) cp = (cp << 6) | (s[i] & 0x3F);
                if (i < n) { s += i; continue; }
                s += n;
                char code[16];
                snprintf(code, sizeof(code), "u+%x", (unsigned)cp);
                pcPushKey(0, code);
            }
            return 0;
        }
        if (ev->type != SDL_KEYDOWN) return 0;

        const SDL_Keycode sym = ev->key.keysym.sym;
        const uint8_t mods = pcKeyMods(ev->key.keysym.mod);
        const char* name = nullptr;
        switch (sym) {
            case SDLK_RETURN: case SDLK_KP_ENTER: name = "enter"; break;
            case SDLK_BACKSPACE: name = "backspace"; break;
            case SDLK_TAB:       name = "tab"; break;
            case SDLK_ESCAPE:    name = "esc"; break;
            case SDLK_DELETE:    name = "delete"; break;
            case SDLK_LEFT:      name = "left"; break;
            case SDLK_RIGHT:     name = "right"; break;
            case SDLK_UP:        name = "up"; break;
            case SDLK_DOWN:      name = "down"; break;
            case SDLK_HOME:      name = "home"; break;
            case SDLK_END:       name = "end"; break;
            case SDLK_PAGEUP:    name = "pageup"; break;
            case SDLK_PAGEDOWN:  name = "pagedown"; break;
            default: break;
        }
        if (name) {
            pcPushKey(mods, name);
            return 0;
        }
        //Ctrl/Alt付きの文字(Ctrl+C等)。SDLのキーコードは英字なら小文字のASCII
        if ((mods & 3) && sym >= 0x20 && sym < 0x7F) {
            char code[16];
            snprintf(code, sizeof(code), "u+%x", (unsigned)sym);
            pcPushKey(mods, code);
        }
        return 0;
    }

    void setupPcKeyboard()
    {
        const char* env = getenv("PICOOS_PC_KEYBOARD");
        if (env && strcmp(env, "off") == 0) g_pc_keyboard = false;
        if (!g_pc_keyboard) return;
        //LovyanGFXのSDLパネルは修飾キー無しの r / l / 1〜6 を画面の回転・拡大に使うので、
        //文字を打つと画面が回ってしまう。左Ctrl+左Altを押したときだけにずらす
        lgfx::Panel_sdl::setShortcutKeymod((SDL_Keymod)(KMOD_LCTRL | KMOD_LALT));
        SDL_AddEventWatch(pcKeyboardWatch, nullptr);
        SDL_StartTextInput();
        printf("[PC] PCのキーボードで文字を入力できます(PICOOS_PC_KEYBOARD=offで無効)\n");
    }

    // 2コア目の代わりのスレッド。実機のloop1()と同じく回し続ける
    // (loop1()は仕事が無ければdelay(1)で休むので、CPUを食い潰さない)
    std::atomic<bool> g_core1_run{true};
    void core1Thread()
    {
        setup1();
        while (g_core1_run.load()) loop1();
    }

    int picoosMain(bool* running)
    {
        std::thread core1(core1Thread);
        //どの道から抜けても、SDLが片付く前に2コア目を止めて待つ
        struct Core1Joiner {
            std::thread& t;
            ~Core1Joiner() { g_core1_run.store(false); t.join(); }
        } joiner{core1};

        setupPcKeyboard();
        setup();

        int frame = 0;
        while (*running) {
            loop();
            frame++;

            if (g_shot_path && frame >= g_shot_frames) {
                writeScreenshot(g_shot_path);
                requestQuit();
                return 0;
            }

            // 実機は描画とSPI転送で自然に律速されるが、PCではloop()が回りきって
            // CPUを1コア食い潰す。表示が目的なので1フレーム分だけ待って抑える
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return 0;
    }
}

#endif  // !__EMSCRIPTEN__

#if defined(__EMSCRIPTEN__)

// ページ(pc/web/shell.html)のコントローラーが、USBシリアルの代わりに1バイトずつ入れる口。
// 中身は script/pad_serial.py と同じ "pad XXXX\n" の行(PadFunctions がそのまま読む)
extern "C" EMSCRIPTEN_KEEPALIVE void picoos_serial_push(int c) {
    PicoPcSerial::Push(c);
}

namespace {
    // ブラウザに環境変数は無いので、URLのクエリを環境変数として置き直す。
    // こうしておけば pc/compat/ 側(WiFi.h / SdFat.h)の getenv をそのまま使える。
    //
    //   index.html?wifi=disconnected&rssi=-85&ssid=cafe-wifi
    //
    // PICOOS_ で始まるキーはそのまま環境変数名として扱う(将来増えた分も拾える)。
    struct QueryAlias { const char* key; const char* env; };
    const QueryAlias kQueryAliases[] = {
        { "wifi", "PICOOS_WIFI_STATE" },
        { "rssi", "PICOOS_WIFI_RSSI"  },
        { "ssid", "PICOOS_WIFI_SSID"  },
        { "scan", "PICOOS_WIFI_SCAN"  },
        { "sd",   "PICOOS_SD_ROOT"    },
        { "render", "PICOOS_RENDER_DRIVER" },
        { "spi_wait", "PICOOS_SPI_WAIT" },
        { "sound", "PICOOS_SOUND_STATE" },
    };

    // application/x-www-form-urlencoded をほどく(%XX と '+' だけ)
    void urlDecode(const char* src, char* dst, size_t dst_size)
    {
        size_t o = 0;
        for (size_t i = 0; src[i] && o + 1 < dst_size; i++) {
            if (src[i] == '+') {
                dst[o++] = ' ';
            } else if (src[i] == '%' && isxdigit((unsigned char)src[i + 1])
                                     && isxdigit((unsigned char)src[i + 2])) {
                char hex[3] = { src[i + 1], src[i + 2], '\0' };
                dst[o++] = (char)strtol(hex, nullptr, 16);
                i += 2;
            } else {
                dst[o++] = src[i];
            }
        }
        dst[o] = '\0';
    }

    void applyQueryParams()
    {
        char query[512];
        query[0] = '\0';
        EM_ASM({ stringToUTF8(location.search || "", $0, $1); }, query, (int)sizeof(query));

        char* p = query;
        if (*p == '?') p++;

        while (*p) {
            char* amp = strchr(p, '&');
            if (amp) *amp = '\0';

            char* eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char key[64], value[256];
                urlDecode(p, key, sizeof(key));
                urlDecode(eq + 1, value, sizeof(value));

                const char* env = nullptr;
                if (strncmp(key, "PICOOS_", 7) == 0) {
                    env = key;
                } else {
                    for (const auto& a : kQueryAliases) {
                        if (strcmp(key, a.key) == 0) { env = a.env; break; }
                    }
                }

                if (env) {
                    setenv(env, value, 1);
                    printf("[WEB] %s=%s (URLのクエリ %s より)\n", env, value, key);
                } else {
                    printf("[WEB] 知らないクエリなので無視します: %s\n", key);
                }
            }

            if (!amp) break;
            p = amp + 1;
        }
    }

    //選んだ描画ドライバ("software" か "gl")。起動後の自己チェックのログで使う
    const char* g_render_driver = "software";

    //ウィンドウを作る間だけcanvasへ入れておくCSS上の大きさ(px)。
    //1.5pxなのは「floorすると1になる範囲」(1以上2未満)のちょうど真ん中だから。理由は下記
    constexpr double kProbeCssPx = 1.5;

    //canvasの大きさが決まるのを待つ上限(60フレーム≒1秒)。
    //待っても決まらない場合は、そのまま進めて自己チェックのログに任せる
    constexpr int kMaxLayoutWaitFrames = 60;

    // **SDLがウィンドウを作る間だけ、canvasのCSS上の大きさを1.5pxに固定する。**
    //
    // emscriptenのSDLは Emscripten_CreateWindow() で毎回こうする:
    //   1. canvasの属性を 1x1 にする
    //   2. CSS上の大きさ(getBoundingClientRect)を測る
    //   3. **floor(実測値) != 1 なら「CSSが大きさを決めている」(external_size)と見なし、
    //      実測値をそのまま画面の大きさに採用する**
    //
    // CSSが何も指定していなければ 2. は 1x1 を返す……はずが、ページズームや端数の都合で
    // **0.9999998 のように1をわずかに下回る値**が返ることがある。すると floor で0になり、
    // 「CSSが0を指定している」と解釈されて **canvasもSDLのウィンドウも 0x0 で作られる**。
    // そうなるとソフトウェア描画が createImageData(0, 0) で例外を投げ、
    // **メインループが1フレーム目で止まる**(画面は出ず、C++のログだけが残る)。
    // 実際に「canvas=0x0 / フレーム数=1 / createImageDataのIndexSizeError」という報告が出た。
    //
    // 1.5pxを入れておけば、端数が出ても実測値は1以上2未満に収まり floor は必ず1になる。
    // つまり **3. の判定を「CSSは大きさを決めていない」側へ確実に倒せる**。
    //
    // **external_size側へ倒してはいけない。** そちらへ入るとSDLはCSS上の大きさを画面の
    // 大きさとして採用し、以後ウィンドウの内部サイズとCSSの箱を同期しなくなる。
    // LovyanGFXのSDLパネルは「ウィンドウの大きさは自分が決める」前提で拡大率
    // (`_update_scaling`)とタッチ座標の換算を組み立てているので、この組み合わせでは
    // **初期表示の縦横比が崩れ、タップ位置もずれる**(実際にその報告が出た)。
    // 480x640のように正しい値を明示しても、値が正しいだけでモードは同じなので同様に崩れる。
    //
    // 固定するのは判定の間だけで、ウィンドウができたら releaseCanvasCssPin() で外す。
    // 以後の見た目はページのCSS(`pc/web/shell.html`)に任せてよい —
    // SDLはマウス座標をCSS上の大きさで割り戻すので、縮小表示されていてもタップはずれない。
    void pinCanvasCssSizeForProbe()
    {
        emscripten_set_element_css_size("#canvas", kProbeCssPx, kProbeCssPx);
    }

    // 判定用に入れた1.5pxを外し、見た目をページのCSSへ返す。
    // dprが1以外のときはSDL自身がCSS上の大きさ(=ウィンドウの大きさ)を入れているので、
    // そちらは正しい値なのでそのまま残す
    void releaseCanvasCssPin()
    {
        EM_ASM({
            var c = Module['canvas'] || document.querySelector('#canvas');
            if (!c) return;
            //判定用の値(2px未満)がまだ残っていれば外す
            if (parseFloat(c.style.width) < 2) {
                c.style.width = "";
                c.style.height = "";
            }
        });
    }

    // canvasのCSS上の大きさが測れる状態か。
    // レイアウト前は0が返るので、その状態でウィンドウを作らせると上記の 0x0 になる
    bool canvasBoxReady()
    {
        double w = 0, h = 0;
        emscripten_get_element_css_size("#canvas", &w, &h);
        return w >= 1.0 && h >= 1.0;
    }

    // 描画ドライバを決める。**Webでは既定でSDLのソフトウェア描画(canvas 2D)を使う。**
    //
    // LovyanGFXの sdl_create() は
    // SDL_CreateRenderer(..., SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) を要求する。
    // SDLのGLES2レンダラは、ウィンドウに SDL_WINDOW_OPENGL が立っていないと
    // **SDL_RecreateWindow() でウィンドウを作り直す**。そしてemscriptenのSDLは
    // ウィンドウを壊すときcanvasそのものは壊せないので **0x0へ縮める**
    // (SDL_emscriptenvideo.c の "We can't destroy the canvas, so resize it to zero instead")。
    // つまりGPU描画では、起動のたびにcanvasが必ず一度 0x0 を通る。
    // 作り直しに失敗するとcanvasは **0x0のまま**になり、C++側は何事もなく回り続けるため
    // 「フレームは進んでいるのに画面が出ない」という形になる。
    //
    // 作り直しが失敗する条件はブラウザ任せで、こちらからは予測できない:
    // SDLが要求するEGL/WebGLサーフェスの属性が通らない、GPUがブロックリスト入り、
    // WebGLコンテキスト数の上限、など。**捨てcanvasへ getContext('webgl') が通ることは
    // 何の保証にもならない** — 実際に「WebGLあり・canvas 0x0・フレームは180」という
    // 報告が出ており、以前のWebGL有無での切り替えではこれを防げなかった。
    //
    // ソフトウェア描画ならウィンドウの作り直しが起きない(SW_CreateRendererは
    // SDL_WINDOW_OPENGL を要求しない)ので、この経路ごと消える。SDLはヒントで名指しした
    // ドライバを SDL_RENDERER_ACCELERATED の要求と突き合わせないため、LovyanGFX側は無改造でよい。
    // 240x320を2倍で出す程度では速度差も出ない(どちらも実測60fps)。
    //
    // 比較したいときは ?render=gl で従来のGPU描画に戻せる。
    //
    // (以下 hasWebGL() → selectRenderDriver() の順に定義する)

    // ブラウザでWebGLが使えるか。**?render=gl を指定されたときだけ確かめる。**
    // 無い環境でGPU描画を選ぶとレンダラがnullptrになり確実に何も描かれないので、
    // その場合はソフトウェア描画へ戻す。
    // (「あれば映る」保証は無いので、既定の判断材料には使わない)
    bool hasWebGL()
    {
        return 0 != EM_ASM_INT({
            try {
                //本番のcanvasには触らない(コンテキストは1つしか持てないため)
                var probe = document.createElement('canvas');
                var gl = probe.getContext('webgl2') || probe.getContext('webgl')
                      || probe.getContext('experimental-webgl');
                if (!gl) return 0;
                //確かめるだけなので即座に手放す
                var lose = gl.getExtension('WEBGL_lose_context');
                if (lose) lose.loseContext();
                return 1;
            } catch (e) { return 0; }
        });
    }

    void selectRenderDriver()
    {
        const char* req = getenv("PICOOS_RENDER_DRIVER");
        bool use_gl = req && (strcmp(req, "gl") == 0 || strcmp(req, "webgl") == 0
                           || strcmp(req, "gpu") == 0);

        if (use_gl && !hasWebGL()) {
            use_gl = false;
            printf("[WEB] ?render=gl を指定されましたが、このブラウザではWebGLが使えません。"
                   "ソフトウェア描画で動かします\n");
        }

        if (use_gl) {
            g_render_driver = "gl";
            printf("[WEB] 描画=GPU(?render=gl の指定)。"
                   "画面が出ない場合はクエリを外してください\n");
        } else {
            g_render_driver = "software";
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
            printf("[WEB] 描画=ソフトウェア(canvas 2D)。GPU描画を試すなら ?render=gl\n");
        }

        //ページ側の見張りが「どちらで動いているか」を報告できるようにする
        EM_ASM({ window.picoosRenderDriver = UTF8ToString($0); }, g_render_driver);
    }

    // ウィンドウ(canvas)が本当に用意できたかを起動直後に一度だけ確かめる。
    //
    // canvasが0x0なら、SDLのウィンドウ作成/作り直しが失敗したということで、
    // 「画面が出ない」の原因はまずこれ。黙って回り続けさせるとログに何も残らず
    // 原因が見えないので、SDLのエラー文字列ごと書き出す
    void checkCanvasReady()
    {
        int w = 0, h = 0;
        emscripten_get_canvas_element_size("#canvas", &w, &h);

        double css_w = 0, css_h = 0;
        emscripten_get_element_css_size("#canvas", &css_w, &css_h);

        if (w > 0 && h > 0) {
            printf("[WEB] 画面を用意しました: canvas %dx%d (CSS上は%.1fx%.1f / 描画=%s)\n",
                   w, h, css_w, css_h, g_render_driver);
            return;
        }

        const char* err = SDL_GetError();
        printf("[WEB] 画面(canvas)が作られていません: %dx%d / CSS上は%.4fx%.4f / 描画=%s"
               " / SDLのエラー: %s\n",
               w, h, css_w, css_h, g_render_driver, (err && *err) ? err : "(なし)");
        printf("[WEB] SDLのウィンドウ作成に失敗しています。"
               "?render=gl を付けている場合は外して読み込み直してください\n");
    }

    // 1フレーム分。requestAnimationFrame から呼ばれるので、
    // ここで待ったり回し続けたりしてはいけない(タブが固まる)
    void webFrame()
    {
        // 「C++は動いているのに画面だけ出ない」を切り分けられるよう、
        // 最初の数秒だけフレーム数をJSへ渡す(ページ側の見張りが読む)
        static int frames = 0;
        if (frames < 180) {
            ++frames;
            EM_ASM({ window.picoosFrames = $0; }, frames);
        }

        //ウィンドウが作られるのは最初の Panel_sdl::loop() の中。その前に、canvasの
        //CSS上の大きさが確定しているのを確かめる(レイアウト前だと0が返り、0x0のウィンドウが
        //できてしまう。pinCanvasCssSizeForProbe() のコメント参照)
        static bool layout_ready = false;
        if (!layout_ready) {
            static int waited = 0;
            if (!canvasBoxReady() && waited < kMaxLayoutWaitFrames) {
                ++waited;
                pinCanvasCssSizeForProbe();  //まだ効いていない可能性があるので掛け直す
                return;
            }
            layout_ready = true;
            if (waited > 0) {
                printf("[WEB] canvasの大きさが決まるまで%dフレーム待ちました\n", waited);
            }
        }

        loop();
        //2コア目の代わり(Webにはスレッドが無い)。1回で最大512サンプル(約23ms)まで作る
        loop1();

        //SDLのイベント取り込みとウィンドウへの反映(ネイティブではSDL側スレッドの仕事)
        const int sdl_state = lgfx::Panel_sdl::loop();

        //ウィンドウはこの最初の Panel_sdl::loop() の中で作られる。
        //判定用の固定をここで外し、大きさが取れているかを一度だけ確認する
        static bool canvas_checked = false;
        if (!canvas_checked) {
            canvas_checked = true;
            releaseCanvasCssPin();
            checkCanvasReady();
        }

        if (0 != sdl_state) {
            //ウィンドウが閉じられた。ブラウザでは普通起きないので、黙って止まらず残す
            printf("[WEB] SDLのウィンドウが閉じられたため描画を終了します\n");
            emscripten_cancel_main_loop();
            lgfx::Panel_sdl::close();
        }
    }
}

//公開ビルドがどのコミットのものかを見分けるための表記(CIが -DPICOOS_WEB_REV=... で渡す)
#if !defined(PICOOS_WEB_REV)
    #define PICOOS_WEB_REV "dev"
#endif

int main(int, char**)
{
    printf("[WEB] pico-os build: %s\n", PICOOS_WEB_REV);

    applyQueryParams();

    //SDLを起こす前に決めること(ヒントはSDL_Initより先に立てる必要がある)
    selectRenderDriver();

    //ウィンドウが作られるより前に、大きさの判定が通るようcanvasを固定する(0x0を防ぐ)
    pinCanvasCssSizeForProbe();

    if (0 != lgfx::Panel_sdl::setup()) return 1;

    //LovyanGFXのSDLパネルは修飾キー無しの r / l / 1〜6 を画面の回転・拡大に使う。
    //ページで文字を打つ(shell.htmlの「文字入力」)ときに画面が回らないよう、左Ctrl+左Altの時だけにする
    lgfx::Panel_sdl::setShortcutKeymod((SDL_Keymod)(KMOD_LCTRL | KMOD_LALT));

    setup();
    setup1();

    //第2引数0 = requestAnimationFrame任せ、第3引数1 = ここで抜けずにループへ入る
    emscripten_set_main_loop(webFrame, 0, 1);
    return 0;
}

#else

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tap") == 0 && i + 1 < argc) {
            //"X,Y@FRAME[:HOLD]" を読む
            const char* spec = argv[++i];
            int x = 0, y = 0, frame = 0, hold = 3;
            const int got = sscanf(spec, "%d,%d@%d:%d", &x, &y, &frame, &hold);
            if (got < 3) {
                printf("[PC] --tap の書式は X,Y@FRAME[:HOLD] です: %s\n", spec);
                return 1;
            }
            if (!PicoOsTouchScript::Add(x, y, frame, hold)) {
                printf("[PC] --tap が多すぎます(最大%d件)\n", PicoOsTouchScript::kMaxTaps);
                return 1;
            }
            continue;
        }
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            g_shot_path = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                g_shot_frames = atoi(argv[++i]);
                if (g_shot_frames <= 0) g_shot_frames = 1;
            }
        }
    }

    return lgfx::Panel_sdl::main(picoosMain);
}

#endif
