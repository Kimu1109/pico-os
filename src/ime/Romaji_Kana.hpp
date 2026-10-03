#pragma once
//
// Romaji_Kana.hpp
// 物理キーボードのローマ字入力 → ひらがな。
//
// 打った英字(小文字)を1文字ずつ Feed() し、確定したかなを受け取る。まだかなにならない
// ローマ字(例: "ky")は呼び出し側が持つ pending に残る。描画にもSDにも依存しないので
// ホストテスト(romaji_kana_test)で規則を固めてある。
//
// 規則:
//   - 表(kTable)で一番長く一致するものを取る。表のどれかの頭になっている間は待つ
//   - n' → ん、n の後に子音(y以外)が来たら ん。nn は次が母音/y なら「ん+な行」、それ以外は ん 1つ
//   - 同じ子音が続いたら っ("tta" → った。"tchi" → っち)
//   - 記号: - → ー , → 、 . → 。 [ → 「 ] → 」 ~ → 〜 / → ・
//   - どれにも当たらない文字はそのまま出す(英字が読みに残る)
//
// 状態は pending(数バイト)だけで、表はフラッシュに置かれる const 配列。確保は一切しない。
//
#include <cstddef>
#include <cstring>
#include "util/FixedString.hpp"

namespace RomajiKana {

    struct Entry { const char* romaji; const char* kana; };

