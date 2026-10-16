#include "home/Home_Layout.hpp"

#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    const HomeLayout::KindInfo kKinds[HomeLayout::kKindCount] = {
        { "clock",  "時計",           HomeLayout::S1x1 | HomeLayout::S2x1 },
        { "analog", "アナログ時計",   HomeLayout::S1x1 | HomeLayout::S2x2 },
        { "music",  "音楽",           HomeLayout::S2x1 | HomeLayout::S2x2 },
        { "agenda", "予定",           HomeLayout::S2x1 | HomeLayout::S2x2 },
        { "todo",   "TODO",           HomeLayout::S2x1 | HomeLayout::S2x2 },
    };

    HomeWidget* impls[HomeLayout::kKindCount] = {};
    uint32_t revision = 1;

    using namespace HomeLayout;

    // 並べ替えた並びから anchor を付け直し、placed[] を並びの順にする。
    // 並びのアプリの順は AppFunctions の見えている順と同じであること(呼び出し側が揃える)
    void Rebuild(const SeqEntry* seq, int n){
        Placed out[kMaxWidgets];
        int count = 0;
        int apps = 0;
        for(int i = 0; i < n; i++){
            if(seq[i].type == ItemType::App){ apps++; continue; }
            if(count >= kMaxWidgets) continue;
            out[count] = placed[seq[i].index];
            out[count].anchor = (uint8_t)apps;
            count++;
        }
        for(int i = 0; i < count; i++) placed[i] = out[i];
        placed_count = count;
        revision++;
    }

    bool ValidSize(Kind k, uint8_t w, uint8_t h){
        if((int)k < 0 || (int)k >= kKindCount) return false;
        return (Info(k).sizes & SizeBitOf(w, h)) != 0;
    }
}

const HomeLayout::KindInfo& HomeLayout::Info(Kind k){
    int i = (int)k;
    if(i < 0 || i >= kKindCount) i = 0;
    return kKinds[i];
}

bool HomeLayout::KindFromKey(const char* key, Kind& out){
    for(int i = 0; i < kKindCount; i++){
        if(strcmp(kKinds[i].key, key) == 0){ out = (Kind)i; return true; }
    }
    return false;
}

uint8_t HomeLayout::SizeBitOf(uint8_t w, uint8_t h){
    if(w == 1 && h == 1) return S1x1;
    if(w == 2 && h == 1) return S2x1;
    if(w == 2 && h == 2) return S2x2;
    return 0;
}

const char* HomeLayout::SizeName(uint8_t w, uint8_t h){
    switch(SizeBitOf(w, h)){
        case S1x1: return "小";
        case S2x1: return "横長";
        case S2x2: return "大";
        default:   return "?";
    }
}

bool HomeLayout::HasKind(Kind k){
    for(int i = 0; i < placed_count; i++) if(placed[i].kind == k) return true;
    return false;
}

int HomeLayout::Sequence(SeqEntry* out, int max){
    const int v = AppFunctions::VisibleCount();
    int n = 0;
    for(int pos = 0; pos <= v; pos++){
        for(int i = 0; i < placed_count; i++){
            const int a = placed[i].anchor > v ? v : placed[i].anchor;
            if(a != pos) continue;
            if(n < max) out[n++] = { ItemType::Widget, (int8_t)i };
        }
        if(pos < v && n < max) out[n++] = { ItemType::App, (int8_t)AppFunctions::VisibleAt(pos) };
    }
    return n;
}

int HomeLayout::SequencePosition(ItemType type, int index){
    SeqEntry seq[kMaxItems];
    const int n = Sequence(seq, kMaxItems);
    for(int i = 0; i < n; i++) if(seq[i].type == type && seq[i].index == index) return i;
    return -1;
}

static int cells_of(int rows){ return rows * HomeLayout::kCols; }

