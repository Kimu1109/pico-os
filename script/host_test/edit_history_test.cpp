// 元に戻すの記録(src/util/Edit_History.hpp)とクリップボード(Clipboard_Functions)のテスト。
//
// 確かめること:
//   - 元に戻す/やり直しで文書が1件ずつ前の状態に戻る(乱数の編集で、各時点の写しと突き合わせる)
//   - 溢れたら古い件から捨て、残った件は正しく戻せる
//   - 続けて打った文字・Backspace・Deleteはまとめる(1秒を空ける/改行/離れた所はまとめない)
//   - ひとまとまり(chained)は一度に戻り、一度にやり直す
//   - 保存した時点へ戻ると atSaved()、やり直しを捨てると戻れなくなる
//   - クリップボード: \r とNULを捨てる・4KiBで文字の途中で切らない・空にすると返す
#include "util/Edit_History.hpp"
#include "functions/Clipboard_Functions.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// 文書へ置き換えを当てて記録する
static void apply(EditHistory& h, std::string& doc, int pos, int del_n, const std::string& ins, uint32_t now, bool chained = false){
    h.record(pos, doc.data() + pos, del_n, ins.data(), (int)ins.size(), now, chained);
    doc.replace(pos, del_n, ins);
}

static bool undoOne(EditHistory& h, std::string& doc){
    EditHistory::Step st;
    bool any = false;
    while(h.undo(st)){
        doc.replace(st.pos, st.ins_n, std::string(st.del, st.del_n));
        any = true;
        if(!st.chained) break;
    }
    return any;
}

static bool redoOne(EditHistory& h, std::string& doc){
    EditHistory::Step st;
    bool any = false;
    do{
        if(!h.redo(st)) break;
        doc.replace(st.pos, st.del_n, std::string(st.ins, st.ins_n));
        any = true;
    }while(h.nextRedoChained());
    return any;
}

static void testRandom(){
    srand(12345);
    EditHistory h;
    std::string doc = "hello\nworld";
    std::vector<std::string> snaps{doc};
    uint32_t now = 0;
    bool ok = true;
    for(int i = 0; i < 400; i++){
        now += 5000; //まとめない
        const int pos = doc.empty() ? 0 : rand() % (int)(doc.size() + 1);
        int del_n = rand() % 6;
        if(pos + del_n > (int)doc.size()) del_n = (int)doc.size() - pos;
        std::string ins;
        const int n = rand() % 8;
        for(int k = 0; k < n; k++) ins += (char)("ab\ncd"[rand() % 5]);
        if(del_n == 0 && ins.empty()) continue;
        apply(h, doc, pos, del_n, ins, now);
        snaps.push_back(doc);
    }
    const std::string final_doc = doc;
    //戻せるところまで戻す(溢れて捨てた分は戻れない)
    int k = (int)snaps.size() - 1;
    int undone = 0;
    while(undoOne(h, doc)){
        k--;
        undone++;
        if(k < 0 || doc != snaps[k]){ ok = false; break; }
    }
    check(ok, "乱数の編集: 1件ずつ元の写しと同じに戻る");
    check(undone > 20 && undone < (int)snaps.size() - 1, "乱数の編集: 溢れた古い件は捨てて、残りは戻せる");
    check(h.usedBytes() <= EditHistory::kBytes, "4KiBに収まる");
    int redone = 0;
    while(redoOne(h, doc)) redone++;
    check(redone == undone && doc == final_doc, "やり直しで最後の状態へ戻る");
}

