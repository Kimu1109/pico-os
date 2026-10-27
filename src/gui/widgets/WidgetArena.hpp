#pragma once

#include <cstddef>

// シーンアリーナ: ウィジェット本体(Widget::operator new)の置き場。中身は util/ChunkArena.hpp。
//
// - 起動時の常駐ウィジェット(ステータスバー・キーボード・トースト等)はふつうのヒープに置き、
//   main.cpp が SealPermanentBaseline() の後で Enable() してから先(=シーンの物)だけをアリーナへ置く。
// - シーンを離れるとき(SceneFunctions)に OnSceneExit() を呼び、空いた塊をヒープへ返す。
// - 塊は 4KiB を要るぶんだけ借りる(最大16個)。大きいウィジェット(MarkdownView 等、塊の半分を超える物)と、
//   塊が足りなくなった後はふつうのヒープへ回すので、溢れても動き続ける。
// - シーンの途中で開いて閉じるダイアログは、閉じた跡(同じ大きさの空き)を次に開いたときに使い回す。
//
// 設計の経緯は docs/design/performance.md「シーンアリーナ」。
namespace WidgetArena {
    constexpr size_t kChunkBytes = 4096;
    constexpr size_t kMaxChunks = 16;

    // これより後の Widget の new をアリーナへ置く(main.cpp。ホストテストは既定で切ったまま)
    void Enable(bool enable);
    bool IsEnabled();

    // シーンのウィジェットを全部消した直後に呼ぶ。空いた塊をヒープへ返す
    void OnSceneExit();

    // いま借りている塊の数・生きている数(テスト・診断用)
    size_t ChunkCount();
    size_t LiveCount();
    size_t BorrowedCount();   // 塊をヒープから借りた回数(累計)
}
