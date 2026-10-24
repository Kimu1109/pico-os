# OSのサービス(アラーム・通知・ホーム画面・バックグラウンド・スクリーンショット他)

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## アラーム (`src/services/Alarm_Functions` / `ClocksScene`の「アラーム」タブ) (2026-09-30)

SUMMARY.md未掲載(小粒の機能のため、この節にだけ残す)。

- **鳴らす側はシーンではなくOS(`main.cpp`の`loop()`、`TimeFunctions::Update()`の後)が持つ**。
  タイマーは`ClocksScene`のメンバで、シーンを閉じると止まるが、アラームはアプリを閉じていても鳴る必要があるため。
  時計アプリの「アラーム」タブは4件の編集画面でしかない。
- 設定は`/sys/alarm.cfg`(無くてよい)。`alarm1 = 07:30,daily,on`(時刻 , `once|daily|weekdays|weekends` , `on|off`)、4件まで。
  書式違いの行は警告して読み飛ばす。`Set()`は1キーだけ差し替える(`SetValue()`)。
- **鳴らし方**: 確認ダイアログ(`MsgDialog`「止める」/「5分後」)+ビープ音(ch0、4拍のうち3拍)+毎フレーム`PowerFunctions::KeepAwake()`
  (スリープ中でも画面が起きる)。止められなければ`kRingMaxMs`(60秒)で自動停止。
  **ダイアログは`WidgetId`で持って毎フレーム`Resolve()`する**(シーン遷移で`dialog_roots`ごと破棄されうるので、
  生ポインタだとダングリングになる)。鳴っている間に消えたら出し直す。
- **NTP同期前(2020年より前)は鳴らさない**。「同じ分に2回鳴らさない」は日付込みの通算分(`last_minute_key`)で見る。
  **1回だけ(`once`)のアラームは鳴った時点でoffにして保存する**。同じ分に複数あっても鳴らすのは1つ。
- **UIの編集はSDへ即書きしない**(`Set(idx, a, persist=false)`)。▲▼の長押しは110msごとに値が変わり、
  SDへの書き込みは1回数十msかかるため、`ClocksScene`が最後の変更から600ms後(または画面を離れる/別のアラームを選ぶ/`onExit()`)に
  `Save()`する。時刻を編集したら自動でオンにする。DurationPickerは秒の桁も出るが、変えても0へ戻す(アラームは分単位)。
- **タイマーの完了音**: タイマーが`Finished`になると、アラームと同じビープ(`AlarmFunctions::PlayBeepStep()`。3拍鳴らして1拍休む)を
  `kRingMaxMs`(60秒)まで鳴らす。2026-10-11からタイマーはOS側(`Timer_Functions`)にあり、時計アプリを閉じても進んで鳴る
  (下の「BMP・予定の通知・…・画面ロック」)。
- **タブ名は半角カナ(`ﾀｲﾏｰ`/`ｽﾄｯﾌﾟ`/`ｱﾗｰﾑ`)**。4タブにすると1タブ約58pxで、全角4文字(64px)が収まらず崩れて折り返した。
- 検証: `alarm_test`(run.sh。時刻はUpdateAt()へ直接渡す)と、PCビルドの`--tap`/`--shot`(編集→保存、時刻を今の分にして起動すると鳴る)。
  **PCビルドの表示タイムゾーンは`network.cfg`の`timezone`(既定JST)なので、鳴らして試すときは`TZ=JST-9 date +%H:%M`で合わせること**
  (`date`だけだとUTCで1分もずれて鳴らない)。**実機では未確認**(I2Sでの音の聞こえ方・スリープからの復帰)。

## 通知 (`src/system/Notification_Functions` / `Notification_Sources.cpp` / `NotificationScene` / `widgets/systems/NotificationToast`) (2026-09-30)

SUMMARY.md未掲載。アプリを開いていなくても、時間・時刻・電池・Wi-Fiの条件で知らせる仕組み。**Luaアプリも`pico.notify`で使える**。

- **条件を見張るのはOS(`main.cpp`の`loop()`、アラームの直後)。アプリは予約するだけ。** `LuaScene`は閉じると`LuaEngine`ごと消えるので、
  Luaのコードで条件を判定することはできない(**任意のLuaで判定する条件は対象外**と決めた。必要になったら「数秒おきに小さな予算で1回だけ走る
  使い捨てのエンジン」を別に検討する)。
- **中身とつなぎを分けてある**: `Notification_Functions.cpp`は予約・履歴・条件の判定だけ(描画・Wi-Fi・電池・登録簿に依存しない。
  `UpdateAt(now_ms, tm, epoch, Sensors)`でホストテストする)。`Notification_Sources.cpp`が実際の時刻/電池/Wi-Fiの取り込み・トーストの出し入れ・
  通知音・`AppFunctions::LaunchByName`の差し込み・`OpenCenter()`を持つ(`Setup()`/`Update()`の定義もこちら)。
