# 暗号化・端末の鍵・Wi-Fi

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## Wi-Fi認証情報の暗号化保存 (`src/crypto/Secret_Cipher.hpp`) (2026-09-27)

SDカードだけを紛失/盗難された場合に、`/sys/network.cfg`の`wifi-ssid`/`wifi-password`を
テキストエディタでそのまま読まれないようにする対策。相談の発端は「フラッシュに鍵を焼けば
SD紛失時も安全では」という提案で、そこから以下の設計に落とした。

- **守れるのは「SDカードだけの紛失/盗難」のケースだけ**。本体(基板)ごと持ち去られた場合は
  無力(鍵はソースにハードコードされ実機のフラッシュに焼かれるだけなので、SWD等でフラッシュを
  吸い出せば鍵も一緒に読める)。さらに**鍵は既定値のままリポジトリに公開されている**ため、
  本気の攻撃者(ファームウェアの吸い出しを厭わない相手)には無力で、「肩越しに見られた・
  SDだけ他人に渡した」程度の偶発的な漏洩を防ぐのが目的と割り切った。本格的に守るなら
  RP2350のOTP/Secure Bootでデバイス固有鍵を焼く必要があるが、実機ビルド・検証手段が
  このリモート環境に無く実装コストも高いため見送り、この簡易版(相談時の案で言う「a」)を採用した。
  **forkして実運用する場合は`PICO_Secret::kKey`を必ず書き換えること**(コメントに明記)。
- **方式はXTEA(128bit固定鍵)をCTRモードのストリーム暗号として使うだけ**。ブロック暗号の
  出力(暗号文ではなく鍵ストリーム)を平文とXORするので、暗号化と復号が同じ関数
  (`XorStream()`)で済む。TLS(BearSSL/OpenSSL)のような重い依存を持ち込まず、PC/実機で
  同じコードがそのまま動く(他のsrc/コード全般と同じ方針)。
- **「用途文字列(purpose)」を鍵ストリームの種に混ぜる**(`Fnv1aHash(purpose)`を初期カウンタにする)。
  SSIDとパスワードで別々のストリームになるので、同じ鍵ストリームを使い回して2つの暗号文を
  XORすると平文同士のXORが漏れる、という典型的な弱点を避けられる。
- **保存形式は`"enc1:"`接頭辞+16進文字列**。接頭辞が無い値は「まだ暗号化されていない平文」と
  みなしてそのまま返す(後方互換)。既存の`network.cfg`(`pc/sdcard/sys/network.cfg`のサンプルも
  含め平文のまま)は変更不要で、次に`SettingsScene`から保存し直したときに暗号化形式へ移行する。
- **`PICO_Config::kConfigMaxValueLen`を128→160、`kConfigMaxLineLen`を192→256へ拡張した**
  (`Config_Functions.hpp`)。64バイトのWi-Fiパスフレーズ(64桁PSKの16進表記を想定した上限)を
  暗号化すると`"enc1:"`(5)+16進128文字+終端=134文字になり、旧上限の128に収まらないため。
  他の設定キー(NTPサーバー名等)には影響しない(バッファが大きくなるだけ)。
- **暗号化・復号の呼び出し元は2箇所**: `NetworkFunctions::Setup()`(起動時、読み込んだ値を
  復号してから使う)と`SettingsScene`(`loadValues()`でSSID表示用に復号、`commitEdit()`で
  保存前に暗号化)。`SD_Functions`/`Config_Functions`自体には手を入れていない
  (暗号化はWi-Fi認証情報という特定の値の扱いであり、汎用のconfigパーサの仕事ではないため)。
- ホストテストは`script/host_test/secret_cipher_test.cpp`(往復・用途違いで暗号文が変わること・
  取り違えた用途では正しく復号できないこと・空文字列の扱い・後方互換・壊れたデータやバッファ
  不足への安全な失敗・上限ちょうどの長さの往復)。PCビルドでも、ホストテストのSdFatスタブ経由で
  `SettingsScene::commitEdit()`と同じ手順(暗号化→`SetValue()`→ファイルの生内容確認→
  `ParseFile()`→復号)を通し、SD上には`enc1:...`の形で保存され、読み込み側で元の文字列に
  戻ることを確認済み。既存の平文`network.cfg`(`pc/sdcard/sys/network.cfg`)がそのまま読める
  ことはPCビルドの`--shot`で確認した(設定画面にSSID/パスワードが正しく表示される)。

**チャットのトークン(`/sys/chat.cfg`のtoken)も同日中に同じ方式へ広げた。** 実装直後にユーザーから
「チャットの認証情報は暗号化されているか」と問われて気づいた漏れで、この時点では`Chat_Client::loadConfig()`
がtokenを平文のまま読んでいた。

- **`ChatScene`に「設定」ボタンを新設した**(一覧画面でだけ、`[招待]`ボタンと同じ位置に出す。
  部屋の中では招待、一覧では設定という切り替え)。押すと`InputDialog`で「チャットサーバURL:」→
  (1フレーム空けて)「トークン(空欄で変更なし):」の順に編集する
  (`openChatSettings()`/`openTokenDialog()`。ダイアログからダイアログは1フレーム空ける、
  という「ブラウザのヘッダー」節の既存ルールをそのまま踏襲)。