int HomeLayout::Compute(int rows, Item* out, int max, int* pages){
    if(rows < 1) rows = 1;
    if(rows > 16) rows = 16; //1ページの使用中のマスを32bitで持つ
    SeqEntry seq[kMaxItems];
    const int n = Sequence(seq, kMaxItems);

    //ページごとの使用中のマス。項目は並びの順に、空いている一番前の所へ置く
    //(大きなウィジェットが次の行/ページへ送られて空いた所を、後ろの小さな項目が埋める)
    uint32_t used[kMaxItems + 1] = {};
    const uint32_t full = (cells_of(rows) >= 32) ? 0xFFFFFFFFu : ((1u << cells_of(rows)) - 1u);
    int page = 0;
    int first_open = 0;   // 埋まりきっていない一番前のページ
    int last_page = 0;    // 使った一番後ろのページ
    int count = 0;
    const int cells = rows * kCols;

    for(int i = 0; i < n; i++){
        uint8_t w = 1, h = 1;
        if(seq[i].type == ItemType::Widget){
            w = placed[seq[i].index].w;
            h = placed[seq[i].index].h;
        }
        if(w > kCols) w = kCols;
        if(h > rows) h = (uint8_t)rows;

        int at = -1;
        while(first_open < kMaxItems && used[first_open] == full) first_open++;
        for(page = first_open; at < 0 && page <= kMaxItems; ){
            for(int c = 0; c < cells; c++){
                const int col = c % kCols, row = c / kCols;
                if(col + w > kCols) continue;
                if(row + h > rows) break;
                bool free_cell = true;
                for(int dy = 0; dy < h && free_cell; dy++){
                    for(int dx = 0; dx < w; dx++){
                        if(used[page] & (1u << ((row + dy) * kCols + col + dx))){ free_cell = false; break; }
                    }
                }
                if(free_cell){ at = c; break; }
            }
            if(at < 0) page++;
        }
        if(at < 0) break; //入らない(起きないはず)
        const int col = at % kCols, row = at / kCols;
        for(int dy = 0; dy < h; dy++){
            for(int dx = 0; dx < w; dx++) used[page] |= 1u << ((row + dy) * kCols + col + dx);
        }
        if(page > last_page) last_page = page;
        if(count < max){
            out[count++] = { seq[i].type, seq[i].index, (uint8_t)page, (uint8_t)col, (uint8_t)row, w, h };
        }
    }
    if(pages) *pages = (n == 0) ? 1 : last_page + 1;
    return count;
}

int HomeLayout::Add(Kind kind, uint8_t w, uint8_t h, int seq_pos){
    if(placed_count >= kMaxWidgets || !ValidSize(kind, w, h)) return -1;
    SeqEntry seq[kMaxItems + 1];
    int n = Sequence(seq, kMaxItems);
    if(seq_pos < 0 || seq_pos > n) seq_pos = n;
    placed[placed_count] = Placed{ kind, w, h, 0 };
    for(int i = n; i > seq_pos; i--) seq[i] = seq[i - 1];
    seq[seq_pos] = { ItemType::Widget, (int8_t)placed_count };
    placed_count++;
    n++;
    Rebuild(seq, n);
    Save();
    //並べ直したので、足したものの番号は並びから探し直す
    int widgets_before = 0;
    for(int i = 0; i < seq_pos; i++) if(seq[i].type == ItemType::Widget) widgets_before++;
    return widgets_before;
}

bool HomeLayout::Remove(int widget_index){
    if(widget_index < 0 || widget_index >= placed_count) return false;
    for(int i = widget_index; i + 1 < placed_count; i++) placed[i] = placed[i + 1];
    placed_count--;
    revision++;
    Save();
    return true;
}

bool HomeLayout::Resize(int widget_index, uint8_t w, uint8_t h){
    if(widget_index < 0 || widget_index >= placed_count) return false;
    if(!ValidSize(placed[widget_index].kind, w, h)) return false;
    if(placed[widget_index].w == w && placed[widget_index].h == h) return false;
    placed[widget_index].w = w;
    placed[widget_index].h = h;
    revision++;
    Save();
    return true;
}

namespace {
    bool MoveOnce(ItemType type, int& index, int delta){
        SeqEntry seq[kMaxItems];
        const int n = Sequence(seq, kMaxItems);
        int p = -1;
        for(int i = 0; i < n; i++) if(seq[i].type == type && seq[i].index == index) p = i;
        const int q = p + delta;
        if(p < 0 || q < 0 || q >= n) return false;
        if(seq[p].type == ItemType::App && seq[q].type == ItemType::App){
            //アプリ同士はアプリの並びを入れ替えるだけ(ウィジェットの anchor は見えている数で数えるので変わらない)
            if(!AppFunctions::MoveApp(seq[p].index, delta)) return false;
            revision++;
            return true;
        }
        const SeqEntry t = seq[p];
        seq[p] = seq[q];
        seq[q] = t;
        Rebuild(seq, n);
        //並べ直したので、動かしたウィジェットの番号は並びから引き直す
        if(type == ItemType::Widget){
            int widgets = 0;
            for(int i = 0; i < q; i++) if(seq[i].type == ItemType::Widget) widgets++;
            index = widgets;
        }
        return true;
    }

    // 並べたマス目での位置(page<<16 | row<<8 | col)。無ければ -1
    int CellOf(int rows, ItemType type, int index){
        Item items[kMaxItems];
        const int n = Compute(rows, items, kMaxItems, nullptr);
        for(int i = 0; i < n; i++){
            if(items[i].type == type && items[i].index == index) return (items[i].page << 16) | (items[i].row << 8) | items[i].col;
        }
        return -1;
    }
}

bool HomeLayout::Move(ItemType type, int index, int delta, int rows, int* new_index){
    if(delta != 1 && delta != -1) return false;
    const int before = rows > 0 ? CellOf(rows, type, index) : -1;
    bool moved = false;
    //詰め方の都合で見た目が変わらないことがある(空いた所へ後ろの項目が入る)ので、見た目が動くまで進める
    for(int k = 0; k < kMaxItems; k++){
        if(!MoveOnce(type, index, delta)) break;
        moved = true;
        if(rows <= 0 || CellOf(rows, type, index) != before) break;
    }
    if(new_index) *new_index = index;
    if(moved && placed_count > 0) Save();
    return moved;
}

