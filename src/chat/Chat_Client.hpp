#pragma once

#include "chat/Chat_Proto.hpp"
#include "task/Http_Request.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

// チャットサーバ(CHAT_PROTOCOL.md)との通信係。ChatScene が値メンバとして持ち、毎フレーム update() を呼ぶ。
//
// - 設定は SD の /sys/chat.cfg(server = https://..., token = ...)。Webの「pico-os の設定」で出る内容をそのまま置く
// - **接続は使い回す**(HttpRequest の keep-alive)。HTTPSはハンドシェイクで1〜2秒画面が止まるので、
//   最初の1回だけ止まり、あとは数秒ごとの問い合わせでも止まらない。**繋いでいる間はTLSの約40KBを持つ**
// - 同時に投げる要求は1本だけ。優先順は「送信 > 利用者の操作(検索・参加・招待) > 開いている部屋の新着 > 部屋の一覧」
// - 部屋の一覧に出るのは**参加している部屋だけ**。オープンチャットは search() → joinOpen()、
//   プライベートチャットは参加者が requestInvite() で出した参加コードを joinByCode() して入る
// - 部屋を開いている間は poll_ms(既定3秒)ごとに新着を、一覧を見ている間は10秒ごとに部屋の一覧を取る。
//   ロングポーリングにしないのは、待っている間は送信できなくなるため(接続が1本しか無い)
// - 失敗したら5秒→10秒→…→60秒と間を空けて取り直す。401(トークン違い)は自動では取り直さない
//
// 発言は新しいほうから kMaxMessages 件だけ持つ(リングバッファ。1件約580B)。
class ChatClient : public IHttpSink, public ChatProto::IMessageSource {
    public:
        constexpr static int kMaxRooms = 16;
        constexpr static int kMaxMessages = 30;
        constexpr static int kMaxSearchHits = 16;

        constexpr static uint32_t kDefaultPollMs = 3000;
        constexpr static uint32_t kRoomsPollMs = 10000;
        constexpr static uint32_t kMinBackoffMs = 5000;
        constexpr static uint32_t kMaxBackoffMs = 60000;

        enum class State : uint8_t {
            NotConfigured, // /sys/chat.cfg が無い/足りない
            Offline,       // Wi-Fiに繋がっていない
            Ok,            // 最後の問い合わせは成功した
            Error,         // 最後の問い合わせは失敗した(しばらくして取り直す)
            AuthError,     // トークンが違う(取り直さない)
        };

        ChatClient() = default;
        ChatClient(const ChatClient&) = delete;
        ChatClient& operator=(const ChatClient&) = delete;

        // /sys/chat.cfg を読む。使える設定が揃っていればtrue
        bool loadConfig();
        // 設定を直接与える(ホストテスト用。loadConfig() と同じ検査をする)
        bool configure(const char* server, const char* token, uint32_t poll_ms = kDefaultPollMs);

        // 開く部屋を変える(0 = 部屋の一覧を見ている)。変わったら発言を捨てて最新から取り直す
        void setRoom(uint32_t room_id);
        uint32_t room() const { return room_; }

        // 開いている部屋へ発言する。送信待ちが既にある/長すぎる/部屋を開いていないならfalse
        bool send(const char* text);
        bool sending() const { return pending_send_ || kind_ == Kind::Send; }

        // 待たずに今すぐ取り直す(「更新」ボタン。401の後もこれで再開する)
        void refreshNow();

        // 毎フレーム呼ぶ。network_up=false(Wi-Fiが切れている)の間は何もせず接続も閉じる
        void update(bool network_up = true);
        // 進行中の要求をやめ、持っている接続も閉じる(onExit用)
        void stop();

        // ---- 利用者の操作(1度に1つ。結果は actionRevision() が変わったら lastAction() 等で読む) ----
        enum class Action : uint8_t { None, Search, JoinOpen, JoinCode, Invite };

        // オープンチャットを名前で探す(空なら全部)。結果は searchCount()/searchAt()
        bool search(const char* query);
        // 検索で見つけたオープンチャットに参加する。成功すると部屋の一覧へ足し、joinedRoomId() に入る
        bool joinOpen(uint32_t room_id);
        // 参加コードでプライベートチャットに入る(小文字・区切り無しでもよい)
        bool joinByCode(const char* code);
        // 開いている部屋(プライベートチャット)の参加コードを発行する。結果は inviteCode()/inviteExpires()
        bool requestInvite();

        bool actionBusy() const { return pending_action_ != Action::None || isActionKind(kind_); }
        uint32_t actionRevision() const { return action_rev_; }
        Action lastAction() const { return last_action_; }
        bool lastActionOk() const { return last_action_ok_; }
        // 失敗の理由(サーバの1行目など)。成功なら空
        const char* actionMessage() const { return action_msg_.c_str(); }
        uint32_t joinedRoomId() const { return joined_room_; }
        const char* inviteCode() const { return invite_code_.c_str(); }
        uint32_t inviteExpires() const { return invite_expires_; }
        int searchCount() const { return search_count_; }
        const ChatProto::SearchHit& searchAt(int i) const { return search_hits_[i]; }