- **サーバURLは暗号化しない。** Wi-FiのSSIDと同じ判断で、URL自体は秘匿情報ではなく、
  `chat_server_value`としてプレフィル・表示に使うため平文のまま保持・保存する。
  **トークンだけ暗号化する**(用途文字列は`"chat-token"`。Wi-Fiの`"wifi-ssid"`/`"wifi-password"`と
  同じ`PICO_Secret`を使うので新規実装は無し)。トークンの編集ダイアログは
  Wi-Fiパスワードと同じく**常に空欄から始まり**、空欄のまま決定すると既存の値を変更しない
  (`commitChatToken()`。`ChatScene`はトークンの平文を保持しない)。
- `Chat_Client.cpp::loadConfig()`のtoken読み込み箇所に`PICO_Secret::Decrypt("chat-token", ...)`を
  挟んだだけ(後方互換で`enc1:`接頭辞が無ければ平文のまま使う)。`configure()`自体
  (ホストテスト等からの直接注入経路)は平文のtokenを受け取る前提のまま変えていない
  (暗号化を意識するのは「SDのconfigファイルを読む」`loadConfig()`側だけでよいため)。
- 検証: `chat_scene_test`(run.sh、既存)が設定ボタン追加後も全項目パス。
  ホストテストのSdFatスタブ経由で、`ChatScene::commitChatToken()`相当の手順
  (`Encrypt("chat-token",...)`→`SetValue()`)で`/sys/chat.cfg`を書き、
  `ChatClient::loadConfig()`が実際にそれを復号して`State::Ok`まで進むことを確認した
  (このリポジトリには含めていない検証用の使い捨てプログラムで確認。手順はWi-Fiの
  検証と同じ)。PCビルドの`--shot`で「設定」ボタンから実際にサーバURL入力ダイアログが
  開くことまで確認済み(オンスクリーンキーボードでの実入力までは行っていない。
  暗号化ロジック自体は上記の往復検証で担保されている)。

**カレンダーの非公開URL(`/calendar/sources.cfg`)も同日中に同じ方式へ広げた。** Googleカレンダーの
非公開URLはSDカードから読める平文のままだと、SDだけ紛失した場合にWi-Fi/チャットと同じ脅威に晒される。

- **`PICO_Secret::kMaxPlainBytes`を64→255へ拡張した**(`Secret_Cipher.hpp`)。Googleの非公開URLは
  200文字を超えることがあり(`Url::path`を`PICO_STR_LL`へ広げた経緯と同じ理由)、Wi-Fi/チャット向けの
  64では全く足りないため。既存の暗号文字列の復号には影響しない後方互換な変更(上限を緩めるだけ)。
- **用途文字列はカレンダーの名前ごとに分けた**(`"calendar-url:" + 名前`)。sources.cfgは複数件
  持てるため、Wi-Fiの`"wifi-ssid"`/`"wifi-password"`のように固定1種類の用途文字列を使い回すと、
  複数のURLの暗号文をXORして平文同士のXORが漏れる、という`Secret_Cipher.hpp`の弱点をカレンダーが
  複数あるときに実際に踏むことになるため。
- **`CalendarSync::WriteSource(name, url)`を新設した**(`Config_Functions::SetValue()`と同じ
  「一時ファイル経由で1行だけ差し替え、無ければ追記」の手順だが、暗号化後の値(最大約516文字)が
  `Config_Functions::kConfigMaxValueLen`(160、Wi-Fi/チャット向け)を大きく超えるため、
  `Config_Functions`自体には触れず`Calendar_Sync.cpp`内に専用の大きめバッファ(`PICO_STR_512B`)で
  実装した。ロジックはほぼ`SetValue()`のコピーになるが、汎用パーサの制約(全設定ファイル共通の
  バッファサイズ)を変えるより影響範囲を`Calendar_Sync.cpp`へ閉じ込める方を優先した)。
- **`CalendarScene`に「追加」ボタンを新設した**(`[更新]`の左、`source_count`に関わらず常に表示)。
  押すと名前→(1フレーム空けて)URLの順に`InputDialog`で編集し、`WriteSource()`で暗号化して保存する。
  名前が不正/保存に失敗した場合は`ErrorFunctions::ShowFatal()`で理由を出す(GameBoyScene等、
  既存のC++シーンからの利用例と同じ)。
- **`CalendarSync::ReadSource()`(≒`ParseSourceLine()`)がenc1:接頭辞を見て復号する**(後方互換:
  接頭辞が無ければ平文のまま使う)。母艦のSDカードリーダーで`sources.cfg`を直接編集する既存の運用
  (URLを平文で書く)は変わらず使える。`webcal://→https://`の変換は復号した後の値に対して行う
  (暗号化された16進文字列は`webcal://`では始まらないため、順序を間違えると判定が効かなくなる)。
