// ローマ字→かな(src/ime/Romaji_Kana.hpp)のテスト。
//
// 確かめること: 基本の50音・拗音・ん(nn / n' / n+子音 / 最後のn)・っ(子音の重ね / tch)・
// 記号・表に無い文字はそのまま・送りの印(OkuriMarker)
#include "ime/Romaji_Kana.hpp"
#include <cstdio>
#include <cstring>

static int failures = 0;

static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// romajiを全部打って(最後にFlushして)できたかな
static FixedString<256> conv(const char* romaji, bool flush = true){
    FixedString<8> pending;
    FixedString<256> out;
    for(const char* p = romaji; *p; p++) RomajiKana::Feed(pending, *p, out);
    if(flush) RomajiKana::Flush(pending, out);
    else out.append(pending); // 残りを見えるように
    return out;
}

static void expect(const char* romaji, const char* kana){
    const FixedString<256> got = conv(romaji);
    char label[256];
    snprintf(label, sizeof(label), "%s → %s (実際: %s)", romaji, kana, got.c_str());
    check(strcmp(got.c_str(), kana) == 0, label);
}

int main(){
    expect("aiueo", "あいうえお");
    expect("konnnichiha", "こんにちは");
    expect("konnichiha", "こんにちは");
    expect("shinbun", "しんぶん");
    expect("kanji", "かんじ");
    expect("hon'ya", "ほんや");
    expect("honya", "ほにゃ");
    expect("kyouto", "きょうと");
    expect("gakkou", "がっこう");
    expect("matcha", "まっちゃ");
    expect("tsukue", "つくえ");
    expect("fairu", "ふぁいる");
    expect("ti-mu", "ちーむ");
    expect("hai,sou.", "はい、そう。");
    expect("[kagi]", "「かぎ」");
    expect("xtu", "っ");
    expect("ltsu", "っ");
    expect("qwerty", "qうぇrty");
    expect("n", "ん");
    expect("kannji", "かんじ");
    expect("onnna", "おんな");
    expect("nn", "ん");
    expect("kann'i", "かんい");

    // 打っている途中: "ky" はまだかなにならない
    check(strcmp(conv("ky", false).c_str(), "ky") == 0, "途中の ky は保留される");
    check(strcmp(conv("kan", false).c_str(), "かn") == 0, "かn の n は保留される(次が母音かもしれない)");

    // 送りの印
    check(RomajiKana::OkuriMarker("く") == 'k', "く → k");
    check(RomajiKana::OkuriMarker("い") == 'i', "い → i(形容詞)");
    check(RomajiKana::OkuriMarker("う") == 'u', "う → u");
    check(RomajiKana::OkuriMarker("っ") == 't', "っ → t");
    check(RomajiKana::OkuriMarker("し") == 's', "し → s");
    check(RomajiKana::OkuriMarker("ん") == 'n', "ん → n");
    check(RomajiKana::OkuriMarker("ゃ") == 0, "ゃ → 0");
    check(RomajiKana::OkuriMarker("a") == 0, "ASCII → 0");

    check(RomajiKana::StartsComposition('a') && RomajiKana::StartsComposition('K') && RomajiKana::StartsComposition('-'),
          "英字と記号は読みを始める");
    check(!RomajiKana::StartsComposition('1') && !RomajiKana::StartsComposition(' '), "数字と空白は読みを始めない");

    printf(failures ? "\n%d 件失敗\n" : "\n全て成功\n", failures);
    return failures ? 1 : 0;
}
