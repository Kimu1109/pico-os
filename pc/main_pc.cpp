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
#include <thread>
#include <vector>

#if defined(__EMSCRIPTEN__)
    #include <emscripten.h>
    #include <emscripten/html5.h>  // emscripten_get_canvas_element_size
#endif

#include "consts.hpp"
#include "OS_Data.hpp"
#include <functions/Touch_Functions_PC.hpp>  // PicoOsTouchScript

// src/main.cpp が提供する
void setup(void);
void loop(void);

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

        fprintf(fp, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
        for (const auto& c : pixels) {
            fputc(c.R8(), fp); fputc(c.G8(), fp); fputc(c.B8(), fp);
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

    int picoosMain(bool* running)
    {
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

        if (w > 0 && h > 0) {
            printf("[WEB] 画面を用意しました: canvas %dx%d (描画=%s)\n", w, h, g_render_driver);
            return;
        }

        const char* err = SDL_GetError();
        printf("[WEB] 画面(canvas)が作られていません: %dx%d / 描画=%s / SDLのエラー: %s\n",
               w, h, g_render_driver, (err && *err) ? err : "(なし)");
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

        loop();

        //SDLのイベント取り込みとウィンドウへの反映(ネイティブではSDL側スレッドの仕事)
        const int sdl_state = lgfx::Panel_sdl::loop();

        //ウィンドウはこの最初の Panel_sdl::loop() の中で作られる。直後に一度だけ確認する
        static bool canvas_checked = false;
        if (!canvas_checked) {
            canvas_checked = true;
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

    if (0 != lgfx::Panel_sdl::setup()) return 1;

    setup();

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