- ホストテストは`script/host_test/calendar_sync_secret_test.cpp`(189文字の長いURLでの往復・
  `enc1:`接頭辞で保存されること・平文がSDにそのまま書かれないこと・複数件・同名の上書き・
  後方互換(平文の既存行がそのまま読める)・不正な名前の拒否)。`calendar_scene_test`は
  「追加」ボタン追加後も既存の全項目がパスすることを確認済み。PCビルドの`--shot`で
  「追加」ボタンから実際に名前入力ダイアログが開くことまで確認済み(実際の保存はホストテストの
  SdFatスタブ経由で確認。暗号化ロジック自体は上記の往復検証で担保されている)。

## 端末ごとの鍵(OTP) (`src/services/DeviceKey_Functions` / `src/crypto/Device_Key.hpp`) (2026-10-12)

上の「Wi-Fi認証情報の暗号化保存」の固定鍵`kKey`はリポジトリで公開されていて、SDだけを盗まれても読めた。
**本体のOTPに端末ごとの32バイトの鍵を焼き、以後の暗号化をその鍵で行う**。Secure Bootは使わない(利用者の判断。2026-10-12)。

- **守れるのは「SDカードだけを盗まれた/見られた」場合だけ**。Secure Bootもデバッグ禁止も焼いていないので、本体を持ち去られて
  BOOTSELで別のファームを書かれればOTPは読める。**鍵はその本体にしか無いので、基板が壊れると端末の鍵で暗号化したもの
  (Wi-Fiのパスワード・SSHの鍵・トークン等)は二度と読めない**(確認のダイアログで、元の値をPCに控えるよう伝える)。
- **作り方**: 設定アプリの「画面」タブの7行目「端末の鍵」[作成] → 確認 → `DeviceKeyFunctions::Provision()`。
  鍵は本体の中でハードウェア乱数から作り(`SecretAead::Random()`)、外へは出さない。作った後は[更新]で`MigrateSecrets()`をやり直せる。
- **OTPの置き場所**: ページ60(行0xF00〜。3〜61が利用者向け)に2枠(各20行、ECCつき16bit/行): 目印`0x4B44`・版1・鍵16行・確かめの値2行
  (鍵のBLAKE2bの先頭4バイト)。**鍵→確かめの値→版→目印の順に書く**ので、途中で失敗した枠は「目印が無いのに中身がある」壊れた枠になり、
  次の枠に作れる。両方壊れたら`State::Broken`(固定鍵のまま)。書く前に枠が全部0か(生の値)を確かめ、書いた後に読み戻して比べる。
  実機の読み書きはboot ROMの`rom_func_otp_access()`(`pico/bootrom.h`)。ページのロック(PAGEn_LOCK・SW_LOCK)は使っていない。
- **鍵の使い方**: 端末の鍵そのものは持ち続けず、`Derive()`がBLAKE2b(鍵付き)で用途ごとの鍵を作って`DeviceKey::Install()`する:
  `cipher_key`(`Secret_Cipher.hpp`の`"enc3:"`。中身の形はenc1:と同じで鍵だけ違う)/ `aead_key`(Luaの暗号のversion 3=パスワード無し)/
  `pin_key`(画面ロックの`v2:`)。**`Device_Key.hpp`はヘッダだけ**なので、ホストテストは`DeviceKey::Install()`を直接呼べば「鍵がある」状態を作れる
  (呼ばなければ今までどおり固定鍵)。`main.cpp`は`CrashDumpFunctions::InstallHandlers()`の直後(暗号化された設定を読むものより前)に`Setup()`。
- **読み方**: enc1:/Luaのversion 1/PINのv1は鍵を作った後も読める。enc3:/version 3/v2は端末の鍵が無いと読めない
  (`Decrypt()`はfalse、Luaは`NoDeviceKey`)。**PINのv2が確かめられないとき(別の本体のSD・OTPを読めない)はロックしない**
  (ロックすると二度と解けないため)。PINの変更はそのまま新しいPINへ、解除はそのまま外す。
- **暗号化し直し(`MigrateSecrets()`)**: wifi.cfg(ssidN/passN)・network.cfg(旧形式のwifi-ssid/password)・chat.cfgのtoken・todoist.cfgのtoken・
  calendar/sources.cfg(全部の名前)の値のうち、平文かenc1:のもの(`PICO_Secret::NeedsUpgrade()`)を1行ずつ書き直す
  (コメント・他のキー・行の順は残す。暗号化した値を読み戻して比べ、読む側の値の上限を超えるものは書き直さない。一時ファイル→差し替え)。
  SSHの鍵は`SshUtil::LoadPrivateKey()`がenc1:を読んだとき端末の鍵で書き直す。**PINはハッシュしか持たないので、次に正しく打ったときに
  v2へ書き直す**(`LockFunctions::UpgradeRecord`)。Luaの`store.json`等は次に保存したとき。後から古いSDの値を戻しても、Wi-Fi・Todoist・SSHは
  読んだときに書き直す(それ以外は[更新])。
