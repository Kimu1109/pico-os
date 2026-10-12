#include "gui/scenes/TouchCalibScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Touch_Functions.hpp"
#include "functions/TouchCalib_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Power_Functions.hpp"

#include <cstdio>

void TouchCalibScene::onEnter(){
    const Rect content = Scene::contentRect();

    this->view = new CalibView(content.x, content.y, content.w, content.h);
    WidgetFunctions::Add(this->view);

    this->message = new Label<PICO_STR_LL>(content.x + MARGIN, content.y + 24, "");
    this->message->setFontSize(FontFn::Small);
    this->message->setMaxWidth(content.w - MARGIN * 2);
    this->message->setMaxHeight(140);
    this->message->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->message);

    const int bw = content.w - MARGIN * 2;
    const int bh = 40;
    int y = content.y + content.h - (bh + 6) * 3 - 4;
    this->btn_a = new Button(content.x + MARGIN, y, "開始");
    this->btn_a->setW(bw); this->btn_a->setH(bh);
    this->btn_a->setOnPressEnd([this](){
        if(this->phase == Phase::Intro) this->startCapture();
        else if(this->phase == Phase::Result){
            // 保存
            if(TouchCalibFunctions::Save(this->fitted)){
                this->saved = true;
                this->backup = this->fitted;
                this->setMessage("保存しました。次の起動からもこの補正を使います", PICO_DARKGREEN);
                this->btn_a->setVisible(false);
                this->btn_c->setText("設定へ戻る");
            }else{
                this->setMessage("保存できませんでした(SDカードを確認してください)", PICO_RED);
            }
        }else{
            this->startCapture(); // Failed: やり直し
        }
    });
    WidgetFunctions::Add(this->btn_a);

    y += bh + 6;
    this->btn_b = new Button(content.x + MARGIN, y, "既定に戻す");
    this->btn_b->setW(bw); this->btn_b->setH(bh);
    this->btn_b->setOnPressEnd([this](){
        if(this->phase == Phase::Intro){
            TouchCalibFunctions::Clear();
            this->backup = TouchCalib::current;
            this->setMessage("補正を外して、初期の設定に戻しました", PICO_DARKGREEN);
        }else{
            this->btn_c->setText("破棄して戻る");
            this->startCapture(); // Result: やり直し
        }
    });
    WidgetFunctions::Add(this->btn_b);

    y += bh + 6;
    this->btn_c = new Button(content.x + MARGIN, y, "戻る");
    this->btn_c->setW(bw); this->btn_c->setH(bh);
    this->btn_c->setOnPressEnd([this](){
        // Result で未保存なら、押した「破棄」は元に戻して戻る(onExitでも戻す)
        SceneFunctions::Pop();
    });
    WidgetFunctions::Add(this->btn_c);

    if(!TouchCalib::has_current) TouchCalibFunctions::Reset();
    this->backup = TouchCalib::current;
    this->saved = false;
    this->phase = Phase::Intro;
    this->showPhase();
}

void TouchCalibScene::onExit(){
    this->restoreBackup();
    this->view = nullptr;
    this->message = nullptr;
    this->btn_a = this->btn_b = this->btn_c = nullptr;
}

void TouchCalibScene::restoreBackup(){
    //保存していない仮の補正のまま画面を離れても、元の補正へ戻す
    TouchCalib::current = this->backup;
    TouchCalib::has_current = true;
}

void TouchCalibScene::setMessage(const char* text, int8_t color){
    if(!this->message) return;
    this->message->setText(text);
    this->message->setTextColor(color);
}

