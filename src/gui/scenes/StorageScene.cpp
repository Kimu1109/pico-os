#include "gui/scenes/StorageScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "task/StepBudget.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>

void StorageScene::FormatBytes(uint64_t b, char* out, size_t cap){
    if(b < 1024){ snprintf(out, cap, "%luB", (unsigned long)b); return; }
    const char* units[] = {"KB", "MB", "GB", "TB"};
    double v = (double)b / 1024.0;
    int u = 0;
    while(v >= 1024.0 && u < 3){ v /= 1024.0; u++; }
    if(v < 10.0) snprintf(out, cap, "%.2f%s", v, units[u]);
    else if(v < 100.0) snprintf(out, cap, "%.1f%s", v, units[u]);
    else snprintf(out, cap, "%.0f%s", v, units[u]);
}

StorageScene::StorageScene(const char* p){
    this->start_path.assign(p && *p ? p : "/");
    this->reset(this->start_path.c_str());
}

void StorageScene::reset(const char* new_path){
    this->walker.end();
    this->path.assign(new_path);
    this->entry_count = 0;
    this->more_entries = false;
    this->listed = false;
    this->counting = -1;
    this->counted = false;
    this->sorted = false;
    this->total_bytes = 0;
    if(this->path_label) this->path_label->setText(this->path.c_str());
    if(this->list) this->list->clear();
}

void StorageScene::listDirect(){
    this->listed = true;
    if(!OSData::SD_usable) return;
    FsFile dir = OSData::SD.open(this->path.c_str(), O_RDONLY);
    if(!dir || !dir.isDirectory()) return;
    FsFile f;
    while(f.openNext(&dir, O_RDONLY)){
        char name[128];
        const bool named = f.getName(name, sizeof(name));
        const bool is_dir = f.isDirectory();
        const uint64_t size = is_dir ? 0 : (uint64_t)f.fileSize();
        f.close();
        if(!named) continue;
        if(this->entry_count >= kMaxEntries){ this->more_entries = true; continue; }
        Entry& e = this->entries[this->entry_count++];
        e.name.assign(name);
        e.is_dir = is_dir;
        e.size = size;
    }
    dir.close();
}

void StorageScene::step(){
    if(!this->listed){
        this->listDirect();
        this->counting = -1;
        return;
    }
    if(this->counted) return;
    StepBudget budget(kStepBudgetUs);
    while(budget.ShouldContinue()){
        if(this->counting >= 0 && this->walker.active()){
            SdWalker::Entry e;
            if(this->walker.next(e)){
                this->entries[this->counting].size += e.size;
                continue;
            }
        }
        // 次のフォルダへ
        int next = this->counting + 1;
        while(next < this->entry_count && !this->entries[next].is_dir) next++;
        this->counting = next;
        if(next >= this->entry_count){
            this->counted = true;
            break;
        }
        FixedString<PICO_PATH_LEN> sub;
        if(!PICO_IO::join(sub, this->path.c_str(), this->entries[next].name.c_str()) || !this->walker.begin(sub.c_str())){
            continue;   // 開けないフォルダは0のまま
        }
    }
    if(this->counted && !this->sorted){
        // 大きい順(高々48件なので挿入ソート)
        for(int i = 1; i < this->entry_count; i++){
            Entry t = this->entries[i];
            int j = i;
            while(j > 0 && this->entries[j - 1].size < t.size){ this->entries[j] = this->entries[j - 1]; j--; }
            this->entries[j] = t;
        }
        this->total_bytes = 0;
        for(int i = 0; i < this->entry_count; i++) this->total_bytes += this->entries[i].size;
        this->sorted = true;
    }
}