- **PC/Webビルド**: OTPはファイル(`DeviceKeyFunctions::pc_otp_file`、既定はSDのルートの隣の`otp_emu.bin`=`pc/otp_emu.bin`、
  `PICOOS_OTP_FILE`で変えられる。`.gitignore`済み)で真似る(ページ60だけ、書いた行は書き直せない)。ファイルを消すと「鍵の無い本体」を試せる。
  ホストテストはメモリの中だけ(`Test::ResetOtp/PokeRaw/FailWriteAfter`)。
- RAM: 静的に約84バイト(用途ごとの鍵)。作る/暗号化し直す間だけ約2KB(行のバッファ)を`malloc`。起動時間はOTPを数十行読むだけ。
- 検証: `devkey_test`(run.sh の input。作る前は固定鍵・作るとSDの全部の種類がenc3:になり中身は同じ・コメントが残る・2回目は何もしない・
  Luaのversion 1/3・PINのv1→v2・起動し直しても読める・鍵が無いと読めずPINでロックしない・書きかけの枠は次の枠・両方壊れたらBroken)、
  PCビルドの`--tap`/`--shot`(設定アプリで作成→「4項目を暗号化し直しました」→再起動で読めてWi-Fiに接続、`otp_emu.bin`を消すと読めない)、
  実機ファームのビルド(maxgerhardtのplatformで`pio run`。`rom_func_otp_access`がリンクされる)。**実機では未確認**(OTPへの書き込み・
  ページ60が本当に空いているか・ECCの読み出し)。

## 固定IP/DNS・本体の名前(mDNS)・キャプティブポータル・AP設定モード (`util/Net_Addr` / `net/Captive_Portal` / `net/Setup_Portal` / `services/WifiSetup_Functions`) (2026-10-24)

「WPA-Enterprise、キャプティブポータル、固定IP/DNS、mDNS(picoos.local)、ホットスポット/AP接続(モバイル設定用)が無い」という改善案への対応。
**WPA-Enterpriseだけは実装していない**(下の「WPA-Enterpriseを見送った理由」)。

### 固定IP/DNS(保存済みネットワークごと)

- **`wifi.cfg` に `ipN = 192.168.1.50/24`・`gwN`・`dnsN = 1.1.1.1,8.8.8.8` を足した**(`WifiProfiles::IpConfig`。既定=DHCPのときは書かない。
  秘密ではないので暗号化しない)。固定IPは場所ごとに違うので、全体の設定ではなく保存済みのネットワークごとに持つ。
  DHCPのままDNSだけ上書きもできる。並べ替え・削除で組がずれないよう、組の写しは `CopyProfile()` に1本化した。
  手で壊した値(サブネットの合わないゲートウェイ等)は読むときにDHCPへ戻す(繋がらなくなるより良い)。
- アドレスの読み書き・検査は `util/Net_Addr.hpp`(ヘッダだけ。`/24` と `/255.255.255.0` の両方、`/` を省くと `/24`、
  ネットワーク部/ブロードキャストのアドレスは不可、プレフィックスは8以上 — arduino-picoの `config()` が
  「3つ目の引数の先頭が255かどうか」で引数の並びを推測する(`LwipIntf::ipAddressReorder`)ため、/8未満は誤読される)。
- **適用(`NetworkFunctions::StartConnect()`)**: arduino-picoの `config()` は無線を止めている間しか効かない
  (`LwipIntfDev::config()` が begin の後なら何もしない)ので、`WiFi.disconnect()` → `config()` → `beginNoBlock()` の順。
  `beginNoBlock()` も内部で同じ切断をするので、切断を1回早めるだけ。DHCPへは `config(0.0.0.0)` で戻す。
  固定IPでDNSが空ならゲートウェイをDNSにする(0のままだと前のネットワークのDHCPのDNSが残る)。
  DNSの上書きはDHCPが繋がったときにDNSを書き換えるので、繋がった後に `setDNS()` で掛け直す。
- 設定アプリの「Wi-Fi」タブに **[IP]**(下段のボタンを5つにした。検索・削除のアイコンは幅が足りないので外した)。
  選んだネットワークについて 固定IP(空欄でDHCP)→ゲートウェイ(候補はそのサブネットの .1)→DNS の順に InputDialog で聞き、
  保存して、今そのネットワークに繋いでいれば繋ぎ直す(`ReapplyIpConfig()`)。一覧に `[固定IP]`/`[DNS]` の印。

### 本体の名前・mDNS

- `network.cfg` の `hostname`(既定 `picoos`。英数字とハイフンの1〜63文字、小文字へ揃える)。DHCPで名乗り(`setHostname()`)、
  arduino-picoの **SimpleMDNS** で `<名前>.local` に応答する(LEAmDNSより小さい。フラッシュ +約17KB)。
  `MDNS.begin()` は最初に繋がったときに1回だけ。以後のnetifの出入り(切断・再接続・別のSSID)はSimpleMDNSがlwIPのコールバックで追う。
  名前を変えたら `MDNS.end()` → `begin()` で名乗り直す。ファームの送信(`fwpush --upload-port picoos.local`)に使える。
