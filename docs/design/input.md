# 入力(コントローラー・物理キーボード・フォーカス・CardKB2)とUSBドライブ

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## 外部コントローラー (`src/system/Pad_Functions` / `script/pad_serial.py`) (2026-09-25)

SUMMARY.md #10。**方式は市販のWiiクラシックコントローラー**(I2C0のGP0/GP1、アドレス0x52。抜き差しはACKの有無で分かるので
検出用のピンが要らない。十字キー/ABXY/L R ZL ZR/START SELECT HOMEが揃い3.3Vで動く)に決めたが、**実物がまだ無い**。
そこで先に「アプリが読む窓口」と、**USBシリアル経由でPCのキーボードを代わりにする入力元**を作った。
比べた方式(GPIO直結=ピン不足、SFCパッド=5V・独自コネクタ、UART=相手のファームが要る、Bluetooth=RAMとWi-Fi同居の負担)は
会話の記録なので割愛する。空きピンはGP0/1/7/8/14/26/27/28の8本(その後GP0/1はI2C、GP26〜28はマイクに使った。今の空きはGP7/8/14)(GP23〜25/29は無線チップが使う)。

- **窓口(`PadFunctions`)**: 状態は`uint16_t`のビットマスク1つ(`Button`: 十字4 + ABXY + L R ZL ZR + START SELECT HOME の15個)。
  **`loop()`の頭(`PICO_Touch::Update()`の直後、シーンの`onUpdate()`より前)で1回だけ更新**するので、1フレームの間は
  `IsDown()`/`Pressed()`/`Released()`の答えが変わらない(`Pressed`/`Released`はそのフレームだけtrue)。
  **ビットの並びはシリアルの取り決め・`pad_serial.py`・Luaの名前表に直結しているので変えないこと。**
- **USBシリアルの取り決め**: PC→pico-osへ1行`pad XXXX\n`(押しているボタンの16進数、1〜4桁)。ログと同じ`Serial`の逆向き。
  - PC側は**状態が変わったときと、変わらなくても100msごとに**送る。pico-os側は**`kSerialTimeoutMs`(500ms)届かなければ外れた扱いにして全部離す**
    (スクリプトを止めた・ケーブルを抜いた・離した瞬間の行が落ちた、のどれでも押しっぱなしにならない)
  - 形式に合わない行は黙って捨てる(シリアルモニタで`pad 10`と打てばAを押したことになる)。1行は32Bまで、長い行は改行まで読み捨てる。
    1回の`Update()`で読むのは256Bまで(送り付けられてもフレームが止まらない)
  - **キーの割り当てはPC側(`pad_serial.py`)が持つ**。pico-osが受け取るのはボタンの状態だけなので、実物のコントローラーに替えても
    アプリ側は何も変わらない。キーの押下/離しを1件ずつ送る方式にしなかったのは、離した行が1つ落ちるだけで押しっぱなしになるため
- **`script/pad_serial.py`**: tkinterの小窓でキーの押下/離しを拾い、上の行を送る。
  - シリアルはpyserialで開く(Windowsは必須)。無ければLinux/macOSだけ`termios`で開く
  - **ポートは同時に1つのプログラムしか開けない**のでシリアルモニタとは併用できない。代わりに**pico-osのログをそのままターミナルへ出す**
  - ポートが消えても(書き込みでの再起動等)1秒ごとに開き直す。フォーカスが外れたら全部離す
  - X11のキーの自動リピートは「離した→押した」を連続で送ってくるので、離したのは30ms待ってから採る
  - `--stdout`でPCビルドの標準入力へパイプできる
  - **ターミナルに打った1行をそのまま送る**(`start_console()`。`hw info`・`dbg pause`等のコマンド用。2026-10-11)。小窓はキーの押下/離ししか送らないので、
    ポートを開いたままコマンドを打つ手段がこれしか無かった。書き込みは`write_lock`で、`pad`の行と混ざらない
- **使っている所**: `GameBoyScene`(画面の`GameBoyPad`とORで重ねる。対応は`apps/gb/Gb_PadMap.hpp`、BはYでも押せる。HOMEで戻る)、
  Lua(`pico.pad_connected/pad_down/pad_pressed/pad_released`、ボタンは小文字の名前。知らない名前はエラー)、
  ステータスバー(つながっている間だけゲームパッドのアイコン。無いのが普通なのでバツは付けない)、
  動作確認は「動作確認」アプリの「コントローラー」(`pc/sdcard/lua/apps/動作確認/pad.lua`)。
  通常の画面をコントローラーで操作するのは下の「ウィジェットのフォーカス」(2026-10-10)。
- **PCビルド**: `pc/compat/Arduino.h`の`Serial.available()/read()`が**標準入力**を別スレッドで読む(`PICOOS_SERIAL_STDIN=off`で無効)。
  `python3 script/pad_serial.py --stdout | ./pc/build/picoos_pc`、ヘッドレスなら`echo "pad 0011"`を100msごとに流し込めばよい。