void StorageScene::refreshList(){
    if(!this->list) return;
    const int keep = this->list->getSelectedIndex();
    this->list->clear();
    for(int i = 0; i < this->entry_count; i++){
        const Entry& e = this->entries[i];
        ScrollListTools::Item item;
        item.icon = e.is_dir ? IconID::Folder : IconID::File;
        char sz[16];
        const bool pending = e.is_dir && !this->counted && i >= this->counting;
        if(pending) snprintf(sz, sizeof(sz), "…");
        else FormatBytes(e.size, sz, sizeof(sz));
        item.text.assign(sz);
        item.text.append("  ");
        item.text.append(e.name);
        this->list->add(item);
    }
    if(keep >= 0 && keep < this->entry_count) this->list->setSelectedIndex(keep);

    if(this->status_label){
        char buf[PICO_STR_L];
        if(!this->counted){
            snprintf(buf, sizeof(buf), "数えています… %d/%d", this->counting < 0 ? 0 : this->counting, this->entry_count);
        }else{
            char t[16];
            FormatBytes(this->total_bytes, t, sizeof(t));
            snprintf(buf, sizeof(buf), "%d件 合計%s%s", this->entry_count, t, this->more_entries ? "(多すぎて一部)" : "");
        }
        this->status_label->setText(buf);
    }
}

uint64_t StorageScene::ClusterRound(uint64_t size, bool is_dir, uint64_t bpc){
    if(bpc == 0) return size;
    if(is_dir) return bpc;
    return (size + bpc - 1) / bpc * bpc;
}

void StorageScene::beginCapacity(){
    this->cap_counting = false;
    this->cap_walker.end();
    if(!OSData::SD_usable) return;
    this->cap_bpc = OSData::SD.bytesPerCluster();
    // clusterCount()は覚えてある値を返すだけ(freeClusterCount()と違いFATを読まない)
    cap_total = (uint64_t)OSData::SD.clusterCount() * this->cap_bpc;
    cap_partial = false;
    this->cap_acc = this->cap_bpc;   // ルートのフォルダ自身(FAT32では1クラスタ以上)
    this->cap_frames = 0;
    if(!this->cap_walker.begin("/")){
        // たどれなければ全体だけ出す(使用量は分からないまま)
        cap_used = 0;
        cap_at = millis();
        cap_valid = true;
        return;
    }
    this->cap_counting = true;
}

void StorageScene::stepCapacity(){
    if(!this->cap_counting) return;
    StepBudget budget(kStepBudgetUs);
    SdWalker::Entry e;
    while(budget.ShouldContinue()){
        if(!this->cap_walker.next(e)){
            this->cap_counting = false;
            this->cap_walker.end();
            cap_used = this->cap_acc;
            if(cap_total > 0 && cap_used > cap_total) cap_used = cap_total;
            cap_at = millis();
            cap_valid = true;
            break;
        }
        this->cap_acc += ClusterRound(e.size, e.is_dir, this->cap_bpc);
        if(e.too_deep) cap_partial = true;
    }
}

void StorageScene::refreshCapacity(){
    if(!this->capacity_label) return;
    if(!OSData::SD_usable){
        this->capacity_label->setText("SDカードがありません");
        return;
    }
    char total[16];
    FormatBytes(cap_total, total, sizeof(total));
    char buf[PICO_STR_L];
    if(!cap_valid){
        if(this->cap_counting){
            char used[16];
            FormatBytes(this->cap_acc, used, sizeof(used));
            snprintf(buf, sizeof(buf), "使用量を数えています… %s/%s", used, total);
        }else{
            snprintf(buf, sizeof(buf), "全体%s", total);
        }
        this->capacity_label->setText(buf);
        if(this->bar && cap_total > 0 && this->cap_counting){
            const uint64_t u = this->cap_acc < cap_total ? this->cap_acc : cap_total;
            this->bar->setValue((float)((double)u * 100.0 / (double)cap_total));
        }
        return;
    }
    char used[16], fr[16];
    FormatBytes(cap_used, used, sizeof(used));
    FormatBytes(cap_total - cap_used, fr, sizeof(fr));
    // 空きは「全体 - ファイルの合計」の見積もり(FATの空きを数えたものではない)
    snprintf(buf, sizeof(buf), "%s/%s 空き約%s%s", used, total, fr, cap_partial ? "以下" : "");
    this->capacity_label->setText(buf);
    if(this->bar && cap_total > 0){
        this->bar->setValue((float)((double)cap_used * 100.0 / (double)cap_total));
    }
}