- 「Wi-Fi」タブの一覧の下に2行(`picoos.local` / 今のIP)と[スマホ][名前]。1行だと固定IPの長いアドレスが入りきらなかった。
- PCビルドは母艦のmDNSとぶつからないよう応答しない(`pc/compat/SimpleMDNS.h` は名乗ったことにするだけ)。

### キャプティブポータル(ホテル・カフェ等のログイン/同意のページ)

- **判定(`CaptivePortal::CaptiveProbe` / `Judge()`)**: 繋がって1.5秒後に `http://connectivitycheck.gstatic.com/generate_204` を
  **転送を追わずに**GET(`HttpRequest::setFollowRedirects(false)` を足した)。204(と中身の無い200)= 外へ出られる、
  3xx = ポータル(転送先がログインのページ)、中身のある200 = 横取りされた(プローブのURL自身を開けばポータルが出る)、
  失敗 = 出られない。HTTPだけなのでTLSの約40KBは使わない。判定の間だけ約2KBを `new`。
  ポータル/出られないときは1分ごとに判定し直す(外で同意を済ませた・回線が戻ったのを拾う)。`network.cfg` の
  `captive-probe-url` で変えられる(gstaticに届かない国・自前のサーバ。`off` で判定しない)。
  状態は `NetworkFunctions::internet`(Lua の `pico.wifi_status().internet` にも出す)。ポータルなら接続ごとに1回通知(タップで設定アプリ)。
- **ログイン(`CaptiveLogin`)**: このOSのブラウザはHTMLを描けないので、ページを見せる代わりに **最初の `<form>` を読んで送る**。
  `FormScanner` はHTMLを溜めずに流しながら読む(タグ1つぶん512Bの溜め場所だけ)。`<script>`/`<style>` の中とコメントは飛ばし
  (中の `"<form"` に騙されない)、属性値の引用符の中の `>` でタグを終えない。隠し項目・チェックボックス(同意の印。チェックして送る)・
  選ばれたラジオ・最初の送信ボタンの名前と値をそのまま送る。テキスト/パスワードの欄が空なら、設定アプリが1つずつ聞く(部屋番号・メール等)。
  空欄が無ければ「利用規約に同意して接続しますか?」を確かめてから送る。`<meta refresh>` だけのページは2回まで追う。
  送った後は判定し直して通ったかを確かめる。**JavaScriptでフォームを組み立てるポータルは通せない**(「JavaScriptで動くポータルは使えません」と出す)。
  フォームの持ち物(12項目・本文1KiB等、約6KB)はログインの間だけ `new`。
- 設定アプリの状態の行は「要ログイン: SSID」/「外へ出られません: SSID」(状態を前に置く。SSIDが長いと後ろが切れるため)。
  ポータルのときだけ見出しの右に[ログイン]。
- 検証: `captive_portal_test`(判定・フォームの読み取り(まとめて/1バイトずつ)・文字参照・本文と送り先・meta refresh・上限・長すぎるタグ)。
  PCビルドで偽のポータル(302でログインのページへ転送し、`<script>` やコメントの中に偽のフォーム、隠し項目・同意のチェック・ラジオ・送信ボタン2つ・
  2つ目のフォームを持つPythonのサーバ。`captive-probe-url` を向ける)に対して、要ログインの表示・通知→[ログイン]→同意→POSTの中身が正しく受理→
  「インターネットへ出られます」まで、部屋番号の欄があるページでは入力のダイアログ→送信まで通した。**実機・本物のポータルでは未確認(Todoist参照)**。

### AP設定モード(スマホから繋ぎ先を送る)

- 「Wi-Fi」タブの **[スマホ]** → `WifiSetupScene`。開いている間だけ本体がAP「`<名前>-setup`」になる(WPA2、パスワードは毎回ちがう8桁の数字を
  画面に大きめの字で出す。開いたAPにすると近くの誰でも繋ぎ先を書き込めるため)。arduino-picoのAPだけのモード(192.168.42.1、DHCPサーバつき)。
  STAとAPを同時に使うモードもあるが、スマホ1台で設定する間だけなのでSTAは切って単純にした(`NetStatus::AP_SETUP` の間は自動接続しない)。
- **どの名前を聞かれても本体のアドレスを答えるDNS**(`SetupPortal::BuildDnsReply()`。arduino-picoの DNSServer はArduinoの `String` を使うので
  自前の数十行にした)と、**どのGETにも入力のページを返すHTTP**(`/generate_204` や `/hotspot-detect.html` にも)で、スマホはAPへ繋ぐと
  「ログインが必要なネットワーク」としてページを自動で開く。開かなければ画面のURL。
- ページ(`SetupPortal::Respond()`)はSSIDとパスワードの2つの欄だけ。`POST /save` で `WifiProfiles::Put()` し(同じSSIDならパスワードの書き換え)、
  画面は「受け取りました」を2.5秒見せて閉じ、APを止めてそのネットワークへ繋ぐ。[戻る]で閉じれば元の自動接続へ戻る。
  HTMLへ埋める文字列(SSID・本体の名前)は文字参照にする。要求は2KBまで(超えたら413)。同時の接続は1本。
  サーバの持ち物(要求と応答の溜め場所・UDPのバッファ、約4.5KB)は開いている間だけ `new`。