- **Webビルド**(2026-09-26): 標準入力が無いので、ページ(`pc/web/shell.html`)の**コントローラー**(Wiiクラシックと同じボタンの並び。
  ポインタごとに覚えるので**複数の指で同時押し**でき、押したまま滑らせると隣のボタンへ移る)と**キーボード**(割り当ては`pad_serial.py`と同じ、
  `KeyboardEvent.code`で引く)が`pad XXXX\n`を作り、`main_pc.cpp`の`picoos_serial_push()`(`EMSCRIPTEN_KEEPALIVE`+`-sEXPORTED_FUNCTIONS`)で
  1バイトずつSerialの受信口へ入れる。**取り決めは実機と同じ**(変わったとき+100msごと、チェックを外すと送らなくなり500msで外れる)ので、
  pico-os側は何も変えていない。タブを離れたら(`blur`)全部離す。Chromium(Playwright)で、キーボードとページのボタンの両方でテトリスを操作できることを確認した。
- 検証: `pad_test`(run.sh。行の読み取り、押した/離したはそのフレームだけ、途切れたら外れる、行が分かれて届く、長すぎる行、1回に読む量、GBの対応)、
  `lua_engine_test`(Lua API)、PCビルドで`pad_serial.py --stdout`→`picoos_pc`をXvfb+xdotoolで通し(十字キー斜め+Bの同時押しで点が斜めに動く)、
  擬似端末を相手にシリアルの経路(pyserial有り/無し、ログの折り返し)。**実機のUSBシリアル(arduino-picoのCDC)では未確認**。
- **Wiiクラシックコントローラーのドライバ(`src/hal/WiiClassic_Functions`、2026-10-12)**: I2C0(`I2C_Bus.hpp`、CardKB2と共有。`begin()`はここで1回)のアドレス0x52。
  - 見つける: 未接続の間は500msごとに`[F0 55]`→`[FB 00]`(暗号化しない初期化。純正もクローン品も)→`[00]`(変換の開始)を書き、ACKがあれば接続。
  - 読む: 10msごとに6バイト読み、すぐ次の`[00]`を書く(書いてから読むまでの待ちを1回ずらして吸収する。止まらない)。標準の形式の4・5バイト目がボタン(0=押している)、
    左スティック(0〜63、つながって最初に読めた値が中心)を±12以上傾けたら十字キーとしても押す。全部0xFF(初期化が外れた)・全部押している、は読めていない扱いで初期化し直す。
    3回続けて読めなければ外れた扱い(全部離す)。クラシックコントローラーPro・ファミコン/スーファミのミニの付属コントローラーも同じ形式。
  - `PadFunctions::SetDevice(buttons, connected)`へ渡し、PadはUSBシリアルの入力とORで重ねる(`Source::WiiClassic`が優先して見える)。`main.cpp`は`PadFunctions::Update()`の前に読む。
  - 1回の読み出しは100kHzで約0.7ms(CardKB2が400kHzに耐えるか分からないのでバスは100kHzのまま)。
  - PC/ホストテストの`pc/compat/Wire.h`は`PicoPcWire::devices`(アドレス→書き込み/読み出しを答える偽物)と`Wire.write()`を足した。
  - 検証: `wii_classic_test`(run.sh の input。15ボタンの位置・同時押し・スティック・読めない値・呼びかけの間隔・初期化の書き方・Padへ届く・USBシリアルと重なる・抜き差し)。
    **実物・実機では未確認**(3.3Vのプルアップ・純正/クローン品の初期化の違い・変換の待ちが10msで足りるか)。
- 次: PCビルドにSDLのキーボード/ゲームパッドを直接つなぐのも手軽な追加候補。

## 物理キーボード (`src/system/KeyInput_Functions` / `KeyInput_Dispatch.cpp`) (2026-10-01)

SUMMARY.md #10の追加項目。**PCのキーボードを物理キーボードとして使い、そのまま文字を打てるようにした**(第1段: USBシリアル経由)。
本体へ直接つなぐ方式は比べた上で後回しにした(Bluetoothのキーボード=arduino-picoのBluetoothHIDMasterがあるがBTstackでRAMを数十KB食い、
Wi-Fiと無線チップを共有する / PIO-USBのホスト=CPUクロックを12MHzの倍数にする必要がありSPI/I2S/CYW43の分周が狂い、1msごとの処理に
2コア目(音声専用)が要る / 本体のmicro-USBをOTGに=ログとpad_serialが使えず、電池のVSYSでは5Vを出せない / PS/2=軽いがレベル変換が要り入手しにくい)。
次に試すならBluetooth(RAMとWi-Fi同居を実機で測ってから)。

- **出来事の列**: `PadFunctions`が「押している状態」なのに対し、こちらは**1打鍵=1件の出来事**(`Event{key, mods, cp}`)。
  文字入力は回数が大事なので状態にしない。長押しの連打はPC側のキーリピートがそのまま届く。列は32件の固定長(溢れたら捨てて数える)。
- **USBシリアルの取り決め**: `key M CODE\n`。Mは修飾キーの16進1桁(1=Ctrl 2=Alt 4=Shift)、CODEは`u+XXXX`(符号位置)か
  `enter backspace tab esc delete left right up down home end pageup pagedown`。文字はShift・キー配列を反映済みで送る
  (母艦のIMEで確定した日本語もそのまま送れる)。**Serialを読むのは`PadFunctions`の1か所**で、`pad `以外の行を`KeyInputFunctions::FeedLine()`へ回す。
  1行落ちても1文字落ちるだけ(押しっぱなしの事故は無いので、padのような生存確認は要らない)。