- 予約(`Rule`、`kMaxRules=16`、送り主ごとに`kMaxRulesPerOwner=4`): `Delay`(millis差分。再起動で消える)/ `At`(エポック秒。NTP同期前は待ち、
  過ぎていれば同期した時点で出す)/ `Daily`(HH:MM。アラームと同じく同じ分に2回出さない。**登録した分は見送る**)/ `Every`(10秒以上。遅れても溜めて何回も出さない)/
  `BatteryLow`(下回ったら1回、+5%戻るまで次は出さない)/ `WifiConnected`・`WifiDisconnected`(変わった瞬間だけ。登録時の状態では出さない)。
  **送り主(`owner`)はLuaアプリのディレクトリ**(`LuaEngine::app_dir_`。C++からは空)。同じ送り主・同じ`tag`は置き換え。他の送り主の予約はLuaから消せない。
- 保存: `Delay`以外を`/sys/notify_rules.tsv`(1行1件のタブ区切り。値が`Config_Functions`の上限160Bを超えるので自前の書式)へ、
  変更から`kSaveDelayMs`(1秒)後にまとめて一時ファイル→差し替えで書く。文字列は`Sanitize()`でタブ/改行を空白にしてから持つ。
  起動時に`RefreshOwners()`で登録簿(`AppFunctions::NameForDir()`=argの親ディレクトリで引く)と突き合わせ、**送り主が消えた(アンインストールした)予約は捨てる**。
  そのため`NotificationFunctions::Setup()`は`AppFunctions::Setup()`より後、かつトーストを最前面に置くためキーボードより後。
- 履歴(`kMaxHistory=8`の輪。2026-10-10に16から減らした)はRAMだけ(再起動で消える)。見せ方は`/sys/notify.cfg`の`mode = on|quiet`と`sound = true|false`(通知センターの[通常/控えめ][音あり/なし])。
  quietはトーストも音も出さず、ステータスバーの印(右端の赤いベル+未読数)と通知センターだけ。**`Scene::quietNotifications()`がtrueの画面(ゲームボーイ)の間も同じ扱い**。
  **`Scene::keepForeground()`がtrueの画面(SSH・ゲームボーイ。離れると接続が切れる/ROMを閉じる)では、ステータスバーやトーストをタップしても別の画面へ移らない**
  (トーストは既読にして閉じるだけ)。うっかり触って接続やゲームを失わないため。
- **トースト**(`NotificationToast`)はオーバーレイの一番上(キーボードや半透明のダイアログの上にも出る)、約5秒(後ろに待ちがあれば2.5秒)。
  本体タップで送ったアプリを`Open()`(起動できなければ通知センター)、右端の×で既読にして閉じる。見せている間は`PowerFunctions::KeepAwake()`(スリープ中なら起きる)。
  **消すときは新設の`PICO_GFX::MarkDirtyBelow()`**: 普通の`MarkDirty()`だと`FlushDirty()`が「TRANSLUCENTの下は描き直さない」近道を使うため、
  半透明のダイアログ/キーボードの上に出していたトーストの跡が残る。`MarkDirtyBelow()`の矩形だけその近道を使わず一番下から描き直す
  (ヘッダの`inline`なので`MarkDirty()`を偽物にしたホストテストでもそのまま使える)。PCビルドでキーボードのダイアログの上で跡が残らないことを確認した。
- 通知音は最後のチャンネル(`SoundFunctions::kChannels-1`)を借りて2音(E6→A6)。同じフレームに何件出ても1回。
- **起動理由**: `Open()`が送り主・tag・dataを覚えてアプリを起動し、`LuaScene::onEnter()`が`TakeLaunchReason(正規化したapp_dir)`で受け取って
  `LuaEngine::SetLaunchReason()`へ渡す(`pico.launch_reason()`)。1回きりで、`kLaunchReasonTtlMs`(3秒)を過ぎたら捨てる(起動に失敗した理由が後の別の起動に混ざらないように)。
- **通知センター**(`NotificationScene`、ランチャの「通知」、ステータスバーのタップでも開く=`main.cpp`が`status->setOnPressEnd()`): [履歴|予約]のタブ、
  1回目のタップで下に詳しく、2回目で開く/取り消す(予約はどのアプリのものでも取り消せる)。`Revision()`が変わったら一覧を作り直す。画面を離れるときに全部既読にする。
- Lua: `pico.notify{title=, body=, tag=, data=, sound=, delay_ms|at|daily|every_ms|when(+below)}` → id(すぐ出したら0)/ `nil, 理由`(権限無し・上限)、
  `pico.notify_cancel([id|tag])` → 件数、`pico.notify_list()`、`pico.launch_reason()`。**権限`LuaPermissions::notify`(app.cfgの`permission_notify`)が要る**
  (アプリを閉じた後にも画面と音へ出るため既定では許さない)。引数の誤りは権限より先に`luaL_error`。ドキュメントは`lua-api-doc/content/api/system/notify.md`。