    // 並び順に意味は無い(Lookup()が全件を見る)
    inline constexpr Entry kTable[] = {
        {"a","あ"},{"i","い"},{"u","う"},{"e","え"},{"o","お"},
        {"ka","か"},{"ki","き"},{"ku","く"},{"ke","け"},{"ko","こ"},
        {"ca","か"},{"cu","く"},{"co","こ"},{"qa","くぁ"},{"qi","くぃ"},{"qe","くぇ"},{"qo","くぉ"},
        {"sa","さ"},{"si","し"},{"su","す"},{"se","せ"},{"so","そ"},{"shi","し"},
        {"ta","た"},{"ti","ち"},{"tu","つ"},{"te","て"},{"to","と"},{"chi","ち"},{"tsu","つ"},
        {"na","な"},{"ni","に"},{"nu","ぬ"},{"ne","ね"},{"no","の"},
        {"ha","は"},{"hi","ひ"},{"hu","ふ"},{"he","へ"},{"ho","ほ"},{"fu","ふ"},
        {"ma","ま"},{"mi","み"},{"mu","む"},{"me","め"},{"mo","も"},
        {"ya","や"},{"yu","ゆ"},{"ye","いぇ"},{"yo","よ"},
        {"ra","ら"},{"ri","り"},{"ru","る"},{"re","れ"},{"ro","ろ"},
        {"la","ぁ"},{"li","ぃ"},{"lu","ぅ"},{"le","ぇ"},{"lo","ぉ"},
        {"wa","わ"},{"wi","うぃ"},{"we","うぇ"},{"wo","を"},
        {"n'","ん"},{"xn","ん"},
        {"ga","が"},{"gi","ぎ"},{"gu","ぐ"},{"ge","げ"},{"go","ご"},
        {"za","ざ"},{"zi","じ"},{"zu","ず"},{"ze","ぜ"},{"zo","ぞ"},{"ji","じ"},
        {"da","だ"},{"di","ぢ"},{"du","づ"},{"de","で"},{"do","ど"},
        {"ba","ば"},{"bi","び"},{"bu","ぶ"},{"be","べ"},{"bo","ぼ"},
        {"pa","ぱ"},{"pi","ぴ"},{"pu","ぷ"},{"pe","ぺ"},{"po","ぽ"},
        {"va","ゔぁ"},{"vi","ゔぃ"},{"vu","ゔ"},{"ve","ゔぇ"},{"vo","ゔぉ"},
        {"fa","ふぁ"},{"fi","ふぃ"},{"fe","ふぇ"},{"fo","ふぉ"},{"fyu","ふゅ"},
        {"kya","きゃ"},{"kyi","きぃ"},{"kyu","きゅ"},{"kye","きぇ"},{"kyo","きょ"},
        {"gya","ぎゃ"},{"gyi","ぎぃ"},{"gyu","ぎゅ"},{"gye","ぎぇ"},{"gyo","ぎょ"},
        {"sya","しゃ"},{"syi","しぃ"},{"syu","しゅ"},{"sye","しぇ"},{"syo","しょ"},
        {"sha","しゃ"},{"shu","しゅ"},{"she","しぇ"},{"sho","しょ"},
        {"zya","じゃ"},{"zyi","じぃ"},{"zyu","じゅ"},{"zye","じぇ"},{"zyo","じょ"},
        {"ja","じゃ"},{"ju","じゅ"},{"je","じぇ"},{"jo","じょ"},
        {"jya","じゃ"},{"jyi","じぃ"},{"jyu","じゅ"},{"jye","じぇ"},{"jyo","じょ"},
        {"tya","ちゃ"},{"tyi","ちぃ"},{"tyu","ちゅ"},{"tye","ちぇ"},{"tyo","ちょ"},
        {"cha","ちゃ"},{"chu","ちゅ"},{"che","ちぇ"},{"cho","ちょ"},
        {"cya","ちゃ"},{"cyi","ちぃ"},{"cyu","ちゅ"},{"cye","ちぇ"},{"cyo","ちょ"},
        {"dya","ぢゃ"},{"dyi","ぢぃ"},{"dyu","ぢゅ"},{"dye","ぢぇ"},{"dyo","ぢょ"},
        {"tha","てゃ"},{"thi","てぃ"},{"thu","てゅ"},{"the","てぇ"},{"tho","てょ"},
        {"dha","でゃ"},{"dhi","でぃ"},{"dhu","でゅ"},{"dhe","でぇ"},{"dho","でょ"},
        {"twu","とぅ"},{"dwu","どぅ"},{"tsa","つぁ"},{"tsi","つぃ"},{"tse","つぇ"},{"tso","つぉ"},
        {"nya","にゃ"},{"nyi","にぃ"},{"nyu","にゅ"},{"nye","にぇ"},{"nyo","にょ"},
        {"hya","ひゃ"},{"hyi","ひぃ"},{"hyu","ひゅ"},{"hye","ひぇ"},{"hyo","ひょ"},
        {"bya","びゃ"},{"byi","びぃ"},{"byu","びゅ"},{"bye","びぇ"},{"byo","びょ"},
        {"pya","ぴゃ"},{"pyi","ぴぃ"},{"pyu","ぴゅ"},{"pye","ぴぇ"},{"pyo","ぴょ"},
        {"mya","みゃ"},{"myi","みぃ"},{"myu","みゅ"},{"mye","みぇ"},{"myo","みょ"},
        {"rya","りゃ"},{"ryi","りぃ"},{"ryu","りゅ"},{"rye","りぇ"},{"ryo","りょ"},
        {"xa","ぁ"},{"xi","ぃ"},{"xu","ぅ"},{"xe","ぇ"},{"xo","ぉ"},
        {"xya","ゃ"},{"xyu","ゅ"},{"xyo","ょ"},{"lya","ゃ"},{"lyu","ゅ"},{"lyo","ょ"},
        {"xtu","っ"},{"ltu","っ"},{"xtsu","っ"},{"ltsu","っ"},
        {"xwa","ゎ"},{"lwa","ゎ"},{"xka","ゕ"},{"xke","ゖ"},
        {"-","ー"},{",","、"},{".","。"},{"[","「"},{"]","」"},{"~","〜"},{"/","・"},
    };
    inline constexpr size_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);

    // 読みの途中に置ける記号(Feed()が扱う英字以外の文字)
    inline bool IsSymbol(char c){
        return c == '-' || c == ',' || c == '.' || c == '[' || c == ']' || c == '~' || c == '/' || c == '\'';
    }
    // 打ったときに読み(変換)を始める文字か。英字(大小)と上の記号
    inline bool StartsComposition(char c){
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || IsSymbol(c);
    }

    inline bool IsVowel(char c){ return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o'; }

    // s(長さn)が表のどれかと完全一致すればそのかな、しなければnullptr。
    // prefix(どれかの頭になっている)ならtrueを返す
    inline const char* Lookup(const char* s, size_t n, bool& prefix){
        prefix = false;
        const char* exact = nullptr;
        for(size_t i = 0; i < kTableSize; i++){
            const char* r = kTable[i].romaji;
            if(strncmp(r, s, n) != 0) continue;
            if(r[n] == '\0') exact = kTable[i].kana;
            else prefix = true;
        }
        return exact;
    }

    // pending に残ったローマ字から、確定できるかなを全部outへ出す。
    // 最後に「まだ表の頭になっている」ぶんだけが pending に残る
    template<size_t P, size_t N>
    inline void Settle(FixedString<P>& pending, FixedString<N>& out){
        while(pending.length() > 0){
            const char* s = pending.c_str();
            const size_t n = pending.length();
            bool prefix = false;
            const char* kana = Lookup(s, n, prefix);
            if(kana && !prefix){
                out.append(kana);
                pending.clear();
                return;
            }
            if(prefix) return; //続きを待つ("n" は "na" の頭なので待つ)

            //"nn" は次の文字を見てから決める("konnichiha" の nn は ん+に、"kannji" の nn は ん だけ)
            if(s[0] == 'n' && n >= 2 && s[1] == 'n'){
                if(n == 2) return;
                out.append("ん");
                const char x = s[2];
                FixedString<P> rest;
                if(IsVowel(x) || x == 'y') rest.assign(s + 1);   // 2つ目の n は次のかなの頭
                else if(x == '\'') rest.assign(s + 3);          // "nn'" は ん 1つ
                else rest.assign(s + 2);                          // "nnk" の2つ目の n は捨てる
                pending = rest;
                continue;
            }

            //表に無い並び。頭の1文字をどう扱うか決めて、残りでもう一度
            FixedString<P> rest;
            rest.assign(s + 1);
            if(s[0] == 'n' && n >= 2 && s[1] != 'y'){
                out.append("ん");               // n + 子音(または記号)
            }else if(n >= 2 && ((s[0] == s[1] && !IsVowel(s[0]) && s[0] >= 'a' && s[0] <= 'z') ||
                                (s[0] == 't' && s[1] == 'c'))){
                out.append("っ");               // 子音の重ね("tchi" → っち も)
            }else if(kana){
                out.append(kana);               // 完全一致したが、続きでは頭にならなかった
                pending.clear();
                return;
            }else{
                const char raw[2] = { s[0], '\0' };
                out.append(raw);                // どれにも当たらない文字はそのまま
            }
            pending = rest;
        }
    }

    // 1文字足す。c は英字なら小文字にしてから渡すこと
    template<size_t P, size_t N>
    inline void Feed(FixedString<P>& pending, char c, FixedString<N>& out){
        const char ch[2] = { c, '\0' };
        if(!pending.append(ch)){
            //溢れた(普通は起きない)。持っているぶんをそのまま出して入れ直す
            out.append(pending);
            pending.clear();
            pending.append(ch);
        }
        Settle(pending, out);
    }

    // 変換・確定の前に、残ったローマ字を片付ける("n" → ん、それ以外はそのまま)
    template<size_t P, size_t N>
    inline void Flush(FixedString<P>& pending, FixedString<N>& out){
        if(pending.length() == 0) return;
        if(strcmp(pending.c_str(), "n") == 0 || strcmp(pending.c_str(), "nn") == 0) out.append("ん");
        else out.append(pending);
        pending.clear();
    }

    // 送り仮名の頭の1文字(UTF-8) → SKK辞書の送りの印("あるk" の k)。分からなければ0。
    // SKK-JISYO の印は子音、母音の送りは母音そのもの(あ行 → a i u e o)
    inline char OkuriMarker(const char* kana){
        struct Row { char marker; const char* chars; };
        static constexpr Row kRows[] = {
            {'a',"あ"},{'i',"い"},{'u',"う"},{'e',"え"},{'o',"お"},
            {'k',"かきくけこ"},{'g',"がぎぐげご"},
            {'s',"さしすせそ"},{'z',"ざじずぜぞ"},
            {'t',"たちつてとっ"},{'d',"だぢづでど"},
            {'n',"なにぬねのん"},
            {'h',"はひふへほ"},{'b',"ばびぶべぼ"},{'p',"ぱぴぷぺぽ"},
            {'m',"まみむめも"},{'y',"やゆよ"},{'r',"らりるれろ"},{'w',"わを"},
        };
        if(!kana || strlen(kana) < 3) return 0;
        for(const Row& row : kRows){
            for(const char* p = row.chars; *p; p += 3){
                if(strncmp(p, kana, 3) == 0) return row.marker;
            }
        }
        return 0;
    }
}