- **配り先(`KeyInputFunctions::Update()`、`loop()`で`SceneFunctions::Update()`の後・`WidgetFunctions::UpdateAll()`の前)**:
  1. 今の画面の`Scene::onKey(ev)`(既定はfalse)。SSH(`SshScene`)はここで全部取ってシェルへ送る(Ctrl+文字→制御文字、Alt→ESC前置、
     Esc/Tab/矢印/Home/End(`?1h`ならSS3)/Delete/PageUp/PageDownのエスケープ列。接続前は端末の中の1行へ。Ctrl+Cは中止/打ち直し)。
     テキストエディタ(`TextEditorScene`)は↑↓/PageUp/PageDown(10行)で行を移り、Ctrl+Sで保存、Ctrl+Z/Y・Ctrl+C/X/V/A・Ctrl+F/H/G(上の「テキストエディタ」)、キーボードが閉じていれば打ったときに開いて
     文字そのものはキー盤へ回す(ダイアログを出している間は何もしない)。
  2. 開いているキー盤(`KeyboardFunctions::VisiblePanel()`)の`KeyboardPanel::onPhysicalKey(ev)`。**Textbox/InputDialog/チャット等、
     オンスクリーンキーボードを開いて入力する所はどこでも、入力先側は無改修で打てる**。共通の振る舞いは基底(`KeyboardPanel.cpp`)にあり、
     各キー盤は`physicalInsert/Backspace/Move`(+日本語は`physicalEnter`)で自分の編集操作へ繋ぐだけ:
     文字→カーソル位置 / Enter→単一行は決定・複数行は改行(Ctrl+Enterは常に決定) / Esc→決定して閉じる / Backspace・←→・Home・End /
     Delete→「右へ1つ動いて前を消す」(末尾なら`ITextInputTarget::onDeleteAtEnd()`。テキストエディタが次の行と繋ぐ) /
     Ctrl+C・X・V→クリップボード(入力欄の全部/貼り付け)。
     ↑↓・Tab・Alt付きの文字・それ以外のCtrl付きの文字はキー盤では扱わない。
     - 日本語のキー盤は**ローマ字かな漢字変換をする**(下の「物理キーボードのかな漢字変換」)
     - 数字のキー盤は数字・`.`と、使えるタブの記号表にあるものだけ(`*`→`×`、`/`→`÷`に直す)。英字は断る(他へも回さない)
  3. どちらも取らなければウィジェットのフォーカスへ(Tab・矢印・Enter・Space・Esc。下の「ウィジェットのフォーカス」)。それ以外は捨てる。
- **メディアキー(2026-10-14)**: `key 0 playpause|stop|nexttrack|prevtrack|volumeup|volumedown|mute`(`Key::PlayPause`〜`Mute`。Enumの末尾)。
  キー盤・フォーカスは使わず、画面の`onKey()`とLuaの`on_key`(同じ名前)へ届く。ミュージックアプリは再生/一時停止・次・前・停止に使う。
  音量キーはOS全体では扱っていない(配り先がSound/Configへ依存するとホストテストのリンクが広がるため。要るなら画面かLuaで)。
  入力元: `pad_serial.py`(X11の`XF86Audio*`。コントローラーのモードでも送る。デスクトップが先に取ると届かない)、PCビルド(`SDLK_AUDIO*`)、Webのページ(`Media*`/`AudioVolume*`)。
- **自動調光/スリープ**: 列に打鍵があれば操作とみなす(`DisplayFunctions`/`PowerFunctions`が`Pending()`を見る)。**スリープから起こした打鍵は捨てる**
  (`DiscardPending()`。暗い画面のどこへ入るか見えないため。タッチの`swallow_touch`と同じ考え方)。
- **列・行の読み取り(`system/KeyInput_Functions.cpp`)は何にも依存しない**ので、Pad/Powerのホストテストはこちらだけを引っ張る。
  配り先(`boot/KeyInput_Dispatch.cpp`)は`SceneFunctions`/`KeyboardFunctions`に依存する(`Notification_Functions`/`_Sources`と同じ分け方)。
- **入力元**:
  - 実機: `script/pad_serial.py --mode text`(F1でコントローラー⇔文字入力を切り替え)。tkinterのkeysym/char/stateから行を作る
    (Ctrl+CはcharがETXになるのでkeysymを送る。AltのビットはOSで違う: Windows 0x20000 / X11 0x8 / macOS 0x10)
  - PCビルド: ウィンドウでのキー入力を`main_pc.cpp`の`SDL_AddEventWatch`が同じ行にして`PicoPcSerial::PushLine()`で受信口へ入れる
    (文字はSDL_TEXTINPUT、名前のあるキーとCtrl/Alt付きはSDL_KEYDOWN)。**LovyanGFXのSDLパネルは修飾キー無しのr/l/1〜6を画面の回転・拡大に
    使うので、`Panel_sdl::setShortcutKeymod(左Ctrl+左Alt)`へずらした**(Webも同じ)。`PICOOS_PC_KEYBOARD=off`で無効
  - Webビルド: ページの「PCのキーボード: 文字入力に使う」でJSが行を作る(`keydown`をcaptureで取り、SDL/コントローラーへは渡さない。
    Cmd/⌘付きはブラウザへ渡す)。ページの入力欄にフォーカスがあるときは取らない