- 動作確認は「動作確認」アプリの「通知」のページ(`pc/sdcard/lua/apps/動作確認/notify.lua`、`permission_notify=true`)。
- RAMは静的に約9KB(予約16件×約380B + 履歴8件×約360B)。
- 検証: `notification_test`(run.sh。種類ごとの発火・置き換え/上限・履歴の輪・控えめ・保存と読み込み・送り主の掃除・起動理由)、`lua_engine_test`(Lua API)、
  PCビルドの`--tap`/`--shot`(アプリで10秒後を予約→閉じてランチャでトースト→タップでアプリが`launch_reason`付きで開く、ステータスバーから通知センター、
  再起動しても`daily`/`every`の予約が残る、キーボードのダイアログの上でトーストの跡が残らない)。**実機・Webビルドは未確認**。
- 未: カレンダーの予定を閉じている間に取り直すこと(チャットの未読は下の「バックグラウンド実行の枠組み」の`ChatWatch`で入った)、
  アラーム/タイマーの完了を通知へ統合すること。

## ホーム画面のウィジェット (`src/apps/home/` / `AppGrid` / `HomeScene` / `apps/todo/Todo_Cache` / `apps/todo/Todo_Watch`) (2026-10-16)

ランチャのアプリのタイルと**同じ並びに**ウィジェット(時計・アナログ時計・音楽・予定・TODO)を混ぜて置く。方針は利用者と相談して決めた(2026-10-16):
グリッドに混ぜる(専用ページ・上部の帯ではなく)/ 最初の種類は時計・音楽・予定・TODO / 今回はC++だけ(Luaアプリのウィジェットは後で)/ 中のボタンも押せる(ボタンの外は対応するアプリを開く)。

- **並び(`HomeLayout`、`Home_Layout.hpp/.cpp`。描画にもSDの中身にも依存しない)**: アプリの並び・隠した印は今までどおり`AppFunctions`(launcher.cfg)。
  ウィジェットは**「見えているアプリの何番目の前に置くか」(anchor。フォルダを入れてからはトップの項目=アプリとフォルダの何番目。gui.md「ランチャのフォルダ」)**だけを`/sys/home.cfg`(`widgetN = 種類,幅x高さ,anchor`、Nの順)に持つ。8個まで(`kMaxWidgets`)。
  `Sequence()`がアプリとウィジェットを1本の並びにし、`Compute(rows)`が2列のマス目へ詰める:
  **並びの順に、埋まりきっていない一番前のページの、空いている一番前のマスへ**(大きなウィジェットが次の行/ページへ送られて空いた所を後ろの小さな項目が埋める。
  最初は「前の項目の後ろからだけ」詰めていたが、2x2が入らないと半ページ空いたので変えた)。そのぶん並びの順と見た目の順は少し入れ替わるので、
  `Move(type, index, ±1, rows)`は**見た目が動くまで続けて進める**。隣どうしがアプリならアプリの並び(`AppFunctions::MoveApp`)を入れ替え、
  そうでなければ並びを入れ替えて anchor を付け直す(`Rebuild()`。placed[] は並びの順に並べ直すので**ウィジェットの番号は動かすと変わる**。`new_index`で受け取る)。
  アプリを隠すときは`HideApp()`(後ろのウィジェットの位置がずれないよう並びから抜いて付け直す)。大きさは種類ごとに決まった組(`KindInfo::sizes`: 1x1/2x1/2x2)だけ。
- **描き方(`HomeWidget`、`Home_Widget.hpp`)**: 種類ごとに1つの静的なオブジェクト(`Home_Widgets.cpp`、`HomeWidgets::Setup()`が`HomeLayout::SetImpl()`して`Load()`。
  `main.cpp`で`AppFunctions::Setup()`の直後)。`stamp()`(見た目が変わると変わる値)・`draw(r, w, h, pressed_button)`・`buttonAt()`/`press()`・`appName()`・
  `onHomeEnter()`(ランチャを開いたとき。SDを読む等)・`tick()`(ランチャを開いている間毎フレーム)。**差していなければ`AppGrid`は枠と名前だけ描く**(ホストテストは偽物を差す)。
- **`AppGrid`**: 並べた結果(`items_`、並び・隠した印・登録簿・ウィジェット・行数の署名が変わったときだけ`Compute()`し直す)で描き・当たり判定・フォーカスを行う。
  `tileRect(slot)`/`hitTile()`(アプリだけ)/`tilesPerPage()`はアプリだけのときの今までの形のまま。ウィジェットは角の丸い枠の中を`ScopedClip`して`draw()`。
  **今のページにウィジェットがあれば`wantsFrameUpdate()`が真**で、合成の外の回に各ウィジェットの`stamp()`を見て、変わったものの矩形だけを`MarkDirty()`する。
  合成ではクリップ(dirty矩形)にかからない項目は描かない。押したとき`buttonAt()`があればそのボタン(離したときに同じボタンの上なら`press()`)、無ければ本体
  (離したとき枠の中なら`appName()`のアプリを開く)。長押しは`takeLongPress(now, ms, LongPress&)`(項目か空いた所か)。矢印は向きにある一番近い項目へ(端でページ送り)。
