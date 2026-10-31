// 生成物: python3 script/i18n.py update が i18n/ の .po から作る。手で直さない(docs/design/i18n.md)
#include "system/I18n_Functions.hpp"

// hash の昇順(I18nFunctions::Lookup() が二分探索する)
const I18nFunctions::Entry I18nFunctions::kTableEn[] = {
    { 0x0073e150u, "Name" }, // 名前
    { 0x0078838cu, "Polish zloty" }, // ポーランドズロチ
    { 0x009f0d0bu, "Connection closed (exit code %d)" }, // 接続を閉じました(終了コード %d)
    { 0x00a6de36u, " Can roll back to %s" }, //  前の版%sに戻せます
    { 0x0149b128u, "SSID of the Wi-Fi to add" }, // 追加するWi-FiのSSID
    { 0x01658a21u, "Auto" }, // 自動
    { 0x01abb140u, "Transcription done" }, // 文字起こしが終わりました
    { 0x02031fe0u, "Use 4-8 digits" }, // 数字4〜8桁にしてください
    { 0x0210484eu, "Hungarian forint" }, // ハンガリーフォリント
    { 0x0219bbcfu, "Fetching…" }, // 取っています…
    { 0x0231bd89u, "Yesterday" }, // 昨日
    { 0x0269fef1u, "broker does not speak MQTT 3.1.1" }, // ブローカがMQTT 3.1.1を話せない
    { 0x02a3c42fu, "Checked %lu min ago (no updates)" }, // %lu分前に確認(更新なし)
    { 0x02c0e6dfu, "Lock now" }, // 今すぐロック
    { 0x02da6b45u, "Nothing to redo" }, // やり直せる操作がありません
    { 0x02ddaa3au, "Cancel" }, // やめる
    { 0x02f34bf3u, "Swedish krona" }, // スウェーデンクローナ
    { 0x0316468fu, "bit" }, // ビット
    { 0x03258dd5u, "Press the center of each cross with a fingertip or stylus, 5 times in order. Hold briefly on each for accuracy." }, // 画面に出る十字の中心を、指先かタッチペンで順に5回押してください。押すたびに少し止めると正確になります。
    { 0x03259dcfu, "Not saved: the network was deleted" }, // ネットワークが削除されたので保存しませんでした
    { 0x033e2df8u, "0 found" }, // 0件
    { 0x034014bau, "No notes. Create one with [New] or put .txt / .md files in /notes/ on the SD. Write #tag in a note to filter with [Tags]" }, // メモがありません。[新規]で作るか、SDの /notes/ に .txt / .md を置いてください。本文に #タグ と書くと[タグ]で絞り込めます
    { 0x038e2fbfu, "Kept by the DS3231 when off" }, // DS3231で電源を切っても保持
    { 0x0392ca48u, "Cannot read the URL (start with http:// or https://)" }, // URLを読めません(http:// か https:// で始めてください)
    { 0x039cf935u, "Font:L" }, // 文字:大
    { 0x03a058a8u, "Speed" }, // 速さ
    { 0x03a366aeu, "Unknown escape" }, // 知らないエスケープです
    { 0x03b5a925u, "Set up in the TODO app" }, // TODOアプリで設定してください
    { 0x03e3a2d8u, "No free space in OTP" }, // OTPに空きがありません
    { 0x043ef87fu, "Key exchange failed (invalid public value)" }, // 鍵交換に失敗しました(不正な公開値)
    { 0x04565bfdu, "OK" }, // 決定
    { 0x0460ca52u, "Del" }, // 消す
    { 0x046b263du, "Enter the IP address like \"192.168.1.50/24\"" }, // IPアドレスは「192.168.1.50/24」の形で入れてください
    { 0x047c6769u, "No due  " }, // 期限なし  
    { 0x04ade488u, "cannot send the request" }, // リクエストを送れない
    { 0x04d51a72u, "Next" }, // 次へ
    { 0x050258e4u, "Paste" }, // 貼付
    { 0x05082672u, "square foot" }, // 平方フィート
    { 0x052990c2u, "Roll back to %s" }, // %s に戻す
    { 0x05325543u, "Not a .pvid video" }, // .pvidの動画ではありません
    { 0x05351ccau, "Next" }, // 次
    { 0x055dfa42u, "Device key: %s" }, // 端末の鍵: %s
    { 0x0611693cu, "Unreadable audio file" }, // 読めない音のファイルです
    { 0x0648c213u, "Time on %d/%d" }, // %d/%d の時刻
    { 0x068e88f5u, "Could not save the IP settings" }, // IPの設定を保存できませんでした
    { 0x06a94959u, "Could not fetch" }, // 取得できませんでした
    { 0x06b17c6au, "Public key was rejected" }, // 公開鍵は受け付けられませんでした
    { 0x06d60a6du, "Update file has a wrong format or is broken" }, // 更新ファイルの形式が違うか、壊れています
    { 0x06da7f10u, "Counting down" }, // カウントダウン中
    { 0x06e9a8abu, "Writing ROM to flash %lu%%" }, // ROMを内蔵フラッシュへ書いています %lu%%
    { 0x0724f0a8u, "Finished" }, // 最後まで再生しました
    { 0x072bacc8u, "Search names (whole SD)" }, // 名前で検索(SD全体)
    { 0x07831deau, "Current %s\n%s" }, // 今の版 %s\n%s
    { 0x079000ddu, "Not found on the server (404)" }, // 配布元に見つかりません(404)
    { 0x079203aau, "(For public key auth, put a key in /sys/ssh/id_ed25519)\r\n" }, // (公開鍵認証を使うには /sys/ssh/id_ed25519 に鍵を置きます)\r\n
    { 0x07af94a9u, "Running…" }, // 動いています…
    { 0x07bec7adu, "Reset order" }, // 並びを元に戻す
    { 0x07c418a2u, " Withdrawn" }, //  取り下げられました
    { 0x07dd9506u, "Host name is empty or too long" }, // ホスト名が空か長すぎます
    { 0x0802e1afu, "\nand enter the code below to sign in" }, // \nを開き、下のコードを入れてサインインしてください
    { 0x083b1c57u, " +%d" }, //  他%d人
    { 0x08605de0u, "Signed with an unregistered key" }, // 登録されていない鍵で署名されています
    { 0x08a73ac1u, "Not fetched yet" }, // まだ取っていません
    { 0x08d91308u, "[Direct] works with just a phone and the device" }, // [直接つなぐ]ならスマホと本体だけで使えます
    { 0x08e457b6u, "Cannot start Lua (out of memory)" }, // Luaを起動できません(メモリ不足)
    { 0x090d614bu, "liter" }, // リットル
    { 0x095bb1f5u, "Waiting" }, // 待っています
    { 0x09627364u, " (%lu)" }, //  (%lu人)
    { 0x09732eeeu, " Needs OS #%lu or later" }, //  本体#%lu以降が要ります
    { 0x098e4278u, "Create a new folder\nName:" }, // 新しくフォルダを作成\n名前:
    { 0x0a09ba2fu, "ri" }, // 里
    { 0x0a5991dfu, "monme" }, // 匁
    { 0x0a67f230u, "Reinstall" }, // 入れ直す
    { 0x0a86d43fu, "NTP server 2" }, // NTPサーバー2
    { 0x0afe1f4cu, "received a frame with RSV bits set" }, // 拡張の印(RSV)が立った枠を受けた
    { 0x0b86d5d2u, "NTP server 1" }, // NTPサーバー1
    { 0x0b931eeau, "Tue" }, // 火
    { 0x0bbc8e8du, "Cannot send" }, // 送れません
    { 0x0bf09e87u, "Loading" }, // 読み込み中です
    { 0x0c7134a5u, "Overwritten by NTP sync" }, // NTP同期で上書きされます
    { 0x0c72aee1u, "Fetching rates…" }, // レートを取得中…
    { 0x0cedc37du, "Server returned %d" }, // 配布元が %d を返しました
    { 0x0d07b0dcu, "7 days" }, // 7日間
    { 0x0d1299c4u, "not enough memory for TLS" }, // TLSの準備に必要なメモリが無い
    { 0x0d40a828u, "Firmware without audio" }, // 音声出力なしのファームです
    { 0x0d54a2a9u, "(cannot read the source)" }, // (ソースを読めません)
    { 0x0da0aabbu, "\033[93mCould not re-save the private key encrypted (still plain)\033[0m\r\n" }, // \033[93m秘密鍵を暗号化して保存し直せませんでした(平文のままです)\033[0m\r\n
    { 0x0dace3d7u, "Cannot update now" }, // 今は更新できません
    { 0x0dc32e53u, "Update %d apps?\n" }, // %d件のアプリを更新しますか?\n
    { 0x0dcc9decu, "at %02d:%02d" }, // %02d:%02dから
    { 0x0dd194e4u, "Scanning..." }, // スキャン中...
    { 0x0de6e7c8u, "Updated to rates of %s" }, // %s のレートに更新しました
    { 0x0e1911efu, "#Tags" }, // #タグ
    { 0x0e5d6affu, "Could not start" }, // 始められませんでした
    { 0x0e6ff123u, "Failed while reading the state" }, // 状態の読み込みの途中で失敗しました
    { 0x0ea1dc47u, "Searching…" }, // 探しています…
    { 0x0ea55817u, "This app cannot read there" }, // このアプリからは読めない場所です
    { 0x0ea9e22du, "Developer" }, // 開発者向け
    { 0x0ee398c7u, "What to do with %s?" }, // %s をどうしますか?
    { 0x0f2a8098u, "New versions of %s and others (%d apps)" }, // %s など%d件のアプリに新しい版があります
    { 0x0fabb517u, "Clock is not set" }, // 時計が合っていません
    { 0x101ca2b4u, "Cannot open the book" }, // 本を開けません
    { 0x1023f6a4u, "  [running]" }, //   [動作中]
    { 0x10270098u, "\nPress [Update] to show a sign-in code" }, // \n[更新] を押すとサインインのコードを出します
    { 0x10291f05u, "Sign-in was declined" }, // サインインが断られました
    { 0x102bd53au, "Mexican peso" }, // メキシコペソ
    { 0x108d2ed8u, "Login" }, // 要ログイン
    { 0x113f114au, "Other" }, // 他
    { 0x1145e333u, "No tasks today" }, // 今日のタスクはありません
    { 0x115217b9u, "Installing…" }, // インストール中…
    { 0x1166b970u, "No token. Set it in [Settings]" }, // Todoistのトークンがありません。[設定] から入れてください
    { 0x11b00230u, "No tags yet (write #tag in a note)" }, // タグはまだありません(本文に #タグ と書く)
    { 0x11bc36f1u, "Calendar URL (iCal/.ics):" }, // カレンダーのURL(iCal/.ics):
    { 0x11ffa06bu, "Off" }, // オフ
    { 0x1246a63bu, "(Check that IMAP is enabled in Outlook.com settings)" }, // (Outlook.com の設定で IMAP を有効にしているか確かめてください)
    { 0x127aafa6u, "h" }, // 時間
    { 0x12877152u, "Cannot open %s" }, // %s を開けません
    { 0x12957e4bu, "Enter a token in [Settings] first" }, // 先に [設定] でトークンを入れてください
    { 0x12aa4eefu, "Ruby" }, // ルビ
    { 0x12b3d6b3u, "Not run yet" }, // まだ動いていません
    { 0x12b5f10au, "Calib." }, // 補正
    { 0x12c7002du, "Flash contents do not match" }, // 内蔵フラッシュに書いた内容が合いません
    { 0x12ce16a5u, "Cancelled" }, // 取り消しました
    { 0x12e3f95fu, "All" }, // すべて
    { 0x130ed599u, "Stopped" }, // 停止中
    { 0x13586460u, "Every %luh" }, // %lu時間ごと
    { 0x136ba886u, "Script aborted: exceeded the instruction limit (%u) (possible infinite loop)" }, // スクリプトの実行が命令数の上限(%u)を超えたため打ち切りました(無限ループの可能性があります)
    { 0x1377c0b1u, "NTP server 2: %s" }, // NTPサーバー2: %s
    { 0x137dd2a8u, "Connecting to %s:%u...\r\n" }, // %s:%u へ接続しています...\r\n
    { 0x13addcefu, "30 min" }, // 30分
    { 0x13b90a3bu, "cannot read the trusted root certificates" }, // 信頼するルート証明書を読めない
    { 0x13d6e680u, "Delete %s?" }, // %s を削除しますか?
    { 0x13e4d902u, "Not saved: the password is empty" }, // パスワードが空なので保存しませんでした
    { 0x141111e6u, "[Plot] to graph (%s)" }, // [描画]でグラフ (%s)
    { 0x141db499u, "token in chat.cfg is too long" }, // chat.cfg の token が長すぎます
    { 0x14480c8eu, "Microsoft sign-in expired. Press [Update] to sign in again" }, // Microsoft のサインインが切れました。[更新] を押してサインインし直してください
    { 0x14511d79u, "Stopped in the debugger" }, // デバッガで停止しました
    { 0x146e2ffdu, "Token is too long" }, // トークンが長すぎます
    { 0x15087cb2u, "Swap" }, // 入替
    { 0x152a25efu, "received a broken PUBLISH" }, // ブローカから壊れたPUBLISHが届いた
    { 0x152b4d5cu, "Can cut up to 1KiB" }, // 切り取れるのは1KiBまでです
    { 0x15796e21u, "Closing an element that is not open" }, // 開いていない要素を閉じています
    { 0x15d975abu, "broker unavailable" }, // ブローカが使えない状態
    { 0x1640ca78u, "Japanese yen" }, // 日本円
    { 0x16de3f1eu, "Found %d" }, // %d件見つかりました
    { 0x16e3c638u, "This app cannot write there" }, // このアプリからは書けない場所です
    { 0x1779f2f9u, "Could not save (no SD, or URL too long)" }, // 保存できませんでした(SD無しか、URLが長すぎます)
    { 0x177b78b1u, "Not found" }, // 見つかりません
    { 0x17c46d55u, "Sign-in required" }, // サインインが必要です
    { 0x180e72a3u, "Could not save the account" }, // アカウントを保存できませんでした
    { 0x188183eeu, "Cannot read %s\n\n%s" }, // %s を読めません\n\n%s
    { 0x18b687a7u, "Could not fully delete %s" }, // %s を削除しきれませんでした
    { 0x190092f5u, "Key %s" }, // キー %s
    { 0x1996cfa6u, "Wi-Fi is not connected" }, // Wi-Fiにつながっていません
    { 0x19a8b633u, "Chinese yuan" }, // 人民元
    { 0x19ad8705u, "Cannot build the request (model / prompt too long?)" }, // 送る内容を作れません(model / prompt が長すぎる?)
    { 0x19dbf6e2u, "Chat" }, // チャット
    { 0x19e12484u, "tsp" }, // 小さじ
    { 0x19e7f353u, "Searching… %lu checked, %d found" }, // 検索中… %lu件中 %d件
    { 0x1a0a847bu, "megapascal" }, // メガパスカル
    { 0x1a363879u, "Could not delete" }, // 削除できませんでした
    { 0x1a782219u, "Unread chat" }, // チャットの未読
    { 0x1a8673b7u, "Missing ','" }, // ',' がありません
    { 0x1acbd3dcu, "disconnected during the handshake" }, // 開始の挨拶の途中で切れた
    { 0x1af16218u, "None found" }, // 見つかりませんでした
    { 0x1b02ef42u, "acre" }, // エーカー
    { 0x1b048a70u, "Calendar" }, // カレンダー
    { 0x1b063332u, "Monthly" }, // 毎月
    { 0x1b54ef1cu, "App update check" }, // アプリの更新の確認
    { 0x1b57b9e0u, "Could not write to OTP%s" }, // OTPへ書けませんでした%s
    { 0x1b5df62au, "Chat: %lu unread" }, // チャット: 未読 %lu件
    { 0x1b838896u, "No TODO widget on the home screen" }, // ホーム画面にTODOウィジェットがありません
    { 0x1be31701u, "Cannot write the text to the SD" }, // 文章をSDへ書けません
    { 0x1c14a1e5u, "The peer closed the connection" }, // 相手が接続を切りました
    { 0x1c20a35du, "Too many lines to open (1000 max)" }, // 行数が多すぎて開けません(1000行まで)
    { 0x1c2847ccu, "Cannot parse the URL (too long or not http(s)://)" }, // URLを解釈できない(長すぎるか http(s):// で始まっていない)
    { 0x1c498d8fu, "%d apps" }, // %d件のアプリ
    { 0x1c760f20u, "Sign-in failed: %s" }, // サインインできません: %s
    { 0x1cbd16e8u, "Received an empty packet" }, // 空のパケットが届きました
    { 0x1cdb9877u, "Yes" }, // 対応
    { 0x1d143700u, " (OS too old)" }, //  (本体が古い)
    { 0x1d191730u, "Move back" }, // 前へ移動
    { 0x1d5bc4e0u, "received an unexpected packet" }, // ブローカから想定外のパケットが届いた
    { 0x1d700f61u, "not authorized" }, // 接続を許されていない(認証)
    { 0x1d7c1e07u, "Verifying what was written" }, // 書いた中身を確かめています
    { 0x1d97262fu, "Success" }, // 成功
    { 0x1dc015d5u, "Thu" }, // 木
    { 0x1dc2d468u, "Brazilian real" }, // ブラジルレアル
    { 0x1dca4f56u, " (partial)" }, // (多すぎて一部)
    { 0x1e176be5u, "Join Wi-Fi, press [Update]" }, // Wi-Fiに繋いで [更新] を押してください
    { 0x1e222f51u, "Choose a ROM" }, // ROMを選んでください
    { 0x1e2ab228u, "Signing key not trusted (check key in store.cfg)" }, // 署名の鍵が信頼されていません(store.cfgのkeyを確かめてください)
    { 0x1e5e95cau, "2 min" }, // 2分
    { 0x1ead88ffu, "CDATA outside the root element" }, // ルート要素の外にCDATAがあります
    { 0x1eb17d9cu, "A new version of %s is available" }, // %s の新しい版があります
    { 0x1ef5f75bu, "Really delete?" }, // 本当に削除しますか?
    { 0x1f00ceebu, "Chat is not set up" }, // チャットが設定されていません
    { 0x1f04b1a3u, "Does not look like an IMAP server" }, // IMAPのサーバではないようです
    { 0x1f24841cu, "\n(Install updates that add permissions one by one)" }, // \n(権限が増える更新は1つずつ入れてください)
    { 0x1f2df3e7u, "Cannot read the comment start" }, // コメントの始まりが読めません
    { 0x1f8d6245u, "Recent files" }, // 最近開いたファイル
    { 0x1fc2073au, "Waiting (%d devices connected)" }, // 待っています(接続している端末: %d台)
    { 0x1fd67942u, " (some failed; see the log)" }, // (失敗あり。ログを見てください)
    { 0x1fe29312u, "The page has no form" }, // ページにフォームがありません
    { 0x203c0767u, "UAE dirham" }, // UAEディルハム
    { 0x20433713u, "No signed versions" }, // 署名のある版がありません
    { 0x20eea1b5u, "Cannot erase the flash" }, // 内蔵フラッシュを消せません
    { 0x211b42d4u, "kilometers per hour" }, // キロメートル毎時
    { 0x215c8a5au, "Signing key: %s%s" }, // 署名の鍵: %s%s
    { 0x2164db4fu, "Unexpected message during key exchange" }, // 鍵交換の途中で想定外のメッセージが届きました
    { 0x21a8ce61u, "Arithmetic" }, // 四則演算
    { 0x21b28e5bu, "Type a word" }, // 語句を入力してください
    { 0x21c62fcau, "Date: \n" }, // 日時: \n
    { 0x21c7e602u, "Counting… %d/%d" }, // 数えています… %d/%d
    { 0x21eab57du, "Skipped: the app is open" }, // アプリを開いているので見送りました
    { 0x2217c111u, "Failed: %s" }, // 失敗: %s
    { 0x22192444u, "Extra data after the package" }, // パッケージの後ろに余分なデータがあります
    { 0x222f049fu, "Mark: %s" }, // しおり: %s
    { 0x22df9765u, "cannot send CONNECT (too long or disconnected)" }, // CONNECTを送れない(長すぎる/切れた)
    { 0x22fbb7ddu, "Save path is too long" }, // 保存先のパスが長すぎます
    { 0x23018aa5u, "South African rand" }, // 南アフリカランド
    { 0x2334e6e0u, "Received a broken packet" }, // 壊れたパケットが届きました
    { 0x23406565u, "Cancelled" }, // 取りやめました
    { 0x2356da4du, "Server returned %d" }, // サーバが %d を返しました
    { 0x23a1118du, "(no sender)" }, // (差出人なし)
    { 0x23ab48a9u, "Transcription API key (blank to keep):" }, // 文字起こしのAPIキー(空欄で変更なし):
    { 0x23d8f00du, "Up to %d networks can be saved. Delete one you don't need" }, // 保存できるのは%d件までです。不要なものを削除してください
    { 0x2418839au, "Stopwatch" }, // ストップウォッチ
    { 0x2444f75bu, "Undo" }, // 元に戻す
    { 0x245f7f6cu, "server did not switch to WebSocket" }, // サーバがWebSocketに切り替えない
    { 0x24620418u, "Already a USB drive" }, // すでにUSBドライブになっています
    { 0x2488170cu, "All" }, // 全曲
    { 0x24fe0d6cu, "NTP server 1: %s" }, // NTPサーバー1: %s
    { 0x2539c1ffu, "pascal" }, // パスカル
    { 0x2599547cu, "Paste" }, // 貼り付け
    { 0x25b058fcu, "Gmail / other" }, // Gmail・その他
    { 0x2615675eu, "Contents & marks (loading contents)" }, // 目次・しおり(目次は読み込み中)
    { 0x2636aa74u, "client ID rejected" }, // クライアントIDを断られた
    { 0x2645e023u, "Search contents (blank to clear):" }, // 中身から探す(空で解除):
    { 0x265798c8u, "Wi-Fi is not connected" }, // Wi-Fiに接続していません
    { 0x2685088bu, "No response from the server (timeout)" }, // サーバから応答がありません(時間切れ)
    { 0x26aae084u, "Cannot read the attribute name" }, // 属性名が読めません
    { 0x26eae00du, "Not enough memory to open the App Store" }, // メモリが足りないため、アプリストアを開けません
    { 0x26f33069u, "\nNote: this update asks for more permissions" }, // \n※この更新で使う権限が増えます
    { 0x27324b57u, "Package is truncated" }, // パッケージが途中で切れています
    { 0x274159e9u, "That name already exists" }, // 同じ名前があります
    { 0x275f779cu, "x is only for graphs" }, // xはグラフでだけ使えます
    { 0x27e19a9eu, "Cannot open the recording" }, // 録音を開けません
    { 0x28156895u, "Selection cleared" }, // 選択を解除しました
    { 0x285d92b0u, "sho" }, // 升
    { 0x28a4a2d1u, "body was cut off" }, // 本文が途中で切れた
    { 0x28b90e22u, "not created" }, // 未作成
    { 0x28bdd93eu, "Hide from launcher" }, // ランチャから隠す
    { 0x28fae1c1u, "electronvolt" }, // 電子ボルト
    { 0x292404cfu, "Runtime error in on_key()" }, // on_key()の実行時エラー
    { 0x29321171u, "New PIN (4-8 digits)" }, // 新しいPIN(数字4〜8桁)
    { 0x295e8637u, "Unexpected list format (catalog.tsv)" }, // 一覧(catalog.tsv)の形式が違います
    { 0x29750bcau, "Clock" }, // 時計
    { 0x29d186beu, "broker reply was not CONNACK" }, // ブローカの返事がCONNACKでない
    { 0x29e32651u, " (a manually added app has this name)" }, //  (手で入れた同名のアプリがあります)
    { 0x2a1cdbfdu, "Cannot read the app info in the package" }, // パッケージのアプリの情報を読めません
    { 0x2a444dc6u, "Tap a song to play" }, // 曲をタップすると再生します
    { 0x2a465f59u, "Tmrw %d:%02d" }, // 明 %d:%02d
    { 0x2a479b3bu, "Disconnected by the server" }, // サーバに切られました
    { 0x2a598cbdu, "Outgoing packet too large" }, // 送信するパケットが大きすぎます
    { 0x2a9fe8a8u, "Set PIN" }, // PINを決める
    { 0x2ab5018eu, "Access token is too long" }, // アクセストークンが長すぎます
    { 0x2ac3720cu, " Paused" }, //  一時停止中
    { 0x2addb1acu, "Menu" }, // メニュー
    { 0x2ae80d4fu, "Password change requested (not supported)" }, // パスワードの変更を求められました(未対応)
    { 0x2b05a63du, "Attachments: %d (%s)" }, // 添付: %d件(%s)
    { 0x2b209367u, "Bookmarks are full (remove one first)" }, // しおりがいっぱいです(先に外してください)
    { 0x2b2a2b2bu, "User name (usually your email):" }, // ユーザー名(ふつうはメールアドレス):
    { 0x2b4e272cu, "calorie" }, // カロリー
    { 0x2bb910aau, "Cannot open the update file" }, // 更新ファイルを開けません
    { 0x2be4cc79u, "Host key is not ssh-ed25519" }, // ホスト鍵の形式が ssh-ed25519 ではありません
    { 0x2bfc6839u, "Cannot resolve the link" }, // リンクを解決できません
    { 0x2c213c8fu, "No apps registered.\nAdd them in Setup() of App_List.cpp." }, // アプリが登録されていません。\nApp_List.cpp のSetup()へ追加してください。
    { 0x2c2e6432u, "Search failed" }, // 検索できませんでした
    { 0x2c437911u, "Fetching images %d/%d" }, // 画像を取得中 %d/%d
    { 0x2c86ea98u, " (%d unread)" }, //  (未読 %d)
    { 0x2c9d27f6u, "Enter code %s at %s" }, // %s でコード %s を入れてください
    { 0x2ca5152bu, "Folder name:" }, // フォルダ名:
    { 0x2caa61ccu, "api in todoist.cfg is not a URL" }, // todoist.cfg の api が URL ではありません
    { 0x2cbd272du, "Battery < %u%%" }, // 電池%u%%未満
    { 0x2d0b11a4u, "Nothing runs in the background" }, // バックグラウンドで動くものはありません
    { 0x2dd81fb6u, "(not set)" }, // (未設定)
    { 0x2de33dd6u, "Cannot get an access token: %s" }, // アクセストークンを取れません: %s
    { 0x2e8448f8u, "Server has no ssh-ed25519 host key" }, // サーバに ssh-ed25519 のホスト鍵がありません
    { 0x2e86e69cu, "Unexpected file list (files.tsv)" }, // ファイル一覧(files.tsv)の形式が違います
    { 0x2ea78303u, "Cannot send to the portal" }, // ポータルへ送れません
    { 0x2ecad6fdu, "Loading…" }, // 読み込み中…
    { 0x2f481137u, "Unexpected rate format" }, // レートの形式が違います
    { 0x2f535ae3u, "Transcribe: %s" }, // 文字起こし: %s
    { 0x2f57c9fau, "Calendar: " }, // カレンダー: 
    { 0x2f78371du, "File on the server is too large" }, // 配布元のファイルが大きすぎます
    { 0x2f8b915eu, "Request is too long" }, // 送る内容が長すぎます
    { 0x2f93c9a5u, "Sending to the portal..." }, // ポータルへ送っています...
    { 0x2fb0cbdeu, "Fetching" }, // 取得中
    { 0x30598208u, "Start" }, // 開始
    { 0x3069ed42u, "mebibyte" }, // メビバイト
    { 0x3090db76u, "Error in a timer callback" }, // タイマーのコールバックでエラーが発生しました
    { 0x310a58a1u, "Retry" }, // 再試行
    { 0x311cb483u, "Kept across restarts" }, // 再起動では消えません
    { 0x3141c395u, "Calendar name (letters, digits, _ -; up to 20):" }, // カレンダーの名前(英数字と_-、20文字まで):
    { 0x31a482f8u, "Lua runtime error" }, // Luaスクリプトの実行時エラー
    { 0x31dd5e2bu, "Pick a network to join from the list" }, // 接続するネットワークを一覧から選んでください
    { 0x3231870fu, "Version list is too large" }, // 版の一覧が大きすぎます
    { 0x32d3a0a3u, "Disconnected" }, // 切断しました
    { 0x32d572bbu, "Not available in the Web version" }, // Web版では受け取れません
    { 0x32de65a3u, "Pressure" }, // 圧力
    { 0x32f94f06u, "Connect to a PC over USB" }, // PCとUSBでつないでください
    { 0x32fbd6fbu, "Extra characters in the closing tag" }, // 閉じタグの中に余分な文字があります
    { 0x330589afu, "The app was removed" }, // アプリが無くなりました
    { 0x33384ac7u, "Cannot read the signature (catalog.sig)" }, // 署名(catalog.sig)を読めません
    { 0x335fc0ccu, "Tap to choose..." }, // タップして選択...
    { 0x33674c41u, "Transcribe" }, // 文字起こし
    { 0x33961587u, "Invite code:" }, // 参加コード:
    { 0x33a6c26bu, "%s\"%s\": %d+ (first %d)" }, // %s「%s」%d件以上(先頭%d件)
    { 0x33c2eb5bu, "Network error: " }, // 通信に失敗: 
    { 0x342363f6u, "State saved" }, // 途中保存しました
    { 0x342b5a89u, "kilobyte" }, // キロバイト
    { 0x342ed250u, "Scan" }, // 検索
    { 0x344cce9bu, "URL is not mqtt:// or mqtts://, or out of memory" }, // URLが mqtt:// か mqtts:// でない、またはメモリが足りません
    { 0x345200ecu, "Delete %s?\n(the transcript is deleted too)" }, // %s を削除しますか?\n(文字起こしの文章も消えます)
    { 0x347881eeu, "Book not found (deleted or moved; removed from the list)" }, // 本が見つかりません(消すか移したため、一覧から外しました)
    { 0x348fc60fu, "Move to top" }, // 先頭へ移動
    { 0x34bf3129u, "Cannot divide by 0" }, // 0で割ることはできません
    { 0x34ea0706u, "Wrong token (HTTP %d)" }, // トークンが違います(HTTP %d)
    { 0x353595fcu, "Timed out (network or timers did not finish)" }, // 時間切れ(通信やタイマーが終わりませんでした)
    { 0x358d5180u, "Could not start the mic I2S" }, // マイクのI2Sを開始できませんでした
    { 0x36189fa9u, "mile" }, // マイル
    { 0x361d66e8u, "Cancel" }, // キャンセル
    { 0x368f24b9u, "\033[93m(Could not write known_hosts; you will be asked again)\033[0m\r\n" }, // \033[93m(known_hosts へ書き込めませんでした。次回も確認します)\033[0m\r\n
    { 0x36953f37u, "Invite code\n%s\nValid until %02d:%02d" }, // 参加コード\n%s\n%02d:%02dまで有効
    { 0x36aa7f99u, "Restart when frozen" }, // 固まったら再起動する
    { 0x36bd9515u, "You have unsaved changes. Discard and start a new file?" }, // 保存していない変更があります。破棄して新規作成しますか?
    { 0x36c30965u, "Cannot get the list" }, // 一覧を取れません
    { 0x372e4c18u, "degree" }, // 度
    { 0x3749d1e3u, "Reader" }, // 読書
    { 0x3788e8d6u, "Command is too long" }, // コマンドが長すぎます
    { 0x37f6a0b1u, "Large" }, // 大
    { 0x37ffdc3du, "On" }, // オン
    { 0x381a18d0u, "\033[91mWi-Fi is not connected\033[0m\r\n" }, // \033[91mWi-Fiに繋がっていません\033[0m\r\n
    { 0x381f8da7u, "Norwegian krone" }, // ノルウェークローネ
    { 0x386e061fu, "square meter" }, // 平方メートル
    { 0x38dd86ceu, "Line too long (191 bytes max)" }, // 1行が長くなりすぎます(191バイトまで)
    { 0x398c2549u, "%s and %d more" }, // %s ほか%d件
    { 0x39d06a66u, "Invite code is too long" }, // 参加コードが長すぎます
    { 0x39d8f9e3u, "SSID or password too long (password: 64 chars max)" }, // SSIDかパスワードが長すぎます(パスワードは64文字まで)
    { 0x39dcbd65u, "cannot send (disconnected)" }, // 送れない(切れた)
    { 0x3a28128eu, "Offline (saved copy)" }, // オフライン表示(保存済み)
    { 0x3a59c669u, "millimeter of mercury" }, // 水銀柱ミリメートル
    { 0x3a851bf7u, "Chat server URL:" }, // チャットサーバURL:
    { 0x3ade698fu, "Nothing scheduled" }, // 予約はありません
    { 0x3aead33cu, "Not permitted" }, // 使う権限がありません
    { 0x3af025ccu, "(unknown Lua location)" }, // (Luaの場所が分かりません)
    { 0x3afc0aeeu, "Unexpected version list (old firmware or wrong server?)" }, // 版の一覧の形式が違います(本体が古い・配布元が違う?)
    { 0x3b7e0141u, "Failed to read the state" }, // 状態の読み込みに失敗しました
    { 0x3b9a64f1u, "Math" }, // 数学記号
    { 0x3bc333fcu, "'<' in an attribute value" }, // 属性値に '<' があります
    { 0x3bcabe08u, "Bad list signature (tampered, or updated during download)" }, // 一覧の署名が合いません(改ざん・取得の途中で更新された可能性)
    { 0x3bceaa4fu, "Replaced %d (%s left)" }, // %d件置換(残りは%s)
    { 0x3beca0e8u, "Too many scheduled notices" }, // 通知の予約がいっぱいです
    { 0x3c01482du, " (every %d)" }, // (%d回に1回)
    { 0x3c379679u, "Device name (found as <name>.local; letters, digits, hyphens)" }, // 本体の名前(<名前>.local で見つかる。英数字とハイフン)
    { 0x3c3c20fbu, "Cannot parse the source URL" }, // 取得元のURLを解釈できません
    { 0x3c4de1b5u, "Server sent a packet that is too large" }, // サーバから大きすぎるパケットが届きました
    { 0x3c7b650fu, "(new)" }, // (新規)
    { 0x3c7c5895u, "Danish krone" }, // デンマーククローネ
    { 0x3c913c70u, "Cannot open the result" }, // 結果を開けません
    { 0x3cfc1352u, "kilopascal" }, // キロパスカル
    { 0x3d0af8cau, "\nUnsigned files accepted after a prompt" }, // \n署名なしも確認して受け付ける
    { 0x3e0302adu, " / last: %s" }, //  / 最後: %s
    { 0x3e083633u, "Cannot replace the saved file" }, // 保存先へ差し替えられない
    { 0x3e11d023u, "Cannot read the server name (e.g. imap.example.com:993)" }, // サーバの名前が読めません(例: imap.example.com:993)
    { 0x3e9d2d5eu, "Two root elements" }, // ルート要素が2つあります
    { 0x3eb3fd6fu, "%s.local\n(no IP)" }, // %s.local\n(IP無し)
    { 0x3efbd237u, "URL is not ws:// or wss://, or out of memory" }, // URLが ws:// か wss:// でない、またはメモリが足りません
    { 0x3efcfdbfu, "Notices" }, // 通知
    { 0x3f1bd91eu, "Replace: find" }, // 置換: 探す文字列
    { 0x3f29cdebu, ". The PIN follows at next unlock" }, // 。PINは次に解除したとき
    { 0x3f771f1eu, "\033[93mThe key has a passphrase and cannot be used (using password)\033[0m\r\n" }, // \033[93m鍵がパスフレーズ付きのため使えません(パスワードで続けます)\033[0m\r\n
    { 0x3faafc41u, "Unknown access point address" }, // アクセスポイントのアドレスが分かりません
    { 0x3fccebd1u, "DNS (up to 2, comma separated; blank for gateway):" }, // DNS(カンマ区切りで2つまで。空欄でゲートウェイ):
    { 0x3fde1c50u, "Closing tag does not match the opening tag" }, // 閉じタグが開きタグと合いません
    { 0x3fe0b400u, "Writing ROM to flash 0%" }, // ROMを内蔵フラッシュへ書いています 0%
    { 0x40313637u, "Server does not support SSH2" }, // SSH2に対応していないサーバです
    { 0x4069eb01u, "Energy" }, // エネルギー
    { 0x40c14702u, "Resume" }, // 続きから読む
    { 0x40ffdd0eu, "Copying..." }, // コピー中...
    { 0x411f8a97u, "hectopascal" }, // ヘクトパスカル
    { 0x412f8d81u, "Request headers too long or invalid" }, // リクエストヘッダが長すぎる/不正
    { 0x41598718u, "Creates a device key and writes it to the OTP.\n- OTP cannot be erased (no undo)\n- Wi-Fi, SSH keys, tokens, calendar URLs and the PIN are re-encrypted with this key\n- If the board breaks, they can never be read again. Keep copies on a PC\n- This only protects against a stolen SD card (not the whole device)" }, // 端末の鍵を作り、本体のOTPへ書き込みます。\n・OTPは消せません(取り消せません)\n・Wi-Fi・SSHの鍵・トークン・カレンダーのURL・PINを、この本体の鍵で暗号化し直します\n・基板が壊れると、それらは二度と読めません。元の値はPCに控えてください\n・守れるのはSDだけを盗まれた場合です(本体ごとは守れません)
    { 0x4161ec1bu, "Fetching %s (%d/%d)" }, // %s を取得中 (%d/%d)
    { 0x41915c69u, "%s not connected" }, // %sがつながっていません
    { 0x41c48d89u, "Korean won" }, // 韓国ウォン
    { 0x426afef3u, "Select all" }, // すべて選択
    { 0x42884520u, "No trusted key (key in /sys/store.cfg)" }, // 信頼する鍵がありません(/sys/store.cfg の key)
    { 0x42ba4a91u, "No SD card" }, // SDカードがありません
    { 0x42da01d6u, "Fetching versions…" }, // 版の一覧を取得中…
    { 0x42e28322u, "Account saved" }, // アカウントを保存しました
    { 0x431884ddu, "Mailbox \"" }, // メールボックス「
    { 0x435c66f3u, "Play" }, // 再生
    { 0x435d230au, "More…" }, // 戻す/削除
    { 0x4416815du, "Normal" }, // 標準の字
    { 0x441a0290u, "tbsp" }, // 大さじ
    { 0x4433d4dfu, "Close" }, // 閉じる
    { 0x44f9d315u, "Continue" }, // 前回の続きから
    { 0x45141b0fu, "The gateway must be in the same subnet as the IP address" }, // ゲートウェイはIPアドレスと同じサブネットのアドレスにしてください
    { 0x453b4b47u, "Disconnected" }, // 未接続
    { 0x45b3a052u, "milligram" }, // ミリグラム
    { 0x45b77babu, "Video index is broken" }, // 動画の目録が壊れています
    { 0x45bbe57cu, "Checked %lus ago (%d rooms)" }, // %lu秒前に確認(%d部屋)
    { 0x45ed28d7u, "Could not read the whole script" }, // スクリプトを最後まで読めません
    { 0x464e0659u, "Headers too long, or invalid names or characters" }, // ヘッダが長すぎるか、使えない名前・文字があります
    { 0x465a4a84u, "foot" }, // フィート
    { 0x46695bf8u, "Not available in the Web version" }, // Web版では使えません
    { 0x469134c7u, " (installed)" }, //  (入っています)
    { 0x472fcc54u, "Network error: " }, // 通信に失敗しました: 
    { 0x47989cccu, "Keeps listening after you close Wi-Fi Share" }, // Wi-Fi転送の画面を閉じると待ち受けを続けます
    { 0x481a5a88u, "Stopped" }, // やめました
    { 0x4833fbefu, "Run self-test at boot" }, // 起動時に自己診断を実行
    { 0x4835fe9au, "Wide" }, // 横長
    { 0x48482327u, "Now" }, // 今すぐ
    { 0x489e3f02u, "Search term is empty" }, // 検索語が空です
    { 0x497392bau, "Loading the portal page..." }, // ポータルのページを読んでいます...
    { 0x49d44dc3u, "refused by the server (auth)" }, // サーバに断られた(認証)
    { 0x4a31b8c4u, "millisecond" }, // ミリ秒
    { 0x4a3682c2u, "Deselect" }, // 選択を解除
    { 0x4a8f004eu, "Cannot read this path (permission)" }, // このパスは読めません(権限)
    { 0x4ad746fdu, "wrong user name or password" }, // ユーザー名かパスワードが違う
    { 0x4ae33f8eu, "Background: none" }, // バックグラウンド: なし
    { 0x4b187cb2u, "Digital" }, // デジタル
    { 0x4b2a99fdu, "yr" }, // 年
    { 0x4b58311eu, "Volume" }, // 体積
    { 0x4b61ae26u, "Copied the first 1KiB" }, // 1KiBまでコピーしました
    { 0x4b6322ceu, "Ungroup folder" }, // フォルダを解除
    { 0x4b7f5579u, "Font:S" }, // 文字:小
    { 0x4b88c8e0u, "USB: enumerated, no port" }, // USB:列挙済/口まだ
    { 0x4b893934u, "State file is broken or from another version" }, // 状態のファイルが壊れているか、別の版のものです
    { 0x4bb8add8u, "refused by the broker" }, // ブローカに断られた
    { 0x4c5b664du, "Video data is broken" }, // 動画のデータが壊れています
    { 0x4c82a4edu, "No free file number" }, // 空き番号がありません
    { 0x4c8c15c4u, "Euro" }, // ユーロ
    { 0x4cc8ee9au, "(cont.)" }, // (続き)
    { 0x4d032b35u, "centimeter" }, // センチメートル
    { 0x4d3c2168u, "Message not found (it may have been deleted)" }, // メールが見つかりません(削除されたかもしれません)
    { 0x4d43a3e1u, "Back to DHCP" }, // DHCPに戻しました
    { 0x4d6e7131u, "Cannot read after '<!'" }, // '<!' の後ろが読めません
    { 0x4db22689u, "quart (US)" }, // クォート(米)
    { 0x4e064232u, "Restart" }, // 再起動
    { 0x4e52f4feu, "Number too long" }, // 数値が長すぎます
    { 0x4e553fa3u, "User name is too long" }, // ユーザー名が長すぎます
    { 0x4e5e36bdu, "Invalid expression" }, // 式が正しくありません
    { 0x4e7bb777u, "Record" }, // 録音
    { 0x4ee14fa9u, "Every %lus" }, // %lu秒ごと
    { 0x4f0c0ea3u, "Invalid write" }, // 不正な書き込み
    { 0x4f337bc8u, "Still on %s (could not write #%lu %s)" }, // 今は %s のままです(#%lu %s を書けませんでした)
    { 0x4f3a6c8eu, "HTML shown as plain text" }, // HTMLを文字だけにして表示
    { 0x4f4e10e6u, "Rejected" }, // 受け付けられませんでした
    { 0x4f5e184du, "Cannot write to the staging area" }, // 置き場に書き込めません
    { 0x4f95f25du, "This network needs no login" }, // ログインの要るネットワークではありません
    { 0x4fb7c2b0u, "Transcription needs a connection. Connect to Wi-Fi" }, // 文字起こしはオンラインが前提です。Wi-Fiに接続してください
    { 0x4fde78afu, "Could not generate random numbers" }, // 乱数を作れませんでした
    { 0x4ff68b19u, "Paused " }, // 一時停止 
    { 0x5008977au, "No tasks. Add one with [Add]" }, // タスクはありません。[追加] で足せます
    { 0x5009d9d1u, "Update file does not fit in the staging area" }, // 更新ファイルが置き場に入りません
    { 0x50297c63u, "Could not open this image" }, // この画像を開けませんでした
    { 0x50b97713u, " [static IP]" }, //  [固定IP]
    { 0x50c03867u, "Cannot resolve the path" }, // パスを解決できません
    { 0x50da7870u, "inch" }, // インチ
    { 0x50fde3e8u, "Open" }, // 開く
    { 0x516a2312u, "Could not save the server (use a URL or host name without spaces)" }, // 送り先を保存できませんでした(空白を含まないURLかホスト名で)
    { 0x519ceb75u, "Overwrite" }, // 上書き
    { 0x51f5616cu, "No rates yet. Fetch with [Update]" }, // レート未取得。[更新]で取得
    { 0x52417fa5u, "Mark" }, // しおり
    { 0x524451e5u, "Broken channel reply" }, // チャネルの応答が壊れています
    { 0x52617a89u, "Move to folder" }, // 別のフォルダへ
    { 0x52916576u, "Power" }, // 電源
    { 0x52b8c2ebu, "Got \"%s\". Connecting" }, // 「%s」を受け取りました。接続します
    { 0x52c6b6d1u, "Choose a version" }, // 書き込む版を選ぶ
    { 0x52f5c3e4u, "Could not open a shell (channel refused)" }, // シェルを開けませんでした(チャネルを拒否されました)
    { 0x5307f37eu, "%d/%d (%s) no events" }, // %d月%d日(%s) 予定なし
    { 0x53c1c1fau, "Destination path is too long" }, // 置き場所のパスが長すぎます
    { 0x53e5ec14u, "Wrong password" }, // パスワードが違います
    { 0x54425ec7u, "Update file does not match the list (server updated during download?)" }, // 更新ファイルの中身が一覧と合いません(取得の途中で配布元が更新された?)
    { 0x54691298u, "Lua debugger" }, // Luaデバッガ
    { 0x54a32c3eu, "US dollar" }, // 米ドル
    { 0x54a7fc42u, "Checked %lu min ago (%d updates)" }, // %lu分前に確認(更新%d件)
    { 0x54c64e21u, "Calibrated (error avg %.1fpx / max %.1fpx). Touch the screen and check that the blue dot follows your finger." }, // 補正できました(ずれ 平均%.1fpx / 最大%.1fpx)。画面のどこかを押して、青い点が指の位置に出るか確かめてください。
    { 0x54e8f510u, "Kelvin" }, // ケルビン
    { 0x54f7ca6cu, "Cannot change the rate while recording" }, // 録音中は周波数を変えられません
    { 0x54fdc6b2u, "received a broken packet" }, // ブローカから壊れたパケットが届いた
    { 0x554e0a4du, " (another app has this name)" }, //  (同名の別のアプリがあります)
    { 0x5578728cu, "Host key changed during rekey" }, // 鍵の交換し直しでホスト鍵が変わりました
    { 0x558d6ad2u, "Screen lock: PIN" }, // 画面ロック: PIN
    { 0x55aec449u, "Installing %s %s" }, // %s %s を入れています
    { 0x55c0e016u, "Fetched %d/%d %d:%02d" }, // 取得 %d/%d %d:%02d
    { 0x55da8a1fu, "The device key in OTP is broken and there is no room to make a new one. The fixed key stays in use" }, // OTPの端末の鍵が壊れていて、作り直す空きもありません。固定鍵のまま暗号化します
    { 0x55ebe20bu, "Re-encrypts the settings on the SD that are not yet encrypted with the device key" }, // SDの暗号化された設定のうち、まだ端末の鍵で暗号化していないものを暗号化し直します
    { 0x56003366u, "Response was cut off" }, // 途中で終わっています
    { 0x56550526u, "Repeats: " }, // 繰り返し: 
    { 0x565cec2cu, "Starting sign-in…" }, // サインインを始めています…
    { 0x568fb228u, "USB: no cable" }, // USB:線なし
    { 0x56e68855u, "Name to find:" }, // 探す名前:
    { 0x56e8b3d5u, "To" }, // 変換先
    { 0x57762704u, "Cannot re-read the file list" }, // ファイル一覧を読み直せません
    { 0x577c8ef4u, "Choose a task to complete" }, // 完了にするタスクを選んでください
    { 0x57aa7a55u, "SSID is too long (32 bytes max)" }, // SSIDが長すぎます(32バイトまで)
    { 0x57b7d8a5u, "Extra data after the value" }, // 値の後に余計なものがあります
    { 0x57ca8d9du, "1 %s = %s %s\nRates of %s" }, // 1 %s = %s %s\n%s のレート
    { 0x57e78110u, "knot" }, // ノット
    { 0x57f1cc97u, "Disconnected (will reconnect)" }, // 未接続(自動で再接続します)
    { 0x587835c6u, "Find rooms" }, // 部屋を探す
    { 0x58ae4975u, "Not signed" }, // 署名がありません
    { 0x58d2b726u, "Error (value type: %s)" }, // エラー(値の種類: %s)
    { 0x58e33d7au, "square kilometer" }, // 平方キロメートル
    { 0x59003419u, "Lower volume on battery" }, // 電池で音量を絞る
    { 0x592ba4b7u, "Cannot open the dictionary" }, // 辞書ファイルが開けません
    { 0x599af33cu, "kan" }, // 貫
    { 0x59a6fd2au, "pound per square inch" }, // 重量ポンド毎平方インチ
    { 0x59bddc14u, "Could not rename" }, // 名前を変えられませんでした
    { 0x59cc7562u, "Battery is low (%d%%). Connect USB or charge" }, // 電池の残りが少ないです(%d%%)。USBに繋ぐか充電してください
    { 0x59e58dc1u, "Could not read the response: " }, // 応答を読めませんでした: 
    { 0x59ee4109u, "Overflow" }, // 桁があふれました
    { 0x59ffe754u, "Cannot read the sign-in server URL" }, // サインインのサーバのURLが読めません
    { 0x5a2f0787u, "Source changed. Fetch with [Update]" }, // 配布元を変えました。[更新] で取得します
    { 0x5a71294fu, "Continue where you left off?" }, // 続きから遊びますか
    { 0x5ae88df6u, "Weekdays" }, // 平日
    { 0x5b6abc74u, "Accept" }, // 同意する
    { 0x5b9525adu, "Added to known_hosts\r\n" }, // known_hosts へ追加しました\r\n
    { 0x5ba7d221u, "%s\nAPI key (blank to keep):" }, // %s\nAPIキー(空欄で変更なし):
    { 0x5bac6f0du, "Read" }, // 読む
    { 0x5bb50943u, "Mass" }, // 重さ
    { 0x5bba1532u, "cannot read the file to send" }, // 送るファイルを読めない
    { 0x5be8bbb4u, "From" }, // 変換元
    { 0x5c3895e2u, "%s\"%s\": %d" }, // %s「%s」%d件
    { 0x5ce2be9du, " (danger)" }, //  (危険)
    { 0x5ced0738u, "mail.cfg has no oauth-client-id" }, // mail.cfg に oauth-client-id がありません
    { 0x5d087cf4u, "millimeter" }, // ミリメートル
    { 0x5d0d554au, "Wrong (%d tries before a wait)" }, // 違います(あと%d回で待ち時間)
    { 0x5d0ed9aeu, "Could not write the update request" }, // 書き換えの指示を書けませんでした
    { 0x5d55f6b9u, "Clock is not set" }, // 時刻が合っていません
    { 0x5da75aa8u, "No versions available" }, // 配布されている版がありません
    { 0x5e1ae7f3u, "Script aborted" }, // スクリプトを打ち切りました
    { 0x5e2b56f5u, "radian" }, // ラジアン
    { 0x5e472d51u, "Up to %d shots" }, // 弾は %d 個までです
    { 0x5e4f9591u, "Add widget" }, // ウィジェット追加
    { 0x5f0e0c7eu, "Area" }, // 面積
    { 0x5f2ce043u, "Clipboard is empty" }, // クリップボードは空です
    { 0x5f2e1233u, "server used an extension that was not offered" }, // 申し出ていない拡張をサーバが使おうとした
    { 0x5fae389eu, "Yearly" }, // 毎年
    { 0x6002cb07u, "Indian rupee" }, // インドルピー
    { 0x60112c52u, "Tap to pick a song" }, // タップして曲を選ぶ
    { 0x603fbbe0u, "Library" }, // 本棚
    { 0x6059d551u, "Pick a color" }, // 色を選択
    { 0x607c0d99u, "Invalid host key signature" }, // ホスト鍵の署名が正しくありません
    { 0x608128b1u, "Sat" }, // 土
    { 0x60b6bd2bu, "Exit" }, // 終了
    { 0x6108b164u, "\nAuthor: %s  Permissions: %s" }, // \n作者: %s  権限: %s
    { 0x610de476u, "Stop waiting" }, // 待ち受けをやめる
    { 0x6124246fu, "close code is missing" }, // closeの番号が欠けている
    { 0x6128a03fu, "QR: join the device's Wi-Fi" }, // QRで本体のWi-Fiへ
    { 0x613d9c96u, "Nothing more to undo" }, // これ以上元に戻せません
    { 0x61577298u, "Put .ics files in /calendar/" }, // /calendar/ に .ics を置いてください
    { 0x6181e389u, "Czech koruna" }, // チェココルナ
    { 0x61b94028u, "Mismatched brackets" }, // 括弧の対応が合いません
    { 0x61f5e700u, "%d/%d (%s)" }, // %d月%d日(%s)
    { 0x621e9d91u, "New name:" }, // 新しい名前:
    { 0x62643347u, "Writing" }, // 書き込んでいます
    { 0x6275e17au, "Zoom-" }, // 縮小
    { 0x62989a1cu, "Text outside the root element" }, // ルート要素の外に文字があります
    { 0x62d195cfu, "Search contents for:" }, // 中身から探す言葉:
    { 0x6321a0dcu, "Waiting for sign-in…" }, // サインインを待っています…
    { 0x635c1e5cu, " (unsaved)" }, //  (未保存)
    { 0x63a0dbb7u, ", next in %lus" }, // ・次は%lu秒後
    { 0x63d5fbd0u, "Search URL is too long" }, // 検索URLが長すぎます
    { 0x63d5fe8fu, "Transcribe: sending %s %d%%" }, // 文字起こし: %s を送っています %d%%
    { 0x640a190cu, "gallon (US)" }, // ガロン(米)
    { 0x6433c46eu, "gibibyte" }, // ギビバイト
    { 0x6456f4e6u, "Update" }, // 更新
    { 0x64b5988au, " This version is skipped" }, //  この版は見送り中
    { 0x64bb5828u, "Move forward" }, // 後ろへ移動
    { 0x64c451adu, "Cannot join: the line would be too long" }, // 1行が長くなりすぎるため繋げられません
    { 0x64ef238eu, "JSON is truncated" }, // JSONが途中で切れています
    { 0x650b099fu, "Wrong token. Enter it again in [Settings]" }, // トークンが違います。[設定] から入れ直してください
    { 0x650c8807u, "wk" }, // 週
    { 0x650d1890u, "Choose one from the list" }, // 一覧から選んでください
    { 0x65177d0cu, "Roll %s back from %s to %s?\nFiles the app wrote (saves etc.) are kept. %s will not be offered as an update until you press [Update]" }, // %s を %s から %s へ戻しますか?\nアプリが書いたファイル(セーブ等)はそのまま残ります。%s は、[更新] を押すまで更新の知らせに出しません
    { 0x655b1978u, "year (365 days)" }, // 年(365日)
    { 0x658444c9u, "Nested too deeply" }, // 入れ子が深すぎます
    { 0x65b464cbu, "nautical mile" }, // 海里
    { 0x65c3ccc4u, "Due %s (in %d min)" }, // 期限 %s(%d分前)
    { 0x65fa0df0u, "Cache not found" }, // キャッシュが見つかりません
    { 0x6614e845u, "Mach" }, // マッハ
    { 0x662a147du, "Philippine peso" }, // フィリピンペソ
    { 0x66671a86u, "Direct" }, // 直接つなぐ
    { 0x66749be0u, "Text" }, // 文章
    { 0x66f83cdau, " (no space left)" }, // (空きもありません)
    { 0x676de1d9u, "Screen lock" }, // 画面ロック
    { 0x677ddda4u, "Could not open" }, // 開けませんでした
    { 0x67d43652u, "Running" }, // 計測中
    { 0x6812f7e2u, "Indonesian rupiah" }, // インドネシアルピア
    { 0x68b82dfcu, "bar" }, // バール
    { 0x68ca54f3u, "header line too long" }, // ヘッダ行が長すぎる
    { 0x69101fedu, "OK" }, // 問題なし
    { 0x692c12a8u, "Replaced %d" }, // %d件置換しました
    { 0x695abb4fu, "Could not create /notes/" }, // /notes/ を作れませんでした
    { 0x6964d7abu, "Jump host " }, // 踏み台 
    { 0x69bb719du, "%d" }, // %d件
    { 0x69bbcf6cu, "Microsoft sign-in required. Press [Update] to sign in" }, // Microsoft のサインインが必要です。[更新] を押してサインインしてください
    { 0x69d4879cu, "until %02d:%02d" }, // %02d:%02dまで
    { 0x69fb0c45u, "Pick a network to set its IP" }, // IPを設定するネットワークを一覧から選んでください
    { 0x6a17f1acu, "Video" }, // 動画
    { 0x6a2710d7u, "Cannot prepare the save location" }, // 保存先を用意できません
    { 0x6a4e7409u, "Error in a Lua callback" }, // Luaコールバックでエラーが発生しました
    { 0x6a6a9112u, "TODO today" }, // TODO 今日
    { 0x6aae264du, "Server not responding" }, // サーバが応答しません
    { 0x6abcd2b3u, "Cannot move files into place" }, // インストール先へファイルを移せません
    { 0x6ac45d0cu, "joule" }, // ジュール
    { 0x6b57be6du, "No matching notes" }, // 当てはまるメモはありません
    { 0x6b6d944cu, "[Update]" }, // [更新]
    { 0x6c22fb68u, "USB: port OK" }, // USB:口OK
    { 0x6c539896u, "Could not fetch the page: " }, // ページを取れませんでした: 
    { 0x6d14a300u, "Online" }, // 接続中
    { 0x6d4b7f9au, "Endpoint not found (check api / model)" }, // 送り先が見つかりません(api / model を確認)
    { 0x6d4f275cu, "Agenda" }, // 予定
    { 0x6d718420u, "Cannot write to the SD card (out of space?)" }, // SDカードに書き込めません(空きが足りない?)
    { 0x6da4d067u, "(now)" }, // (今)
    { 0x6db64676u, "No saved state" }, // 保存した状態がありません
    { 0x6dc23aadu, "Wrong API key" }, // APIキーが違います
    { 0x6de498d5u, "Celsius" }, // 摂氏
    { 0x6e586857u, "Downloaded #%lu (%s)" }, // #%lu (%s) をダウンロードしました
    { 0x6e72e574u, "Note not found" }, // メモが見つかりません
    { 0x6ea4205fu, "Long message, showing the start" }, // 長いので途中までを表示
    { 0x6edf4f9du, "Measuring usage… %s/%s" }, // 使用量を数えています… %s/%s
    { 0x6ef43357u, "Mail" }, // メール
    { 0x6f086a34u, "Set it by hand with \"Change\"" }, // 「変更」で手動で合わせられます
    { 0x6f21001du, "Cannot open the file" }, // ファイルを開けません
    { 0x6f391f2du, "Too many; showing some" }, // 多すぎるので一部だけ表示しています
    { 0x6f5fdd11u, "Search contents (whole SD)" }, // 中身で検索(SD全体)
    { 0x6f95da70u, " (sound)" }, //  (音あり)
    { 0x6f9b05a1u, "Downloading #%lu (%s)…" }, // #%lu (%s) をダウンロード中…
    { 0x6fb02f9eu, "megabyte" }, // メガバイト
    { 0x6fd26b37u, "Preparing…" }, // 準備しています…
    { 0x701cf2c9u, "Aborted: host key not trusted" }, // ホスト鍵を信頼しなかったため中止しました
    { 0x703a5835u, "Temperature" }, // 温度
    { 0x70464d44u, "Save RAM too large (32KB max)" }, // セーブ領域が大きすぎます(32KBまで)
    { 0x70b47641u, "Timer: time is up" }, // タイマー: 時間になりました
    { 0x70d197c2u, "Invite code\n%s\nExpires in 30 min" }, // 参加コード\n%s\n30分で無効
    { 0x70ea3dbeu, "Paused" }, // 一時停止中
    { 0x711e8862u, "Choose ROM" }, // ROMを選ぶ
    { 0x7174a291u, "cannot send the handshake" }, // 開始の挨拶を送れない
    { 0x71be189au, "Restarting" }, // 再起動します
    { 0x7209ce14u, "No notification permission" }, // 通知の権限がありません
    { 0x72281d91u, "%s (at %d)" }, // %s (位置 %d)
    { 0x723647c1u, "Unknown error" }, // 不明な誤り
    { 0x72b9fa2du, "Server disconnected: %.*s" }, // サーバが切断しました: %.*s
    { 0x72ce6f16u, "Login failed" }, // ログインできません
    { 0x73645683u, "Del" }, // 削除
    { 0x73af6fbau, "Write failed" }, // 書き込みに失敗しました
    { 0x73b6a1acu, "Unknown package format (update the firmware)" }, // 知らない形式のパッケージです(本体の更新が要ります)
    { 0x73da1e6fu, "kibibyte" }, // キビバイト
    { 0x73e81e3au, "New: #%lu (%s)\n%s%s%luKB\nCurrent: %s\nWrite it?" }, // 新しい版: #%lu (%s)\n%s%s%luKB\n今の版: %s\n書き込みますか?
    { 0x73fca42bu, "Connecting to %s:%u via the jump host...\r\n" }, // 踏み台経由で %s:%u へ接続しています...\r\n
    { 0x7400a453u, "Timed out" }, // 時間切れ
    { 0x7427ca35u, "Cannot talk to the sign-in server: " }, // サインインのサーバと通信できません: 
    { 0x745ab838u, "Contents " }, // 中身に
    { 0x74756138u, "Wait for the list to finish loading" }, // 一覧の取得が終わってから入れてください
    { 0x74812a4fu, "%d-%02d" }, // %d年%d月
    { 0x74b43ab6u, "Send failed: %s" }, // 送れませんでした: %s
    { 0x74f61dd2u, "Missing '>' after '/'" }, // '/' の後ろに '>' がありません
    { 0x75058d90u, "Cannot fetch: " }, // 取得できません: 
    { 0x750a5d83u, "Could not open this file" }, // このファイルを開けませんでした
    { 0x753617ddu, "Cannot start sign-in: %s" }, // サインインを始められません: %s
    { 0x7588e975u, "Exceeds the line limit (1000)" }, // 行数の上限(1000行)を超えます
    { 0x75acd44du, "Angle" }, // 角度
    { 0x7614d7d0u, "Too large to open (32KiB max)" }, // 大きすぎて開けません(32KiBまで)
    { 0x7637a2bdu, "Content size does not match the header" }, // 中身の大きさが見出しと合いません
    { 0x764f4f84u, "The touches did not line up. Press the center of each cross again, in order." }, // 押した位置がうまく合いませんでした。十字の真ん中をもう一度、順番どおりに押してください。
    { 0x768535dbu, "Cannot open the package" }, // パッケージを開けません
    { 0x7696412fu, "Cont" }, // 続行
    { 0x769b726eu, "Enter PIN" }, // PINを入力
    { 0x76bd4895u, "api in transcribe.cfg is not a URL" }, // transcribe.cfg の api が URL ではありません
    { 0x76cc1f93u, "Find" }, // 検索する文字列
    { 0x76e25c15u, "Cannot reach the portal" }, // ポータルへ繋げません
    { 0x76e351b0u, "Repeats: unsupported rule, showing the first only\n" }, // 繰り返し: 未対応の規則のため初回だけ表示しています\n
    { 0x770e3369u, "Not an iCalendar response (check the URL)" }, // iCalendarではない応答(URLを確かめてください)
    { 0x7751431au, "min" }, // 分
    { 0x7762fdeeu, "Cannot add more lines" }, // これ以上行を増やせません
    { 0x77670398u, "Password for \"%s\" (leave blank if none):" }, // 「%s」のパスワード(不要なら空欄のまま決定):
    { 0x7779a315u, "Microsoft sign-in required" }, // Microsoft のサインインが必要です
    { 0x77da04cfu, "Dim when idle" }, // しばらく触らないと暗くする
    { 0x77f626d4u, "No builds for this config (%s)" }, // この構成(%s)向けの配布はありません
    { 0x78160baeu, "2026-12" }, // 2026年12月
    { 0x7816d499u, "Text" }, // 文字
    { 0x782c3f19u, "Sign-in cancelled. Sign in again with [Update]" }, // サインインをやめました。[更新] でサインインし直せます
    { 0x787cff56u, "Cannot install the app" }, // アプリを入れられません
    { 0x789f00cdu, "Select: tap the end position" }, // 範囲選択: 終わりの位置をタップ
    { 0x78a33606u, " (author key changed)" }, //  (作者の鍵が変わりました)
    { 0x78ae6203u, "\r\nConnect to (user@host[:port] [-J jump]): " }, // \r\n接続先 (ユーザー@ホスト[:ポート] [-J 踏み台]): 
    { 0x7937f253u, "Not fetched yet" }, // まだ取得していません
    { 0x793fb9bcu, "Stopped listening" }, // 待ち受けをやめました
    { 0x799ca5edu, "Unsigned update files are not accepted" }, // 署名の無い更新ファイルは受け付けません
    { 0x79c808cfu, "Cannot open the script" }, // スクリプトを開けません
    { 0x79ddfde0u, "Could not start the access point" }, // アクセスポイントを始められませんでした
    { 0x7a54fa52u, "Eject on the PC before exiting. pico-os cannot use the SD meanwhile" }, // PCで「取り出し」をしてから終了してください。その間pico-osはSDを使えません
    { 0x7b102667u, "Cannot send (disconnected)" }, // 送れません(接続が切れました)
    { 0x7b88fcf9u, "Read" }, // 既読
    { 0x7ba37e6fu, "%d/%d" }, // %d月%d日
    { 0x7c07a65cu, "Playing from %s" }, // 今は%sから鳴ります
    { 0x7c30d67fu, "Cannot read the author signature (files.sig)" }, // 作者の署名(files.sig)を読めません
    { 0x7c327d9fu, "Preparing the mic…" }, // マイクを準備しています…
    { 0x7c552082u, "Could not save the API key (letters, digits, - _ . only)" }, // APIキーを保存できませんでした(英数字と - _ . だけ)
    { 0x7c5cf13bu, "mailbox in mail.cfg has invalid characters" }, // mail.cfg の mailbox に使えない文字があります
    { 0x7c71d93cu, "Screen lock: none" }, // 画面ロック: なし
    { 0x7c8486aeu, "App Store" }, // アプリストア
    { 0x7ca306a2u, "Currency" }, // 通貨(為替)
    { 0x7caa0f70u, "An app with the same name is installed (confirm to overwrite)" }, // 同じ名前のアプリが入っています(上書きの確認が要ります)
    { 0x7cabcc8fu, "no reply from the broker (keepalive)" }, // ブローカの返事が無い(keepalive)
    { 0x7cb067beu, "Cannot name the new note" }, // 新しいメモの名前を作れません
    { 0x7d12ee3eu, "Redo" }, // やり直し
    { 0x7d4fff0eu, "Due %s" }, // 期限 %s
    { 0x7d59f4d2u, "Load state" }, // 途中から再開
    { 0x7d9ae68au, "Done" }, // 完了
    { 0x7de551ecu, "Nightly" }, // 夜間
    { 0x7e373d22u, "Write to the staging area failed" }, // 置き場への書き込みに失敗しました
    { 0x7e5bbfffu, "Search again" }, // 別の語で探す
    { 0x7e7da710u, "Staged content is too long" }, // 置き場の中身が長すぎます
    { 0x7e7f8733u, "Cannot put a folder inside itself" }, // フォルダをその中へは置けません
    { 0x7eb596a4u, "Eject on the PC before exiting.\nExit anyway?" }, // PCで「取り出し」をしてから終了してください。\nこのまま終了しますか?
    { 0x7ecfc451u, "Loop" }, // ループ
    { 0x7f33ce56u, "received a masked frame" }, // マスクされた枠を受けた
    { 0x7f3810a2u, "Back to Settings" }, // 設定へ戻る
    { 0x7f996d36u, "Recording %s" }, // 録音中 %s
    { 0x7fa161efu, "Login" }, // ログイン
    { 0x7fe4c573u, "Update file has a bad size" }, // 更新ファイルの大きさがおかしいです
    { 0x7fe66e39u, "Cancelled on the device" }, // 本体で取り消されました
    { 0x7fef8d25u, "Cannot save the update file" }, // 更新ファイルを保存できません
    { 0x80d4e4fdu, "Sun" }, // 日
    { 0x80d66a35u, "Cannot write to the SD card" }, // SDカードに書き込めません
    { 0x80dfd17eu, "Cannot compute (out of domain)" }, // 計算できません(定義域の外)
    { 0x81318221u, "Cannot open the script (check the path)" }, // スクリプトを開けません(パスを確認してください)
    { 0x81701a13u, "Quality" }, // 音質
    { 0x81721adeu, "Vietnamese dong" }, // ベトナムドン
    { 0x81d2f08cu, "3. Enter the Wi-Fi name and password for the device and send" }, // 3. 本体を接続したいWi-Fiの名前とパスワードを入れて送る
    { 0x81eb0951u, "Step" }, // 1行
    { 0x82006ae3u, "Move" }, // 移動
    { 0x8221e92du, "%s (partial)" }, // %s(途中まで)
    { 0x822833e2u, "byte" }, // バイト
    { 0x82325859u, "No API key. Enter one with [Settings]" }, // APIキーがありません。[設定] から入れてください
    { 0x824954f6u, "Alarm" }, // ｱﾗｰﾑ
    { 0x82642529u, "Off" }, // 切る
    { 0x8268b0d6u, "Search for:" }, // 検索語:
    { 0x8291fd02u, "continuation frame without a start" }, // 始まりの無い継続の枠を受けた
    { 0x829ffe5du, "Edit" }, // 編集
    { 0x82a29195u, "%s ep%02X/%02X\nINQ%lu TUR%lu CAP%lu oth%lu(%02X) err%lu" }, // %s ep%02X/%02X\nINQ%lu TUR%lu CAP%lu 他%lu(%02X) 誤%lu
    { 0x82deb986u, "Analog" }, // アナログ
    { 0x8309ef63u, "Cannot read the sign-in server response (HTTP %d)" }, // サインインのサーバの応答が読めません (HTTP %d)
    { 0x830e9716u, "at %02d:%02d (in %d min)" }, // %02d:%02dから(%d分前)
    { 0x839b18b9u, "Delete %s?\nThis removes all of /lua/apps/%s (including save data)" }, // %s を削除しますか?\n/lua/apps/%s を丸ごと消します(セーブデータ等も消えます)
    { 0x841ae773u, "Cut" }, // 切り取りました
    { 0x845448ecu, "%s/%s, about %s%s free" }, // %s/%s 空き約%s%s
    { 0x846c7aa4u, "\033[93mCould not read the key file\033[0m\r\n" }, // \033[93m鍵ファイルを読めませんでした\033[0m\r\n
    { 0x852b552au, " (withdrawn)" }, //  (取り下げ)
    { 0x856049c3u, "Max 20%, speaker, on battery" }, // 電池では最大20%(スピーカー)
    { 0x858d2213u, "Not a WAV file" }, // WAVではありません
    { 0x85c00471u, "(first %d)" }, // (先頭%d件まで)
    { 0x85c6fff2u, "Stopped" }, // 止まっています
    { 0x85d014f7u, "This app has too many scheduled notices" }, // このアプリの通知の予約が上限です
    { 0x85ecd6fau, "Copied a line" }, // 1行コピーしました
    { 0x86217800u, "Time is up" }, // 時間になりました
    { 0x8651bddfu, "Size %s\nRead %luKB / Write %luKB" }, // 容量 %s\n読み %luKB / 書き %luKB
    { 0x86661f35u, "Off" }, // 切っています
    { 0x866947afu, "Music" }, // ミュージック
    { 0x86788557u, "Exceeds the size limit (32KiB)" }, // 容量の上限(32KiB)を超えます
    { 0x86db69e6u, "2. If nothing opens, open this" }, // 2. 自動で開かなければ次を開く
    { 0x872b29fbu, "Cannot start sign-in (HTTP %d)" }, // サインインを始められません (HTTP %d)
    { 0x878cdc8du, "Data" }, // データ量
    { 0x87b09ee3u, "Reset" }, // 初期化
    { 0x87b731ebu, "Stopped %s" }, // 停止中 %s
    { 0x87bf4f42u, "Wi-Fi Share" }, // Wi-Fi転送
    { 0x87d0629cu, "No recent files" }, // 最近開いたファイルはありません
    { 0x87fcc70au, "Time zone" }, // タイムゾーン
    { 0x8818717fu, "Searching..." }, // 検索中...
    { 0x8820abb4u, "Transcription server (host)\ne.g. 192.168.1.10:8080 / http://pc.local:8080\nLeave empty to go back to the default (OpenAI). To keep it, just press [OK]" }, // 文字起こしの送り先(ホスト)\n例: 192.168.1.10:8080 / http://pc.local:8080\n空欄で既定(OpenAI)に戻す。変えないなら そのまま[決定]
    { 0x88256f1cu, " until" }, //  まで
    { 0x882b6ffdu, "Could not save the token" }, // トークンを保存できませんでした
    { 0x8837677au, "New" }, // 新規
    { 0x884178d2u, "Authentication failed" }, // 認証に失敗しました
    { 0x88fee8dfu, "File not found (removed from the list)" }, // ファイルが見つかりません(一覧から外しました)
    { 0x890e88dbu, "USB: enumerating" }, // USB:列挙待ち
    { 0x891d79c9u, "Stopped: another app used audio" }, // 他のアプリが音を使ったため止まりました
    { 0x8946b352u, "Size: wide" }, // 大きさ: 横長
    { 0x8955a234u, "Token saved" }, // トークンを保存しました
    { 0x897c1924u, "Cannot open the ROM" }, // ROMを開けません
    { 0x89d3cda4u, "Widget" }, // ウィジェット
    { 0x8a0cd64bu, "Roll back" }, // 戻す
    { 0x8a242ca2u, "Put .mml or .wav files in /music/, or make folders (playlists)" }, // /music/ に .mml か .wav を置くか、フォルダ(=プレイリスト)を作ってください
    { 0x8a251b4bu, "Wrong. Wait %lu s" }, // 違います。%lu秒待ってください
    { 0x8a3c59a6u, "yard" }, // ヤード
    { 0x8a55e7acu, "Failed" }, // 失敗しました
    { 0x8a854e6cu, "Broken key exchange reply" }, // 鍵交換の応答が壊れています
    { 0x8aabeaacu, "Hidden apps" }, // 隠したアプリ
    { 0x8ad9cb36u, "Transcribe: waiting for reply…" }, // 文字起こし: 返事を待っています…
    { 0x8aea5860u, "Line %d, col %d: %s" }, // %d行%d列: %s
    { 0x8b006c22u, "No mic signal (check the wiring)" }, // マイクの信号がありません(配線を確認)
    { 0x8b027464u, "Line %d, col %d: " }, // %d行%d列: 
    { 0x8b1377b7u, "server Accept key does not match" }, // サーバの応答の確認(Accept)が合わない
    { 0x8b23c2dbu, "Ran %lu times" }, // %lu回動きました
    { 0x8b57ffbeu, "Todoist is not set up" }, // Todoistが設定されていません
    { 0x8b958157u, "Values to send are too long" }, // 送る値が長すぎます
    { 0x8bef7f3fu, "disconnected" }, // 接続が切れた
    { 0x8c4b130bu, "Choose an app" }, // アプリを選んでください
    { 0x8cc29252u, "Not saved: the app ID is empty" }, // アプリのIDが空なので保存しませんでした
    { 0x8cd1ccf1u, "Could not start: %s" }, // 始められませんでした: %s
    { 0x8cd5478du, "Server does not support uncompressed transport" }, // サーバが圧縮なしの通信に対応していません
    { 0x8ce539a1u, "Nothing today or tomorrow" }, // 今日・明日の予定はありません
    { 0x8ce5b059u, "Cannot name the pasted item" }, // 貼り付け先の名前を作れません
    { 0x8cf4da71u, "Name is too long" }, // 名前が長すぎます
    { 0x8cf7688bu, "This app was withdrawn" }, // 取り下げられたアプリです
    { 0x8d3d7cd9u, "Scan failed" }, // スキャンに失敗しました
    { 0x8d59f370u, "Choose a video" }, // 動画を選んでください
    { 0x8d61906cu, "Password: " }, // パスワード: 
    { 0x8dbd9c0au, "Timer" }, // タイマー
    { 0x8dcacbf8u, "Unsupported video version or size" }, // 対応していない版・大きさの動画です
    { 0x8dff8a49u, "This server does not support search" }, // このサーバは検索に対応していません
    { 0x8e0ff741u, "Change PIN" }, // PINを変える
    { 0x8e1cd927u, "All" }, // 全部
    { 0x8e2741a8u, "Not in the launcher yet (appears after a restart)" }, // ランチャに登録されていません(再起動すると並びます)
    { 0x8e2b22f4u, "Join by code" }, // コードで参加
    { 0x8f389c11u, "%s · Finished" }, // %s · 再生が終わりました
    { 0x8f7ca3a1u, "Clear" }, // 全消去
    { 0x8fc46544u, "Unsupported cartridge" }, // 対応していないカートリッジです
    { 0x8fdc9606u, "Copied" }, // コピーしました
    { 0x8ffb9a1cu, "Cannot re-read the staging area" }, // 置き場を読み直せません
    { 0x90246d25u, "Server error" }, // 相手のサーバの不調です
    { 0x90253202u, "Stopped: paused" }, // 停止: 一時停止
    { 0x9025c93du, "Static IP for \"%s\" (e.g. 192.168.1.50/24, blank for DHCP):" }, // 「%s」の固定IP(例 192.168.1.50/24。空欄でDHCP):
    { 0x902e90d2u, "Needs OS build #%lu or later (this is #%lu)" }, // 本体の更新が要ります(#%lu以降。今は#%lu)
    { 0x90304d0eu, "No notices" }, // 通知はありません
    { 0x9052ef18u, " Flagged as dangerous" }, //  危険と判断されました
    { 0x905ce2e9u, "This URL does not point to a document" }, // 文書を指していないURLです
    { 0x90d9dfc2u, "Hidden apps: none" }, // ランチャで隠したアプリ: なし
    { 0x91551567u, "Firmware updated" }, // ファームを更新しました
    { 0x92012343u, "Alarm %02d:%02d" }, // アラーム %02d:%02d
    { 0x9212fb4au, "%s does not match the list (too large)" }, // %s の中身が一覧と合いません(大きすぎます)
    { 0x921978adu, "Wed" }, // 水
    { 0x9223aa24u, "Discard" }, // 破棄する
    { 0x924102f5u, "Enter current PIN to remove it" }, // PINを外すには今のPINを入力
    { 0x92521932u, "Prev" }, // 前
    { 0x92833865u, "id or version in app.cfg does not match the list" }, // app.cfg の id か版が一覧と合いません
    { 0x92ab1ec4u, "\\u is not followed by hex digits" }, // \\u の後が16進数ではありません
    { 0x92bb66dau, "fluid ounce (US)" }, // 液量オンス(米)
    { 0x92c914b4u, "message too large" }, // メッセージが大きすぎる
    { 0x92dd2a4cu, "Cannot read as WAV" }, // WAVとして読めません
    { 0x92dedde6u, "Jump host could not reach %s:%u (%.*s)" }, // 踏み台から %s:%u へ繋げませんでした(%.*s)
    { 0x935a6cefu, "unsupported transfer encoding" }, // 未対応の転送符号化
    { 0x93b5681eu, "Older than the current version" }, // 今より古い版です
    { 0x93b580a4u, "Waiting for confirmation" }, // 確認を待っています
    { 0x93c6b2bcu, "ROM header is broken" }, // ROMのヘッダが壊れています
    { 0x9410fac7u, "Invite" }, // 招待
    { 0x948014d7u, "No events in 7 days" }, // 7日以内に予定はありません
    { 0x948893a0u, "\033[93mThe key is not ssh-ed25519 and cannot be used\033[0m\r\n" }, // \033[93m鍵の種類が ssh-ed25519 ではないため使えません\033[0m\r\n
    { 0x94b289f8u, "Whole SD" }, // SD全体
    { 0x94c171d4u, "Invalid opcode" }, // 不正な命令
    { 0x950c777eu, "Send failed: " }, // 送れませんでした: 
    { 0x954bc8e9u, "Tmrw %s" }, // 明 %s
    { 0x95665b44u, "Add" }, // 追加
    { 0x964478c5u, "Could not read the server response" }, // サーバの応答を読めませんでした
    { 0x964679ecu, "Portal field: %s%s" }, // ポータルの入力: %s%s
    { 0x969e89a2u, "Re-encrypted %d items%s%s" }, // %d項目を暗号化し直しました%s%s
    { 0x96caf7a9u, "Search term is too long" }, // 検索語が長すぎます
    { 0x96ed0c80u, "Double-tap to cancel" }, // 2回タップで取り消す
    { 0x971b92bbu, "Remove from folder" }, // フォルダから出す
    { 0x9739ed6eu, "Wait %lu s" }, // %lu秒待ってください
    { 0x975fe8d6u, "Replace with (blank to delete)" }, // 置き換える文字列(空なら消す)
    { 0x9784784fu, "Saved %d/%d" }, // 保存済み %d/%d
    { 0x97be02efu, "Large" }, // 大きい字
    { 0x97c5cca4u, "Could not connect (unreachable or port closed)" }, // 接続できませんでした(到達しない/ポートが閉じている)
    { 0x97ca56deu, "On Wi-Fi connect" }, // Wi-Fi接続時
    { 0x97e7ffc0u, "Rate or balance limit reached" }, // 回数か残高の上限です
    { 0x97e917f6u, "You are in no rooms. Find one below or join with a code" }, // 参加している部屋がありません。下のボタンから探すか、参加コードで入れます
    { 0x97ee9f9du, "Double-tap to open" }, // 2回タップで開く
    { 0x9804a23cu, "Delete" }, // 削除する
    { 0x984aeaaeu, "Package header is broken" }, // パッケージの見出しが壊れています
    { 0x98b70a6cu, "IP settings saved" }, // IPの設定を保存しました
    { 0x98e7c231u, "Use up to 63 letters, digits and hyphens (no hyphen at either end)" }, // 名前は英数字とハイフンの63文字までです(先頭と末尾にハイフンは使えません)
    { 0x98ecb219u, "Source file not found" }, // 元のファイルが見つかりません
    { 0x994e1c96u, "no reply from the server (handshake)" }, // サーバの応答が無い(開始の挨拶)
    { 0x99591e6fu, "Enter current PIN" }, // 今のPINを入力
    { 0x99ca3cf3u, "Send failed" }, // 送信に失敗しました
    { 0x9a04b61au, "Staged content is too short" }, // 置き場の中身が短すぎます
    { 0x9a1b7bacu, "Mic" }, // マイク
    { 0x9b075aa4u, "Every %lum" }, // %lu分ごと
    { 0x9b182b55u, "Plot" }, // 描画
    { 0x9b1c629du, "Rename" }, // 名前を変える
    { 0x9b403362u, "\033[91mEnter it as \"user@host[:port]\". To go through a jump host, add \" -J user@jump[:port]\"\033[0m" }, // \033[91m「ユーザー名@ホスト[:ポート]」の形で入力してください。踏み台を通すときは後ろに「 -J ユーザー名@踏み台[:ポート]」\033[0m
    { 0x9b590760u, "Connection closed" }, // 接続を閉じました
    { 0x9b949f7bu, "Failed to write the state" }, // 状態の書き込みに失敗しました
    { 0x9b9e13c9u, "Author key changed (delete the app, then install again)" }, // 作者の鍵が変わりました(アプリを削除してから置き直してください)
    { 0x9baef0aeu, "jo (tatami)" }, // 畳
    { 0x9bc4f4b2u, "File too large (1MiB max)" }, // ファイルが大きすぎます(1MiBまで)
    { 0x9bd910d5u, "Update all (%d)" }, // すべて更新 (%d件)
    { 0x9c04df93u, "broken chunked encoding" }, // chunked転送が壊れている
    { 0x9c073b7du, "Waiting for an update from the PC\n%s" }, // PCからの更新を待っています\n%s
    { 0x9cdb9be3u, "Cancel" }, // 取消
    { 0x9d0ae165u, "Deleted %s" }, // %s を削除しました
    { 0x9d24e86eu, "%d/%d (%s) %d events" }, // %d月%d日(%s) %d件
    { 0x9d2f149fu, "Login failed: %s" }, // ログインできませんでした: %s
    { 0x9d497cf2u, "(no headings or bookmarks)" }, // (見出し・しおりはありません)
    { 0x9d51aba4u, "Home" }, // ホーム
    { 0x9d714c2cu, "Cannot add the task" }, // タスクを追加できません
    { 0x9de7acf8u, "No game running" }, // ゲームが動いていません
    { 0x9e024f22u, "Notes" }, // メモ
    { 0x9e25cb8du, "TODO refresh" }, // TODOの取り直し
    { 0x9e2dc2a3u, "Write to the staging area failed (out of space?)" }, // 置き場への書き込みに失敗しました(空きが足りない?)
    { 0x9e358d2eu, "Recording stopped: %s\n(saved up to %s)" }, // 録音を途中で止めました: %s\n(%s までは保存されています)
    { 0x9e458b44u, "Update" }, // 本体の更新
    { 0x9e755ffcu, "Enter again" }, // もう一度入力
    { 0x9eabeae5u, "10 min" }, // 10分
    { 0x9f3a2f2fu, "Added" }, // 追加しました
    { 0x9f99ee14u, "File name..." }, // ファイル名を入力...
    { 0x9f9ef9e2u, "Cannot read the author signature" }, // 作者の署名を読めません
    { 0x9fb2c6bcu, "Error in a pico.http_request callback" }, // pico.http_requestのコールバックでエラーが発生しました
    { 0x9fc18388u, "Time" }, // 時刻
    { 0x9fc6cfb2u, "Back to LAN" }, // LANへ戻す
    { 0x9fd46ab8u, "Too many open sockets" }, // 同時に開けるソケットの数を超えています
    { 0xa0035cfdu, "From: " }, // 差出人: 
    { 0xa071b856u, "List" }, // 一覧
    { 0xa07641b5u, "Fetched %lum ago (%d)" }, // %lu分前に取得(%d件)
    { 0xa07b8fd8u, "SD card unavailable" }, // SDカードが使えません
    { 0xa1007605u, "server in chat.cfg is not a URL" }, // chat.cfg の server が URL ではありません
    { 0xa111d403u, "ounce" }, // オンス
    { 0xa13eabaau, "Wrong PIN" }, // PINが違います
    { 0xa1975ecbu, "Recording %s\nDropped %lums (slow SD?)" }, // 録音中 %s\n取りこぼし %lums(SDが遅い?)
    { 0xa1a335f9u, "Lock" }, // ロック
    { 0xa1a46e63u, "Delete \"%s\"?" }, // 「%s」を削除しますか?
    { 0xa1eaaee8u, " or less" }, // 以下
    { 0xa2094a36u, "Music" }, // 音楽
    { 0xa214f5c6u, "Yes" }, // はい
    { 0xa22c0934u, "Token (blank to keep):" }, // トークン(空欄で変更なし):
    { 0xa2586e25u, "Save failed" }, // 保存に失敗しました
    { 0xa261b0d3u, "USB setup failed" }, // USBの準備に失敗しました
    { 0xa2790c50u, "control frame fragmented or too long" }, // 制御の枠が分割されている/長すぎる
    { 0xa28040deu, "Storage" }, // 容量
    { 0xa2cb64ecu, "Starting Microsoft sign-in…" }, // Microsoft のサインインを始めています…
    { 0xa2f850e2u, "Clear history" }, // 履歴を消去
    { 0xa33b7107u, "Content is not gzip" }, // 中身がgzipではありません
    { 0xa375beeau, "Battery %d%% (%.2fV)" }, // 電池 %d%% (%.2fV)
    { 0xa3a68b60u, "Australian dollar" }, // 豪ドル
    { 0xa3df92f7u, "New" }, // 新規作成
    { 0xa41bacbau, "%d versions" }, // %d個の版があります
    { 0xa44eccc5u, "Broken key exchange message" }, // 鍵交換のメッセージが壊れています
    { 0xa451019eu, "Location is too long" }, // 場所が長すぎます
    { 0xa47bade6u, "Runtime error in loop()" }, // loop()の実行時エラー
    { 0xa4888a54u, "An app with the same name is installed" }, // 同じ名前のアプリが入っています
    { 0xa4b011bbu, "Total %s" }, // 全体%s
    { 0xa4f35f82u, "Sent. Checking the internet connection" }, // 送信しました。インターネットへ出られるか確かめています
    { 0xa53516c5u, "Calibration removed; back to defaults" }, // 補正を外して、初期の設定に戻しました
    { 0xa58e1c1du, "1 min" }, // 1分
    { 0xa5c0dd3du, "Showing newest %d (of %u)" }, // 新しい %d 通を表示(全 %u 通)
    { 0xa62825d6u, "Replaced all" }, // すべて置換しました
    { 0xa67ea8f7u, "(blank line)" }, // (空行)
    { 0xa6a0ededu, "Skipped %s (tap for details)\n" }, // %s を飛ばしました(タップで全文)\n
    { 0xa6b0a94cu, ", next in %lum" }, // ・次は%lu分後
    { 0xa6e49025u, "Discard" }, // 破棄して戻る
    { 0xa705cb0fu, "Loading..." }, // 読み込み中...
    { 0xa7096126u, "Signature verified" }, // 署名を確かめました
    { 0xa77eaa55u, "Romanian leu" }, // ルーマニアレウ
    { 0xa7c06e58u, "Open it, enter the key" }, // ブラウザで開いてキーを入れる
    { 0xa7c20d1du, "IMAP server (e.g. imap.gmail.com; add :port if not 993):" }, // IMAPサーバ(例: imap.gmail.com。ポートが993以外なら :番号):
    { 0xa7ee00dau, "Connecting to jump host %s:%u...\r\n" }, // 踏み台 %s:%u へ接続しています...\r\n
    { 0xa812ec6cu, "Type" }, // 種類
    { 0xa8251ba8u, "arcsecond" }, // 秒(角度)
    { 0xa8301f03u, "Wi-Fi is not connected" }, // Wi-Fiに繋がっていません
    { 0xa848ea73u, "That name is not allowed" }, // その名前は使えません
    { 0xa85c0c13u, "Output" }, // 出力先
    { 0xa884d8f1u, "Calc" }, // 電卓
    { 0xa8cac8f1u, "Checked %lum ago (%d rooms)" }, // %lu分前に確認(%d部屋)
    { 0xa8f886ddu, "carat" }, // カラット
    { 0xa920a53eu, "Attribute value is not quoted" }, // 属性値が引用符で囲まれていません
    { 0xa93fc28cu, "Cannot read the previous version's file list" }, // 前の版の一覧を読めません
    { 0xa96255cdu, "cannot verify the certificate: " }, // 証明書を確認できない: 
    { 0xa9629697u, "Save" }, // 保存
    { 0xa9638688u, "go" }, // 合
    { 0xa9743eb5u, "server reply is not a WebSocket handshake" }, // サーバの応答がWebSocketの形でない
    { 0xaa1ff9adu, "Pick a version from the Web, choose an update file (.pfw) on the SD, or send one from a PC" }, // Webの版の一覧から選ぶか、更新ファイル(.pfw)をSDから選ぶか、PCから送ってください
    { 0xaa251e4cu, "Headphones" }, // イヤホン
    { 0xaa63e78eu, "DNS (up to 2, comma separated; blank for auto):" }, // DNS(カンマ区切りで2つまで。空欄で自動):
    { 0xaad3503du, "terabyte" }, // テラバイト
    { 0xab38b5bbu, "Speaker" }, // スピーカー
    { 0xab4098b7u, "token is too long" }, // token が長すぎます
    { 0xab888f4cu, "Got an older list than before" }, // 前に取得したものより古い一覧が返されました
    { 0xabc63d25u, "LCD" }, // 画面
    { 0xabd9410eu, "Firmware update failed" }, // ファームを更新できませんでした
    { 0xac01fc6du, "Cannot write to the flash" }, // 内蔵フラッシュへ書けません
    { 0xac06a108u, "\033[93mRe-saved the private key encrypted\033[0m\r\n" }, // \033[93m秘密鍵を暗号化して保存し直しました\033[0m\r\n
    { 0xac1a70c4u, "Date: %d/%02d/%02d (%s) %02d:%02d\n" }, // 日時: %d/%02d/%02d(%s) %02d:%02d\n
    { 0xac3186dbu, "new message started before the last one ended" }, // 前のメッセージが終わる前に次が始まった
    { 0xac321ce6u, "No mail" }, // メールはありません
    { 0xac44c6feu, "Not a valid number" }, // 数値として読めません
    { 0xac5d58e8u, "server reply headers too large" }, // サーバの応答ヘッダが大きすぎる
    { 0xac7c36f0u, "Joining…" }, // 参加しています…
    { 0xac97e38du, "Cancel" }, // 取り消す
    { 0xaca3a146u, "Home: %s" }, // ホーム: %s
    { 0xacc330f6u, "Missing value" }, // 値がありません
    { 0xacce64c1u, "The world is too wide" }, // 世界が広すぎます
    { 0xacd19db3u, "s" }, // 秒
    { 0xacfa12b2u, "The PC build does not rewrite itself (#%lu %s)" }, // PCビルドは本体を書き換えません(#%lu %s)
    { 0xad0ff795u, "On Wi-Fi disconnect" }, // Wi-Fi切断時
    { 0xad4d792fu, "Contents & marks" }, // 目次・しおり
    { 0xad5068b6u, "Snooze 5m" }, // 5分後
    { 0xad82542du, "Game Boy" }, // ゲームボーイ
    { 0xad90f729u, "Size: large" }, // 大きさ: 大
    { 0xada7648au, "File total does not match the list" }, // ファイルの合計が一覧と合いません
    { 0xadb7a50du, "Place: " }, // 場所: 
    { 0xade7fb52u, "An error occurred" }, // エラーが発生しました
    { 0xadec4e51u, "Receive" }, // 受け取る
    { 0xae75ce8au, "Install %s %s?\nAuthor: %s\nPermissions: %s" }, // %s %s を入れますか?\n作者: %s\n使う権限: %s
    { 0xae94968bu, " (password)" }, // (パスワード)
    { 0xaec3fbd4u, "The code expired. Sign in again" }, // コードの期限が切れました。もう一度サインインしてください
    { 0xaeca5359u, "Weekends" }, // 土日
    { 0xaed1c23du, "Set up from phone" }, // スマホから設定
    { 0xaee77846u, "  [on]" }, //   [入]
    { 0xaf0fac79u, "Send" }, // 送信
    { 0xaf56b7f6u, "\r\nConnect to [%s]: " }, // \r\n接続先 [%s]: 
    { 0xaf8e6b0au, "Saving…" }, // 保存中…
    { 0xafac8f54u, "set" }, // 登録済み
    { 0xafbb740au, "turn" }, // 回転
    { 0xafefc472u, "Daily" }, // 毎日
    { 0xb03387c1u, "Expr" }, // 式
    { 0xb074f564u, "Write" }, // 書き込む
    { 0xb0789c92u, "Stop" }, // 停止
    { 0xb07b94ceu, "(no variables)" }, // (変数なし)
    { 0xb08e0ccau, "Attribute is missing '='" }, // 属性に = がありません
    { 0xb08f4d06u, "Cannot delete the old file" }, // 古いファイルを消せない
    { 0xb0943fe9u, "Recording is empty" }, // 録音が空です
    { 0xb0e032f9u, "Cannot build the submit URL" }, // 送り先のURLを作れません
    { 0xb0f91883u, "Control character in string" }, // 文字列に制御文字があります
    { 0xb0fe1df6u, "Cannot read (tap for details)\n" }, // 読めません(タップで全文)\n
    { 0xb13dc812u, "Could not resolve the name: " }, // 名前を解決できませんでした: 
    { 0xb1418341u, "Runtime error in on_back()" }, // on_back()の実行時エラー
    { 0xb14f1b3au, "Cannot read from the SD card" }, // SDカードから読めません
    { 0xb17b41abu, "Stopw" }, // ｽﾄｯﾌﾟ
    { 0xb18e6a83u, "Wi-Fi is off. Turn it on before scanning" }, // Wi-FiがOFFです。ONにしてから検索してください
    { 0xb1bec1d8u, "Flash layout differs from the current firmware (flash over USB)" }, // フラッシュの割り当てが今のファームと違います(USBで書き込んでください)
    { 0xb2161e7fu, "Write server and token in /sys/chat.cfg" }, // /sys/chat.cfg に server と token を書いてください
    { 0xb243e5ceu, "Name to find (partial OK):" }, // 探す名前(一部でよい):
    { 0xb295d2fdu, "Install" }, // 入れる
    { 0xb299d985u, "Stopped: pico.breakpoint()" }, // 停止: pico.breakpoint()
    { 0xb2f1f2ccu, "Nothing yet. Files you open in the editor or Files app appear here" }, // まだありません。エディタやファイルアプリで開いたファイルがここに並びます
    { 0xb32d0322u, "Graph" }, // グラフ
    { 0xb370846au, "chat.cfg has no server and token" }, // chat.cfg に server と token がありません
    { 0xb3a94130u, "Sign-in failed: %s (HTTP %d)" }, // サインインできません: %s (HTTP %d)
    { 0xb3ae5336u, "File list (files.tsv) does not match the app info" }, // ファイル一覧(files.tsv)がアプリの情報と合いません
    { 0xb3b309fau, "Nothing transferred yet" }, // まだ何も送っていません
    { 0xb3da8294u, "Create" }, // 作成
    { 0xb3efad68u, "Tags" }, // タグ
    { 0xb420d6b4u, "main.lua or app.cfg is missing" }, // main.lua か app.cfg がありません
    { 0xb4312e4du, "Cont." }, // 続き
    { 0xb4374f17u, "Stop alarm/timer" }, // アラーム/タイマーを止める
    { 0xb437cbe4u, "Lap" }, // ラップ
    { 0xb441cfe3u, "Volume" }, // 音量
    { 0xb4a781bau, "Received data was tampered with" }, // 受信したデータの改ざんを検出しました
    { 0xb4ad8cd4u, "tonne" }, // トン
    { 0xb53d8ec5u, "Pick a network to delete from the list" }, // 削除するネットワークを一覧から選んでください
    { 0xb55476f4u, "Screenshot failed" }, // スクリーンショット失敗
    { 0xb5858017u, "tsubo" }, // 坪
    { 0xb5b146edu, " (update)" }, //  (更新)
    { 0xb5bfe05bu, "Not checked yet" }, // まだ確認していません
    { 0xb5cdb9d7u, "Battery: USB power" }, // 電池 USB給電中
    { 0xb62272a4u, "Failed to load the ROM" }, // ROMの読み込みに失敗しました
    { 0xb64497f9u, "Base" }, // 進数
    { 0xb64d353au, "Wrong token" }, // トークンが違います
    { 0xb65b25cfu, "Show: %s" }, // 表示: %s
    { 0xb7375890u, "Sleep" }, // スリープ
    { 0xb73cdcb2u, "gigabyte" }, // ギガバイト
    { 0xb745703au, "opts.protocol has invalid characters" }, // opts.protocol に使えない文字があります
    { 0xb79829b6u, "Pass %s" }, // パス %s
    { 0xb7a9005bu, "No screen" }, // 画面がありません
    { 0xb7c56f1bu, "QR: no key" }, // QRならキーは不要
    { 0xb7cc72b0u, "watt-hour" }, // ワット時
    { 0xb84779d0u, "Clock not set" }, // 時計が未設定
    { 0xb883d49bu, " (connected)" }, //  (接続中)
    { 0xb8a217f1u, "Copy" }, // コピー
    { 0xb8a83481u, "Cannot fetch (server returned %d)" }, // 取得できません(サーバが %d を返しました)
    { 0xb8ba936au, "Postponed: low memory" }, // メモリが足りないので延ばしました
    { 0xb8c3a75eu, "Some apps could not be installed" }, // 入れられないアプリがあります
    { 0xb8c47be2u, "Select, Copy/Move, then Paste" }, // 選んでコピー/移動→貼付
    { 0xb92b5ba1u, "Select" }, // 範囲選択
    { 0xb962a4abu, "%s%s is a new host.\r\nED25519 key fingerprint:\r\n%s\r\n" }, // %s%s は初めて接続するホストです。\r\nED25519の鍵の指紋:\r\n%s\r\n
    { 0xb9c16b95u, "arcminute" }, // 分(角度)
    { 0xb9de6364u, "Cannot create the file" }, // ファイルを作れません
    { 0xba0da3beu, "%d to %d" }, // %d件目から%d件
    { 0xba48d7efu, "Author key changed (delete and reinstall to use the new key)" }, // 作者の鍵が変わりました(削除してから入れ直すと、新しい鍵で入ります)
    { 0xba9fc7f3u, "Cannot restore the previous version's files" }, // 前の版のファイルを戻せません
    { 0xba9fd649u, "Password (app password for Gmail; blank to keep):" }, // パスワード(Gmailはアプリパスワード。空欄で変更なし):
    { 0xbaa088edu, "Normal" }, // 通常
    { 0xbabb7d70u, "Never" }, // しない
    { 0xbadb4b7bu, " (skipped)" }, //  (見送り中)
    { 0xbaf46b49u, "Could not allocate a pty" }, // 端末(pty)を確保できませんでした
    { 0xbb1420aau, "Scheduled" }, // 予約
    { 0xbb1583feu, "shaku" }, // 尺
    { 0xbb32f5c9u, "Ready. Restarting to update" }, // 準備ができました。再起動して書き換えます
    { 0xbb464167u, "Joining: %s" }, // 接続しています: %s
    { 0xbbdc1804u, "New folder" }, // 新しいフォルダ
    { 0xbbe7e23cu, "Daily %02u:%02u" }, // 毎日%02u:%02u
    { 0xbc65ca97u, "Attribute without a value" }, // 値のない属性です
    { 0xbcc90d98u, "Dictionary" }, // 辞書
    { 0xbcfd1236u, "Stopped: error" }, // 停止: エラー
    { 0xbd69b625u, "Error in a pico.socket callback" }, // pico.socket のコールバックでエラーが発生しました
    { 0xbd91bee4u, " (newer %s installed)" }, //  (新しい%sが入っています)
    { 0xbe1ec972u, "Cannot install a withdrawn app" }, // 取り下げられたアプリなので入れられません
    { 0xbe75631cu, "kilometer" }, // キロメートル
    { 0xbed6beeau, "Failed to prepare the key exchange" }, // 鍵交換の準備に失敗しました
    { 0xbf12d742u, "Share (stays on)" }, // 転送(裏で続く)
    { 0xbf1fbc8du, "Key exchange out of order" }, // 鍵交換の順序が正しくありません
    { 0xbf5ce00bu, "Signed with an unknown key" }, // 知らない鍵の署名です
    { 0xbfa26026u, "Notes " }, // メモ 
    { 0xbfa3f065u, "Invalid notice" }, // 通知の指定が正しくありません
    { 0xbfd42175u, "Log" }, // 履歴
    { 0xbffb79e4u, "British pound" }, // 英ポンド
    { 0xc0154cf8u, "Close" }, // 切断
    { 0xc02b23d7u, "Digit not valid in this base" }, // この基数では使えない数字です
    { 0xc02d1799u, "gram" }, // グラム
    { 0xc047bc42u, "in %ldm %lds" }, // あと%ld分%ld秒
    { 0xc0563e26u, "Disconnected" }, // 接続が切れました
    { 0xc0a92eb7u, "Rename" }, // 名前の変更
    { 0xc0b9a256u, "Enter y%d (x key for the variable)" }, // y%dの式を入力 (x キーで変数)
    { 0xc15bea5bu, "Alarm" }, // アラーム
    { 0xc17feec7u, "File list (files.tsv) does not match the signed list" }, // ファイル一覧(files.tsv)が一覧の署名と合いません
    { 0xc1c8fa03u, "Length" }, // 長さ
    { 0xc2203225u, "Fetching the list…" }, // 一覧を取っています…
    { 0xc284a0cau, "New task:" }, // 新しいタスク:
    { 0xc28d04f6u, "Web" }, // Webから
    { 0xc2aa0ad7u, "Launcher order reset (folders removed)" }, // ランチャの並びを登録順に戻しました(フォルダも解除)
    { 0xc30905c0u, "Saudi riyal" }, // サウジリヤル
    { 0xc30d5868u, "Resume: " }, // 続きから: 
    { 0xc311beffu, "sun" }, // 寸
    { 0xc31a8886u, "Filter is too long" }, // フィルタが長すぎます
    { 0xc359393cu, "App installed" }, // アプリを入れました
    { 0xc3890afdu, "not set" }, // 未登録
    { 0xc40d96fau, "No calendars" }, // カレンダーがありません
    { 0xc423cec0u, "too many redirects" }, // リダイレクトが多すぎる
    { 0xc429bfc5u, "Conv" }, // 変換
    { 0xc4495b36u, "Could not read: " }, // 読めませんでした: 
    { 0xc46dd057u, "Cannot prepare the destination" }, // 書き込み先を用意できない
    { 0xc595716au, "Server does not support curve25519-sha256 key exchange" }, // サーバが curve25519-sha256 の鍵交換に対応していません
    { 0xc5e3c2bcu, "Updated %d apps" }, // %d件のアプリを更新しました
    { 0xc6a506edu, "Another update is in progress" }, // 別の更新を進めています
    { 0xc6c87ba0u, "Timer" }, // ﾀｲﾏｰ
    { 0xc7277728u, ", " }, // ・
    { 0xc7b8f86bu, "Cannot start sending" }, // 送り始められません
    { 0xc7ddff08u, "app.cfg asks for more permissions than listed" }, // app.cfg の権限が一覧より多くなっています
    { 0xc8314557u, "Background" }, // バックグラウンド
    { 0xc8542c08u, "Cannot save the list" }, // 一覧を保存できません
    { 0xc8a6dc24u, "Start over" }, // 最初から
    { 0xc8a9ac04u, "Save failed" }, // 保存に失敗した
    { 0xc8bd7890u, "Press Start to time" }, // 開始を押すと計測
    { 0xc8c6b02au, "All songs" }, // すべての曲
    { 0xc8e08431u, "Could not read the auth prompt" }, // 認証の問い合わせを読めませんでした
    { 0xc931ed27u, "Saved. This calibration is used from now on" }, // 保存しました。次の起動からもこの補正を使います
    { 0xc99448a4u, "Cannot get an access token (HTTP %d)" }, // アクセストークンを取れません (HTTP %d)
    { 0xca238beau, "kilogram" }, // キログラム
    { 0xca4f4121u, "Zoom+" }, // 拡大
    { 0xca6c014fu, "Malaysian ringgit" }, // マレーシアリンギット
    { 0xca6ff162u, "Server responded %d" }, // サーバの応答が %d です
    { 0xcaa2d716u, "Screenshot" }, // スクリーンショット
    { 0xcac47886u, "Offline" }, // 外へ出られません
    { 0xcb7da42fu, "XML is truncated" }, // XMLが途中で切れています
    { 0xcb902c6fu, "Type here..." }, // ここに入力...
    { 0xcb96e1f9u, "All day" }, // 終日
    { 0xcba30b9du, "(untitled event)" }, // (無題の予定)
    { 0xcba708a0u, "Today" }, // 今日
    { 0xcbad3587u, "Bad author signature" }, // 作者の署名が合いません
    { 0xcbcb5459u, "Icelandic krona" }, // アイスランドクローナ
    { 0xcbfd2f94u, "hectare" }, // ヘクタール
    { 0xcc1d9efcu, "Author key does not match the app info" }, // 作者の署名の鍵がアプリの情報と合いません
    { 0xcc86765au, "No books. Put Aozora Bunko texts (.txt) in /books/ on the SD, or pick one with [Open]" }, // 本がありません。SDの /books/ に青空文庫のテキスト(.txt)を置くか、[開く]から選んでください
    { 0xcca713beu, "Update the OS first (#%lu or later)" }, // 本体の更新が要ります(#%lu以降)
    { 0xccaaa141u, "Remove PIN" }, // PINを外す
    { 0xccb74609u, "meter" }, // メートル
    { 0xccd398ceu, "No song selected" }, // 曲が選ばれていません
    { 0xcd2476a6u, "Too large (over 1MB) or write failed" }, // 大きすぎる(1MB超)か、書き込みに失敗した
    { 0xcd293547u, " (you can retry)" }, // (もう一度試せます)
    { 0xcd37f789u, "User name has invalid characters (ASCII only)" }, // ユーザー名に使えない文字があります(半角英数字と記号だけ)
    { 0xcd3af6e2u, "Touch calibration" }, // タッチ位置の補正
    { 0xcd52d2f5u, "No" }, // 非対応
    { 0xcd55bdf7u, " (%s installed)" }, //  (%s が入っています)
    { 0xcdbd6b24u, "Show frame time" }, // フレーム時間を表示
    { 0xce508d75u, "kilowatt-hour" }, // キロワット時
    { 0xce51a61fu, "Failed" }, // 失敗
    { 0xcf458776u, "Audio" }, // 音
    { 0xcf81dc1cu, "Network" }, // ネットワーク
    { 0xcfa01ce2u, "Installed %s %s" }, // %s %s を入れました
    { 0xcfb80850u, "Update file is too short" }, // 更新ファイルが短すぎます
    { 0xcfca515eu, "USB Drive" }, // USBドライブ
    { 0xcfe53d9fu, "Transfers are not encrypted. Use a trusted Wi-Fi" }, // 暗号化しない送り方です。信頼できるWi-Fiで使ってください
    { 0xd01ee65du, "in %ldh %ldm" }, // あと%ld時間%ld分
    { 0xd0204f64u, "Select a file or folder first" }, // 先にファイルかフォルダを選んでください
    { 0xd04a6a87u, "are" }, // アール
    { 0xd04a8b5du, "Checking…" }, // 確認しています…
    { 0xd0612776u, "milliliter" }, // ミリリットル
    { 0xd0bcd584u, "Re-read content does not match" }, // 置き場から読み直した中身が合いません
    { 0xd0bdacc2u, "Tap to select, tap again to complete" }, // タップで選択、もう一度タップで完了
    { 0xd0c69898u, "The page has no form (JavaScript-based portals are not supported)" }, // フォームが無いページです(JavaScriptで動くポータルは使えません)
    { 0xd0c87c79u, "Connected to the PC" }, // PCにつながっています
    { 0xd0d1e09fu, "URL is too long" }, // URLが長すぎます
    { 0xd11c6332u, "Cannot install: a different app has this name" }, // 同じ名前の別のアプリが入っているので入れられません
    { 0xd11d49fbu, "Author key changed. Delete it, then install again" }, // 作者の鍵が変わりました。削除してから入れ直してください
    { 0xd125d7c2u, "%s · Stopped" }, // %s · 停止中
    { 0xd133e30du, "Update file is empty" }, // 更新ファイルが空です
    { 0xd138ae5cu, "Cannot get the message" }, // メールを取れません
    { 0xd1cb6ad1u, "Ready (Space / r to record)" }, // 待機中(Space / r で録音)
    { 0xd1d9a39bu, "Cannot read the portal URL" }, // ポータルのURLを読めません
    { 0xd205073eu, "Type: %s" }, // 種類: %s
    { 0xd23d841du, "Missing ':'" }, // ':' がありません
    { 0xd2af9ed8u, "Create" }, // 作る
    { 0xd3143699u, "Could not move" }, // 移動できませんでした
    { 0xd325bb46u, "gradian" }, // グラード
    { 0xd3338cb3u, "Swiss franc" }, // スイスフラン
    { 0xd349f15eu, "Stopped" }, // 止めました
    { 0xd3842d92u, " from\n" }, //  から\n
    { 0xd3b17087u, "Not enough memory to read the script" }, // スクリプトを読むメモリが足りません
    { 0xd3db420bu, "Cannot reach the sign-in server" }, // サインインのサーバへ送れません
    { 0xd3ddb20eu, "Authentication failed (server accepts: %s)" }, // 認証に失敗しました(サーバが受け付ける方式: %s)
    { 0xd40ea558u, "Could not open a shell" }, // シェルを開けませんでした
    { 0xd4274235u, "Invalid read" }, // 不正な読み出し
    { 0xd43695b7u, "Updated %d, %d failed (%s)" }, // %d件を更新、%d件は失敗 (%s)
    { 0xd4585243u, "Text" }, // テキスト
    { 0xd4616118u, "Cannot start listening" }, // 待ち受けを始められません
    { 0xd494c2ebu, "Task name is too long" }, // タスクの名前が長すぎます
    { 0xd4c3963du, "Password has invalid characters (ASCII only)" }, // パスワードに使えない文字があります(半角英数字と記号だけ)
    { 0xd4f9d0bcu, "Change" }, // 変更
    { 0xd516f289u, "Searching… %d%% (%d)" }, // 検索中… %d%%(%d件)
    { 0xd55e0655u, "Size: small" }, // 大きさ: 小
    { 0xd5622895u, "Russian ruble" }, // ロシアルーブル
    { 0xd5928a55u, "Error (nil)" }, // エラー(nil)
    { 0xd5964fc2u, "pound" }, // ポンド
    { 0xd5a9d452u, "Due (e.g. tomorrow 3pm; blank for none):" }, // 期限(例: 明日 15時。空欄で無し):
    { 0xd5c4d115u, "received an unknown frame type" }, // 知らない種類の枠を受けた
    { 0xd5e1682fu, "Lua failed to start (possibly out of memory)" }, // Luaの初期化に失敗しました(メモリ不足の可能性があります)
    { 0xd65470f7u, "Mach (15°C)" }, // マッハ(15°C)
    { 0xd66af443u, "Unknown value" }, // 知らない値です
    { 0xd70e0e32u, "No account. Add it in [Setup]" }, // メールのアカウントがありません。[設定] から入れてください
    { 0xd7475b09u, " - please delete (danger)" }, // を削除してください(危険)
    { 0xd774dc03u, "Pause" }, // 一時停止
    { 0xd7960761u, "Save state" }, // 途中保存
    { 0xd84b29cfu, "Cannot start the download" }, // 取得を始められません
    { 0xd870230du, "Saved %d/%d (double-tap to join)" }, // 保存済み %d/%d (2回タップで接続)
    { 0xd899c681u, "Unexpected key exchange reply" }, // 想定外の鍵交換の応答が届きました
    { 0xd8f29346u, "Thai baht" }, // タイバーツ
    { 0xd92ee843u, "No messages yet" }, // まだ発言がありません
    { 0xd967fbc2u, "in %lds" }, // あと%ld秒
    { 0xd98e7ce9u, "You have unsaved changes. Discard and open another file?" }, // 保存していない変更があります。破棄して別のファイルを開きますか?
    { 0xd9b9eb45u, "Cut" }, // 切り取り
    { 0xd9bb519fu, "\n\nNote: %s" }, // \n\n注意: %s
    { 0xd9f51766u, "clock not set (before NTP sync)" }, // 時計が合っていない(NTP同期前)
    { 0xda042796u, "Server does not support %s" }, // サーバが %s に対応していません
    { 0xda3567ebu, "Canadian dollar" }, // カナダドル
    { 0xda5ad380u, "Out" }, // 抜ける
    { 0xda86a451u, "NZ dollar" }, // NZドル
    { 0xdaa0585au, "Content does not match the header (broken)" }, // 中身が見出しと合いません(壊れています)
    { 0xdaa59d29u, "No microphone permission" }, // マイクの権限がありません
    { 0xdb757407u, "Author key does not match the list" }, // 作者の署名の鍵が一覧と合いません
    { 0xdb7622d1u, "Closing the app" }, // アプリを閉じています
    { 0xdb78d2bcu, "Cannot open the staging area (flash)" }, // 置き場(内蔵フラッシュ)を開けません
    { 0xdc1fcbc2u, "No flash area for ROMs" }, // ROMを置く内蔵フラッシュの領域がありません
    { 0xdc37dec2u, "The device key already exists" }, // 端末の鍵はもうあります
    { 0xdc93e2f8u, "%s does not match the list" }, // %s の中身が一覧と合いません
    { 0xdcb6280au, "%d+ (narrow it down)" }, // %d件以上(絞り込み推奨)
    { 0xdd01716cu, "Bad signature (broken or tampered)" }, // 署名が合いません(壊れているか、書き換えられています)
    { 0xdd576738u, "Does not look like an SSH server" }, // SSHサーバではないようです
    { 0xdd9803c0u, "Room name to find:" }, // 探す部屋の名前:
    { 0xddded743u, "unsigned" }, // 無署名
    { 0xdde0f365u, "State loaded" }, // 途中から再開しました
    { 0xddf43b01u, "Analog clock" }, // アナログ時計
    { 0xde0983efu, "Does not fit in 64 bits" }, // 64bitに収まりません
    { 0xde18f5afu, "square centimeter" }, // 平方センチメートル
    { 0xde1d6dc4u, "Hong Kong dollar" }, // 香港ドル
    { 0xde899e03u, "Rescan" }, // 再スキャン
    { 0xde927183u, "Password (app password for Gmail):" }, // パスワード(Gmailはアプリパスワード):
    { 0xdeec8a66u, "Cannot listen on UDP (port in use?)" }, // UDPの待ち受けを始められません(番号が使用中?)
    { 0xdf992696u, "Stopped: step" }, // 停止: ステップ
    { 0xdfbbd691u, "no response" }, // 応答がない
    { 0xdfc7af1du, "Lua hot reload" }, // Luaのホットリロード
    { 0xdfe6a002u, "App (client) ID registered in Microsoft Entra:" }, // アプリ(クライアント)ID(Microsoft Entra に登録したもの):
    { 0xe06a97f4u, "Message" }, // メッセージ
    { 0xe0dacfddu, "feet per second" }, // フィート毎秒
    { 0xe13ce3eeu, "App updates available" }, // アプリの更新があります
    { 0xe15e5a11u, "Recording too large (%luMB; 25MB max)" }, // 録音が大きすぎます(%luMB。上限25MB)
    { 0xe176a8b2u, "Could not save (check the SD card)" }, // 保存できませんでした(SDカードを確認してください)
    { 0xe18c11ebu, "Wait for the previous action to finish" }, // 前の操作が終わるまで待ってください
    { 0xe1b4699fu, "ROM too large (256KB max in RAM)" }, // ROMが大きすぎます(RAMに載るのは256KBまで)
    { 0xe1d6787du, "%s@%s %s" }, // %s@%s の%s
    { 0xe206e2e8u, "Checks installed apps for updates once a day" }, // 1日1回、入っているアプリの更新を確かめます
    { 0xe222959bu, "Settings" }, // 設定
    { 0xe29dfa50u, "Israeli shekel" }, // イスラエルシェケル
    { 0xe2b55d7eu, "atmosphere" }, // 気圧
    { 0xe324e4adu, "Silent" }, // 音なし
    { 0xe3501e02u, "Adding…" }, // 追加しています…
    { 0xe3797261u, "Completing…" }, // 完了にしています…
    { 0xe3e38f64u, "Search for:" }, // 探す言葉:
    { 0xe42242d9u, "Firmware for a different board" }, // 別の基板向けのファームです
    { 0xe4395c21u, "Lua syntax error" }, // Luaスクリプトの構文エラー
    { 0xe4d98c0fu, "Digits" }, // 数字
    { 0xe506dfaau, "(untitled)" }, // (無題)
    { 0xe56b694du, "Background: %d" }, // バックグラウンド: %d件
    { 0xe58228efu, "Key exchange requested during key exchange" }, // 鍵交換の途中で鍵交換を求められました
    { 0xe5b2e9bfu, "Ejected on the PC. You can exit" }, // PCで取り出されました。終了してよいです
    { 0xe627caa1u, "cup (US)" }, // カップ(米)
    { 0xe63063c3u, "Device key created. Re-encrypted %d items%s%s" }, // 端末の鍵を作りました。%d項目を暗号化し直しました%s%s
    { 0xe63491bau, "Recording too large" }, // 録音が大きすぎます
    { 0xe6541da4u, "no reply from the broker (CONNACK)" }, // ブローカの返事が無い(CONNACK)
    { 0xe6a9f40du, "Cannot read app.cfg" }, // app.cfg を読めません
    { 0xe7b3a142u, "Emulator stopped: " }, // エミュが停止しました: 
    { 0xe7c019a3u, "Update file was cut off" }, // 更新ファイルが途中で切れました
    { 0xe7dac0dcu, "Completed" }, // 完了にしました
    { 0xe87c2a74u, "mail.cfg has no oauth-client-id. Add it in [Setup]" }, // mail.cfg に oauth-client-id がありません。[設定] から入れてください
    { 0xe88c5d90u, "Size limit reached (32KiB)" }, // 容量の上限(32KiB)です
    { 0xe895e084u, "Could not open the file" }, // ファイルを開けませんでした
    { 0xe8afb62au, "Unexpected NEWKEYS" }, // 想定外の NEWKEYS が届きました
    { 0xe8cb1cf2u, "A different app with the same name is installed" }, // 同じ名前の別のアプリが入っています
    { 0xe91684cau, "(no subject)" }, // (件名なし)
    { 0xe9252494u, "The port is not available" }, // 待ち受けの番号(ポート)が使えません
    { 0xe9490db7u, "Add to folder" }, // フォルダへ入れる
    { 0xe95029e7u, "broken" }, // 壊れています
    { 0xe95b2c8cu, "Stop" }, // 止める
    { 0xe964f6a4u, "Rolled %s back to %s" }, // %s を %s へ戻しました
    { 0xe9657a43u, "\r\n\033[93mDisconnected\033[0m\r\n" }, // \r\n\033[93m切断しました\033[0m\r\n
    { 0xe9699ddcu, "Script too large (16KiB max)" }, // スクリプトが大きすぎます(16KiBまで)
    { 0xe9ebf0bfu, "Cannot start the request" }, // 要求を始められません
    { 0xe9f5d906u, "%s · Stopped: another app used audio" }, // %s · 他のアプリが音を使ったため止まりました
    { 0xea385094u, "1. Scan the QR or join the Wi-Fi" }, // 1. QRを読むか、Wi-Fiで接続
    { 0xea4a60e9u, "Expression too complex" }, // 式が複雑すぎます
    { 0xeac6e494u, "Files" }, // ファイル
    { 0xeaf23f4cu, " (connecting)" }, //  (接続しています)
    { 0xeb44c1f5u, "Saved to the DS3231 when set" }, // 合わせるとDS3231へ控えます
    { 0xeb5f2c1au, "Once" }, // 1回
    { 0xeb6063acu, "(old)" }, // (古)
    { 0xeb82e305u, "ROM too large (flash holds up to %luKB)" }, // ROMが大きすぎます(内蔵フラッシュに置けるのは%luKBまで)
    { 0xeb8b6718u, "Hidden apps: %d" }, // ランチャで隠したアプリ: %d
    { 0xebab40f0u, "Phone" }, // スマホ
    { 0xebb52480u, "Run" }, // 実行
    { 0xebcef66eu, "Cannot start the request" }, // リクエストを開始できない
    { 0xebf00081u, "Accept the terms of the \"%s\" portal and connect?" }, // 「%s」のポータルの利用規約に同意して接続しますか?
    { 0xebf066c3u, "no reply from the server (ping)" }, // サーバの応答が無い(pingに答えない)
    { 0xec0469beu, "State is from another ROM" }, // 別のROMの状態です
    { 0xec3203c4u, "Cannot install an older version" }, // 入っている版より古い版は入れられません
    { 0xecc2fd9fu, "Resume" }, // 再開
    { 0xecd6de5du, "This app was flagged as dangerous" }, // 危険と判断されたアプリです
    { 0xed2abcc1u, "Remove" }, // 外す
    { 0xed5bc620u, "Details" }, // 詳細
    { 0xed8ee91eu, "Not a package" }, // パッケージではありません
    { 0xed9b4328u, "Date" }, // 日付
    { 0xeda13825u, "A line is too long to open (191 bytes max)" }, // 1行が長すぎて開けません(191バイトまで)
    { 0xeda3fadfu, "Up to %d can be placed" }, // 置けるのは %d 個までです
    { 0xeda8001fu, "created" }, // 作成済み
    { 0xedbe7b6cu, "Weekly" }, // 毎週
    { 0xedce448eu, "Type a word and press Search" }, // 語句を入力し検索を押す
    { 0xeded1ff7u, "Bright" }, // 輝度
    { 0xee0afae7u, "Turkish lira" }, // トルコリラ
    { 0xee0d2105u, "Out of memory" }, // メモリが足りません
    { 0xee9e0c64u, "You have unsaved changes. Discard and go back?" }, // 保存していない変更があります。破棄して戻りますか?
    { 0xeef69d05u, "write failed" }, // 書き込みに失敗
    { 0xef47ff57u, "Update file is larger than listed" }, // 更新ファイルが一覧の大きさより大きいです
    { 0xefd23e36u, "Gateway (router) IP (blank for none):" }, // ゲートウェイ(ルーター)のIP(空欄で無し):
    { 0xefd63865u, "To: " }, // 宛先: 
    { 0xf015d76du, "Small" }, // 小
    { 0xf0919720u, "Fetching the list…" }, // 一覧を取得中…
    { 0xf0d817ebu, "Jump: " }, // 踏み台: 
    { 0xf0e495f5u, "Same as the current version" }, // 今と同じ版です
    { 0xf0f6765cu, "Cannot read after '<'" }, // '<' の後ろが読めません
    { 0xf134fafdu, "\033[91mWARNING: the host key of %s has changed!\r\nSomeone may be impersonating it. If you expected this,\r\nremove the matching line from %s.\r\nCurrent key: %s\033[0m\r\n" }, // \033[91m警告: %s のホスト鍵が以前と違います!\r\nなりすましの可能性があります。心当たりがあれば\r\n%s から該当する行を消してください。\r\n今の鍵: %s\033[0m\r\n
    { 0xf157f6c3u, "Path is too long" }, // パスが長すぎます
    { 0xf1989d68u, "Taiwan dollar" }, // 台湾ドル
    { 0xf1a887dau, "Trust this host and connect? (yes/no): " }, // このホストを信頼して接続しますか? (yes/no): 
    { 0xf1ae3015u, "(no body)" }, // (本文がありません)
    { 0xf1b4fdcdu, "Issuing an invite code…" }, // 参加コードを発行しています…
    { 0xf1b6d662u, "\n\nAn app with this name is in /lua/apps/%s. It will be overwritten (files the app wrote are kept)" }, // \n\n/lua/apps/%s に同じ名前のアプリがあります。上書きします(アプリが書いたファイルは残ります)
    { 0xf1bae7aau, "Defaults" }, // 既定に戻す
    { 0xf1d0c021u, "received an unknown control frame" }, // 知らない制御の枠を受けた
    { 0xf1f684f2u, "Not a firmware update file" }, // ファームの更新ファイルではありません
    { 0xf2209dd9u, "Join" }, // 接続
    { 0xf230e877u, "  [off]" }, //   [切]
    { 0xf23c76beu, "Source URL:" }, // 配布元のURL:
    { 0xf26ee67eu, "Cannot read the source" }, // 取得元を読めない
    { 0xf28e4a1du, "Sending the previous message" }, // 前の発言を送っています
    { 0xf291d5aeu, "model / language / prompt in transcribe.cfg has invalid characters" }, // transcribe.cfg の model / language / prompt に使えない文字があります
    { 0xf29e6e9eu, "Wi-Fi disconnected" }, // Wi-Fiが切れました
    { 0xf301f499u, "No network permission" }, // ネットワークの権限がありません
    { 0xf30a06acu, "Conversion type" }, // 変換の種類
    { 0xf32890acu, "Unknown" }, // 不明
    { 0xf3743f81u, "Dark theme" }, // ダークテーマ
    { 0xf37a73f8u, "Reset" }, // リセット
    { 0xf3a85ce4u, "No match. Start with the new PIN" }, // 一致しません。新しいPINから
    { 0xf3c8128cu, "\" cannot be opened" }, // 」を開けません
    { 0xf3d0351au, "Internal flash" }, // 内蔵フラッシュ
    { 0xf3ef6062u, "%d items, total %s%s" }, // %d件 合計%s%s
    { 0xf3f2b105u, "In a browser on a phone or PC, open\n" }, // スマホかパソコンのブラウザで\n
    { 0xf43ca391u, "kilojoule" }, // キロジュール
    { 0xf4d917c1u, "kilocalorie" }, // キロカロリー
    { 0xf52feee4u, "Store" }, // ストア
    { 0xf546340bu, "Tomorrow" }, // 明日
    { 0xf555b5c2u, "Could not set the time" }, // 時刻を合わせられませんでした
    { 0xf5fae8fbu, "(no sound)" }, // (音は出ません)
    { 0xf61f9160u, "Install folder name is too long" }, // インストール先の名前が長すぎます
    { 0xf65cf4c6u, "Todoist token (blank to keep):" }, // Todoistのトークン(空欄で変更なし):
    { 0xf68cdadeu, "None" }, // なし
    { 0xf6ae6069u, "SD" }, // SDから
    { 0xf706de96u, "Set time with ▲▼" }, // ▲▼で時間を設定
    { 0xf77a3bacu, "Fahrenheit" }, // 華氏
    { 0xf78f2923u, "Replace" }, // 置換
    { 0xf7d21303u, "cannot connect" }, // 接続できない
    { 0xf7fe86c5u, "Microsoft email address (e.g. you@outlook.com):" }, // Microsoft のメールアドレス(例: you@outlook.jp):
    { 0xf80775cbu, "cubic meter" }, // 立方メートル
    { 0xf82fcd9eu, "1/1 (Mon)" }, // 1月1日(月)
    { 0xf83c6a84u, "API key is too long" }, // APIキーが長すぎます
    { 0xf8a948fdu, "Singapore dollar" }, // シンガポールドル
    { 0xf8fc668bu, "Invalid name (letters, digits, _ - only; up to 20)" }, // 使えない名前です(英数字と _ - のみ、20文字まで)
    { 0xf927114eu, "Update file is truncated" }, // 更新ファイルが途中で切れています
    { 0xf987aee5u, "Cannot open the save location" }, // 保存先を開けない
    { 0xf9f6a8b2u, "Quiet" }, // 控えめ
    { 0xfa04446du, "Too short for a ROM" }, // ROMとして短すぎます
    { 0xfa39b659u, "System" }, // システム
    { 0xfa8380c4u, "%s" }, // %s曜日
    { 0xfa97e01fu, "Complete \"%s\"?" }, // 「%s」を完了にしますか?
    { 0xfab73a34u, "meters per second" }, // メートル毎秒
    { 0xfadf3557u, "Could not copy" }, // コピーできませんでした
    { 0xfb2dc3bdu, "Confirm on the device" }, // 本体で確認してください
    { 0xfb4b313cu, "Stable" }, // 安定
    { 0xfb55c7b3u, "Reading the update file" }, // 更新ファイルを読んでいます
    { 0xfb5887f1u, "miles per hour" }, // マイル毎時
    { 0xfb8c774bu, "No previous version is kept" }, // 前の版が残っていません
    { 0xfbc63018u, "Search failed (%d)" }, // 検索に失敗しました (%d)
    { 0xfbf2847au, "Cannot write the header to flash" }, // 内蔵フラッシュへ見出しを書けません
    { 0xfbfcf1b7u, "Token has invalid characters" }, // トークンに使えない文字があります
    { 0xfc144586u, "Back" }, // 戻る
    { 0xfc3ad3a2u, "API key has invalid characters or is too long" }, // APIキーに使えない文字があるか、長すぎます
    { 0xfc55599eu, "Save path too long" }, // 保存先のパスが長すぎる
    { 0xfc7e9319u, "5 min" }, // 5分
    { 0xfcb6c3fbu, "Cannot open the book (check the SD card and file)" }, // 本を開けません(SDカードとファイルを確かめてください)
    { 0xfcdb59a8u, "Search again" }, // 再検索
    { 0xfd09a5b8u, "Fri" }, // 金
    { 0xfd3eae35u, "Stopped: breakpoint" }, // 停止: ブレークポイント
    { 0xfda834dcu, "/sys/chat.cfg not found" }, // /sys/chat.cfg がありません
    { 0xfdae20a7u, "bad status line" }, // ステータス行が不正
    { 0xfdb81a25u, "Account type" }, // アカウントの種類
    { 0xfdbfe375u, "Mon" }, // 月
    { 0xfde8539eu, "Which folder?" }, // どのフォルダへ
    { 0xfe407283u, "Loading… %d" }, // 読み込み中… %d件
    { 0xfe505ffdu, "Enter up to 2 DNS servers like \"1.1.1.1,8.8.8.8\"" }, // DNSは「1.1.1.1,8.8.8.8」のように2つまで入れてください
    { 0xfe62bab2u, "%u min left" }, // あと %u 分
    { 0xfebd868bu, "token in chat.cfg has invalid characters" }, // chat.cfg の token に使えない文字があります
    { 0xff2e078fu, "bad redirect" }, // 転送先が不正
    { 0xff52605cu, "Press Start to count down" }, // 開始を押すとカウントダウン
    { 0xff61d346u, "Permission denied" }, // 権限がありません
    { 0xff87fd58u, "Browser home URL" }, // ブラウザのホームURL
    { 0xffaf61beu, "Cannot check for new mail" }, // 新着を確かめられません
    { 0xffdae38bu, "Fetching %s…" }, // %s を取得中…
    { 0xfff3714fu, "No" }, // いいえ
};
const size_t I18nFunctions::kTableEnCount = 1463;