- 検証: `key_input_test`(run.sh。行の読み取り・padの行との同居・列・UTF-8・画面→キー盤の順・英字/数字のキー盤の編集と決定)、
  PCビルドで標準入力へ`key`の行を流して: テキストエディタ(打鍵でキーボードが開く・改行・↑End Backspace ← Delete・↓Home)、
  SSH(接続先の入力・Backspace・Ctrl+Cで打ち直し)、入力テストのTextbox(日本語のキー盤へ英字と「あ」)/NumberInput(英字・使えない記号を断る)。
  `pad_serial.py`の行の組み立て、Webのページ(Chromiumで「文字入力」の行とコントローラーのときは送らないこと)。
  **実機のUSBシリアル・Webビルド本体(emsdk無し)・PCビルドの実ウィンドウでのSDLのキー入力(ヘッドレスでは来ない)・実際のSSHサーバ相手は未確認**。

## ウィジェットのフォーカス (`src/gui/core/Focus_Functions` / `Widget`の`focus_mode`と仮想関数) (2026-10-10)

SUMMARY.md未掲載。物理キーボード・外部コントローラーで、画面のボタン等を選んで押せるようにした。**RAMは状態だけで約10バイト**
(`FocusFunctions::focused`・`ring_visible`・`saved_id`(WidgetId)・`saved_ring`、十字キーの繰り返し6バイト)。
ウィジェット側は`Widget::focus_mode`(1バイト。`background_color`の直後の詰め物に入るので**sizeof(Widget)は変わらない**)と
仮想関数6つ(`focusableByDefault/onFocusKey/onFocusChanged/drawsOwnFocus/focusRect/revealRect`。vtableはフラッシュ)だけ。
候補の並べた配列も持たず、操作のたびに`widgets`(またはダイアログの部分木)をなめて選ぶ。

- **範囲**: 一番上に開いている(見えている)ダイアログの中、無ければ通常レイヤ。オーバーレイ(ステータスバー・キーボード)には移らない。
  候補は`isFocusable()`かつ自分と親が全部見えていて有効(`isEffectivelyEnabled()`)で`hit_transparent`でないもの。
- **移り方**: Tab/Shift+Tab(コントローラーはR・ZR/L・ZL)は**読む順**(画面座標の上から、同じ高さなら左から。端で回る)。
  矢印/十字キーは、まずフォーカスのあるウィジェットの`onFocusKey()`に渡し、扱わなければ**その向きの一番近いもの**
  (中心がその向きにあるものの中で「向きの距離+横ずれ×3」が最小。横ずれは直角方向で範囲が重なれば0)。基準は`focusRect()`
  (AppGridは選んでいるタイル)。十字キーは押したままで400ms後から110msごとに繰り返す。
- **押す**: Enter/Space/A。`onFocusKey(Activate)`が扱わなければ**真ん中をタップしたことにする**(`OSData::touchX/Y`を一時的に
  真ん中にして`causeOnPressStart()`→`causeOnPressEnd()`、元へ戻す)。押した中で自分が消えたら(`~Widget()`が`focused`を下ろす)離すは配らない。
  Esc/Bは`onFocusKey(Back)`(開いたドロップダウンを閉じる)だけ。
- **枠**: キー/ボタンで動かしたときだけ出す(最初の1回は今の/最初のものに枠を出すだけで、動かしも押しもしない)。**タッチで消える**
  (`WidgetFunctions::UpdateAll()`の押した瞬間に`OnTouchStart()`。押したものが受けるならフォーカスはそこへ移る)。
  描くのは`FlushDirty()`の合成で、dirty矩形ごとに全部のウィジェットを描いた後(子に塗りつぶされないように)、フォーカスのある
  ウィジェットの矩形の**内側**に2pxの青(`kRingColor`)。消す/動かすときは`MarkDirtyBelow()`(半透明のダイアログの下に残らないように)。
  `drawsOwnFocus()`が真のもの(AppGrid)は自分で描く。
- **ダイアログ**: 枠が出ている間にダイアログが開いたら(1フレーム遅れて開く画面もあるので毎フレームの`FocusFunctions::Update()`で見る)、
  開く前のものを**WidgetIdで覚えて**中の最初のものへ移る。閉じたら`WidgetRegistry::Resolve()`で戻す(生ポインタだと破棄を見逃す)。
  タッチで閉じた場合は戻すが枠は出さない。
- **入力の配り先**(`KeyInput_Dispatch.cpp`): 打鍵は 画面の`onKey()` → 開いているキー盤 → フォーカス の順(画面やキー盤が取ったものは動かさない)。
  コントローラーは**`Scene::usesPad()`が偽の画面だけ**(ゲームボーイと、`pico.pad_*`を一度でも呼んだLuaアプリは真)、キー盤が開いている間もしない。