- arduino-picoはAPのモードのままだと次の `beginNoBlock()` をAPとして始める(`_beginInternal()` の「ESP8266互換」)ので、
  APを止めたら `WiFi.mode(WIFI_STA)` に戻す。
- PCビルドはAPを作れないので、ページだけ `http://127.0.0.1:8081/` で開ける(`PICOOS_SETUP_PORT`。母艦の外からは開けない)。
- 検証: `setup_portal_test`(要求の読み取り・フォームの値の復号・ページの出し分け・文字参照・DNSの応答)。PCビルドで[スマホ]→
  curlで `GET /generate_204`(入力のページ)・`POST /save`(`Cafe & Co`・`p@ss word`)→保存・画面が「受け取りました」→閉じて
  そのネットワークへ繋ぎ一覧の先頭へ、まで通した。**実機ではAP・DNS・スマホの自動表示とも未確認(Todoist参照)**。

### WPA-Enterpriseを見送った理由

Pico 2 Wの無線チップ(CYW43439)のarduino-picoのドライバはEAP(802.1X)を持たない(`WiFiClass.h` に
「EAP is not supported by the driver」とあり `beginEnterprise()` はコメントアウト)。CYW43のファームの中のサプリカントも
pico-sdkからは使えない。自前でやるには802.1Xのフレームを生で送受信してPEAP/MSCHAPv2(TLSの中でMS-CHAPv2)を組む必要があり、
ドライバがEAPOLのフレームを上げてこない以上、ドライバの改造から要る。費用に見合わないので、今は対応しない
(職場・学校のWPA-Enterpriseには、スマホのテザリングや来客用のWPA2-PSKを使ってもらう)。ドライバが対応したら `WifiProfiles` に
`eapN`(ID・匿名ID)を足し、パスワードと同じく暗号化して持つ形を想定している。

## 保存済みのWi-Fiネットワーク・Wi-FiのON/OFF・自動再接続 (`src/net/Wifi_Profiles` / `NetworkFunctions` / `SettingsScene`) (2026-10-01)

SSID/パスワードを1組しか持てなかった(`network.cfg`の`wifi-ssid`/`wifi-password`)のを、複数保存して
任意のものへ繋げるようにした。合わせて設定アプリをタブで分けた。

- **一覧は`WifiProfiles`(`src/net/Wifi_Profiles.hpp/.cpp`)、置き場所は`/sys/wifi.cfg`**: `enabled = true|false`と
  `ssid1`/`pass1` … `ssid8`/`pass8`(`kMaxProfiles=8`、固定長の静的領域で約1.2KB、確保なし)。
  **番号の若い順 = 直近で接続できた順**で、`NetworkFunctions`が接続に成功するたびに`MarkConnected()`でその組を先頭へ
  繰り上げる(並びが変わったときだけ保存)。この並びがそのまま自動接続の順番になる。
  - 暗号化はWi-Fiの単一設定と同じ`PICO_Secret`。**用途文字列は番号ごと**(`"wifi-ssid:1"`/`"wifi-password:1"`)。
    同じ用途の暗号文を1ファイルに並べると平文同士のXORが漏れるため(カレンダーのURLと同じ理由)。
  - 書き込みは`Config_Functions::SetValue()`を使わず、**ファイル丸ごとを一時ファイル→差し替え**(`Save()`)。
    1件で2キー、並べ替えで全キーが変わるので、1キーずつ書き換えるより単純。
  - SSIDは32バイト・パスワードは64バイトまで(WPAの上限。暗号化後133文字で`kConfigMaxValueLen`=160に収まる)。
    超えると`Put()`が`Invalid`、9件目は`Full`(自動で古いものを消さない。利用者に消してもらう)。
  - **移行**: `wifi.cfg`が無いときだけ`network.cfg`の`wifi-ssid`/`wifi-password`を1番として取り込み、`wifi.cfg`を作る。
    以後その2キーは読まない(全部消した後に古いSSIDが復活しないように)。`network.cfg`側のキーは消していない。
    PCビルドは起動のたびに`pc/sdcard/sys/wifi.cfg`を作るので`.gitignore`に入れてある。
- **自動接続(`NetworkFunctions`)**: 起動時は先頭(直近で接続したもの)へ繋ぐ。接続に失敗した
  (`TIMEOUT`/`SSID_NOT_FOUND`/`FAILED`)まま`RETRY_INTERVAL`(30秒)経つたびに、保存済みを**先頭から1つずつ**試す
  (`retryRank`。一巡したら先頭へ戻る。接続できたら/手動で選んだら0へ戻す)。依頼は「直近で接続したWi-Fiへ試みる」で、
  先頭がそれ。2番目以降も巡るのは、場所を移った(家⇔職場)ときに繋がらないままにならないため。
  接続中に切れた場合は従来どおり同じSSIDへ繋ぎ直す(5秒ごとの生存確認)。
  試行中(10秒)は`TRYING_CONNECT`なのでスリープに入らないが、30秒のうち残りの20秒で入れる。
