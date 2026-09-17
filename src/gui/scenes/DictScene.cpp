#include "gui/scenes/DictScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "storage/SD_Path.hpp"

void DictScene::onEnter(){
    const Rect content = Scene::contentRect();

    const bool dict_ready = this->dict_.begin(
        PICO_Path::FILE::DICT::DICT_BODY,
        PICO_Path::FILE::DICT::DICT_INDEX
    );

    int y = content.y + MARGIN;

    this->back_button = new Button(content.x + MARGIN, y, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(BACK_BUTTON_H);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);
    y += BACK_BUTTON_H + MARGIN;

    const int box_w = content.w - MARGIN * 3 - SEARCH_BUTTON_W;

    this->search_box = new Textbox<PICO_STR_LL>(
        this->saved_query_.c_str(),
        content.x + MARGIN, (int16_t)y,
        (int16_t)box_w, SEARCH_ROW_H,
        /*is_single_line=*/true
    );
    this->search_box->setFontSize(FontFn::Small);
    WidgetFunctions::Add(this->search_box);

    this->search_button = new Button(content.x + MARGIN * 2 + box_w, y, "検索");
    this->search_button->setFontSize(FontFn::Small);
    this->search_button->setW(SEARCH_BUTTON_W);
    this->search_button->setH(SEARCH_ROW_H);
    this->search_button->setOnPressEnd([this](){ this->startSearch(); });
    WidgetFunctions::Add(this->search_button);
    y += SEARCH_ROW_H + MARGIN;

    this->status_label = new Label<PICO_STR_L>(content.x + MARGIN, y,
        dict_ready ? "語句を入力して「検索」を押してください" : "辞書ファイルが見つかりません(SD確認)");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    WidgetFunctions::Add(this->status_label);
    y += STATUS_H + MARGIN;

    this->result_list = new ScrollList(
        content.x + MARGIN, (int16_t)y,
        (int16_t)(content.w - MARGIN * 2), LIST_H,
        WordDictionary::kMaxHits
    );
    this->result_list->setFontSize(FontFn::Small);
    this->result_list->setOnSelectItem([this](int index, bool /*already_selected*/){
        this->showDetail(index);
    });
    WidgetFunctions::Add(this->result_list);
    y += LIST_H + MARGIN;

    this->detail_label = new Label<PICO_STR_2KiB>(content.x + MARGIN, y, "");
    this->detail_label->setFontSize(FontFn::Small);
    this->detail_label->setMaxWidth(content.w - MARGIN * 2);
    this->detail_label->setMaxHeight(content.y + content.h - y - MARGIN);
    WidgetFunctions::Add(this->detail_label);

    this->shown_count_ = 0;
}

void DictScene::onExit(){
    //次回復元したい状態(検索語)だけ吸い出しておく。ウィジェット本体の破棄は
    //フレームワーク(WidgetFunctions::ClearSceneWidgets)が行うのでここではしない
    if(this->search_box) this->saved_query_.assign(*this->search_box->getText());

    this->back_button   = nullptr;
    this->search_box    = nullptr;
    this->search_button = nullptr;
    this->status_label  = nullptr;
    this->result_list   = nullptr;
    this->detail_label  = nullptr;
}

void DictScene::onUpdate(){
    if(this->dict_.state() == WordDictionary::State::Scanning){
        this->dict_.update();
        this->refreshResults();
    }
}

void DictScene::startSearch(){
    FixedString<PICO_STR_M> query;
    query.assign(*this->search_box->getText());

    this->result_list->clear();
    this->detail_label->setText("");
    this->shown_count_ = 0;

    if(query.length() == 0){
        this->status_label->setText("語句を入力してください");
        return;
    }

    this->dict_.search(query.c_str());
    this->refreshResults();
}

void DictScene::refreshResults(){
    // dict_.hit()は前方一致・全体走査のどちらで見つかった分も同じ配列に
    // 積まれるので、前回からのcount()の増分だけ拾えば一覧に取りこぼしは無い
    for(int i = this->shown_count_; i < this->dict_.count(); i++){
        const WordDictHit& hit = this->dict_.hit(i);

        ScrollListTools::Item item;
        item.text.assign(hit.term.c_str());
        item.text.append("  ");
        // 説明の先頭だけを一覧のプレビューとして添える(全文はタップした時に下の詳細欄へ)
        FixedString<64> snippet;
        snippet.assign(hit.desc.c_str());
        item.text.append(snippet);

        this->result_list->add(item);
    }
    this->shown_count_ = this->dict_.count();

    FixedString<PICO_STR_L> status;
    switch(this->dict_.state()){
        case WordDictionary::State::Scanning:
            status.appendFormat("検索中… %d%%(%d件)",
                (int)(this->dict_.progress() * 100.0f), this->dict_.count());
            break;

        case WordDictionary::State::Done:
            if(this->dict_.count() == 0){
                status.assign("見つかりませんでした");
            } else if(this->dict_.mayHaveMore()){
                status.appendFormat("%d件(他にもあるかも。語句を絞ってください)", this->dict_.count());
            } else {
                status.appendFormat("%d件見つかりました", this->dict_.count());
            }
            break;

        case WordDictionary::State::Idle:
        default:
            return; // 検索前の案内文をそのまま残す
    }
    this->status_label->setText(status);
}

void DictScene::showDetail(int index){
    const WordDictHit* hit = nullptr;
    if(index >= 0 && index < this->dict_.count()){
        hit = &this->dict_.hit(index);
    }
    if(!hit) return;

    FixedString<PICO_STR_2KiB> detail;
    detail.assign("**");
    detail.append(hit->term);
    detail.append("**\n");
    detail.append(hit->desc);

    this->detail_label->setText(detail);
}