- **`HomeScene`**: ページ送りは常に作って1ページなら隠す(ウィジェットを足すとページが増えるので。グリッドの高さは常にページ送りの分を引く。1ページの行数は3のまま)。
  長押し(コントローラーのX/Y・キーの`m`)のメニュー: アプリ=先頭へ/前へ/後ろへ/隠す/ウィジェット追加、ウィジェット=移動/大きさ/削除/ウィジェット追加、
  空いた所=追加のメニュー(並びの最後へ)。追加は押した項目の前に入る。メニューの字は24pxで約8文字しか入らないので短く書く。
- **時計**: 1x1=時刻(32px)+日付、2x1=時刻(48px)+日付・曜日・次のアラーム(7日以内、ベルの印+「7:30」「明 7:30」「金 7:30」)。アナログ: 1x1/2x2(三角形の針。`drawWideLine`は使えない)。
- **音楽**: 2x1/2x2。`MusicService`の曲名・プレイリスト・n/m曲目・進み具合と 前/再生・一時停止/次(`MusicService::Prev/TogglePlay/Next`)。何も選んでいなければボタンを出さず、押すとミュージックアプリ。
  秒が変わるたびに`stamp()`が変わる(鳴っている間だけ)。
- **予定(`HomeAgenda`)**: `/calendar/*.ics`(名前順、色はカレンダーアプリと同じ)を、今日から7日の窓で`IcalCalendar`(約16KB、**読む間だけ`new`**)へ読み、
  見せる分だけ(8件、1件約70B)を静的に持つ。今日の終わった時刻つきの予定・明日以降の「前日からの続き」は入れない。読み直すのは
  日付が変わったとき(`tick()`)・ランチャを開いたとき`Invalidate()`されていたら(カレンダーアプリの`onExit()`が呼ぶ)。**.icsの取得はカレンダーアプリの仕事のまま**。
  2x1は今日と明日だけ。
- **TODO(`TodoCache` / `TodoWatch`)**: 一覧を取ったとき(TODOアプリの`syncReminders()`の隣、バックグラウンドの取り直し)に、今日まで(期限が今日か過ぎた)のタスクを6件+総数だけ
  覚えて`/sys/todo_cache.tsv`へ書く(再起動しても前回の一覧を出す)。**`TodoWatch`(バックグラウンドの仕事「TODOの取り直し」、`background.cfg`の`todo`、既定で入)は
  ホーム画面にTODOウィジェットを置いている間だけ**、15分ごとに`TodoistClient`(約20KB+TLS約40KB、取る間だけ`new`)で「今日」を1回取る。
  todoist.cfgが無ければ何もしない(ログも出さない)。TODOアプリが前に出ている間は画面に任せる。過ぎた期限は日付を赤で出す。
- RAM: 静的に約1.6KB(並び8件・予定8件・TODO6件・`AppGrid`の並べた結果と印 約480B)。
- 検証: `home_test`(run.sh の core。並び・詰め方(重ならない・空きを埋める・入らない2x2は次のページ)・動かす(アプリ同士/ウィジェットとアプリ/見た目が動くまで)・
  先頭へ・隠す・上限・home.cfgの往復と読めない行・AppGridのウィジェットの当たり判定/ボタン/本体でアプリ/長押し/キー/印が変わった矩形だけ描き直す・
  TodoCacheの絞り込みと往復・HomeAgendaの拾い方と色)、`app_test`(アプリだけのときの今までの形)、PCビルドの`--tap`/`--shot`(`PICOOS_VERIFY_LCD=1`で食い違い0):
  home.cfgを置いて各種類の表示、音楽ウィジェット→ミュージックアプリ→曲を選んで戻ると曲名と進み具合、ウィジェットの一時停止でアプリが開かない、長押しのメニュー→追加。
  **実機では未確認**(予定の読み直しの時間、TODOの取り直しのヒープ)。
- 未: Luaアプリのウィジェット(app.cfgに`widget = widget.lua`を書き、使い捨てのエンジンで画像へ描いてホームはそれを貼る、を考えている)、天気等の新しい種類、
  ウィジェットごとの設定(どのカレンダーか等)、予定・TODOのウィジェットから項目を直接開く。

## バックグラウンド実行の枠組み (`src/system/Background_Functions` / `Background_List.cpp` / `BackgroundScene`) (2026-10-15)

画面(シーン)を閉じても動き続ける仕事を登録しておくと、`main.cpp`の`loop()`(`NotificationFunctions::Update()`の後)が毎フレーム
`BackgroundFunctions::Update()`→各仕事の`update(now)`を呼ぶ。方針は利用者と相談して決めた(2026-10-15): C++のサービスの登録簿 + その上に
Luaのバックグラウンドスクリプトを「定期の使い捨て」で載せる / 最初の利用者はミュージックの再生とチャットの未読 / 設定アプリの一覧とステータスバーの印。