- **ON/OFF**(`NetworkFunctions::SetEnabled()`): 新しい状態`NetStatus::OFF`。OFFは`WiFi.disconnect()`して自動接続を止めるだけで、
  **無線チップ自体(CYW43)は止めない** — 電池の残量表示がVBUSの検出に無線チップのGPIO(WL_GPIO2)を使うため
  (`Battery_Functions`参照)。`WiFi.mode(WIFI_OFF)`/`end()`まで落とせばもっと省電力になるが、その場合は電池の読み取りとの
  兼ね合いを実機で確かめること。手動で接続先を選ぶ(`ConnectWiFiAsync()`)とONに戻る。ステータスバーはOFFのとき
  満タンの扇を薄い灰色で出す(バツは付けない)。Wi-Fiの状態が変わったら5秒の定期更新を待たずに描き直す。
- `NetworkFunctions::ConnectProfile(index)`/`RemoveProfile(index)`(今繋いでいる/繋ごうとしているものを消したら切断し、
  30秒後に残りへ自動接続)。
- **設定アプリ(`SettingsScene`)は画面下のタブ**: `Wi-Fi`(ON/OFFのチェックボックス+状態の1行、保存済みの一覧=`ScrollList`、
  [検索][追加][接続][削除]) / `本体`(音量・輝度と自動調光・スリープ) / `時刻`(タイムゾーン・NTP1/2) / `その他`(ブラウザのホーム・
  起動時の自己診断)。全タブぶんのウィジェットを`onEnter()`で作り、`tab_widgets[]`へ登録して表示中のタブだけ見せる
  (`ClocksScene::applyVisibility()`と同じ考え方)。ドロップダウンは開いた一覧が下の行に重なるので最後に`Add()`する(従来どおり)。
  タブ名は幅(5タブで約47px)に収める。2026-10-12に「本体」を「画面」(明るさ・自動調光・ダークテーマ・スリープ・画面ロック・タッチ補正)と「音」(音量・音質・電池で絞る)に分け、「その他」は「他」に縮めた(3文字は2行に折れる)。行の高さ`ROW_H`は34。
  - 一覧: 1回目のタップで選択、2回目で接続(`ScrollList`の流儀)。鍵のアイコン=パスワードあり、扇=オープン。
    接続中のものは緑で「(接続中)」。状態の1行と一覧は接続状態/接続先/ON・OFFが変わったときだけ作り直す(選択はSSIDで覚えて戻す)。
  - [検索]: 従来の`WifiScanDialog`。選んだSSIDが**保存済みならそのまま繋ぎ**、未保存ならパスワードを聞いて保存して繋ぐ。
  - [追加]: SSIDの手入力→パスワード(隠れたSSID用)。**同じSSIDを追加し直すとパスワードの書き換え**(パスワード変更の手段はこれ)。
  - [削除]: 確認の`MsgDialog`の後に消す。確認中に並びが変わっても別の組を消さないよう、SSIDで覚えておく。
  - ダイアログを閉じた直後に次のダイアログを出すときは1フレーム空ける(`Pending`。「ブラウザのヘッダー」節と同じ理由)。
  - 旧来のSSID/パスワードの「編集」行は無くなった。
- ついでに直したもの: **`pc/compat/Arduino.h`の`min`/`max`が引数(値渡しの一時変数)への参照を返していた**
  (`decltype(a < b ? a : b)`はTとUが同じ型だと左辺値参照になる)。`NumberSlider::setValue()`の`min(max(...))`が壊れた値を読み、
  タブ化で生成順が変わった途端に音量/輝度が0で保存される形で表に出た(PCビルドをASan付きでビルドして特定)。`std::decay`で値を返すようにした。
  実機のArduinoはマクロなので無関係。
- 検証: `wifi_profiles_test`(run.sh。旧形式の取り込み・追加/更新/上限/削除・先頭への繰り上げ・ON/OFFの保存・番号ごとの暗号化・番号の抜け)、
  PCビルド(ASan付きでも)の`--tap`/`--shot`: 4タブの表示、OFF→再起動してもOFF→ONで再接続、検索→未保存のネットワークを選択→パスワード空欄で決定→
  保存されて接続し一覧の先頭へ、削除の確認、`PICOOS_WIFI_STATE=failed`で「直近のもの→次→直近のもの」と30秒ごとに巡ること。
  **実機では未確認**(`WiFi.disconnect()`後の`beginNoBlock()`での復帰、別のSSIDへ切り替えるときに切断を挟まなくてよいか)。

## Wi-Fiの新規接続(周辺スキャン→選択→パスワード入力→接続) (`SettingsScene` / `net/NetworkScan.hpp` / `gui/widgets/dialogs/WifiScanDialog`) (2026-09-27)