- **ウィジェットごと**: 受けるのは Button / Checkbox(決定で切り替え。タップは左端の四角しか見ないため) / Textbox / NumberInput(押す=キーボードを開く) /
  NumberSlider(←→で範囲の1/20) / TabBar(←→でタブ、端より先は隣へ) / ScrollList(↑↓で選ぶ=1回目のタップ、決定=同じ項目の2回目のタップ、端より先は隣へ。
  `ensureVisible()`を足した) / DropdownMenu(決定で開く、開いている間は↑↓・決定で確定・戻るで閉じる、フォーカスが外れたら閉じる。中の一覧は`setFocusable(false)`) /
  AppGrid(矢印でタイル、左右の端でページを送る、決定で起動。`HomeScene::onUpdate()`がページの表示を追いかける)。
  `ScrollContainer::revealRect()`は隠れた子へ移ったとき見える所までスクロールする。**新しい操作部品を作ったら`focusableByDefault()`と、
  タップの位置で動きが変わるなら`onFocusKey()`を足すこと**(`AppGrid`/`ColorDialog`型の「直接描く」部品は中で選ぶ位置を持つ必要がある)。
  ColorDialog・KeyboardNum・DurationPicker・MonthGrid・CalculatorKeypad・GameBoyPad等の直接描く部品はまだ受けない(枠もタップの位置も合わない)。
- Lua: `pico.focus(id|nil [, show_ring])` / `pico.get_focus()`、プロパティ`focusable`(`WidgetProperty::Id::Focusable`)。ドキュメントは`lua-api-doc/content/api/ui/widgets.md`。
- 検証: `focus_test`(run.sh の widget。読む順・矢印・候補にならないもの・押す・消える・タッチ・ダイアログの範囲と戻り・各ウィジェット)、
  PCビルドで標準入力へ`key`の行を流して`--shot`(`PICOOS_VERIFY_LCD=1`で食い違い0): ランチャのタイルの移動とページ送り、
  Enterで起動、設定アプリ(一覧・ボタン・タブ・スライダー・ドロップダウン・確認ダイアログへ移って閉じたら戻る)、テキストエディタ(Tabでツールバー)。
  実機(USBシリアルの入力)で動作を確認済み(2026-10-11)。**実物のコントローラーでは未確認**。

## 物理キーボードのかな漢字変換 (`src/ime/Romaji_Kana.hpp` / `Keyboard`の物理キーの処理) (2026-10-03)

日本語のキー盤(`Keyboard`)が開いている間、物理キーボードの英字を**ローマ字として読みにし、Spaceで漢字へ変換する**。
辞書・候補の欄・読みの表示(`~読み~`)はフリック入力と共通で、新しく足したのはローマ字→かなと変換の状態だけ。

- **ローマ字→かな(`ime/Romaji_Kana.hpp`)**: 描画にもSDにも依存しないヘッダだけの部品。表で一番長く一致するものを取る。
  `n'`/n+子音 → ん、`nn` は次が母音/y なら「ん+な行」(konnichiha → こんにちは)・それ以外なら ん 1つ(kannji → かんじ)、
  子音の重ね/`tch` → っ、`- , . [ ] ~ /` → ー 、 。「 」 〜 ・。表に無い文字はそのまま読みに残す。
  まだかなにならない分(`ky`等)は`Keyboard::romaji`に持ち、読みの後ろへ続けて見せる。
- **状態**: 何も入力していない → (英字/上の記号)→ 読みを入力中 → (Space/↓)→ 変換中 → (Enter/1〜9/タップ/次の文字)→ 確定。
  - 読みを入力中: Enter=読みのまま確定、Esc=読みを捨てる、Backspace=ローマ字→かなの順に1つ、数字や他の文字は読みに足す、
    ←→等のカーソル移動は扱わない(画面やシェルへも漏らさない)
  - 変換中: Space/↓/→=次、Shift+Space/↑/←=前、1〜9=見えている候補の番号で確定、Esc/Backspace=読みへ戻る、
    文字を打つと選んでいる候補で確定してから次の読みを始める。キー盤のキーをタップしたときも確定するだけ(そのキーは働かない)
  - Ctrl+U / Ctrl+I: 読み(変換中でも)をひらがな/カタカナで確定
  - 何も入力していないときの数字・空白・記号・PC側のIMEで確定した文字は、今までどおりそのまま入る
- **変換は1回に1つの語(単文節)**。文節の区切りの情報が辞書に無いため。候補は「読み全体(送り無し・完全一致)」→
  「最後の1文字を送り仮名」→「最後の2文字を送り仮名」→ ひらがな → カタカナ の順に重ならないように並べる(`startConversion()`)。
  ひらがな/カタカナを最後に足すので、辞書に無い読み・辞書が無いPCビルドでも変換・確定できる。候補は16件(`IME_MAX_CANDIDATES`)、
  1候補24バイトに収まらないもの(送り仮名を付けて溢れたもの)は捨てる。
- **送り仮名は両方の方式**: 指定が無ければ上の自動。**大文字で送り仮名の頭を示すと(SKK式: `aruKu`/`utukusiI`)その切れ目の候補だけを引く**。
  大文字を含むかなが送り仮名の頭になる(母音の大文字ならそのかな自身)。送りの印は`RomajiKana::OkuriMarker()`で、
  **SKK-JISYOどおり母音の送りは母音そのもの**(い → i。フリックの「送り」が使う`IME_Functions::BuildOkuriKey`の表は
  い/う → w にまとめていて形容詞で外れるが、そちらは今回触っていない)。
