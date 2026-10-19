#pragma once

#include <cstdint>

#include "services/App_Functions.hpp"

class HomeWidget;

// ホーム画面(ランチャ)の並び: アプリのタイルとウィジェットを1本の並びにして、2列のマス目へ詰める。
//
// - アプリの並び・隠した印は今までどおり AppFunctions(/sys/launcher.cfg)が持つ。
//   ウィジェットは「見えているアプリの何番目の前に置くか」(anchor)だけを持ち、/sys/home.cfg に書く。
//   同じ anchor のウィジェットは配列の順。見えているアプリの数より大きい anchor は最後に並ぶ
// - 大きさはマス目の数(1x1 = アプリのタイル1枚 / 2x1 = 横いっぱい / 2x2)
// - 詰め方(Compute): 並びの順に、直前に置いたものの位置から先で最初に入る所へ置く。前に空いた所は
//   埋め戻さない(並びの順と見た目の順を揃えるため)。ページに入らなければ次のページへ
// - 描画にも SD の中身にも依存しない(Load/Save だけ SD に触る)。ウィジェットの描き方は
//   SetImpl() で差す(Home_Widgets.cpp の Setup())。差していなければランチャは枠だけ描く
namespace HomeLayout {
    constexpr int kCols = 2;
    constexpr int kMaxWidgets = 8;
    constexpr int kMaxItems = AppFunctions::kMaxApps + kMaxWidgets;

    enum class Kind : uint8_t { Clock = 0, Analog, Music, Agenda, Todo, Count };
    constexpr int kKindCount = (int)Kind::Count;

    enum SizeBit : uint8_t { S1x1 = 1, S2x1 = 2, S2x2 = 4 };

    struct KindInfo {
        const char* key;    // home.cfg に書く名前
        const char* name;   // メニューに出す名前
        uint8_t sizes;      // 置ける大きさ(SizeBit)
    };
    const KindInfo& Info(Kind k);
    bool KindFromKey(const char* key, Kind& out);
    // 大きさの名前("小" "横長" "大")
    const char* SizeName(uint8_t w, uint8_t h);
    uint8_t SizeBitOf(uint8_t w, uint8_t h);

    struct Placed {
        Kind kind = Kind::Clock;
        uint8_t w = 1, h = 1;
        uint8_t anchor = 0;
    };
    // 置いたウィジェット(並びの順。同じ anchor の中ではこの順)
    inline Placed placed[kMaxWidgets];
    inline int placed_count = 0;
    // 置いてあるか(バックグラウンドの取り直しを置いてあるときだけ動かす等)
    bool HasKind(Kind k);

    // ---- 並び(アプリとウィジェットを混ぜた順) ----
    enum class ItemType : uint8_t { App, Widget };
    struct SeqEntry {
        ItemType type;
        int8_t index;   // App: 登録簿の番号 / Widget: placed[] の番号
    };
    // 今の並びを out へ(長さを返す)
    int Sequence(SeqEntry* out, int max);
    // 並びの中の位置(無ければ-1)
    int SequencePosition(ItemType type, int index);

    // ---- マス目へ詰めた結果 ----
    struct Item {
        ItemType type;
        int8_t index;
        uint8_t page, col, row, w, h;
    };
    // rows = 1ページの行数。並べた数を返し、ページ数を *pages へ(最低1)
    int Compute(int rows, Item* out, int max, int* pages);

    // ---- 操作(変わったら home.cfg / launcher.cfg へ書いて true) ----
    // 並びの seq_pos の前(並びの長さなら最後)にウィジェットを足す。置いた placed[] の番号(失敗は-1)
    int Add(Kind kind, uint8_t w, uint8_t h, int seq_pos);
    bool Remove(int widget_index);
    bool Resize(int widget_index, uint8_t w, uint8_t h);
    // 並びの中で1つ前/後ろへ(隣がアプリ同士ならアプリの並びを入れ替える)。rows(1ページの行数)を渡すと、
    // 詰め方の都合で見た目が変わらないときは見た目が動くまで続けて進める。
    // ウィジェットは並べ直すと placed[] の番号が変わるので、動かした後の番号を *new_index へ
    bool Move(ItemType type, int index, int delta, int rows = 0, int* new_index = nullptr);
    bool MoveToFront(ItemType type, int index);
    // アプリを隠す(後ろのウィジェットの位置がずれないようにしてから AppFunctions::SetHidden)
    bool HideApp(int app_index);

    // 置いたもの・並びが変わるたびに増える(ランチャが詰め直す合図)
    uint32_t Revision();

    // /sys/home.cfg(無くてよい)。行: widgetN = <種類>,<幅>x<高さ>,<anchor>(N の順に並べる)
    void Load();
    bool Save();

    // 種類ごとの描き方
    void SetImpl(Kind k, HomeWidget* w);
    HomeWidget* Impl(Kind k);

    namespace Test {
        void Reset();
    }
}