- **`BackgroundService`**(抽象クラス): `key()`(`/sys/background.cfg`のキーを兼ねる)・`name()`・`icon()`・`update()`・`active()`・`status()`、
  形は2つ: **今だけの仕事**(`toggleable()`=false。動いている間だけ`active()`、一覧の[停止]で`stop()`。ミュージック)と
  **入り切りする定期の仕事**(`toggleable()`=true。`setEnabled()`、[今すぐ]=`runNow()`。チャットの未読・Luaのスクリプト)。
  任意で`appName()`(一覧の[開く])・`preferredSampleRate()`。
- **登録簿**(`Background_Functions.cpp`、描画にも音にも依存しない): 固定長16件。`Add()`のとき`background.cfg`(`<key> = on|off`)の入り切りを当てる。
  `SetEnabled()`が書く。`IndicatorIcon()`=今だけの仕事が動いていればその絵(音符)、定期の仕事だけなら`IconID::Refresh`、無ければ`IconCount`。
  `PreferredSampleRate()`は動いている仕事の望む周波数の最大。**`main.cpp`は画面が何も望まない(0)ときにこれを`RequestSampleRate()`へ渡す**
  (閉じたミュージックの曲が、ランチャへ戻った途端の周波数の切り替えで止まらないように)。画面がはっきり別の周波数を望めば画面が勝ち、曲は止まる。
- **登録**は`Background_List.cpp`の`BackgroundFunctions::Setup()`(`App_List.cpp`と同じ役割。`AppFunctions::Setup()`と`NotificationFunctions::Setup()`の後)。
  仕事を足すときは`BackgroundService`を継承してここで`Add()`するだけ。
- **一覧**(`BackgroundScene`): 設定アプリ「他」タブの3行目[一覧]から。[停止]/[切る]/[入れる]・[今すぐ]・[開く]。0.5秒ごとに状態を見直す。
  「他」タブは行が足りなくなったので、開発者向けの見出しの下を詰めた行(`kDevRowH`=26)にし、起動時の自己診断をそこへ移した。
- **ステータスバーの印**: 何か動いていれば、コントローラーの印の右に`IndicatorIcon()`を青で。

**ミュージック(`src/apps/music/Music_Service`)**: 再生の順番・曲の終わりで次へ・読めない曲を飛ばす・リピート/ミックスを`MusicScene`から移した
(画面は`MusicService`を読んで見せ、ボタンを渡すだけになった)。アプリを閉じても鳴り続け、終われば次の曲へ進む。
- 状態(順番の曲64件・フォルダ名16・`PlayQueue`、約4.2KB)は最初に鳴らすときに`new`、一覧の[停止](`Release()`)で返す。ミックス/リピートは状態の外(`music.cfg`)。
- **他のアプリに音を取られたら止まった扱い**: `SoundFunctions::PlaybackSerial()`(曲/WAVを新しく鳴らす・周波数の切り替えで1つ増える。新設)を鳴らした直後に覚え、
  違っていたら`Interrupted()`(次の曲へは進まない。相手の音も止めない)。再生ボタンで続きから鳴らし直せる。
- 鳴っている間は44100Hzを望む。アプリを開き直すと、鳴っているプレイリストの画面から始まる(名前と番号が一致すれば)。
- **周波数の切り替えで音が止まる罠(2026-10-19に直した)**: ①画面が周波数を望まない所(ホーム画面のウィジェット)から鳴らすと、次のフレームで
  main.cppがバックグラウンドの望み(44100Hz)へ切り替え、その切り替えで今鳴らした曲が止まって「取られた」扱いになっていた →
  `TryPlay()`は要求が無ければ(`SoundFunctions::RequestedSampleRate()==0`)鳴らす前に44100Hzを要求する。②取られた途端に周波数の望みを手放すと、
  22050Hzへの切り替えで取った側の音(動画アプリのWAV)まで止まっていた → 取られた後も相手の曲/WAVが鳴っている間は`holdsSampleRate()`を保つ。
  `background_test`がmain.cppと同じ要求の渡し方で確かめる。

**チャットの未読(`src/apps/chat/Chat_Watch`)**: チャットアプリを閉じている間、`chat.cfg`の`notify-poll-sec`(既定120秒、30以上)ごとに`GET /api/v1/rooms`
(取るだけでは既読にならない)を取り、新しい発言で未読が増えた部屋を通知する(タイトル=部屋名、タップでチャットアプリ)。
- 部屋ごとに「最後に知らせた発言id」をRAMで覚える(16部屋)。再起動後の最初の回は、未読のある部屋を全部知らせる。
- **接続は使い回さない**。要求の道具(HttpRequest+行バッファ+部屋16件、約4KB)とTLSの約40KBは取りに行く数秒の間だけ。
  Wi-Fiに繋がっていない・時計が合っていない間は10秒後に見直す。チャットアプリが前に出ている間は取りに行かない(`foreground_query`)。