- **配り順**: 日本語のキー盤が読みを入力中/変換中、または何も入力していない状態でローマ字を始める文字が来たら、
  **今の画面の`onKey()`より先にキー盤へ配る**(`KeyboardPanel::wantsKeyFirst()`、`KeyInput_Dispatch.cpp`)。
  テキストエディタの↑↓・SSHの「文字を全部取る」に負けないため。扱わない打鍵(Ctrl+S等)は画面へ回る。
  そのため`onPhysicalKey()`は**扱わないときに状態を一切変えずfalseを返す**決まり(同じ打鍵で2回呼ばれることがある)。
- **日本語⇔英字の切り替え**: 半角/全角(`key 0 zenhan`。`Key::Zenhan`)・Ctrl+Space・**Tabを400ms以内に2回**(CardKB2向け)。
  `KeyInputFunctions::CheckImeToggle()`(時刻を引数に取るのでホストテストできる)が見張り、`KeyboardFunctions::ToggleJapanese()`が
  `SwitchPanel()`で入れ替える(変換中の部分は確定して引き継ぐ)。**Tabの1回目は普通に配られる**(SSHなら補完のTabが1回飛ぶ。
  日本語の読みの途中ならキー盤が握りつぶす)。2回目はキー盤が開いていれば切り替えに使って捨てる。開いていなければ普通に配る。
  入力元: `pad_serial.py`(`Zenkaku_Hankaku`等)、PCビルド(`SDL_SCANCODE_LANG5`)、Webのページ(`Zenkaku`/`Hankaku`)。
  **PC側のIMEは切っておくこと**(入れたままだと母艦で変換した文字がそのまま届く。それはそれで入る)。
- **キーを畳む**: 物理キーボードの打鍵を扱ったら、日本語のキー盤を**候補の欄だけ(24px)**にする(`setCompact()`→`setPanelHeight()`)。
  右端の「あ」が日本語入力中の印で、タップするとキーを広げる。入力先から開き直す(`setVisible(true)`)と広げた形に戻り、
  日本語⇔英字の切り替え(`setShownSilently()`)では畳んだまま。据え置き表示ではテキストエディタ等の表示領域がそのぶん広がり、
  ダイアログ表示では空いた所に下の画面(斜線)が見える(`KeyboardDialog::attach()`の描き直し)。英字のキー盤は畳まない。
- 変換中は候補の欄で選んでいる候補を反転し、見えている候補に1〜9の番号を青で振る。選んでいる候補が見えるよう自動でスクロールする
  (`ensureCandidateVisible()`)。入力欄では読みは`~`(波線)、変換中の候補は`_`(下線)で囲む(`ITextInputWidget::isConverting()`)。
- 検証: `romaji_kana_test`(run.sh)、`key_input_test`(テストの中で小さい辞書をSDのスタブへ置き、Space/番号/↑↓/Esc/Backspace・
  自動とSKK式の送り仮名・Ctrl+U/I・変換中に打つと確定・画面より先に配ること・Tab2回/半角全角)。そのためスタブに
  `FsFile::seekSet()`とフォント指定の`textWidth()`を足した。PCビルドで標準入力へ`key`の行を流して、テキストエディタ(据え置き・畳んだ候補の欄)と
  入力テストのTextbox(ダイアログ・Tab2回で英字へ・「あ」で広げる)を`--shot`で確認した(辞書は一時的なSDのルートへ小さいものを置いた)。
  **実機・本物のSKK辞書・CardKB2・Webビルドでは未確認**(本物の辞書では1回の変換で最大3回辞書を引く。SDの速さは実機で見ること)。
- **未対応**: 文節の区切り・連文節変換、変換の学習(候補の並び替え)、読みの途中でのカーソル移動、全角英数への変換(F9/F10)。

## CardKB2(`src/hal/CardKB_Functions`) (2026-10-02)

SUMMARY.md未掲載(小粒のため、この節にだけ残す)。M5Stack Unit CardKB2(I2Cの小型QWERTY)を**あれば使える程度**で足した入力元。
外部コントローラーと同じく無いのが普通なので、未接続でもログ・画面に何も出さない(ステータスバーのアイコンも無し)。

- 打鍵は`KeyInputFunctions`の列へ積むだけ。届け先(画面の`onKey()`→開いているキー盤)はUSBシリアルのキーボードと共通なので、入力先側は無改修。
- **プロトコルはM5Unit-KEYBOARD(`UnitCardKB2`)の実装から**: アドレス0x5F(初代CardKBと同じ)、1バイト読むと押したキーのASCIIが1回(0=押していない)。
  Shift/Sym/Fnは反映済みで届く。リピートは本体側(300ms後から50ms)。**I2CモードではFn+D/Z/X/Cのカーソルキーが出ない**(本体の仕様)。
  初代の0xB4〜0xB7はカーソルキーとして読むが、CardKB2では来ない。UARTモード(Fn+Sym+2)のままだと何も返らず、未接続扱い。
