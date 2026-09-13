// PCビルドのエントリポイント。
//
// 実機ではArduinoフレームワークが setup()/loop() を回すが、PCでは
// LovyanGFXのSDLパネルがその役を担う。Panel_sdl::main() がSDLのイベントループを
// 持ち、渡した関数を別スレッドで回してくれるので、そこから src/main.cpp の
// setup()/loop() をそのまま呼ぶ(src/main.cpp には手を入れない)。
//
// 使い方:
//   picoos_pc                       通常起動(ウィンドウが開く)
//   picoos_pc --shot out.ppm [N]    Nフレーム回してから画面をPPMへ書き出して終了
//                                   (既定60フレーム。CIやヘッドレスでの確認用)
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#include <SDL2/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "consts.hpp"
#include "OS_Data.hpp"

// src/main.cpp が提供する
void setup(void);
void loop(void);

namespace {
    const char* g_shot_path = nullptr;
    int         g_shot_frames = 60;

    // 画面をPPM(P6)として書き出す。
    // RGB565で読み出してRGB888へ広げる
    bool writeScreenshot(const char* path)
    {
        if (!OSData::lcd) return false;

        std::vector<uint16_t> pixels((size_t)SCREEN_WIDTH * SCREEN_HEIGHT, 0);
        OSData::lcd->readRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, pixels.data());

        FILE* fp = fopen(path, "wb");
        if (!fp) {
            printf("[PC] スクリーンショットを書き出せません: %s\n", path);
            return false;
        }

        fprintf(fp, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
        for (uint16_t c : pixels) {
            //RGB565 -> RGB888(下位ビットを複製して明るさの範囲を合わせる)
            const uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
            const uint8_t g = (uint8_t)(((c >> 5)  & 0x3F) * 255 / 63);
            const uint8_t b = (uint8_t)(( c        & 0x1F) * 255 / 31);
            fputc(r, fp); fputc(g, fp); fputc(b, fp);
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

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
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