void TouchCalibScene::showPhase(){
    const bool capture = (this->phase == Phase::Capture);
    this->btn_a->setVisible(!capture);
    this->btn_b->setVisible(!capture);
    this->btn_c->setVisible(!capture);
    this->message->setVisible(true);
    this->view->clearMarks();
    this->view->setProbe(false, 0, 0);

    switch(this->phase){
        case Phase::Intro:
            this->btn_a->setText("開始");
            this->btn_b->setText("既定に戻す");
            this->btn_c->setText("戻る");
            this->setMessage("画面に出る十字の中心を、指先かタッチペンで順に5回押してください。"
                             "押すたびに少し止めると正確になります。", PICO_FORECOLOR);
            break;
        case Phase::Result: {
            char buf[PICO_STR_LL];
            snprintf(buf, sizeof(buf),
                "補正できました(ずれ 平均%.1fpx / 最大%.1fpx)。画面のどこかを押して、"
                "青い点が指の位置に出るか確かめてください。", this->err_mean, this->err_max);
            this->btn_a->setText("保存");
            this->btn_b->setText("やり直し");
            this->btn_c->setText("破棄して戻る");
            this->setMessage(buf, PICO_FORECOLOR);
            break;
        }
        case Phase::Failed:
            this->btn_a->setText("やり直し");
            this->btn_b->setVisible(false);
            this->btn_c->setText("戻る");
            this->setMessage("押した位置がうまく合いませんでした。十字の真ん中をもう一度、"
                             "順番どおりに押してください。", PICO_RED);
            break;
        case Phase::Capture:
            break;
    }
}

void TouchCalibScene::startCapture(){
    //やり直しのときは仮の補正を捨てる(生の値で求めるので影響はしないが、確かめの点を元に戻すため)
    TouchCalib::current = this->backup;
    this->phase = Phase::Capture;
    this->index = 0;
    this->sum_u = this->sum_v = 0;
    this->samples = 0;
    this->touch_frames = 0;
    this->was_touching = false;
    this->wait_release = true;   // 「開始」を押した指が離れるまで待つ
    this->showPhase();
    for(int i = 0; i < kPoints; i++) this->view->addMark(PointX(i), PointY(i));
    this->view->setActive(0);
    this->message->setVisible(false);
}

void TouchCalibScene::finishCapture(){
    float xs[kPoints], ys[kPoints];
    for(int i = 0; i < kPoints; i++){ xs[i] = (float)PointX(i); ys[i] = (float)PointY(i); }

    TouchCalib::Affine t;
    if(!TouchCalib::Fit(this->raw_u, this->raw_v, xs, ys, kPoints, t)
       || !TouchCalibFunctions::Sane(t)){
        LOG_SYS_WARN("タッチ補正: 変換を求められませんでした");
        this->phase = Phase::Failed;
        this->showPhase();
        return;
    }
    TouchCalib::Residual(t, this->raw_u, this->raw_v, xs, ys, kPoints, this->err_max, this->err_mean);
    if(this->err_max > kMaxAcceptErr){
        LOG_SYS_WARN("タッチ補正: ずれが大きすぎます(最大%.1fpx)", this->err_max);
        this->phase = Phase::Failed;
        this->showPhase();
        return;
    }

    this->fitted = t;
    TouchCalib::current = t;   // 確かめの間だけ仮に使う
    TouchCalib::has_current = true;
    LOG_SYS_OK("タッチ補正: 平均%.2fpx 最大%.2fpx", this->err_mean, this->err_max);
    this->phase = Phase::Result;
    this->showPhase();
}

void TouchCalibScene::onUpdate(){
    if(!this->view) return;

    if(this->phase == Phase::Result){
        this->view->setProbe(OSData::isTouched, OSData::touchX, OSData::touchY);
        return;
    }
    if(this->phase != Phase::Capture) return;

    PowerFunctions::KeepAwake();

    if(this->wait_release){
        if(!OSData::isTouched && !OSData::isTouchEnd) this->wait_release = false;
        return;
    }

    if(OSData::isTouched){
        this->was_touching = true;
        this->touch_frames++;
        if(this->touch_frames > kSkipFrames){
            this->sum_u += PICO_Touch::raw_u;
            this->sum_v += PICO_Touch::raw_v;
            this->samples++;
        }
        return;
    }

    if(!this->was_touching) return;

    //指が離れた: 十分に押していれば採用、短すぎれば同じ点をやり直す
    this->was_touching = false;
    const bool enough = (this->samples >= kMinSamples);
    if(enough){
        this->raw_u[this->index] = this->sum_u / (float)this->samples;
        this->raw_v[this->index] = this->sum_v / (float)this->samples;
        this->view->setMarkDone(this->index, true);
        this->index++;
    }
    this->sum_u = this->sum_v = 0;
    this->samples = 0;
    this->touch_frames = 0;

    if(this->index >= kPoints){
        this->finishCapture();
    }else{
        this->view->setActive(this->index);
    }
}
