// 生成物: python3 script/i18n.py update が i18n/ の .po から作る。手で直さない(docs/design/i18n.md)
#include "system/I18n_Functions.hpp"

// hash の昇順(I18nFunctions::Lookup() が二分探索する)
const I18nFunctions::Entry I18nFunctions::kTableEn[] = {
    { 0x0073e150u, "Name" }, // 名前
    { 0x0149b128u, "SSID of the Wi-Fi to add" }, // 追加するWi-FiのSSID
    { 0x01658a21u, "Auto" }, // 自動
    { 0x02031fe0u, "Use 4-8 digits" }, // 数字4〜8桁にしてください
    { 0x02c0e6dfu, "Lock now" }, // 今すぐロック
    { 0x02ddaa3au, "Cancel" }, // やめる
    { 0x03259dcfu, "Not saved: the network was deleted" }, // ネットワークが削除されたので保存しませんでした
    { 0x038e2fbfu, "Kept by the DS3231 when off" }, // DS3231で電源を切っても保持
    { 0x03b5a925u, "Set up in the TODO app" }, // TODOアプリで設定してください
    { 0x04565bfdu, "OK" }, // 決定
    { 0x0460ca52u, "Del" }, // 消す
    { 0x046b263du, "Enter the IP address like \"192.168.1.50/24\"" }, // IPアドレスは「192.168.1.50/24」の形で入れてください
    { 0x04d51a72u, "Next" }, // 次へ
    { 0x055dfa42u, "Device key: %s" }, // 端末の鍵: %s
    { 0x0648c213u, "Time on %d/%d" }, // %d/%d の時刻
    { 0x068e88f5u, "Could not save the IP settings" }, // IPの設定を保存できませんでした
    { 0x07bec7adu, "Reset order" }, // 並びを元に戻す
    { 0x098e4278u, "Create a new folder\nName:" }, // 新しくフォルダを作成\n名前:
    { 0x0a86d43fu, "NTP server 2" }, // NTPサーバー2
    { 0x0b86d5d2u, "NTP server 1" }, // NTPサーバー1
    { 0x0b931eeau, "Tue" }, // 火
    { 0x0c7134a5u, "Overwritten by NTP sync" }, // NTP同期で上書きされます
    { 0x0d40a828u, "Firmware without audio" }, // 音声出力なしのファームです
    { 0x0dd194e4u, "Scanning..." }, // スキャン中...
    { 0x0ea9e22du, "Developer" }, // 開発者向け
    { 0x0fabb517u, "Clock is not set" }, // 時計が合っていません
    { 0x108d2ed8u, "Login" }, // 要ログイン
    { 0x113f114au, "Other" }, // 他
    { 0x1145e333u, "No tasks today" }, // 今日のタスクはありません
    { 0x12b5f10au, "Calib." }, // 補正
    { 0x1377c0b1u, "NTP server 2: %s" }, // NTPサーバー2: %s
    { 0x13addcefu, "30 min" }, // 30分
    { 0x16de3f1eu, "Found %d" }, // %d件見つかりました
    { 0x19dbf6e2u, "Chat" }, // チャット
    { 0x1af16218u, "None found" }, // 見つかりませんでした
    { 0x1b048a70u, "Calendar" }, // カレンダー
    { 0x1d191730u, "Move back" }, // 前へ移動
    { 0x1d97262fu, "Success" }, // 成功
    { 0x1dc015d5u, "Thu" }, // 木
    { 0x1e5e95cau, "2 min" }, // 2分
    { 0x1ef5f75bu, "Really delete?" }, // 本当に削除しますか?
    { 0x1fd67942u, " (some failed; see the log)" }, // (失敗あり。ログを見てください)
    { 0x21a8ce61u, "Arithmetic" }, // 四則演算
    { 0x23d8f00du, "Up to %d networks can be saved. Delete one you don't need" }, // 保存できるのは%d件までです。不要なものを削除してください
    { 0x24620418u, "Already a USB drive" }, // すでにUSBドライブになっています
    { 0x24fe0d6cu, "NTP server 1: %s" }, // NTPサーバー1: %s
    { 0x28bdd93eu, "Hide from launcher" }, // ランチャから隠す
    { 0x29321171u, "New PIN (4-8 digits)" }, // 新しいPIN(数字4〜8桁)
    { 0x29750bcau, "Clock" }, // 時計
    { 0x2a465f59u, "Tmrw %d:%02d" }, // 明 %d:%02d
    { 0x2a9fe8a8u, "Set PIN" }, // PINを決める
    { 0x2c213c8fu, "No apps registered.\nAdd them in Setup() of App_List.cpp." }, // アプリが登録されていません。\nApp_List.cpp のSetup()へ追加してください。
    { 0x2ca5152bu, "Folder name:" }, // フォルダ名:
    { 0x2dd81fb6u, "(not set)" }, // (未設定)
    { 0x2f93c9a5u, "Sending to the portal..." }, // ポータルへ送っています...
    { 0x311cb483u, "Kept across restarts" }, // 再起動では消えません
    { 0x31dd5e2bu, "Pick a network to join from the list" }, // 接続するネットワークを一覧から選んでください
    { 0x335fc0ccu, "Tap to choose..." }, // タップして選択...
    { 0x342ed250u, "Scan" }, // 検索
    { 0x348fc60fu, "Move to top" }, // 先頭へ移動
    { 0x361d66e8u, "Cancel" }, // キャンセル
    { 0x36aa7f99u, "Restart when frozen" }, // 固まったら再起動する
    { 0x3749d1e3u, "Reader" }, // 読書
    { 0x37f6a0b1u, "Large" }, // 大
    { 0x39d8f9e3u, "SSID or password too long (password: 64 chars max)" }, // SSIDかパスワードが長すぎます(パスワードは64文字まで)
    { 0x3b9a64f1u, "Math" }, // 数学記号
    { 0x3beca0e8u, "Too many scheduled notices" }, // 通知の予約がいっぱいです
    { 0x3c379679u, "Device name (found as <name>.local; letters, digits, hyphens)" }, // 本体の名前(<名前>.local で見つかる。英数字とハイフン)
    { 0x3eb3fd6fu, "%s.local\n(no IP)" }, // %s.local\n(IP無し)
    { 0x3efcfdbfu, "Notices" }, // 通知
    { 0x3f29cdebu, ". The PIN follows at next unlock" }, // 。PINは次に解除したとき
    { 0x3fccebd1u, "DNS (up to 2, comma separated; blank for gateway):" }, // DNS(カンマ区切りで2つまで。空欄でゲートウェイ):
    { 0x41598718u, "Creates a device key and writes it to the OTP.\n- OTP cannot be erased (no undo)\n- Wi-Fi, SSH keys, tokens, calendar URLs and the PIN are re-encrypted with this key\n- If the board breaks, they can never be read again. Keep copies on a PC\n- This only protects against a stolen SD card (not the whole device)" }, // 端末の鍵を作り、本体のOTPへ書き込みます。\n・OTPは消せません(取り消せません)\n・Wi-Fi・SSHの鍵・トークン・カレンダーのURL・PINを、この本体の鍵で暗号化し直します\n・基板が壊れると、それらは二度と読めません。元の値はPCに控えてください\n・守れるのはSDだけを盗まれた場合です(本体ごとは守れません)
    { 0x41915c69u, "%s not connected" }, // %sがつながっていません
    { 0x42ba4a91u, "No SD card" }, // SDカードがありません
    { 0x4416815du, "Normal" }, // 標準の字
    { 0x4433d4dfu, "Close" }, // 閉じる
    { 0x45141b0fu, "The gateway must be in the same subnet as the IP address" }, // ゲートウェイはIPアドレスと同じサブネットのアドレスにしてください
    { 0x453b4b47u, "Disconnected" }, // 未接続
    { 0x4833fbefu, "Run self-test at boot" }, // 起動時に自己診断を実行
    { 0x4835fe9au, "Wide" }, // 横長
    { 0x497392bau, "Loading the portal page..." }, // ポータルのページを読んでいます...
    { 0x4ae33f8eu, "Background: none" }, // バックグラウンド: なし
    { 0x4b6322ceu, "Ungroup folder" }, // フォルダを解除
    { 0x4c82a4edu, "No free file number" }, // 空き番号がありません
    { 0x4d43a3e1u, "Back to DHCP" }, // DHCPに戻しました
    { 0x4e064232u, "Restart" }, // 再起動
    { 0x4e7bb777u, "Recorder" }, // 録音
    { 0x4f95f25du, "This network needs no login" }, // ログインの要るネットワークではありません
    { 0x50b97713u, " [static IP]" }, //  [固定IP]
    { 0x52617a89u, "Move to folder" }, // 別のフォルダへ
    { 0x52916576u, "Power" }, // 電源
    { 0x54691298u, "Lua debugger" }, // Luaデバッガ
    { 0x558d6ad2u, "Screen lock: PIN" }, // 画面ロック: PIN
    { 0x55c0e016u, "Fetched %d/%d %d:%02d" }, // 取得 %d/%d %d:%02d
    { 0x55da8a1fu, "The device key in OTP is broken and there is no room to make a new one. The fixed key stays in use" }, // OTPの端末の鍵が壊れていて、作り直す空きもありません。固定鍵のまま暗号化します
    { 0x55ebe20bu, "Re-encrypts the settings on the SD that are not yet encrypted with the device key" }, // SDの暗号化された設定のうち、まだ端末の鍵で暗号化していないものを暗号化し直します
    { 0x57aa7a55u, "SSID is too long (32 bytes max)" }, // SSIDが長すぎます(32バイトまで)
    { 0x57f1cc97u, "Disconnected (will reconnect)" }, // 未接続(自動で再接続します)
    { 0x59003419u, "Lower volume on battery" }, // 電池で音量を絞る
    { 0x5ae88df6u, "Weekdays" }, // 平日
    { 0x5b6abc74u, "Accept" }, // 同意する
    { 0x5d0d554au, "Wrong (%d tries before a wait)" }, // 違います(あと%d回で待ち時間)
    { 0x5d55f6b9u, "Clock is not set" }, // 時刻が合っていません
    { 0x5e4f9591u, "Add widget" }, // ウィジェット追加
    { 0x60112c52u, "Tap to pick a song" }, // タップして曲を選ぶ
    { 0x6059d551u, "Pick a color" }, // 色を選択
    { 0x608128b1u, "Sat" }, // 土
    { 0x61f5e700u, "%d/%d (%s)" }, // %d月%d日(%s)
    { 0x6456f4e6u, "Update" }, // 更新
    { 0x64bb5828u, "Move forward" }, // 後ろへ移動
    { 0x676de1d9u, "Screen lock" }, // 画面ロック
    { 0x69bb719du, "%d" }, // %d件
    { 0x69fb0c45u, "Pick a network to set its IP" }, // IPを設定するネットワークを一覧から選んでください
    { 0x6a17f1acu, "Video" }, // 動画
    { 0x6a6a9112u, "TODO today" }, // TODO 今日
    { 0x6d14a300u, "Online" }, // 接続中
    { 0x6d4f275cu, "Agenda" }, // 予定
    { 0x6ef43357u, "Mail" }, // メール
    { 0x6f086a34u, "Set it by hand with \"Change\"" }, // 「変更」で手動で合わせられます
    { 0x70b47641u, "Timer: time is up" }, // タイマー: 時間になりました
    { 0x723647c1u, "Unknown error" }, // 不明な誤り
    { 0x73645683u, "Del" }, // 削除
    { 0x73af6fbau, "Write failed" }, // 書き込みに失敗しました
    { 0x74812a4fu, "%d-%02d" }, // %d年%d月
    { 0x769b726eu, "Enter PIN" }, // PINを入力
    { 0x77670398u, "Password for \"%s\" (leave blank if none):" }, // 「%s」のパスワード(不要なら空欄のまま決定):
    { 0x77da04cfu, "Dim when idle" }, // しばらく触らないと暗くする
    { 0x7937f253u, "Not fetched yet" }, // まだ取得していません
    { 0x7ba37e6fu, "%d/%d" }, // %d月%d日
    { 0x7c07a65cu, "Playing from %s" }, // 今は%sから鳴ります
    { 0x7c71d93cu, "Screen lock: none" }, // 画面ロック: なし
    { 0x7fa161efu, "Login" }, // ログイン
    { 0x80d4e4fdu, "Sun" }, // 日
    { 0x81701a13u, "Quality" }, // 音質
    { 0x829ffe5du, "Edit" }, // 編集
    { 0x856049c3u, "Max 20%, speaker, on battery" }, // 電池では最大20%(スピーカー)
    { 0x85d014f7u, "This app has too many scheduled notices" }, // このアプリの通知の予約が上限です
    { 0x866947afu, "Music" }, // ミュージック
    { 0x87bf4f42u, "Wi-Fi Share" }, // Wi-Fi転送
    { 0x87fcc70au, "Time zone" }, // タイムゾーン
    { 0x8946b352u, "Size: wide" }, // 大きさ: 横長
    { 0x89d3cda4u, "Widget" }, // ウィジェット
    { 0x8a251b4bu, "Wrong. Wait %lu s" }, // 違います。%lu秒待ってください
    { 0x8aabeaacu, "Hidden apps" }, // 隠したアプリ
    { 0x8ce539a1u, "Nothing today or tomorrow" }, // 今日・明日の予定はありません
    { 0x8d3d7cd9u, "Scan failed" }, // スキャンに失敗しました
    { 0x8e0ff741u, "Change PIN" }, // PINを変える
    { 0x9025c93du, "Static IP for \"%s\" (e.g. 192.168.1.50/24, blank for DHCP):" }, // 「%s」の固定IP(例 192.168.1.50/24。空欄でDHCP):
    { 0x90d9dfc2u, "Hidden apps: none" }, // ランチャで隠したアプリ: なし
    { 0x92012343u, "Alarm %02d:%02d" }, // アラーム %02d:%02d
    { 0x921978adu, "Wed" }, // 水
    { 0x924102f5u, "Enter current PIN to remove it" }, // PINを外すには今のPINを入力
    { 0x948014d7u, "No events in 7 days" }, // 7日以内に予定はありません
    { 0x954bc8e9u, "Tmrw %s" }, // 明 %s
    { 0x95665b44u, "Add" }, // 追加
    { 0x964679ecu, "Portal field: %s%s" }, // ポータルの入力: %s%s
    { 0x969e89a2u, "Re-encrypted %d items%s%s" }, // %d項目を暗号化し直しました%s%s
    { 0x971b92bbu, "Remove from folder" }, // フォルダから出す
    { 0x9739ed6eu, "Wait %lu s" }, // %lu秒待ってください
    { 0x9784784fu, "Saved %d/%d" }, // 保存済み %d/%d
    { 0x97be02efu, "Large" }, // 大きい字
    { 0x98b70a6cu, "IP settings saved" }, // IPの設定を保存しました
    { 0x98e7c231u, "Use up to 63 letters, digits and hyphens (no hyphen at either end)" }, // 名前は英数字とハイフンの63文字までです(先頭と末尾にハイフンは使えません)
    { 0x99591e6fu, "Enter current PIN" }, // 今のPINを入力
    { 0x9b1c629du, "Rename" }, // 名前を変える
    { 0x9cdb9be3u, "Cancel" }, // 取消
    { 0x9d2f149fu, "Login failed: %s" }, // ログインできませんでした: %s
    { 0x9e024f22u, "Notes" }, // メモ
    { 0x9e458b44u, "Update" }, // 本体の更新
    { 0x9e755ffcu, "Enter again" }, // もう一度入力
    { 0x9eabeae5u, "10 min" }, // 10分
    { 0x9f99ee14u, "File name..." }, // ファイル名を入力...
    { 0x9fc18388u, "Time" }, // 時刻
    { 0xa071b856u, "List" }, // 一覧
    { 0xa07b8fd8u, "SD card unavailable" }, // SDカードが使えません
    { 0xa13eabaau, "Wrong PIN" }, // PINが違います
    { 0xa1a335f9u, "Lock" }, // ロック
    { 0xa1a46e63u, "Delete \"%s\"?" }, // 「%s」を削除しますか?
    { 0xa2094a36u, "Music" }, // 音楽
    { 0xa214f5c6u, "Yes" }, // はい
    { 0xa261b0d3u, "USB setup failed" }, // USBの準備に失敗しました
    { 0xa375beeau, "Battery %d%% (%.2fV)" }, // 電池 %d%% (%.2fV)
    { 0xa4f35f82u, "Sent. Checking the internet connection" }, // 送信しました。インターネットへ出られるか確かめています
    { 0xa58e1c1du, "1 min" }, // 1分
    { 0xa85c0c13u, "Output" }, // 出力先
    { 0xa884d8f1u, "Calculator" }, // 電卓
    { 0xaa251e4cu, "Headphones" }, // イヤホン
    { 0xaa63e78eu, "DNS (up to 2, comma separated; blank for auto):" }, // DNS(カンマ区切りで2つまで。空欄で自動):
    { 0xab38b5bbu, "Speaker" }, // スピーカー
    { 0xabc63d25u, "LCD" }, // 画面
    { 0xaca3a146u, "Home: %s" }, // ホーム: %s
    { 0xad5068b6u, "Snooze 5m" }, // 5分後
    { 0xad82542du, "Game Boy" }, // ゲームボーイ
    { 0xad90f729u, "Size: large" }, // 大きさ: 大
    { 0xade7fb52u, "An error occurred" }, // エラーが発生しました
    { 0xae94968bu, " (password)" }, // (パスワード)
    { 0xaeca5359u, "Weekends" }, // 土日
    { 0xafefc472u, "Daily" }, // 毎日
    { 0xb18e6a83u, "Wi-Fi is off. Turn it on before scanning" }, // Wi-FiがOFFです。ONにしてから検索してください
    { 0xb3da8294u, "Create" }, // 作成
    { 0xb4312e4du, "Cont." }, // 続き
    { 0xb4374f17u, "Stop alarm/timer" }, // アラーム/タイマーを止める
    { 0xb441cfe3u, "Volume" }, // 音量
    { 0xb53d8ec5u, "Pick a network to delete from the list" }, // 削除するネットワークを一覧から選んでください
    { 0xb55476f4u, "Screenshot failed" }, // スクリーンショット失敗
    { 0xb5cdb9d7u, "Battery: USB power" }, // 電池 USB給電中
    { 0xb65b25cfu, "Show: %s" }, // 表示: %s
    { 0xb7375890u, "Sleep" }, // スリープ
    { 0xb7a9005bu, "No screen" }, // 画面がありません
    { 0xb84779d0u, "Clock not set" }, // 時計が未設定
    { 0xb883d49bu, " (connected)" }, //  (接続中)
    { 0xb9de6364u, "Cannot create the file" }, // ファイルを作れません
    { 0xbabb7d70u, "Never" }, // しない
    { 0xbb464167u, "Joining: %s" }, // 接続しています: %s
    { 0xbbdc1804u, "New folder" }, // 新しいフォルダ
    { 0xbcc90d98u, "Dictionary" }, // 辞書
    { 0xbfa3f065u, "Invalid notice" }, // 通知の指定が正しくありません
    { 0xc2aa0ad7u, "Launcher order reset (folders removed)" }, // ランチャの並びを登録順に戻しました(フォルダも解除)
    { 0xc40d96fau, "No calendars" }, // カレンダーがありません
    { 0xcaa2d716u, "Screenshot" }, // スクリーンショット
    { 0xcac47886u, "Offline" }, // 外へ出られません
    { 0xcb902c6fu, "Type here..." }, // ここに入力...
    { 0xcb96e1f9u, "All day" }, // 終日
    { 0xcba708a0u, "Today" }, // 今日
    { 0xccaaa141u, "Remove PIN" }, // PINを外す
    { 0xcd3af6e2u, "Touch calibration" }, // タッチ位置の補正
    { 0xcdbd6b24u, "Show frame time" }, // フレーム時間を表示
    { 0xcf458776u, "Audio" }, // 音
    { 0xcfca515eu, "USB Drive" }, // USBドライブ
    { 0xd2af9ed8u, "Create" }, // 作る
    { 0xd4585243u, "Text" }, // テキスト
    { 0xd4f9d0bcu, "Change" }, // 変更
    { 0xd55e0655u, "Size: small" }, // 大きさ: 小
    { 0xd870230du, "Saved %d/%d (double-tap to join)" }, // 保存済み %d/%d (2回タップで接続)
    { 0xddf43b01u, "Analog clock" }, // アナログ時計
    { 0xde899e03u, "Rescan" }, // 再スキャン
    { 0xe222959bu, "Settings" }, // 設定
    { 0xe4d98c0fu, "Digits" }, // 数字
    { 0xe56b694du, "Background: %d" }, // バックグラウンド: %d件
    { 0xe9490db7u, "Add to folder" }, // フォルダへ入れる
    { 0xe95b2c8cu, "Stop" }, // 止める
    { 0xeac6e494u, "Files" }, // ファイル
    { 0xeaf23f4cu, " (connecting)" }, //  (接続しています)
    { 0xeb44c1f5u, "Saved to the DS3231 when set" }, // 合わせるとDS3231へ控えます
    { 0xeb5f2c1au, "Once" }, // 1回
    { 0xeb8b6718u, "Hidden apps: %d" }, // ランチャで隠したアプリ: %d
    { 0xebab40f0u, "Phone" }, // スマホ
    { 0xebb52480u, "Run" }, // 実行
    { 0xebf00081u, "Accept the terms of the \"%s\" portal and connect?" }, // 「%s」のポータルの利用規約に同意して接続しますか?
    { 0xed5bc620u, "Details" }, // 詳細
    { 0xed9b4328u, "Date" }, // 日付
    { 0xeded1ff7u, "Bright" }, // 輝度
    { 0xefd23e36u, "Gateway (router) IP (blank for none):" }, // ゲートウェイ(ルーター)のIP(空欄で無し):
    { 0xf015d76du, "Small" }, // 小
    { 0xf2209dd9u, "Join" }, // 接続
    { 0xf3743f81u, "Dark theme" }, // ダークテーマ
    { 0xf3a85ce4u, "No match. Start with the new PIN" }, // 一致しません。新しいPINから
    { 0xf52feee4u, "Store" }, // ストア
    { 0xf546340bu, "Tomorrow" }, // 明日
    { 0xf555b5c2u, "Could not set the time" }, // 時刻を合わせられませんでした
    { 0xfa8380c4u, "%s" }, // %s曜日
    { 0xfc144586u, "Back" }, // 戻る
    { 0xfc7e9319u, "5 min" }, // 5分
    { 0xfd09a5b8u, "Fri" }, // 金
    { 0xfdbfe375u, "Mon" }, // 月
    { 0xfde8539eu, "Which folder?" }, // どのフォルダへ
    { 0xfe505ffdu, "Enter up to 2 DNS servers like \"1.1.1.1,8.8.8.8\"" }, // DNSは「1.1.1.1,8.8.8.8」のように2つまで入れてください
    { 0xff87fd58u, "Browser home URL" }, // ブラウザのホームURL
    { 0xfff3714fu, "No" }, // いいえ
};
const size_t I18nFunctions::kTableEnCount = 270;