bool HomeLayout::MoveToFront(ItemType type, int index){
    SeqEntry seq[kMaxItems];
    const int n = Sequence(seq, kMaxItems);
    int p = -1;
    for(int i = 0; i < n; i++) if(seq[i].type == type && seq[i].index == index) p = i;
    if(p <= 0) return false;
    if(type == ItemType::App){
        //先頭のアプリでなければ、見えているアプリの先頭へ(他のアプリはそのまま1つずつ後ろへずれる)
        if(AppFunctions::VisiblePosition(index) > 0 && !AppFunctions::MoveAppToFront(index)) return false;
    }
    const SeqEntry t = seq[p];
    for(int i = p; i > 0; i--) seq[i] = seq[i - 1];
    seq[0] = t;
    Rebuild(seq, n);
    Save();
    return true;
}

bool HomeLayout::HideApp(int app_index){
    SeqEntry seq[kMaxItems];
    int n = Sequence(seq, kMaxItems);
    if(!AppFunctions::SetHidden(app_index, true)) return false;
    int w = 0;
    for(int i = 0; i < n; i++){
        if(seq[i].type == ItemType::App && seq[i].index == app_index) continue;
        seq[w++] = seq[i];
    }
    n = w;
    Rebuild(seq, n);
    Save();
    return true;
}

uint32_t HomeLayout::Revision(){ return revision; }

void HomeLayout::Load(){
    placed_count = 0;
    revision++;
    if(!OSData::SD_usable) return;
    const char* path = PICO_Path::FILE::CFG::SYS_HOME_CFG;
    if(!OSData::SD.exists(path)) return;

    //widgetN の N の順に並べる(行の順ではなく)
    Placed got[kMaxWidgets];
    bool has[kMaxWidgets] = {};
    PICO_Config::ParseFile(path, [&](const char* key, const char* value){
        if(strncmp(key, "widget", 6) != 0){
            LOG_SYS_WARN("home.cfg: 知らないキーです: %s", key);
            return;
        }
        const int num = atoi(key + 6);
        if(num < 1 || num > kMaxWidgets) return;
        char kind_name[16];
        int w = 0, h = 0, anchor = 0;
        if(sscanf(value, "%15[^,],%dx%d,%d", kind_name, &w, &h, &anchor) != 4){
            LOG_SYS_WARN("home.cfg: 読めない行です: %s = %s", key, value);
            return;
        }
        Kind k;
        if(!KindFromKey(kind_name, k) || !ValidSize(k, (uint8_t)w, (uint8_t)h) || anchor < 0 || anchor > 255){
            LOG_SYS_WARN("home.cfg: 使えないウィジェットです: %s", value);
            return;
        }
        got[num - 1] = Placed{ k, (uint8_t)w, (uint8_t)h, (uint8_t)anchor };
        has[num - 1] = true;
    });
    for(int i = 0; i < kMaxWidgets; i++){
        if(has[i]) placed[placed_count++] = got[i];
    }
}

bool HomeLayout::Save(){
    if(!OSData::SD_usable) return false;
    const char* path = PICO_Path::FILE::CFG::SYS_HOME_CFG;
    char tmp_path[PICO_PATH_LEN];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FsFile f = OSData::SD.open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC);
    if(!f){
        LOG_SYS_FAIL("ホーム: %s を作れませんでした", tmp_path);
        return false;
    }
    bool ok = true;
    auto put = [&](const char* text){
        const size_t len = strlen(text);
        if(ok && f.write(text, len) != len) ok = false;
    };
    put("# ホーム画面のウィジェット。widgetN = 種類,幅x高さ,見えているアプリの何番目の前か(ランチャの長押しが書き換える)\n");
    char line[64];
    for(int i = 0; i < placed_count; i++){
        snprintf(line, sizeof(line), "widget%d = %s,%dx%d,%d\n", i + 1, Info(placed[i].kind).key,
                 placed[i].w, placed[i].h, placed[i].anchor);
        put(line);
    }
    f.close();
    if(!ok){
        OSData::SD.remove(tmp_path);
        LOG_SYS_FAIL("ホーム: home.cfgの書き込みに失敗しました");
        return false;
    }
    if(OSData::SD.exists(path) && !OSData::SD.remove(path)){
        OSData::SD.remove(tmp_path);
        return false;
    }
    return OSData::SD.rename(tmp_path, path);
}

void HomeLayout::SetImpl(Kind k, HomeWidget* w){
    if((int)k >= 0 && (int)k < kKindCount) impls[(int)k] = w;
}

HomeWidget* HomeLayout::Impl(Kind k){
    if((int)k < 0 || (int)k >= kKindCount) return nullptr;
    return impls[(int)k];
}

void HomeLayout::Test::Reset(){
    placed_count = 0;
    for(auto& p : impls) p = nullptr;
    revision++;
}