- 既定で入っている(`chat.cfg`が無ければ「チャットが設定されていません」で何もしない)。

**Luaのバックグラウンドスクリプト(`src/lua/LuaBackground`)**: app.cfg の`background = background.lua`/`background_interval = 秒`(既定900、10以上)/
**`permission_background = true`**(新しい権限。無ければ動かさない。アプリストアの`perms`は`background`)。
- **定期の使い捨て**: 間隔ごとに予算48KB(64bitは×4/3)の新しい`LuaEngine`(`background=true`)を作ってスクリプトを上から1回動かし、
  `pico.http_request`の応答待ち・`pico.after/every`が残っていれば毎フレーム進め、無くなった/`pico.finish()`/30秒(`kMaxRunMs`)で捨てる。
  画面のLuaアプリ(200KB)と同時にフルのエンジンを2つ持てないため常駐させない。回をまたぐ値は`store_save`等で持ち越す。スクリプトは16KiBまで(読む間だけ`malloc`)。
- **`LuaEngine`の`background`モード**: `pico.*`を`kBackgroundApi`(通知・通信・SD・保存・JSON/XML・タイマー・暗号・`log`等)だけに絞る(`filterBackgroundApi()`。
  `pico.iso`も作らない)。エラーはダイアログを出さず、ログ・`/crash/lua_NNNN.txt`・`lastError()`(一覧の「失敗: …」)へ。
  新しいAPI: `pico.is_background()`・`pico.finish()`(画面では何もせずfalse)。
- 同時に1本。起動から30秒後+5秒ずつずらして始める。そのアプリが前に出ている間(`foreground_script`)・SDが無い間・ヒープの残りが
  予算+`LuaEngine::kHeapReserveBytes`を割る間は見送る/延ばす。
- 仕事の足し引きは`Refresh()`(登録簿のLuaアプリのapp.cfgを読み直す。スクリプトがアプリのフォルダの外を指していれば断る)。
  `LuaAppScanner::apps_changed`(関数ポインタ。`Background_List.cpp`が差す)経由で、アプリストアの入れ替え・USBドライブの後にも呼ばれる。
- ドキュメントは`lua-api-doc/content/api/system/background.md`。

- 検証: `background_test`(run.sh の lua-ext。登録簿と`background.cfg`、`MusicService`を本物の`SoundFunctions`で: 画面無しで次の曲へ・読めない曲を飛ばす・
  最後で止まる・選んだ曲が読めなければ止まる・一時停止・他のアプリに取られる・周波数の切り替え・リピート全曲・[停止]、`ChatWatch::OnRooms`と設定が無いとき、
  `LuaBackground`: 登録・`pico.*`の絞り込み・通知・タイマー待ち・`pico.finish`・時間切れ・エラーでダイアログを出さない・前に出ているアプリは見送る・
  入り切りの保存・権限/フォルダの外・アプリが無くなったら外す)、`app_repo_test`(`background`の権限)、PCビルドの`--tap`/`--shot`。
  **実機では未確認**(使い捨てのエンジンを作る時間・ヒープ・スリープ中の通信。チャットの未読を本物のサーバで通しで見ること)。
- 未: スリープ中に画面を消したまま音楽を鳴らし続けること(今は鳴っている間は自動ではスリープに入らない。電源メニューのスリープなら鳴り続ける)、
  カレンダーの取り直し・Todoistをバックグラウンドへ載せること。

## スクリーンショット (`src/services/Screenshot_Functions` / ステータスバー右端のカメラ) (2026-10-02)

SUMMARY.md未掲載(小粒の機能のため、この節にだけ残す)。

- ステータスバーの**右端のカメラのアイコン(右端24px)をタップすると撮る**。それ以外の場所のタップは従来どおり通知センター(`main.cpp`の`status->setOnPressEnd()`が`OSData::touchX`で振り分ける)。
  カメラは自作アイコン(`custom_icons/camera.svg`、`IconID::Camera`は末尾)。未読のベルはカメラの左へ寄せた。
- `OSData::frame`(合成済みの全画面、4bppパレット)を**4bppのBMP**(`Bmp::Encode4bpp()`。今のパレット16色付き、約38KB)で`/screenshots/shot_0001.bmp`,`0002`…へ書く
  (2026-10-11に.pimgから変えた。PCの画像ビューアでそのまま開ける)。
  本体はファイルアプリ→ファイルビューワーで見られる。書き込みに失敗したら半端なファイルは消す。タップのフレームでは直前に合成した絵が写る(ステータスバーも入る)。
- 結果は通知(`NotificationFunctions::Post()`、音なし)で出す(控えめモードならトーストは出ず通知センターだけ)。成功時は通知の`app`へ`"file:<パス>"`を入れ、**通知をタップするとファイルビューワーで開く**(`Notification_Sources.cpp`の`Launch()`が`file:`接頭辞をアプリ名ではなくファイルとして扱う)。
- 検証: PCビルドの`--tap 230,8@30:5`で保存・内容を確認。ホストテストは無し。**実機では未確認**(1フレーム76800回の`readPixelValue`+SD書き込みの時間)。