- 配線はI2C0のGP0(SDA)/GP1(SCL)。`Wire`のピンと`begin()`は`hal/I2C_Bus.hpp`(Wiiクラシックのドライバと共通、2026-10-12に移した)。
- 検出: 未接続の間は1秒ごとに0x5F へ呼びかけ(ACKで接続)、接続中は10msごとに読み、読み出しが3回続けて失敗したら外れた扱い。
- PC/ホストテストは`pc/compat/Wire.h`(`PicoPcWire`で「I2Cの先に居るもの」を表す。既定は誰も居ない)。`script/host_test/stubs/Wire.h`はそれを取り込む。
- 検証: `cardkb_test`(run.sh)とPCビルドの起動(未接続で静か)。**実機・実物のCardKB2は未確認**
  (3.3V系のバスへ5Vのプルアップが乗らないか・`Wire.setSDA/setSCL`の名前・UARTモード出荷品が無いか)。

### UARTモード(`src/hal/CardKB_Uart` / `/sys/cardkb.cfg`) (2026-10-21)

I2CモードではFn+D/Z/X/Cの矢印が出ないので、UARTモード(本体でFn+Sym+2)でも読めるようにした。**`/sys/cardkb.cfg`に`mode = uart`と書いたときだけ**
(既定はI2Cのまま。起動時に読むだけ)。

- **届くもの**(M5Unit-KEYBOARDの`UnitCardKB2UART`より): 115200bps 8N1、キーの変化ごとに5バイト`AA 03 <番号0〜42> <01押した/02離した> <和の下位8bit>`。
  **文字ではなく押した/離したしか来ない**ので、キー番号→文字の表(同ライブラリ、MIT)・修飾キー・繰り返しをPico側(`CardKbUart`、描画にもシリアルにも依存しない)でやる。
  区切りが合わなければ1バイトずらして次の`AA`から読み直す。
- **修飾キー**: 本体のファームウェア(iot_button)と同じ判定(200ms以内の2回目=2回押し、400ms超え=長押し)で、Aa=押している間/1回押しで次の英字1文字/2回押しで固定、
  Sym=押している間/1回押しで切り替え、Fn=押している間。本体と違うところ: ①1回押しは**離した瞬間に効かせる**(本体は200ms待つので続けて打った文字に間に合わない)、
  ②押している間・離した直後に別のキーを打ったらその回を数えない(Aaを押しながら打って離しても1回押しにならない、Aa・英字・Aaを素早く打っても固定にならない)。
- **Fnの組み合わせ(pico-os独自)**: Fn+D/Z/X/C=↑←↓→、Fn+1=Esc、Fn+Del=Delete、Fn+Space=Tab、Fn+Enter=Enter、Fn+それ以外の英字=Ctrl+英字(小文字)。
  **Ctrl+D/Z/X/Cは矢印に取られて打てない**(SSHの^CはTermKeyBarで)。Aaが効いている間は矢印にShift(テキストエディタの範囲選択)。Fn+数字(1以外)は割り当てなし。
- **繰り返し**: I2Cモードの本体と同じ300ms後から50msごと。修飾キーは繰り返さない。線が抜けて離したフレームが来ないと押しっぱなしになるので、10秒で止める。
- **配線**: CardKB2のTX(Groveの白=SCLの位置)を**GP7**(`CARDKB_UART_RX`)へ。黄(CardKB2のRX)はつながない。ハードのUART0/1のピンが空きと合わないので
  arduino-picoの`SerialPIO`(PIOのステートマシン1つ)で受け、端子は内部プルアップ。始められなければ(PIOの空き無し)警告を1回出して何もしない。
  UARTモードの間はCardKB2としてのI2Cの呼びかけはしない(Wiiクラシックは自分でI2Cを始める)。
- `IsConnected()`はUARTでは「正しいフレームを1つでも受け取ったか」(押したときにしか届かないので抜けたことは分からない)。
- PC/ホストテストは`pc/compat/SerialPIO.h`(`PicoPcSerialPio::rx`に積んだバイトが届く。`begin_ok=false`で始められない場合)。
- 検証: `cardkb_uart_test`(run.sh の input。フレーム・ゴミ/途中から/1バイトずつ・Aa/Sym/Fnの各組み合わせ・繰り返し・cfgの切り替え・始められない場合)、
  PCビルドの起動ログ、実機ファームのビルド。**実機・実物では未確認**(SerialPIOがI2S・マイク・CYW43とPIOを取り合わないか・115200bpsで届くか・キーの手触り)(Todoist参照)。

## USBドライブ(USBマスストレージ) (`src/services/UsbDrive_Functions` / `storage/Msc_Sector_Bridge.hpp` / `UsbDriveScene`) (2026-10-12)

ランチャの「USBドライブ」(アイコンはSDカード)。開くとSDカードがPCのドライブとして見え、SDの抜き差しをせずにLuaアプリ・曲・ROMを行き来できる。

- **切り替え式**: SDを2者が同時に書くとFATが壊れるので、貸し出している間はpico-osはSDに触らない。`Start()`: WAVを止める→`LogFunctions::Suspend()`(log.txtを書き出して閉じる。
  その間のログはシリアルだけ)→`OSData::SD_usable=false`→USBへマスストレージの口を足す。`Stop()`: 口を外す→溜めた書き込みを書いて`syncDevice()`→
  **`PICO_SD::Remount()`(`SD.end()`→`Setup()`。PCが変えたFAT・フォルダの中身を読み直す)**→`LogFunctions::Resume()`→`LuaAppScanner::Scan()`+`PruneMissing()`
  (PCで足した/消したLuaアプリをランチャへ反映)。SDを使う処理は`SD_usable`を見て止まる決まりなので、他の画面・サービスは「SDが無い」扱いになる。
  **`SD_usable`を見ずにSDへ書く処理を足すと、貸し出し中にFATを壊す**ので、新しい常駐の処理は必ず見ること。