void StorageScene::goUp(){
    if(strcmp(this->path.c_str(), this->start_path.c_str()) == 0 || strcmp(this->path.c_str(), "/") == 0){
        SceneFunctions::Pop();
        return;
    }
    FixedString<PICO_PATH_LEN> parent;
    if(!PICO_IO::parent(parent, this->path.c_str()) || parent.length() == 0) parent.assign("/");
    this->reset(parent.c_str());
}

void StorageScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int top = content.y + MARGIN;

    this->back_button = new Button(content.x + MARGIN, top, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([this](){ this->goUp(); });
    WidgetFunctions::Add(this->back_button);

    const Rect bb = this->back_button->getLocalRect();
    const int lx = bb.x + bb.w + MARGIN;
    this->path_label = new Label<PICO_STR_L>(lx, top + 3, this->path.c_str());
    this->path_label->setFontSize(FontFn::Small);
    this->path_label->setMaxWidth(content.x + content.w - MARGIN - lx);
    this->path_label->setMaxHeight(bb.h);
    this->path_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->path_label);

    int y = top + bb.h + MARGIN;
    this->capacity_label = new Label<PICO_STR_L>(content.x + MARGIN, y, "");
    this->capacity_label->setFontSize(FontFn::Small);
    this->capacity_label->setMaxWidth(content.w - MARGIN * 2);
    this->capacity_label->setMaxHeight(16);
    WidgetFunctions::Add(this->capacity_label);
    y += 18;

    this->bar = new ProgressBar(content.x + MARGIN, (int16_t)y, (int16_t)(content.w - MARGIN * 2), 8);
    this->bar->setMaxValue(100);
    WidgetFunctions::Add(this->bar);
    y += 8 + MARGIN;

    this->status_label = new Label<PICO_STR_L>(content.x + MARGIN, y, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    this->status_label->setMaxHeight(16);
    WidgetFunctions::Add(this->status_label);
    y += 18;

    this->list = new ScrollList(content.x, (int16_t)y, content.w, (int16_t)(content.y + content.h - y));
    this->list->setFontSize(FontFn::Small);
    this->list->setEnableIcon(true);
    this->list->setOnSelectItem([this](int index, bool already){
        if(!already || index < 0 || index >= this->entry_count) return;
        const Entry& e = this->entries[index];
        if(!e.is_dir) return;
        FixedString<PICO_PATH_LEN> sub;
        if(PICO_IO::join(sub, this->path.c_str(), e.name.c_str())) this->reset(sub.c_str());
    });
    WidgetFunctions::Add(this->list);

    if(cap_valid && millis() - cap_at > kFreeCacheMs) cap_valid = false;
    this->cap_counting = false;
    this->cap_walker.end();
    if(!cap_valid && OSData::SD_usable){
        this->cap_bpc = OSData::SD.bytesPerCluster();
        cap_total = (uint64_t)OSData::SD.clusterCount() * this->cap_bpc;
    }
    this->refreshCapacity();
    this->refreshList();
}

void StorageScene::onExit(){
    // 数えている途中なら捨てる(次に開いたときに数え直す)
    this->cap_walker.end();
    this->cap_counting = false;
    this->walker.end();
    this->back_button = nullptr;
    this->path_label = nullptr;
    this->capacity_label = nullptr;
    this->bar = nullptr;
    this->status_label = nullptr;
    this->list = nullptr;
}

void StorageScene::onUpdate(){
    if(!this->sorted){
        const bool was_listed = this->listed;
        const int before = this->counting;
        this->step();
        if(!was_listed || before != this->counting || this->sorted) this->refreshList();
        return;
    }
    // フォルダの中身を数え終わってから、SD全体の使用量を少しずつ数える(どちらも1フレームに kStepBudgetUs まで)
    if(!cap_valid && OSData::SD_usable){
        if(!this->cap_counting) this->beginCapacity();
        this->stepCapacity();
        // 表示の更新は数フレームに1回(Labelの組み直しを減らす)
        if(cap_valid || ++this->cap_frames % 8 == 0) this->refreshCapacity();
    }
}
