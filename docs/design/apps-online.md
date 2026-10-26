# 通信するアプリ(カレンダー・チャット・TODO・メール・Spotify・ニュース)

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## iCalendarの読み取り (`src/apps/calendar/Ical.hpp`) (2026-09-23)

カレンダーアプリの第1段。**Google CalendarのOAuth/APIではなくiCal(.ics)を選んだ**
(OAuthはHTTPS・トークン管理・JSONパーサが全て要り、いずれも今のコードに無い。
iCalは行指向のテキストで、Googleも「iCal形式の非公開URL」で同じものを出す)。
当初は「Google側のURLはHTTPSなので母艦のサーバがHTTPで中継する」予定だったが、**arduino-picoにBearSSL(`WiFiClientSecure`)が同梱されていると分かり、本体でHTTPSに対応した**(2026-09-23。下の「HTTPS」参照。取得は`CalendarSync`)。

- **`Ical::Parser`はバイト列を好きな切れ目で受け取る**(`HttpResponse`と同じ増分方式)。
  行の折り返し(次の行頭の空白)もここで畳む。`ParseFile()`はSDから256Bずつ読んで食わせるだけ。
  **`IcalCalendar`へ追記する**(clearしない)ので、複数の.icsを1つへ合成できる。
- **時刻は全て現地時刻へ揃えて持つ**(`IcalTime{day=1970-01-01からの通算日数, sec=0時からの秒/-1で終日}`)。
  `...Z`は`Options::utc_offset_sec`でずらし、`TZID=`付きは現地時刻とみなす(VTIMEZONEは解釈しない。サマータイムも無視)。
- **`Options::window_from_day/to_day`で窓の外の予定を読み捨てる。** Googleの非公開URLは**過去の予定を全部**
  返すので、窓を切らないと`kMaxEvents=64`(約16KB)がすぐ昔の予定で埋まる。
- **RRULEは一部対応**: DAILY/WEEKLY/MONTHLY/YEARLY + INTERVAL/COUNT/UNTIL/BYDAY/WKST、
  MONTHLYの「第n曜日」(1〜5, -1)、DTSTARTと食い違わないBYMONTHDAY/BYMONTH。
  **それ以外(BYSETPOS等)は`rule.supported=false`にして初回だけ出す**(間違った日に出すよりまし)。
- **引き当て(`StartsOn()`/`OccursOn()`)は日ごとの算術で、回を1つずつ展開しない。** 月表示の42マス×64件を
  毎回引いても軽いように。COUNTが絡み、かつ「存在しない日」(31日の無い月・2/29・第5週)が
  あり得る場合だけ周期をループで数える(RFC 5545どおり存在しない日はCOUNTに数えない)。
- **例外**: EXDATEと、RECURRENCE-ID付きの上書き予定。後者は`finish()`で親(同じUIDの32bitハッシュ)の
  `exdates`へ畳み込み、上書き側は単発の予定として残す(STATUS:CANCELLEDなら残さない)。
  除外は「日」で持つ(対応する繰り返しは1日1回までなので足りる)。1件あたり`kMaxExDates=8`まで。
  **覚えるのは読み込みの窓(の`kMaxSpanScan`日前から)にかかる例外だけ**(2026-09-23)。Googleは何年分もの
  例外を全部書いてくるので、以前は長く続く定例の`exdates`が昔の分で埋まり、窓の中の「削除した回/移動した回」が
  元の日にも出ていた。上書き予定の控え(`kMaxOverrides=32`)も同じ理由で窓の近くだけ。
- VEVENTの**直下だけ**を読む。VALARMにもSUMMARYがあり、拾うと予定名が通知文で上書きされる。
- ホストテストは`script/host_test/ical_test.cpp`(RFC 5545のWKSTの例、第n曜日、31日/2/29の飛ばし、
  1バイトずつ食わせた場合、UTF-8の途中での折り返し等)。
- `EventsOn(cal, day, out, max)`が1日ぶんの予定を「終日と前日からの続きが先、残りは開始時刻順」で並べる(一覧の表示順)。
- **説明文(DESCRIPTION)は持たない**(64件ぶん持つとRAMを食う)。各予定は`file_index`(どの.icsか)と
  `ordinal`(そのファイルで何番目のVEVENTか。読み捨てた分も数える)だけを持ち、詳細を開くときに
  `ReadDescription(path, ordinal, out)`でその1件だけ読み直す(`Parser`の「予定を集めず説明文だけ拾う」読み方。
  目当てのVEVENTを読み終えたらファイルの残りは読まない)。説明文は1論理行の上限(512B)で切れる。
- 取得は`apps/calendar/Calendar_Sync`(下の「CalendarScene 実装詳細」参照)。

## CalendarScene 実装詳細 (2026-09-23)

`[戻る] … [<] 2026年9月 [>] [今日]` + 月の格子(`MonthGrid`) + 選んだ日の予定一覧(`ScrollList`)の1画面。

- **SDの`/calendar/`直下の`*.ics`を全部読んで1つの`IcalCalendar`へ重ねる**(`PICO_Path::DIR::CALENDAR`)。
  Googleのカレンダーごとの非公開URLを1ファイルずつ置く想定。
- **読むのは表示中の格子(前後の月の空きマスを含む6週間)にかかる予定だけで、月を移るたびに読み直す**
  (窓を切らないと`kMaxEvents`が過去の予定で埋まるため)。`onEnter()`でも毎回読み直す
  (別のアプリで書き換わっているかもしれないので)。読み直しは同期で、大きな.icsだと一瞬止まる。
  重くなったら`Word_Dict`のようにフレーム分割する。
- **シーン本体は約18KB**(`IcalCalendar`約16KB + 取得用の`CalendarSync`約2.4KB をメンバに持つ)。`MarkdownScene`と同じく「シーン本体は数十バイト」の例外。
- **`utc_offset_sec`は`LocalUtcOffsetSec()`が`localtime_r`と`gmtime_r`の差から求める**(newlibに`tm_gmtoff`が無いため)。
  TZは`network.cfg`の`timezone`(`TimeFunctions`が`setenv("TZ")`済み)。
- **NTP同期前(2020年より前)は「今日」が分からない扱い**で、仮に1970年1月を出す。`onUpdate()`が時計が合った
  時点で今日の月へ飛ぶ(PCビルドでも起動直後の数フレームはこの状態を通る)。0時を回ったら今日の印だけ動かす。
