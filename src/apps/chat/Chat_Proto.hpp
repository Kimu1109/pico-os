#pragma once

#include "net/Http_Response.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstddef>
#include <cstdint>

// チャットサーバ(server/chat/chat_server.py)の応答を読む部品。仕様は CHAT_PROTOCOL.md。
//
// 応答は1行1件のTSVなので、**ソケットを持たずに行を切り出して解釈するだけ**の層にしてある
// (HttpResponse と同じく、ネットワーク無しでホストテストから全経路を確かめられる。
//  script/host_test/chat_proto_test.cpp)。
namespace ChatProto {

    // ---- 上限(CHAT_PROTOCOL.md「制限値」。サーバ側の MAX_* と揃えること) ----
    constexpr size_t kMaxTextBytes = 500;  // 発言1件の本文
    constexpr size_t kMaxNameBytes = 45;   // 表示名・部屋名

    // 1行の上限。本文は \n 等を2文字へエスケープして送られるので最悪で倍になる
    // (id + 時刻 + 名前 + タブ3つ + 本文の倍)
    constexpr size_t kMaxLineBytes = 10 + 1 + 10 + 1 + kMaxNameBytes + 1 + kMaxTextBytes * 2 + 16;

    using Name = FixedString<PICO_STR_M>;
    using Text = FixedString<PICO_STR_512B>;

    static_assert(PICO_STR_M > kMaxNameBytes, "Name に名前が入りきらない");
    static_assert(PICO_STR_512B > kMaxTextBytes, "Text に本文が入りきらない");

    // 部屋での自分の役割(CHAT_PROTOCOL.md「役割と権限」)。強い順に大きい
    enum class Role : uint8_t { Member = 0, Admin = 1, Owner = 2 };

    struct Room {
        uint32_t id = 0;
        Name name;
        uint32_t last_id = 0;   // 部屋の最新の発言id(0 = 発言なし)
        uint32_t unread = 0;    // 自分の未読の数
        bool is_private = false; // プライベートチャット(参加コードで入る)。v1のサーバは列が無いので false
        Role role = Role::Member;
    };

    // オープンチャットの検索結果の1件
    struct SearchHit {
        uint32_t id = 0;
        Name name;
        uint32_t members = 0;   // 参加している人数
        bool joined = false;    // 自分が既に参加しているか
    };

    // 参加コード。"XXXX-XXXX"(9文字)
    using InviteCode = FixedString<PICO_STR_S>;

    struct Message {
        uint32_t id = 0;
        uint32_t epoch = 0;     // UNIX時刻(秒)
        Name name;              // 発言者の表示名
        Text text;              // エスケープを戻した本文(\n を含み得る)
    };

    // 発言の並びを読むだけの口(ChatLogView がこれ越しに ChatClient を読む。ウィジェットに通信を知らせないため)
    class IMessageSource {
        public:
            virtual ~IMessageSource() = default;
            virtual int messageCount() const = 0;
            // 0 が一番古い
            virtual const Message& messageAt(int i) const = 0;
    };

    // "123" → 123。数字以外が混ざる/空/32bitを超えるならfalse
    bool ParseUint(const char* s, uint32_t& out);

    // 本文のエスケープ(\\ \n \t)を戻す。out へ入りきらなければfalse(入ったところまでは残る)。
    // 知らない "\x" は x そのものにする(前方互換)
    bool Unescape(const char* src, Text& out);

    // "id<TAB>名前<TAB>最新id<TAB>未読" の1行。line は区切りを '\0' へ書き換えて使う。
    // 足りない列・数値でない列があればfalse。後ろに列が増えていても無視する(前方互換)
    bool ParseRoom(char* line, Room& out);

    // "id<TAB>時刻<TAB>名前<TAB>本文" の1行
    bool ParseMessage(char* line, Message& out);

    // "private" → true。それ以外(open・空・知らない値)は false
    bool ParseKind(const char* s);
    // "owner"/"admin" → その役割。それ以外は Member(知らない値も一番弱い役割として扱う)
    Role ParseRole(const char* s);

    // 検索結果 "id<TAB>名前<TAB>人数<TAB>参加済み(0/1)" の1行
    bool ParseSearchHit(char* line, SearchHit& out);

    // 参加/部屋を作った応答 "id<TAB>名前<TAB>種類<TAB>役割" の1行。out の last_id/unread は0
    bool ParseJoined(char* line, Room& out);

    // 参加コードの発行の応答 "コード<TAB>期限(UNIX時刻)<TAB>回数" の1行
    bool ParseInvite(char* line, InviteCode& code, uint32_t& expires);

    // 受け取ったバイト列を行へ切り分けて onLine() へ渡すシンク。
    // 1行が kMaxLineBytes を超えたらその行は捨てて数える(次の改行から読み直す)。
    // CRLF の CR は落とす。
    class LineSink : public IHttpSink {
        public:
            bool write(const void* data, size_t len) override;
            // 最後の行が改行で終わっていなかった場合に、それを流す
            void flush();
            void resetLines();
            int droppedLines() const { return dropped_; }

        protected:
            virtual void onLine(char* line, size_t len) = 0;

        private:
            char buf_[kMaxLineBytes + 1];
            size_t len_ = 0;
            bool overflow_ = false;
            int dropped_ = 0;
    };
}