`SettingsScene`のSSID/パスワード編集は元々`InputDialog`への手入力のみだった。
一般的なスマホ/PCの「Wi-Fi設定」と同じ、周辺をスキャンして一覧から選び、
パスワードを入れて繋ぐという操作を追加した。

- **既存の手入力(`ssid_edit_button`「編集」)は残したまま**、SSID行の左隣に
  「検索」ボタンを新設した(`wifi_scan_button`)。隠れたSSIDや特殊な入力が要る場合は
  従来通り手入力できる。このボタン1つぶんだけSSID行のラベル幅(`ssid_label_w`)を
  詰めており、他の行(NTP/ホーム等)のラベル幅は変えていない。
- **`NetworkScan`(既存、未使用のまま放置されていたTask)を初めて実利用する過程で
  所有権の設計を直した。** 元の実装は`NetworkFunctions::ScanAsync()`が生成した
  `NetworkScan`を`PICO_Task::Add()`でグローバルリストに乗せていたが、これは
  「呼び出し側が結果を読み出す前に消える」バグを持っていた: `PICO_Task::Update()`は
  `status()`がPROCESSING以外になったその場で即座に`delete`する。main.cppの`loop()`は
  シーンの`onUpdate()`を`PICO_Task::Update()`より先に呼ぶため、呼び出し側が完了を
  検知できるのは早くても次のフレームであり、その時点では既に解放済み
  (実際にPCビルドで試して初めて踏んだuse-after-free。スキャン自体は成功したのに
  ダイアログが「スキャンに失敗しました」を表示する、という壊れ方をした)。
  **`HttpGet`/`HttpRequest`と同じ「呼び出し側が生ポインタとして持ち、自分の
  `onUpdate()`から毎フレーム`update()`を呼び、終わったら自分で`delete`する」流儀へ
  変更し、`PICO_Task::Add()`には乗せないようにした**(`ScanAsync()`の戻り値の型も
  `Task*`から`NetworkScan*`へ変えて、呼び出し側が`getCount()`/`getResult()`を
  キャストなしで読めるようにした)。
- **`NetworkScan`自体に結果の保持(`Result{ssid, rssi}`の固定長配列
  `kMaxResults=16`)を足した**(以前は見つけたSSID/RSSIを`Serial.printf`で
  ログへ流すだけで、呼び出し側が読み取る手段が無かった)。電波の強い順に
  挿入する(`CalendarScene::reload()`のファイル名ソートと同じ、高々16件の
  挿入ソート)。隠しSSID(空文字列)は選びようが無いので一覧に出さない。
- **`WifiScanDialog`(新設、`gui/widgets/dialogs/`)はSearchDialog(MarkdownScene)と
  全く同じ骨格**(状態1行+`ScrollList`+ボタン群、2回タップで選ぶ流儀)。
  スキャンそのものはせず、結果を詰めるのは`SettingsScene`の仕事(SearchDialogが
  検索そのものをしないのと同じ役割分担)。`ScrollList`のアイコンに
  `IconID::WifiSignal1〜4`(ステータスバーと同じ4段階、`NetworkFunctions::GetWifiStateIconID()`と
  同じ閾値)を使い、電波の強さを一覧内で見せる。ダイアログは開くたびに`new`し、
  閉じたら`DestroyLater()`する(SearchDialogと同じ、使い回さない)。
- **選択後はパスワード入力(`InputDialog`、空欄のまま決定可)を1フレーム空けて開く**
  (`pending_wifi_password_dialog`。ダイアログからダイアログは1フレーム空ける、
  という「ブラウザのヘッダー」節の既存ルールをそのまま踏襲)。
  **`connectScannedNetwork()`は既存の`commitEdit(EditField::Password)`とは
  空文字列の扱いが違う**: 手入力の編集は「空欄なら既存のパスワードを保持」だが、
  スキャンからの新規接続は選んだネットワークへの新規接続そのものなので、
  空欄(オープンネットワークのつもり)でも常に上書きする(別のネットワーク用の
  古いパスワードを引きずらないため)。SSID/パスワードとも暗号化して保存するのは
  既存の「Wi-Fi認証情報の暗号化保存」と同じ経路(`PICO_Secret::Encrypt()`)。
- 検証はPCビルドの`--shot`/`--tap`で行った(ホストテストは無し。GFX_Functions/
  Touch_Functionsと同じく実描画・実タッチに強く依存するため対象外にした):
  スキャン→3件表示(電波の強い順、アイコンの本数が変わること)→選択→
  パスワード入力(空欄で決定)→`NetworkFunctions::ConnectWiFiAsync()`が
  実際に呼ばれ、SSID/パスワードのラベルが更新されること。「再スキャン」
  「閉じる」ボタン、パスワード入力の「キャンセル」(接続を試みずに戻ること)も
  確認済み。**実機での確認は未**(このリモート環境には実機が無い。PC側の
  `WiFi.scanNetworks()`は`pc/sdcard/sys/network.cfg`の`pc-wifi-scan`が返す
  固定リストなので、実際の電波状況に応じた挙動は実機でしか確かめられない)。