- 一覧の時刻欄は「終日」「09:30-10:30」「22:00-」(翌日へまたぐ)「02:00まで」(前日からの続きが今日終わる)「(続き)」。
  **`~02:00`にしないのは、16pxフォントの`~`が上線のような形で読めないため**(PCビルドの`--shot`で気づいた)。
- 一覧は`ScrollList`なので**長い予定名は右で切れる**(折り返さない)。**2回タップで詳細**(`dialogs/EventDetailDialog`)が開き、
  題名・日時(日をまたぐ回は始まりと終わり)・場所・繰り返し・カレンダー名・説明文を全文スクロールで読める
  (本文は`DictScene`の詳細欄と同じく`ScrollContainer`+`Label`)。
- **複数のカレンダーを重ねると色分けする**。`/calendar/*.ics`を**ファイル名順**に並べて`file_index`を振り、
  `kCalendarColors`(白地で読める濃い8色)から色を決める。**名前順にするのは、SdFatの列挙順が「作った順」で、
  取得のたびにファイルを差し替えると入れ替わる(=色が変わる)ため**。格子の点(`MonthGrid::setDots()`。
  違うカレンダーの色を先に並べ、余った点は同じ色を繰り返す)と一覧の文字色(`ScrollListTools::Item::color`)に使う。
  .icsが1つだけなら今までどおり(緑の点・黒い文字)。読む.icsは8つまで。
- **取得(`apps/calendar/Calendar_Sync`)**: `/calendar/sources.cfg`の「名前 = URL」を1件ずつ取り、`/calendar/<名前>.ics`へ置く
  (`webcal://`は`https://`として扱う)。**`Doc_Fetch`/`Doc_Cache`は通さない** — キャッシュは1件64KiBで頭打ちなのに
  Googleの非公開URLは過去の予定を全部返して数百KBになる上、`/cache/<ホスト>/<パス>`へミラーすると非公開URLの
  秘密の部分がディレクトリ名としてSDに散らばるため。上限は1件1MB。
  - **取得に失敗しても手元の`.ics`は壊さない**: `.ics.part`へ書き、200で最後まで読めて**先頭が`BEGIN:VCALENDAR`**のとき
    だけ差し替える(ログイン画面のHTMLを返されても上書きしない)。ETagは`<名前>.etag`に覚えて次から条件付きGET(304)。
  - 名前はファイル名になるので英数字と`_ -`だけ(20文字まで)。値が`Config_Functions`の上限(128B)を超えるので
    `ParseLine()`だけを借りて自前の512Bバッファで読む。
  - `CalendarScene`は**ランチャから開いたとき1回だけ自動で取りに行く**(Wi-Fiに繋がっているときだけ。
    `Push()`から戻っただけでは取り直さない)。**1回描いてから**始めるので、TLSのハンドシェイクで止まる前に
    ボタンの「取得中」が見える。[更新]ボタンで手動でも取れる。失敗が残ればボタンが赤の「再試行」になり、
    表示は前回の`.ics`のまま。中身が変わったものがあったときだけ読み直す(全部304なら何もしない)。
  - `onExit()`で取得は打ち切る(`onUpdate()`が来なくなるため)。
  - Web版は取りに行けない(HTTPSのスタブが常に失敗する)。
- `ScrollListTools::Item`に**項目ごとの文字色`color`(-1で一覧の色)**を足した(カレンダーの色分けのため。汎用の拡張で、選択中の反転表示が優先)。

`MonthGrid`(`widgets/apps/`)は「子を持たずrender()で直接描き、タップ位置から逆算する」型。
日曜赤・土曜青、今日は赤の二重枠、選択中は黒塗り+白抜き、予定のある日は数字の下に点(最大3つ)。
**予定そのものは知らず、シーンが`setMonth()`/`setCounts()`で流し込む**(`AnalogClock`と同じ理由)。
幅を7で割った余りは土曜の列へ足す(`TabBar`と同じ)。「2026年10月」は84pxで2行へ折り返したのでタイトル幅は100px。

## チャット (`src/apps/chat/` / `ChatScene` / `server/chat/`) (2026-09-24)

SUMMARY.md #8の「チャットツール」。**Discordとの連携は見送り、自前のサーバにした。**
Discordは自分のアカウントでの自動操作(self-bot)が規約違反で、Bot名義での発言にしかならない上、
Pico側にJSONパーサ・WebSocket(即時受信したい場合)が要る。自前なら`docs/PROTOCOL.md`と同じく
「pico-osが苦手なことは全部サーバでやる」設計にできる。仕様は`docs/CHAT_PROTOCOL.md`。

- **サーバ**: `server/chat/chat_server.py`(Python標準ライブラリのみ、SQLite)。Raspberry Piで動かし、
  **Let's Encryptの証明書でHTTPS化**する(手順は`server/chat/README.md`)。
  - 80番でACME(http-01)の確認ファイルを配り、それ以外はhttpsへ転送する(`--http-port`/`--acme-root`)。
    **証明書がまだ無ければ置かれるまで80番だけで待つ**ので、初回もcertbotの`--webroot`で取れる
  - **証明書はファイルの更新時刻を見て、次の接続から読み直す**(certbotの自動更新後に再起動が要らない)。
    certbotの`--deploy-hook`(`certbot-deploy-hook.sh`)が`/etc/pico-chat/tls/`へ写す
  - 認証は、pico-osは`Authorization: Bearer <トークン>`、WebはCookie + 書き込みに`X-Pico-Chat`ヘッダ必須。
    トークン/セッションはハッシュだけを保存。トークンはWebの「pico-os の設定」で発行し直せる
  - 未読の数は人ごと・部屋ごとにサーバが覚える(発言を取った分が既読になる。Picoとで共通)