## BMP・予定の通知・ファイル検索/容量・タイマー常駐・ダークテーマ・画面ロック (2026-10-11)

まとめて足した小さめの機能。SUMMARY.mdの「7. 標準アプリ開発」に項目だけ載せてある。

**JPEG(`src/gui/icons/Jpeg_Codec`、2026-10-14)**: Luaの`pico.image_load`が先頭の`FF D8`で見分ける。解くのはLovyanGFX同梱のTJpgDec
(`lgfx_jd_prepare/decomp`、`#include <lgfx/utility/lgfx_tjpgd.h>`。新しいライブラリは足していない)で、作業場所約3.9KBを解く間だけ`malloc`。
ベースラインだけ(プログレッシブは断る)。1/2・1/4・1/8に縮めて解ける(`scale`、無ければ`max_w/max_h`(既定は画面)と画像用メモリの残りに収まる一番大きいもの)。
16色へはBMPと同じ`Bmp::NearestIndex`+4x4の組織的ディザ。**ホストテストはLovyanGFXを持たないので`script/host_test/stubs/lgfx/utility/`に
TJpgDecの写しを置き、`script/host_test/CMakeLists.txt`が`host_tjpgd`にする(LovyanGFXの版を上げたら写し直す)**。スタブの`TFT_RED`等は色の番号そのもの
(RGB565ではない)なので、近い色の選び方を確かめるテストは`PICO_GFX::COLORS`を本物の色へ差し替えて行う(`lua_ext_test`)。
ファイルビューワー(`ImageView`)はまだJPEGを開けない。

**BMP(`src/gui/icons/Bmp_Codec`)**: 読みは無圧縮(BI_RGB)と BI_BITFIELDS の 1/4/8/16/24/32bpp・上下どちらの並び・窓だけ読む(大きな画像を表示欄の大きさだけ)。
RLE/JPEG/PNG埋め込みと12バイトの古い見出し(BITMAPCOREHEADER)は断る。一辺8192まで。作業は1行ぶんの`malloc`とパレット(256色x3、ヒープ)だけ。
出し方は3通り: `DecodeTo4bpp()`(OSのパレットへ寄せる。パレット付きは色の対応表を1回だけ作る、フルカラーは1画素ごとに近い色+任意で4x4の組織的ディザ、
`raw_indices`なら4bpp以下の番号をそのまま=OS向けに16色で描いた絵)/`DecodeRgb565()`(フルカラーのまま1行ずつ)/`DecodeToSprite16()`。
近い色は今の`PICO_GFX::COLORS`で探すので、ダークテーマでも見た目の色に寄る。ファイルビューワー(`ImageView`は先頭の`BM`で見分ける。フルカラーはディザ)と
Luaの`pico.image_load(path, {dither=, raw=})`が使う。**フルカラーのまま画面へ出す口は無い**(frameが4bppのため。`DecodeRgb565()`は液晶へ直接描く用途向けに残した)。
検証: `bmp_test`(core)、PCビルドでPILで書いた24bppのBMPを表示、スクリーンショットをPILで開いて画素を比較。

**カレンダーの予定の通知(`src/apps/calendar/Calendar_Reminders`)**: Todoistのリマインダーと同じ作り。カレンダーアプリが.icsを読み直すたび(読み込んだ6週間が今日を含むときだけ)、
時刻つきの予定の回を近い順に4件まで`At`の予約にする(終日は入れない)。**送り主は`"@calendar"`**: `@`で始まる送り主はOSの機能のもので、
`NotificationFunctions::RefreshOwners()`が登録簿と突き合わせて消さない(送り主ごとに4件の枠がTodoの空の送り主と別になる)。tagは`cal:<UIDのハッシュ>:<日>`。
読み込んだ範囲の中で消えた/動いた予定の予約は消し、範囲の外は残す。`/sys/calendar.cfg`の`reminders`(既定true)・`remind-before-min`(既定10)。
**カレンダーアプリを開かない限り予約は更新されない**(.icsの取得もアプリの中だけのため)。検証: `calendar_scene_test`。