static void testMerge(){
    EditHistory h;
    std::string doc;
    uint32_t t = 100;
    //続けて打つ
    for(char c : std::string("abc")) apply(h, doc, (int)doc.size(), 0, std::string(1, c), t += 100);
    //1秒空ける→別の件
    apply(h, doc, 3, 0, "d", t += 2000);
    apply(h, doc, 4, 0, "e", t += 100);
    check(doc == "abcde", "打った文字");
    undoOne(h, doc);
    check(doc == "abc", "1秒空けた後の打鍵は別の件(まとめて戻る)");
    undoOne(h, doc);
    check(doc == "", "続けて打った文字はまとめて戻る");
    redoOne(h, doc);
    redoOne(h, doc);
    check(doc == "abcde", "やり直し");

    //Backspaceの連打
    apply(h, doc, 4, 1, "", t += 2000);
    apply(h, doc, 3, 1, "", t += 100);
    apply(h, doc, 2, 1, "", t += 100);
    check(doc == "ab", "Backspace");
    undoOne(h, doc);
    check(doc == "abcde", "続けて消した分はまとめて戻る(Backspace)");

    //Deleteの連打
    apply(h, doc, 1, 1, "", t += 2000);
    apply(h, doc, 1, 1, "", t += 100);
    check(doc == "ade", "Delete");
    undoOne(h, doc);
    check(doc == "abcde", "続けて消した分はまとめて戻る(Delete)");

    //改行はまとめない
    apply(h, doc, 5, 0, "x", t += 2000);
    apply(h, doc, 6, 0, "\n", t += 100);
    apply(h, doc, 7, 0, "y", t += 100);
    undoOne(h, doc);
    check(doc == "abcdex\n", "改行の後の文字は別の件");
    undoOne(h, doc);
    check(doc == "abcdex", "改行も別の件");

    //離れた所はまとめない
    apply(h, doc, 0, 0, "1", t += 2000);
    apply(h, doc, 7, 0, "2", t += 100);
    undoOne(h, doc);
    check(doc == "1abcdex", "離れた所への打鍵は別の件");
    undoOne(h, doc);

    //breakMerge
    apply(h, doc, 0, 0, "p", t += 2000);
    h.breakMerge();
    apply(h, doc, 1, 0, "q", t += 100);
    undoOne(h, doc);
    check(doc == "pabcdex", "breakMerge()の後はまとめない");
}

static void testChained(){
    EditHistory h;
    std::string doc = "a-b-c-d";
    uint32_t t = 0;
    apply(h, doc, 0, 0, "X", t += 5000);
    //すべて置換(- → ==)をひとまとまりで
    int count = 0;
    for(size_t p = doc.find('-'); p != std::string::npos; p = doc.find('-', p + 2)){
        apply(h, doc, (int)p, 1, "==", t += 10, count++ > 0);
    }
    check(doc == "Xa==b==c==d", "置換");
    undoOne(h, doc);
    check(doc == "Xa-b-c-d", "ひとまとまりは一度に戻る");
    redoOne(h, doc);
    check(doc == "Xa==b==c==d", "ひとまとまりは一度にやり直す");
    undoOne(h, doc);
    undoOne(h, doc);
    check(doc == "a-b-c-d" && !h.canUndo(), "前の件も戻る");
}

static void testSaved(){
    EditHistory h;
    std::string doc;
    check(h.atSaved(), "最初は保存した状態");
    apply(h, doc, 0, 0, "abc", 0);
    check(!h.atSaved(), "書き換えると未保存");
    h.markSaved();
    apply(h, doc, 3, 0, "d", 100); //保存した後はまとめない
    check(!h.atSaved(), "保存した後の書き換え");
    undoOne(h, doc);
    check(doc == "abc" && h.atSaved(), "保存した時点へ戻ると未保存でない");
    undoOne(h, doc);
    check(!h.atSaved(), "さらに戻ると未保存");
    redoOne(h, doc);
    check(h.atSaved(), "やり直して保存した時点へ");
    undoOne(h, doc);
    apply(h, doc, 0, 0, "z", 5000); //やり直しを捨てる
    check(!h.atSaved() && !h.canRedo(), "やり直しを捨てると保存した時点へは戻れない");
    undoOne(h, doc);
    check(doc == "" && !h.atSaved(), "捨てた後は戻っても未保存のまま");

    //1件で入り切らない
    EditHistory h2;
    std::string big(5000, 'x');
    std::string d2;
    apply(h2, d2, 0, 0, big, 0);
    check(!h2.canUndo() && !h2.atSaved(), "4KiBを超える1件は記録できず、未保存のまま");
}

static void testClipboard(){
    using namespace ClipboardFunctions;
    check(Empty() && Get()[0] == '\0', "最初は空");
    const uint32_t r0 = Revision();
    check(Set("a\r\nb\0c", 6) && std::string(Get()) == "a\nbc" && Length() == 4, "\\r とNULを捨てる");
    check(Revision() != r0, "変わると番号が進む");

    std::string big;
    while(big.size() < kMaxBytes + 10) big += "あ"; //3バイトずつ
    check(!Set(big.c_str(), big.size()), "上限を超えるとfalse");
    check(Length() <= kMaxBytes && Length() % 3 == 0, "上限で文字の途中で切らない");

    Clear();
    check(Empty() && Length() == 0, "空にする");
    check(Set("") && Empty(), "\"\"で空");
}

int main(){
    testRandom();
    testMerge();
    testChained();
    testSaved();
    testClipboard();
    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