- **Webクライアント**: `server/chat/web/index.html`(1ファイル、同じサーバが配る)。ロングポーリング(`wait=25`)で即時受信
- **pico-os側**: `ChatScene`(部屋の一覧 ⇔ 部屋の中を1シーンで切り替え、`applyMode()`)。設定は`/sys/chat.cfg`
  (`server = ...` / `token = ...` / 任意で`poll-ms`)
  - `apps/chat/Chat_Proto`: 1行1件のTSVの読み取り(本文だけ`\n` `\t` `\\`をエスケープして送る)と行の切り出し(`LineSink`)。ソケットを持たない
  - `apps/chat/Chat_Client`: `HttpRequest`で問い合わせる係。**同時に1本、優先順は送信 > 開いている部屋の新着(3秒ごと) > 部屋の一覧(10秒ごと)**。
    **ロングポーリングにしない**のは、接続が1本しか無く、待っている間は送信できなくなるため。失敗は5秒→60秒で取り直し、401は取り直さない
  - **`HttpRequest`にkeep-alive(`setKeepAlive()`)と任意のヘッダ1行(`setExtraHeader()`)を足した**。HTTPSはハンドシェイクで
    1〜2秒画面が止まるので、使い回さないと数秒ごとに固まる。**使い回している間はTLSの約40KBを持ち続ける**(`ChatScene::onExit()`で閉じる)。
    使い回すのは、応答が長さ付きで`Connection: close`でない場合だけ(`HttpResponse::canReuseConnection()`)。
    接続が死んでいたら(`begin()`での`connected()`確認、または1バイトも来ずに閉じた場合)1回だけ繋ぎ直す。既定は無効なので、
    Luaの`pico.http_request`やMarkdownブラウザの挙動は変わらない
  - `widgets/apps/ChatLogView`: 発言の一覧。**「子を持たずrender()で直接描く」型**。折り返しの計算(1文字ごとの`textWidth`)は
    **発言1件につき1回だけ**で、高さをidごとに覚える(新着1件で全件を測り直さない)。一番下を見ている間だけ新着に付いていく
  - **シーン本体は約25KB**(`ChatClient`が発言30件ぶん約17.5KB + 部屋の一覧 + 行バッファ + `HttpRequest`約2KBを持つ)。`MarkdownScene`/`CalendarScene`と同じ例外
  - **Picoから送れるのは1回に約60文字**(オンスクリーンキーボードの入力欄が`Label<PICO_STR_LL>`=191バイトのため)。
    受け取るのは500バイトまで
  - 本文は`Label`を通さず直接描くので、`*`や`~`がマークアップとして消えることは無い
  - 部屋の一覧は参加している部屋だけ(下の「部屋の種類・参加コード・役割」)
- **部屋の種類・参加コード・役割(2026-09-27、プロトコルv2)**: Discord風に**オープンチャット**(検索に出て誰でも参加)と
  **プライベートチャット**(検索に出ず、参加していない人には有ることも404で隠す。参加者が発行する**参加コード**でだけ入れる)の2種類。
  一覧・発言の読み書きは参加している部屋だけ。仕様は`docs/CHAT_PROTOCOL.md`「部屋の種類」「参加コード」「役割と権限」
  - 参加コード: 8文字(`0O1I`抜きの32種=40bit、`XXXX-XXXX`)、**30分で無効**、任意で回数(1回きり等)。**参加者なら誰でも発行でき**、
    管理者以上は全部無効化できる。サーバはハッシュだけ持つ。外し続けると(人/IPごとに10分10回)429。
    「無い/期限切れ/使い切り/追放中」は区別せず404(総当たりの手がかりを与えない)。入力は小文字・区切り無し・全角でも通る(NFKC)
  - 役割: 部屋ごとに owner(1人)> admin > member。権限の表はサーバの`PERMS`1か所(`can(role, action)`)。追い出し(kick)と
    追放(ban。検索からもコードからも戻れない)は自分より弱い人だけ。オーナーが抜けたら一番強く古い人が継ぎ、最後の1人が抜けたら部屋ごと消す
  - **DBは`PRAGMA user_version`で版を持ち、v1のDBは起動時に自動移行**(roomsを作り直して`name`のUNIQUEを外し、
    オープンチャットの名前だけ部分インデックスで重複禁止。既存の部屋は全部オープン、その時点のユーザーは全員を参加者にする)。
    外部キー(ON DELETE CASCADE)があるので、作り直しは`foreign_keys=OFF`の間に行う
  - pico-os側: 一覧はアイコンで区別(吹き出し=オープン、鍵=プライベート)。下の[部屋を探す](`InputDialog`→検索結果の画面`Mode::Search`、
    タップで参加して開く)と[コードで参加]、プライベートチャットの中では上部の[招待]で参加コードを`MsgDialog`に出す
    (期限は時計が合っていれば「13:59まで有効」)。部屋を作る・抜ける・メンバー管理はWebだけ。
    `ChatClient`は利用者の操作(検索/参加/招待)を1つだけ持ち(`pending_action_`)、優先順は送信の次。結果は`actionRevision()`で知らせる。
    開いている部屋の新着取得が403/404になったら「追い出された/部屋が消えた」として部屋を閉じ`roomLostRevision()`を進める(画面は一覧へ戻す)
  - Web: サイドバーをオープン/プライベートに分け、探す/参加コード/作る(種類を選ぶ)、部屋のヘッダに招待・メンバー(役割の変更・
    追い出し・追放・削除)・退出
- **サーバ管理者(2026-09-27、プロトコルv3)**: `users.is_admin`(何人でも)。ユーザーの作成/パスワードの決め直し/無効化/管理者の任命、
  全部屋の一覧、サーバ設定(`settings`表。今は`room_create = all|admin`)。部屋では**管理の操作(`MODERATION_ACTIONS`)だけ**、
  参加していなくてもオーナーより強い立場(`SERVER_ADMIN_ROLE`、`role_rank()`で4)で行える。**発言の読み書き・参加コードは
  管理者でも参加が要る**(プライベートを黙って読めないように)。自分を外す/無効にするのは不可(管理者が0人になるのを防ぐ)。
  最初に`adduser`したユーザーが自動で管理者、v2からの移行では誰も管理者にしない(起動ログで`admin <名前>`を促す)。
  Webは「サーバ管理」ダイアログ(ユーザー・全部屋・設定)。部屋の「管理」はメンバーのダイアログ(`openMembers()`)を
  参加していない部屋にも開く形で使い回す。pico-os側の変更は無い(`/api/v1/me`の3・4列目も読まない)
  - ついでに直したもの: `Store.write()`が失敗した文(重複のINSERT等)の暗黙のトランザクションを残し、DBの書き込みの鍵を
    持ったままになって他のスレッドの書き込みが全部10秒待たされていた(`rollback()`するようにした)
