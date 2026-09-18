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

    // 「戻る」は検索欄と同じ行に置くアイコンボタン(1行分節約して詳細欄へ回す)
    this->back_button = new Button(content.x + MARGIN, y, "");
    this->back_button->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button->setAllowTextSpacing(false);
    this->back_button->setW(BACK_BUTTON_W);
    this->back_button->setH(SEARCH_ROW_H);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    const int box_w = content.w - MARGIN * 4
        - (BACK_BUTTON_W + BUTTON_OVERHEAD) - (SEARCH_BUTTON_W + BUTTON_OVERHEAD);
    const int box_x = content.x + MARGIN * 2 + (BACK_BUTTON_W + BUTTON_OVERHEAD);

    this->search_box = new Textbox<PICO_STR_LL>(
        this->saved_query_.c_str(),
        (int16_t)box_x, (int16_t)y,
        (int16_t)box_w, SEARCH_ROW_H,
        /*is_single_line=*/true
    );
    this->search_box->setFontSize(FontFn::Small);
    WidgetFunctions::Add(this->search_box);

    this->search_button = new Button(box_x + box_w + MARGIN, y, "検索");
    this->search_button->setFontSize(FontFn::Small);
    this->search_button->setAllowTextSpacing(false);
    this->search_button->setW(SEARCH_BUTTON_W);
    this->search_button->setH(SEARCH_ROW_H);
    this->search_button->setOnPressEnd([this](){ this->startSearch(); });
    WidgetFunctions::Add(this->search_button);
    y += SEARCH_ROW_H + MARGIN;

    // 状態表示は1行に収まる長さへ切り詰めてある(Label自体は折り返せるが、
    // 2行になった分の高さをこの下のresult_listの位置計算が見込んでいないため、
    // はみ出した2行目がresult_listの背景で隠れてしまう。--shotで実際に確認して気付いた)
    this->status_label = new Label<PICO_STR_L>(content.x + MARGIN, y,
        dict_ready ? "語句を入力し検索を押す" : "辞書ファイルが開けません");
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

    // 見出し(表示用語句、太字)はスクロールしない固定行。長い説明を
    // スクロールしている間も「今読んでいるのは何の語か」が見えるようにする
    this->detail_title = new Label<PICO_STR_L>(content.x + MARGIN, y, "");
    this->detail_title->setFontSize(FontFn::Small);
    this->detail_title->setMaxWidth(content.w - MARGIN * 2);
    WidgetFunctions::Add(this->detail_title);
    y += DETAIL_TITLE_H + MARGIN;

    // 詳細欄はScrollContainerで包み、説明文の長さに関わらず全文を
    // スクロールして読めるようにする(固定の高さで切り詰めていた以前の実装だと
    // 長い説明が途中で見えなくなっていた)。detail_labelの座標は
    // ScrollContainerを親とするローカル座標(=container内で(0,0)起点)になる。
    this->detail_scroll = new ScrollContainer(
        content.x + MARGIN, (int16_t)y,
        (int16_t)(content.w - MARGIN * 2), (int16_t)(content.y + content.h - y - MARGIN)
    );
    this->detail_label = new Label<PICO_STR_2KiB>(DETAIL_PADDING, DETAIL_PADDING, "");
    this->detail_label->setFontSize(FontFn::Small);
    // 辞書の説明文(3列目)は自由記述で"~"等をマークアップ記号ではなく
    // そのままの文字として使っている(実データで836行)。マークアップとして
    // 解釈させず常に生テキストとして表示する
    this->detail_label->setDisableAutoTextDecoration(true);
    // ScrollContainerの縦スクロールバー(private定数のためここでは数値で
    // 見込むしかない。ScrollContainer::SCROLL_Lと必ず一致させること)ぶん
    // 幅を狭める
    this->detail_label->setMaxWidth(content.w - MARGIN * 2 - DETAIL_SCROLLBAR_W - DETAIL_PADDING * 2);
    this->detail_scroll->add(this->detail_label); // 所有権はdetail_scrollへ移る
    WidgetFunctions::Add(this->detail_scroll); // visitAll()で子(detail_label)もまとめて登録される

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
    this->detail_title  = nullptr;
    this->detail_scroll = nullptr;
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
    this->detail_title->setText("");
    this->detail_label->setText("");
    this->detail_scroll->scrollToTop();
    this->shown_count_ = 0;

    if(query.length() == 0){
        this->status_label->setText("語句を入力してください");
        return;
    }
    // ↑これも1行に収まる長さ(status_labelのコメント参照)。以下の各状態文言も同様

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
                status.appendFormat("%d件以上(絞り込み推奨)", this->dict_.count());
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

    FixedString<PICO_STR_L> title;
    title.assign("**");
    title.append(hit->term);
    title.append("**");
    this->detail_title->setText(title);

    // 説明文(3列目)はマークアップとして解釈させたくない生テキストなので、
    // 見出しの太字マークアップとは別のLabel(setDisableAutoTextDecoration済み)へ
    // そのまま渡す
    this->detail_label->setText(hit->desc);
    // 表示内容が変わったので、スクロール範囲を新しい高さへ引き直し、
    // 前に選んでいた項目のスクロール位置を引きずらないよう先頭へ戻す
    this->detail_scroll->refreshContentBounds();
    this->detail_scroll->scrollToTop();
}