**ファイル検索・容量(`storage/Sd_Walker` / `FileSearchScene` / `StorageScene`)**: ファイルアプリの上の段の「他」(名前の変更もここへ移した。「名前」のボタンで溢れたため)。
(2026-10-24: 中身で探す全文検索と「最近開いたファイル」を足した。`apps-local.md`「メモ・全文検索・最近開いたファイル」)
`SdWalker`はフォルダの下を1件ずつ深さ優先でたどる(深さ8まで、FsFileを段の数だけ持つ、確保なし)。検索はSD全体を名前の部分一致(ASCIIの大小を区別しない)で、
1フレーム6msずつ・32件まで。ファイルはビューワー、フォルダは`FileSearchScene::TakeJump()`でファイルアプリへ戻ってそのフォルダを開く
(ファイルアプリは戻ってきたとき前のフォルダを開き直すようにした)。容量は全体=`SD.clusterCount()×bytesPerCluster()`、
使用量=SD全体を`SdWalker`でたどり「ファイルの大きさをクラスタへ切り上げた合計+フォルダ1つにつき1クラスタ」(フォルダの中身を数え終わった後、1フレーム6msずつ。
60秒覚える)、空き=全体-使用量の見積もり(「空き約」と出す)と、今のフォルダの直下を大きい順(フォルダは中身の合計、48件まで)。
**`freeClusterCount()`は使わない**: FAT32ではFATを全部読むので、実機で同期に呼んだらウォッチドッグ(8秒)で再起動した(2026-10-11)。
FATの空きを数えたものではないので、深すぎて(8段)中を見なかったフォルダ・壊れたチェーンの分は合わない。
PCの互換層(`pc/compat/SdFat.h`)は`statvfs`、ホストテストのスタブは固定値。**スタブの`HostSd::dirs_enabled`**を立てるとファイルのパスからフォルダを作って
`open()`/`openNext()`で列挙できる(既定は今までどおりフォルダ無し)。検証: `file_search_test`(apps)、PCビルドで検索・容量を`--tap`で確認。

**タイマー/ストップウォッチの常駐(`src/services/Timer_Functions`)**: 状態を`ClocksScene`のメンバからOS側へ移し、`main.cpp`の`loop()`(アラームの直後)で進める。
タイマーが0になると`AlarmFunctions::PlayBeepStep()`を60秒・`KeepAwake()`、**時計アプリでタイマーを見ていなければ**「止める」の`MsgDialog`(`WidgetId`で持ち、消えたら出し直す)。
ストップウォッチは**ラップ**(30件、溢れたら古い方から。通し番号は数え続ける)。動いている間は「リセット」が「ラップ」になる。時計アプリはランチャから開くと、
動いているタイマー/ストップウォッチのタブから始まる。検証: `alarm_test`にタイマー/ストップウォッチ、PCビルドでラップ→ランチャへ戻る→開き直して続いていること。

**ダークテーマ(`PICO_GFX::SetTheme()` / `DARK_COLORS`)**: パレットの番号の意味はそのままに、明暗を入れ替えた色を当てる(0=文字が明るく・15=背景が暗く・7/8の灰と11の水色は暗い側/明るい側へ)。
`ResetPalette()`の戻り先を`THEME_COLORS`にしたので、Luaアプリの`pico.set_palette`もアプリを閉じるとテーマの色へ戻る。`/sys/display.cfg`の`theme = light|dark`、
設定アプリ「画面」タブの3行目、Luaの`pico.theme()`。**画像・アイコンも番号で描くので一緒に反転する**(写真のBMPは今のパレットで近い色を選ぶので見た目は保たれる)。
ゲームのように自分の色を持つLuaアプリは`set_palette`で入れ直すこと。

**画面ロック(`src/services/Lock_Functions` / `widgets/systems/LockScreen`)**: PINは数字4〜8桁、`/sys/lock.cfg`の`pin = v1:<塩32桁>:<BLAKE2b-256 64桁>`
(鍵はファームの固定鍵`PICO_Secret::kKey`、塩は`SecretAead::Random()`)。**起動時とスリープに入ったとき(=起きたらロック画面)**にロックする。
ロック画面は**全オーバーレイの一番上**(`LockFunctions::Setup()`を最後に呼ぶ)の全画面OPAQUEで、`hitTest()`が常に真なのでタッチを全部取る(ステータスバーの長押し・通知センターも開かない)。
打鍵は`KeyInputFunctions::intercept`(関数ポインタ。配り先をロックへ依存させないため)で全部ロック画面へ、コントローラーは`PadFunctions::SuppressThisFrame()`
(`LockFunctions::Update()`が`PowerFunctions::Update()`の直後に呼ぶ。自動調光/スリープの判定はその前に済んでいるのでボタンで起きられる)。後ろの画面は動き続ける。
5回続けて間違えると30秒、以後間違えるたびに倍(最大5分)待たせる(RAMだけ。再起動しても起動時にロックするので抜け道にはならない)。
アラーム/タイマーが鳴っていればロック画面に「止める」(`ringing_query/ringing_stop`を`main.cpp`が差す)。PINの決める/変える/外すもロック画面で聞く
(設定アプリ「画面」タブの5行目、電源メニューに「ロック」)。**守れるのは画面の操作だけ**: SDを抜けば中身は読め、4桁のPINはハッシュから総当たりで分かる。
検証: `lock_test`(input。待ち時間が解除後も残るバグをこれで見つけた)、PCビルドでPINを決める→再起動でロック→違うPIN→正しいPIN。**実機では未確認**。