- **USBシリアルは止めない**: arduino-picoのUSBは口を後から足せる(`USB.registerInterface`。`libraries/FatFSUSB`と同じ作り)ので、CDC(ログ・`pad_serial.py`)と同時に使える
  複合デバイスになる。口を足す/外すときにUSBがつなぎ直しになりシリアルが一瞬切れる(`pad_serial.py`は1秒ごとに開き直す)。`#include <tusb-msc.h>`でPlatformIOが
  MSCのクラスドライバ(`libraries/tusb-msc`)を拾う。TinyUSBのコールバック(`tud_msc_*_cb`)は`UsbDrive_Functions.cpp`の実機の側にある(`FatFSUSB`は使わない=同じ名前の
  コールバックが重なるので、**FatFSUSBをincludeしないこと**)。
- **SDとの間(`MscSectorBridge`、ヘッダだけ・ホストテストできる)**: TinyUSBはREAD10/WRITE10を64バイト(`CFG_TUD_MSC_EP_BUFSIZE`)ずつに分けて呼ぶので、直近の1セクタを持つ
  (読み: SDを読むのは1セクタ1回。書き: 頭から書き始めたら読まずに溜めて最後に1回、途中から/隙間があれば先にSDから読んで混ぜる。別のセクタへ移る前・`write10_complete`・取り出しで書く)。
  RAM 1KB。SDの読み書きはUSBの処理(arduino-picoがタイマーの割り込みで回す`tud_task`)の中で同期に行う(FatFSUSBと同じ)。
- **診断の行(2026-10-12、実機のLinux Mintで「PCに認識されない」報告を受けて)**: 画面の青い2行が「USB:線なし(バスリセットが来ていない)/列挙待ち(構成が選ばれていない)/
  列挙済/口まだ(MSCのコマンドが1つも来ない)/口OK」と割り当てたエンドポイント、INQUIRY/TEST UNIT READY/READ CAPACITY/それ以外(最後の番号)/読み書きの失敗の数を出す
  (`UsbDriveFunctions::GetDiag()`。`tud_connected()`/`tud_mounted()`とコールバックの数)。どこで止まったかでPC側(`dmesg`)と突き合わせる。
  **arduino-picoの`unregisterEndpointIn/Out`は引数の扱いが逆**(Inは`1<<ep`、Outは`1<<(ep-0x80)`で戻す)ので、素直に渡すと何も戻らず、始める/終わるのたびにエンドポイントが減って
  いき、尽きると口が壊れる。`UsbDetach()`は`ep_out|0x80`/`ep_in&0x7F`で渡す(FatFSUSBも同じ呼び方で同じ漏れがある)。尽きたら`Start()`が`UsbFailed`。
- **速さ(2026-10-12、実機のLinux Mintで dd が66KB/秒・ls が固まったように見えた)**: arduino-picoは`tud_task()`を1msごとのタイマーでしか回さず、
  MSCは64バイトの受け渡しごとに`tud_task()`が要るので 64B/ms で頭打ちだった。TinyUSBの弱いシンボル`tud_event_hook_cb`(USBの出来事が積まれたとき、
  割り込みの中で呼ばれる)を上書きし、貸し出している間は`irq_set_pending(USB.usbTaskIRQ)`ですぐに続きを回す。実機(Linux Mint)で読み込み・ファイルを開くのが速くなったことを確認した(2026-10-12。ddの数値は未計測)。
  Linuxで手で`mount`すると日本語のファイル名が`????`になるのは`iocharset`の既定のせい(`-o utf8`で直る。ファイルマネージャーの自動マウントはutf8)。
- 状態: Waiting(口は出したがPCがまだ読みに来ない)/Mounted(読み書きがあった)/Ejected(PCで取り出し=START STOP UNIT。以後`test_unit_ready`は「媒体なし」)。
  画面はPCが使っている(Mounted)間に「終了」を押すと確認を出す。画面を離れる(電源メニューの再起動も)と必ず`Stop()`。貸し出し中は`KeepAwake()`・`keepForeground()`。
- **PC/Webビルド**は本物のUSBが無いので口を出さず、状態だけ真似る(`PICOOS_USB_DRIVE_HOST=1`でMounted)。SDを止めてマウントし直す流れは実機と同じに通る
  (`pc/compat/SdFat.h`に`end()`と`card()`(`SdCard`、大きさだけ)を足した)。
- 検証: `msc_bridge_test`(run.sh の core。RAMディスク)、PCビルドの`--tap`/`--shot`(待機→Mounted→確認→終了→`SD Setup`とLuaアプリの走査し直し、`PICOOS_VERIFY_LCD=1`で食い違い0)、
  実機ファームのビルド(`pio run`。`tud_msc_*_cb`と`mscd_*`がリンクされることを`nm`で確認)。**実機では未確認**(PCからドライブとして見えるか・速さ・
  Windows/macOSの取り出し・つなぎ直しでシリアルが戻るか・`SD.end()`→`begin()`でマウントし直せるか)。