- 検証: `chat_proto_test`/`chat_scene_test`(run.sh)、`chat_server_test.py`(run_net.sh。サーバを同じプロセスで立て、部屋の種類・参加コードの
  期限/回数/無効化/総当たり・役割ごとの可否・譲渡と退出・サーバ管理者の権限の範囲・v1のDBの移行を確かめる)、`chat_net_test`(run_net.sh。本物の`chat_server.py`を平文と使い捨てCAのHTTPSで立て、
  送受信・未読・keep-alive・無通信で切られた後の繋ぎ直し・401・検索と参加・参加コード・開いている部屋が消えたときを確かめる)、
  PCビルドの`--tap`で一覧→部屋→キーボード入力→送信まで、v2では一覧のアイコン・検索→参加・招待のダイアログを確認した。
  WebクライアントはChromium(Playwright)で作成/検索/参加コード/メンバー管理(追放すると相手の画面から部屋が消える)を確認した。
  **実機(RP2350 + BearSSL)とLet's Encryptの本物の証明書での接続は未確認**

## TODOアプリ(Todoist連携) (`src/apps/todo/` / `TodoScene` / `util/Json_Reader`) (2026-10-01)

ランチャの「TODO」。**Todoist(https://todoist.com)のタスクを見る・足す・完了にする + 時刻つきの期限をOSの通知で知らせる**。
連携先の比較(Google Tasks/Microsoft To Do=OAuth必須、iCloud=CalDAVのXML、Notion=応答が大きい)の結果、
**個人用のAPIトークン(Bearer)だけで使えるTodoist**にした。OAuthは使わない。

- **API は v1**(`https://api.todoist.com/api/v1/`。旧REST v2/Sync v9は廃止の流れ)。使うのは
  `GET /tasks`(すべて)・`GET /tasks/filter?query=`(今日=`today | overdue`、7日間=`overdue | next 7 days`)・
  `POST /tasks`(`{"content","due_string","due_lang":"ja"}`)・`POST /tasks/<id>/close`(204)。一覧は
  `{"results":[...],"next_cursor":...}`のページ分けで、`kMaxTasks`(30)件に達するまで続きを取る(上限`kMaxPages`=8)。
  フィルタは`todoist.cfg`の`today-filter`/`week-filter`で差し替えられる(英語で書けば利用者の言語設定に依らず通る)。
- **JSONは流しながら読む(`util/Json_Reader`、SAX型)**。応答は1件約800バイト(user_id/added_at/order_key…)あり、30件で20KBを超えるが、
  RAMに載るのはタスク1件ぶんの読みかけと値のバッファ(256B)だけ。`Todoist::TaskParser`が`results[].{id,content,priority,
  checked,is_deleted,parent_id,due.{date,string,is_recurring}}`だけを拾う。文字列は`\uXXXX`/サロゲートペアをUTF-8へ戻し、
  長いものは文字の途中で切らずに切り詰める。**C++側に汎用のJSONパーサが入ったのはこれが初めて**(他で要るときもこれを使う)。
- **期限は現地時刻へ揃えて持つ**(`Todoist::Due{day, sec}`。Icalと同じ)。`due.date`は`"2026-10-01"`(日付だけ)/
  `"…T15:00:00"`(浮動=現地)/`"…T06:00:00.000000Z"`(UTC。`setUtcOffset()`の分ずらす)。一覧は期限順(期限なしは最後、
  同じ日は日付だけ→時刻順、次に優先度)に並べ替える。表記は「今日 15:00」「明日」「10/3(土)」。期限切れは赤。
  優先度はTodoistの`priority`(4=画面のP1)で、一覧では`!!!`(P1)〜`!`(P3)。
- **通信係(`TodoistClient`)はChatClientと同じ形**: keep-alive(TLSの約40KBを開いている間だけ持つ。`onExit()`で閉じる)、
  同時に1本、操作(完了>追加)は一覧のページの間にも割り込む、5分ごとに取り直し、失敗は5秒→2分のバックオフ、401/403は
  `AuthError`で手動の[更新]まで待つ。一覧は受信用の配列へ取り終えてから入れ替える(途中で失敗しても前の一覧が残る。
  その代わり30件×2=約18KB)。完了は取り直しを待たずに一覧から消し、すぐ取り直す(繰り返しのタスクは次の回で並び直す)。
  失敗の理由は応答のJSONの`error`(`Todoist::ErrorParser`)。
- **トークン**: `/sys/todoist.cfg`の`token`。画面の[設定]から入れると`PICO_Secret`(用途`"todoist-token"`)で暗号化して保存する。
  **母艦で平文のまま書いて置いてもよく、読んだときに暗号化して書き直す**(SSHの秘密鍵と同じ流儀)。ほかのキー:
  `api`(既定`https://api.todoist.com`。テストで偽物へ向ける)/`reminders = true|false`/`remind-before-min`(既定0)/
  `today-filter`/`week-filter`。PCビルドで試すときの`pc/sdcard/sys/todoist.cfg`は`.gitignore`済み。
- **TLS**: api.todoist.com の証明書は Amazon(`Amazon RSA 2048 M01` ← `Amazon Root CA 1`)、TLS 1.2の`ECDHE-RSA-AES128-GCM`で
  BearSSLで話せる。**焼き込みのルートに Amazon Root CA 1 が無かったので足した**(上の「HTTPS」)。実際の鎖が新しいルートで
  検証でき、足す前のルートでは失敗することを`openssl verify`で確認した。
  **このリモート環境からの直接の接続は出口のゲートウェイが証明書を差し替えるため、PCビルドから本物には繋がらない**(正しく拒否される)。
- **リマインダー(`Todo_Reminders`)**: 取った一覧の時刻つきの期限を、通知(`NotificationFunctions`)の`At`の予約にする。
  OSが見張り`/sys/notify_rules.tsv`へ保存されるので、**アプリを閉じていても・再起動しても期限の時刻に知らせ、タップでTODOが開く**。
  送り主は空(C++)、tagは`td:<タスクのid>`。**空の送り主の予約は`kMaxRulesPerOwner`(4)件までなので、近い順に4件まで**
  (ほかのC++の予約があればその分減る)。表示(今日/7日間/すべて)ごとにしか一覧を取らないので、
  「一覧が網羅している期限の範囲(今日=今日の終わりまで、7日間=7日後まで、すべて=全部、溢れていたら無し)」の中で
  一覧に無くなった予約だけを消す(範囲の外の予約は別の表示で入れたものとして残す)。変わっていない予約は入れ直さない
  (SDへの書き込みを減らす)。時計が合う(NTP同期)まで入れない。
- **画面(`TodoScene`)**: `[戻る] TODO(件数) [設定][更新]` / `TabBar`(今日|7日間|すべて) / `ScrollList` / 選んだタスクの欄(3行:
  全文・期限(繰り返しは`due.string`)・優先度) / 状態の1行 / `[追加][完了]`。1回のタップで選び、もう一度タップか[完了]で
  確認の`MsgDialog`。[追加]は名前→(1フレーム空けて)期限の`InputDialog`
  (今日の表示からなら期限の初期値は「今日」)。日付が変わる/期限を過ぎると見た目が変わるので10秒ごとに見直す。
  通信中だけ`KeepAwake()`(待っているだけならスリープしてよい)。**シーン本体は約18KB**(ChatSceneと同じ例外)。
  アイコンは既存の`IconID::CheckboxOn`(新しいアイコンは足していない)。
- **未対応**: プロジェクト/セクション/ラベルの表示と絞り込み、説明文、サブタスクの入れ子(一覧では字下げだけ)、
  完了の取り消し(Todoistの画面からはできる)、編集、期限の変更。追加の期限は Todoist の自然言語(`due_lang=ja`。
  「明日 15時」「毎週月曜」)に任せていて、読めなければ`Date is invalid`等の理由が状態の行に出る。
- 検証: `todoist_proto_test`(run.sh。JSON: 入れ子/エスケープ/サロゲート/切り詰め/1バイトずつ/誤り、一覧: 必要なキーだけ・完了済みを
  飛ばす・上限・next_cursor、期限の読み取り/表記/期限切れ/並べ替え、失敗の理由、JSONの書き出し)、`todo_test`(run.sh。本物の
  NotificationFunctionsへの予約: 近い順に枠の数だけ・過ぎたもの/日付だけは入れない・何分前・範囲の中だけ消す・入れ直さない・
  保存される、TodoSceneの生成/解放とトークンの案内)、**`todoist_net_test`(run_net.sh。`script/host_test/todoist_fake_server.py`=
  Todoist API v1の偽物を平文/HTTPS/ページ分けで立てる)**: 3つの表示・並び順・UTC・ページを跨ぐ・30件で打ち切り・追加(" と \ の往復、
  期限)・読めない期限の理由・完了・繰り返しが次の回へ・404・keep-alive・401で止まる・todoist.cfgの読み込みと暗号化の書き直し。
  PCビルドの`--tap`/`--shot`(偽物の`api = http://127.0.0.1:8150`): 一覧・期限切れの赤・選択と詳細・完了の確認→一覧から消える・
  7日間の表示・追加のダイアログ・明日15時のタスクが`notify_rules.tsv`に`At`で入ること。
  **本物のTodoistへ実機(BearSSL、Amazon Root CA 1)で繋がることを確認済み(2026-10-22)**(一覧・追加・完了・リマインダーの通知)。

## メール(IMAPの受信ビュー) (`src/apps/mail/` / `MailScene`) (2026-10-21)

ランチャの「メール」。**IMAPのサーバから受信箱を読んで見せるだけ**(送信・返信・削除・フォルダの切り替えは無い)。

- **作り**(`src/apps/mail/`。層は中身が段2、`MailScene`が段5):
  - `Mail_Charset` … 文字コード → UTF-8。UTF-8(壊れた並びは 〓)/ ISO-2022-JP / Shift_JIS(cp932)/ EUC-JP / ISO-8859-1(windows-1252の
    0x80〜0x9Fも)。日本語の3つは **JIS X 0208 の区点 → Unicode の表(`Mail_Jis_Data.hpp`、94×94×2B=約17KB、フラッシュ)** を引く。
    表は `script/generate_jis_table.py` がPythonの cp932 の写像から作る(13区のNEC特殊文字 ①㈱ 等と、〜=U+FF5E 等のWindowsの字形。
    IBM拡張(0xFA〜)とJIS X 0212は 〓)。状態を持つので何回に分けて渡しても同じ結果。ISO-2022-JPは改行でASCIIへ戻す(戻し忘れ対策)
  - `Mail_Mime` … ヘッダーのRFC 2047(B/Q。隣り合う語は文字コードが同じなら繋いでから直す=UTF-8の1文字を2語に分けるメーラー対策)、
    差出人の名前、INTERNALDATE/Dateの日時、`charset=`等の引数(RFC 2231の`filename*=`と分割も)。
    **`TextExtractor`はメール1通を流しながら読む**: 行(1KB)とヘッダー1つ(1KB)の作業場だけで、multipartの境界を4段まで追い、
    最初の text/plain を本文にする(無ければ text/html をタグ・style・script・head を落として文字に。p/br/li/div等は改行、
    実体参照も)。base64/quoted-printableを解き、空行は1行に詰める。添付は数と名前だけ。置き場が足りなければ文字の途中で切らずに止める
  - `Imap_Proto` … IMAPの応答の組み立て(`Imap::Reader`。`{n}`のリテラルを含む1つの応答を4KBの固定長に。小さいリテラル=ヘッダーは
    中に残し、本文は`Handler`が望めば貯めずに流す。溢れても後ろの応答とずれない)、応答の分類、FETCHの読み取り(知らない項目は
    入れ子の括弧ごと読み飛ばす)、quoted string
  - `Imap_Client` … 通信係(下)。`Mail_OAuth` … Microsoft のサインイン(下の「Outlook.com」)。`MailScene` … 画面(下)
- **通信係(`ImapClient`)**: **IMAPS(993、最初からTLS)** を `net/HttpTransport` で繋ぐ(証明書の確かめ方・`/sys/tls/ca.pem`も同じ)。
  `tls = false` で平文も可(パスワードがそのまま流れるので自前のサーバの試験用)。**STARTTLS は無い**(平文で繋いだソケットを
  後からTLSへ切り替える口が `WiFiClientSecure` に無いため)。認証は `LOGIN "user" "pass"`(quoted string なので **8bitの文字の
  ユーザー名/パスワードは断る**。リテラルで送る形は作っていない)。手順は 挨拶 → LOGIN(か AUTHENTICATE XOAUTH2)→ SELECT → NOOP → FETCH。
  - 一覧は `FETCH lo:N (UID FLAGS INTERNALDATE RFC822.SIZE BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])` で**新しい方から20通**
    (`kMaxMessages`)。受信用の配列へ取り終えてから入れ替える(TODOと同じ。20通×2で約13KB)。日時は INTERNALDATE(届いた日時)
  - 本文は `UID FETCH u (UID FLAGS BODY[]<0.32768>)`。**先頭32KBだけ**取り、届いた分から`TextExtractor`へ流して本文の置き場
    (12KB)へ文字だけを書く。`mark-read = false` なら `BODY.PEEK[]`(既読にしない)。応答のFLAGSで一覧の既読も直す
  - **接続は使い回す**(開いている間はTLSの約40KBを持つ。`onExit()`でLOGOUTを送って閉じる)。3分ごとに NOOP で `EXISTS` を見て、
    増えていれば取り直す([更新]は必ず取り直す)。`EXPUNGE`/`UIDVALIDITY`の変化でも取り直す。失敗は5秒→2分のバックオフ。
    **LOGINの失敗(NO)は`AuthError`で止まり、[更新]まで繋ぎ直さない**(間違ったパスワードを繰り返し送るとサーバに締め出される)
  - 受信は`update()`から1フレーム8KBまで。応答30秒で打ち切り。**約33KB**(一覧×2・本文12KB・組み立て4KB・TextExtractor約3.5KB)
- **設定 `/sys/mail.cfg`**: `auth`(`password` 既定 / `microsoft`)/ `host` / `port`(既定993)/ `tls`(既定true)/ `user` / `password`(`PICO_Secret`、用途`"mail-password"`。
  **平文で置けば読んだときに暗号化して書き直す**)/ `oauth-client-id` / `oauth-tenant` / `oauth-authority`(テスト用)/ `mailbox`(既定INBOX)/ `mark-read`(既定true)。
  画面の[設定]は サーバ(`host` か `host:port`。`imaps://`も読む)→ ユーザー名 → パスワード(同じユーザーなら空欄で今のまま)を
  1フレームずつ空けて聞く。PCで試すときの`pc/sdcard/sys/mail.cfg`は`.gitignore`済み。
- **使えるサーバ**: LOGINで入れるもの。**Gmailは2段階認証を有効にしてアプリパスワードを作る**(普段のパスワードは不可)。
  Outlook.com/Hotmail(outlook.jp 等)は下の「Outlook.com」(2026-10-23)。プロバイダのメール(IMAPS対応のもの)・
  iCloud(アプリ用パスワード)・自前のDovecot等は入れる見込み。**焼き込みのルート証明書に無いCAのサーバは `/sys/tls/ca.pem` に足す**。
- **Outlook.com(Microsoft のアカウント)** (2026-10-23): Outlook.com は2024年からパスワードの LOGIN を受け付けないので、
  **OAuth2 のアクセストークンを `AUTHENTICATE XOAUTH2` で渡す**(`auth = microsoft`)。
  - **サインインはデバイスコードの流れ(RFC 8628)**: 本体ではブラウザを開けないので、`login.microsoftonline.com/<tenant>/oauth2/v2.0/devicecode`
    で「https://www.microsoft.com/link でコード XXXX を入れて」をもらい、画面に大きく出す。利用者がスマホ/PCでサインインする間、
    `token` を `interval` 秒ごとに問い合わせる(`authorization_pending` は待つ、`slow_down` は5秒延ばす、断られた/期限切れは止める)。
    scope は `https://outlook.office.com/IMAP.AccessAsUser.All offline_access`(後者で更新用トークンが返る)。client secret は使わない
  - **client_id は利用者が自分で登録したアプリのもの**(`oauth-client-id`)。Microsoft Entra 管理センター → アプリの登録 →
    「個人用 Microsoft アカウントのみ」(か職場も含める)・リダイレクトURIなし → 認証で「パブリック クライアント フローを許可する」をはい →
    API のアクセス許可で Microsoft Graph の委任 `IMAP.AccessAsUser.All` と `offline_access`(足さなくても最初のサインインで同意を聞かれる)。
    **他のアプリ(Thunderbird 等)の client_id を焼き込むことはしない**(他人のアプリになりすますことになる)。`oauth-tenant` は既定 `consumers`
    (個人のアカウント。職場/学校も使うなら `common` か テナントのID)
  - **トークン**: アクセストークンは約1時間で1〜3KB(RAMの4KBの置き場に。期限の5分前から古いとみなす)。更新用トークンは数百B〜1KBで、
    `mail.cfg` の値(160B)に入らないので **`/sys/mail_token.cfg` に255Bずつ行に分けて `PICO_Secret` で暗号化**(行ごとに用途の文字列
    `mail-oauth-<n>` を変える。同じ鍵ストリームを使い回さない)。取り直すたびに Microsoft は新しい更新用トークンを返すので保存し直す。
    平文/古い鍵で書かれていたら読んだときに書き直す(端末の鍵を作った後も)。ユーザーを変えたら消す
  - **応答のJSONは `JsonReader` で流しながら読む**。アクセストークンは 256B を超えるので、**`JsonReader::Handler::stringBuffer()`(足した口)で
    その値だけ置き場を差し出す**(読み手の作業場は大きくしない)
  - **XOAUTH2 の送り方**: SASL-IR は使わず、`AUTHENTICATE XOAUTH2` → `+` を待って `base64("user=" user ^A "auth=Bearer " token ^A ^A)` を送る
    (どのサーバでも通る形)。中身は数KBあるので組み立てずに base64 にしながら256Bずつ送る。断られると `+ <理由のJSON>` が来るので空行を返す
  - **流れ(`ImapClient`)**: 繋ぐ前に使えるアクセストークンが無ければ、更新用トークンで取り直してから繋ぐ。更新用トークンが無い/取り消された
    (`invalid_grant`)なら `NeedSignIn` で止まり繋がない(画面は「[更新] でサインイン」)。XOAUTH2 を断られたら、取ってから1分以上経った
    トークンなら1回だけ取り直してやり直す(それでも駄目なら `AuthError`。Outlook.com の設定で IMAP が無効のことが多い)
  - **`MailOAuth` は約13KB**(アクセストークン4KB・更新用トークン2KB×2・デバイスコード1KB・送るボディ4KB)。HTTPSは使い回さない(終わったら返す)。
    メールのシーンは合わせて約46KB+繋いでいる間のTLS
  - **画面**: [設定] → 種類(「Outlook.com」/「Gmail・その他」)→ メールアドレス → アプリのID → すぐサインインを始め、一覧の場所に
    URL・コード(大きい字)・残り分を出す。Esc でやめる。設定は `auth` / `host = outlook.office365.com` / `port = 993` / `oauth-client-id`
  - 実機と本物の Microsoft のサーバでは未確認(Todoist参照)。焼き込みのルート証明書(DigiCert Global Root G2)で通る見込み
- **画面(`MailScene`)**: 一覧 `[戻る] メール (未読 N) [設定][更新]` / `ScrollList`(未読=閉じた封筒・黒、既読=開いた封筒・灰色。
  「日付 差出人  件名」。日付は今日なら時刻、今年なら月/日、それより前は年から)/ 状態の1行(「新しい 20 通を表示(全 N 通)」等)。
  **タップで本文を開く**(TODOと違い1回で開く。壊す操作ではないので)。読む画面は同じシーンの中でウィジェットの表示を切り替える:
  `[一覧] 件名` / ヘッダー4行(差出人の名前・日時(曜日)・宛先の最初の1人(他N人)・添付の数と名前 か CC)/ 本文の`TextView`
  (本文の置き場を指すだけでコピーしない)/ 状態の1行(長くて途中まで・HTMLを文字にした)。一覧の行は並び(UIDの列)が同じなら
  作り直さずに中身だけ書き換える(既読になってもスクロールの位置が保たれる)。物理キーボードは Esc/Backspace で戻る、↑↓/PageUp/
  PageDown/スペースで本文のスクロール、r で更新。アイコンは `IconID::Mail` / `MailOpened`(tablerの mail / mail-opened。16pxで目視済み)。
- **未対応**: 送信(SMTP)・返信・転送・削除・フラグ・フォルダの一覧と切り替え(`mailbox`の手書きだけ)・21通目より古いメール・
  添付の保存・HTMLの表・画像・リンク(文字だけ)・GmailのOAuth・STARTTLS・8bitの文字のパスワード・新着の通知(アプリを閉じている間は見ない)・
  IDLE(押し出しの通知)。
- 検証: `mail_proto_test`(run.sh。文字コード: 3種の日本語・1バイトずつ・壊れたUTF-8・サロゲート・1252・戻し忘れ、ヘッダー: B/Q・語の分割・
  空白・折り返し・壊れた語、差出人の名前5形、日時、引数(RFC 2231とその分割)、本文: ISO-2022-JP・multipartの入れ子と添付の名前・HTMLだけ
  (style/コメント/実体参照/箇条書き)・alternativeの並びが逆・置き場不足で文字の切れ目・1KBを超える行と境界に似た行・ヘッダーだけ、
  IMAP: 応答の分類・リテラルの組み立て(1バイトずつ/流す/貯めきれない/溢れた行)・FETCH・知らない項目・Quote)、
  `mail_scene_test`(run.sh。アカウント無しの案内・Wi-Fi無し・生成/解放と入り直し・Esc・サーバ名の読み方・一覧の日付)、
  **`mail_net_test`(run_net.sh。`script/host_test/imap_fake_server.py`=IMAPの偽物を平文/TLSで立てる)**: パスワード違いで止まり
  繋ぎ直さない・新しい20通が新しい順・UIDと番号の取り違え・件名/差出人の各文字コード・未読の数・本文(multipart/HTML/ISO-2022-JP)・
  添付の名前・mark-readの有無・無いUID・取り消した本文を取り違えない・接続の使い回し(LOGINは1回)・新着・mail.cfgの読み込みと暗号化の書き直し・
  SaveAccount・8bitのパスワードを断る・**Microsoft(偽物に `--oauth-port` でサインインの偽物も立てる): サインインが無ければ繋がない・
  デバイスコード(pending→通る)・2KBを超えるアクセストークン・更新用トークンの暗号化保存と取り直し・XOAUTH2・無効になったら1回だけ取り直す・
  取り消されたら NeedSignIn で何度も取りに行かない・断られた・client_id 違い・SaveMicrosoftAccount**。
  `mail_proto_test` に JsonReader の stringBuffer・フォームの値・更新用トークンの保存(行の入れ替えで戻らない・壊れたら読まない)も。PCビルドの`--tap`/`--shot`(偽物を `--seed` で立て `host = 127.0.0.1` / `tls = false`)で
  一覧・4種の本文・既読になって灰色・設定のダイアログを目視した。**実機(BearSSL)と本物のメールサーバ(Gmail等)での確認は未**(Todoist参照)。

## Spotifyリモコン (`pc/sdcard/lua/apps/Spotify/`、Luaアプリ) (2026-10-14)

スマホ/PCのSpotifyで鳴っている再生を Web API で操作する(本体から音は出ない。**操作には Spotify Premium が要る**)。
- ファイル: `main.lua`(上の段のButtonとCanvas1枚・タップ/キー/コントローラー)/`api.lua`(トークン・`A.call`)/`player.lua`(状態の取得と操作)/
  `view.lua`(Canvasへ描く・タップの場所)/`icon.pimg`(`script/assets/generate_spotify_icon.py`)。
- **ログイン**: 本体ではOAuthを受けられないので、PCの`script/spotify_auth.py`(`picoos spotify-auth`)が書いた`spotify_auth.json`をアプリのフォルダに置く。
  起動時(か画面の「読み込む」)に`api.lua`が読み、`pico.store_save({auth=}, {encrypt=true})`へ移して平文のファイルを消す。
  アクセストークンは`grant_type=refresh_token`(`pico.form_encode`、client secretなし)で取り直し、返った新しいrefresh tokenを保存し直す。
  401なら1回だけ取り直して送り直す。`invalid_grant`ならログインの案内の画面へ戻る。
  **トークン(`spotify_auth.json`とトークンの取り直しの応答)は`pico.json_decode`で読む**: アクセストークンは300文字ほどあり、
  `pico.json_pick`(文字列は255バイトまで)で読むと切れて、取り直しても毎回401(`Missing/invalid/expired access token`)になった(実機で発生)。
  `pico_mock.lua`の`json_pick`も本物と同じく255バイトで切るようにし、`spotify_test`は300文字超のトークンで確かめる。
- **状態**: `GET /me/player?market=from_token&additional_types=episode`を3秒ごと(端末が無い204なら10秒、失敗は5秒から倍々で60秒まで)。
  `pico.json_pick`で22個のパスだけ読む。**応答が16KiBを超えたら(err=「書き込みに失敗」。番組の説明が長い回)次から`save_to`でSDへ保存して`pico.sd_json_pick`**。
  再生位置は取った時刻からの経過を足して見せ、再生中は1秒ごとに位置の欄だけ`mark_dirty`。
- **操作**: すぐ見た目を変えてから送り、0.7〜1.5秒後に取り直す(Spotify側の反映待ち)。前へは3秒より後なら頭へシーク。
  音量は続けて変えても0.4秒後に最後の値だけ送る。403(`PREMIUM_REQUIRED`)/404(端末が無い)は下の欄に理由を出す。
  端末は`GET /me/player/devices`→`pico.show_choice`→`PUT /me/player {device_ids, play=true}`。
- **ジャケット**: 曲の2番目の画像(横300px)を`save_to`で`art.jpg`へ取り、`pico.image_load(..., {max_w=80, max_h=80})`(1/4で75px)。
  画像のサーバ`i.scdn.co`は**DigiCert Global Root G3**の下なので焼き込みのルートに足した(10枚。上の「HTTPS」)。
- 通信は全部`keep_alive = true`(api/accountsはホストが違うので切り替えのたびに繋ぎ直しになる)。戻るときに`pico.http_close()`。
- 入力: キー(`playpause`/Space/k、`nexttrack`/→/n、`prevtrack`/←/p、`volumeup`/↑、`volumedown`/↓、`mute`/m、s、r、d=端末)、
  コントローラー(A=再生/停止、L/R・←→=前/次、↑↓=音量、X=シャッフル、Y=リピート、SELECT=端末、START=メニュー、B/HOME=戻る)。
- アプリのフォルダの`settings.cfg`の`api`/`token-url`で向き先を差し替えられる(偽物のサーバで試すとき)。
- 検証: `spotify_test`(run.sh の lua-scene。`pico_mock.lua`の上で、取り込み→取り直し→取得→ジャケット→各操作→Premium/端末なし/401/204/大きな応答/
  通信の失敗の間隔→端末の切り替え→invalid_grant→ログアウト→戻る)。`pico_mock.lua`に本物と同じ形のJSONの読み書き・`json_pick`・`form_encode`・
  `http_close`・`save_to`の偽物を足した。PCビルドでは偽物のSpotifyサーバ(使い捨て)を相手に、本物のLuaエンジンで表示・一時停止・次へ・音量・端末の選択を
  `--tap`/`--shot`(`PICOOS_VERIFY_LCD=1`で食い違い0)。**本物のSpotifyアカウント・実機で動くことを利用者が確認した(2026-10-14。長いトークンが切れる不具合を直した後)**。

## XMLの読み取りとニュースアプリ (`src/util/Xml_Reader` / `pico.xml_pick` / `pc/sdcard/lua/apps/ニュース/`) (2026-10-14)

SUMMARY.md未掲載。Google ニュースのRSS(APIキー不要)を読むために、`JsonReader`と同じ流儀でXMLの読み取りを足した。

- **`XmlReader`**(`util/Xml_Reader`、SAX型・確保なし・1バイトずつ食わせても同じ結果): `onOpen/onAttribute/onText/onClose`。文字は子要素の直前・要素の終わりで区切り、
  前後の空白を落とし、CDATAも同じ文字に混ぜる。実体は `&lt; &gt; &amp; &quot; &apos; &#N; &#xH;` だけ戻し、`&nbsp;` 等の未定義は書かれたまま。名前空間は解決しない
  (`media:content`のまま)。文字・属性値255B/名前31B/入れ子16段まで。DOCTYPEの内部サブセットは読み飛ばすが、その中のコメントに `[ ]` を書くと数え違える。
- **Lua**: `pico.xml_pick(text, paths)` / `pico.sd_xml_pick(path, paths)`(`LuaEngine_Net.cpp`。ドキュメントは`lua-api-doc/content/api/data/xml.md`)。パスはルートから要素名を `.` でつなぎ
  (`rss.channel.item.2.title`。同名の兄弟は番号を省くと1番目)、属性は `@名前`、同名の子の数は末尾に `#`(最後まで読む)。JSONの`json_pick`と同じく結果の値だけをLuaに作る。
  同じ要素を指す書き方(番号あり・なし)を両方頼むと両方に入る。数えられる子の名前の種類は親ごとに16まで。
- **ニュースアプリ**: RSSは数十〜百KBなので`http_request`の`save_to`で`news.xml`へ保存→`sd_xml_pick`で10件ずつ(1回32パスまで)題名・日時・媒体を拾う(最大30件)。
  日時はGMT→日本時間へ自前で換算。一覧はCanvasに直接描き(2行の題名+媒体・時刻)、ドラッグ/↑↓/十字でスクロール、タップ/Enter/Aで全文のダイアログ。
  分野(ヘッドライン・国内・国際…)と検索(`/rss/search?q=`)。記事のリンクはGoogleの中継URLで、本体にブラウザが無いため開けない(出していない)。
  設定の`settings.cfg`の`base=`で向き先を偽物のサーバへ差し替えられる。
- 検証: `xml_reader_test`(run.sh の apps)、`lua_ext_test`(xml_pick/sd_xml_pick)、PCビルドで偽のRSSサーバ(`base=http://127.0.0.1:8161`)を相手に一覧・詳細・キーでのスクロールを`--tap`/`--shot`
  (`PICOOS_VERIFY_LCD=1`で食い違い0)。**本物のGoogle ニュース(news.google.com のTLS・実機)では未確認**: 焼き込みのルートはGTS Root R1/R4があるので通る見込みだが、この環境からは繋がらない。