        // 開いていた部屋が読めなくなった(追い出された/部屋が消えた)たびに増える。画面は一覧へ戻す
        uint32_t roomLostRevision() const { return room_lost_rev_; }

        State state() const { return state_; }
        // 画面へ出す状態の1行(失敗の理由など)。問題が無ければ空
        const char* statusText() const { return status_.c_str(); }
        // 一覧/発言を受け取るたびに増える。画面側はこれが変わったときだけ描き直す
        uint32_t roomsRevision() const { return rooms_rev_; }
        uint32_t messagesRevision() const { return msgs_rev_; }
        uint32_t statusRevision() const { return status_rev_; }
        // 次の問い合わせで使い回す接続を持っているか(keep-alive の確認用)
        bool connectionKept() const { return req_.hasIdleConnection(); }
        // 部屋を開いてから最初の取得が済んだか(「読み込み中」を出すかどうか)
        bool messagesLoaded() const { return loaded_; }

        int roomCount() const { return room_count_; }
        const ChatProto::Room& roomAt(int i) const { return rooms_[i]; }
        const ChatProto::Room* findRoom(uint32_t id) const;

        // 持っている発言。0 が一番古い
        int messageCount() const override { return msg_count_; }
        const ChatProto::Message& messageAt(int i) const override {
            return msgs_[(msg_head_ + i) % kMaxMessages];
        }

        // IHttpSink
        bool write(const void* data, size_t len) override;

    private:
        enum class Kind : uint8_t { None, Rooms, Messages, Send, Search, JoinOpen, JoinCode, Invite };
        static bool isActionKind(Kind k){ return k >= Kind::Search; }

        // 応答の行を、要求の種類に応じて振り分ける
        class Lines : public ChatProto::LineSink {
            public:
                ChatClient* owner = nullptr;
            protected:
                void onLine(char* line, size_t len) override;
        };

        HttpRequest req_;
        Lines lines_;
        Kind kind_ = Kind::None;
        uint32_t req_room_ = 0;  // Messages を投げた部屋
        uint32_t send_room_ = 0; // 送信待ちの発言の宛先

        // ---- 設定 ----
        Url server_;
        FixedString<PICO_STR_LL> auth_;  // "Authorization: Bearer ..."
        uint32_t poll_ms_ = kDefaultPollMs;
        bool configured_ = false;

        // ---- 状態 ----
        State state_ = State::NotConfigured;
        FixedString<PICO_STR_LL> status_;
        uint32_t status_rev_ = 0;
        uint32_t room_ = 0;
        bool loaded_ = false;
        uint32_t next_poll_ms_ = 0;      // この時刻になったら次を取りに行く(millis())
        uint32_t backoff_ms_ = 0;
        bool wait_manual_ = false;       // 401の後。refreshNow() まで取りに行かない

        bool pending_send_ = false;
        ChatProto::Text send_text_;      // 送り終わるまで HttpRequest が指しているので値で持つ

        // ---- 利用者の操作 ----
        Action pending_action_ = Action::None;
        ChatProto::Name action_text_;    // 検索語/参加コード
        uint32_t action_room_ = 0;       // 参加/招待する部屋
        Action last_action_ = Action::None;
        bool last_action_ok_ = false;
        FixedString<PICO_STR_LL> action_msg_;
        uint32_t action_rev_ = 0;
        uint32_t joined_room_ = 0;
        ChatProto::Room joined_;         // 参加の応答(最後まで読めたら一覧へ足す)
        bool joined_ok_ = false;
        ChatProto::InviteCode invite_code_;
        uint32_t invite_expires_ = 0;
        bool invite_ok_ = false;
        ChatProto::SearchHit search_hits_[kMaxSearchHits];
        int search_count_ = 0;
        uint32_t room_lost_rev_ = 0;

        // 応答が2xxでなかったときの本文の1行目(理由)
        FixedString<PICO_STR_LL> error_line_;
        bool error_line_done_ = false;

        // ---- 受け取ったもの ----
        ChatProto::Room rooms_[kMaxRooms];
        int room_count_ = 0;
        ChatProto::Room new_rooms_[kMaxRooms]; // 受信中の一覧(最後まで読めたら入れ替える)
        int new_room_count_ = 0;
        uint32_t rooms_rev_ = 0;

        ChatProto::Message msgs_[kMaxMessages];
        int msg_head_ = 0;
        int msg_count_ = 0;
        uint32_t last_id_ = 0;
        uint32_t msgs_rev_ = 0;
        bool msgs_changed_ = false;

        bool begin(Kind kind, HttpRequest::Method method, const char* path, const char* query,
                   const void* body = nullptr, size_t body_len = 0);
        void finish();
        void setStatus(State s, const char* text);
        void failLater(const char* why);
        bool queueAction(Action a, uint32_t room_id, const char* text);
        void beginAction();
        void finishAction(Kind kind, bool ok, const char* why);
        void upsertRoom(const ChatProto::Room& r);
        void removeRoom(uint32_t id);
        void addMessage(const ChatProto::Message& m);
        void clearMessages();
        void onLine(char* line);
};
