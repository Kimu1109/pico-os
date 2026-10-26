# 画面の明るさ・スリープ・時刻と電源メニュー

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## 画面の明るさ調整と自動調光 (`src/services/Display_Functions`) (2026-09-27)

SUMMARY.md未掲載(小粒の機能のため新規の大項目は起こさず、この節にだけ残す)。

- **明るさの実体はバックライト(`TFT_LED`=GP22)のPWM調光**(`PICO_GFX::SetBrightness(percent)` →
  `OSData::lcd->setBrightness()` → LovyanGFXの`lgfx::Light_PWM`)。`LGFX_Config.hpp`が
  `Light_PWM`を`_panel_instance.setLight()`へ付けている(`pin_bl=TFT_LED`、`pwm_channel=TFT_LED&1`=0)。
  **2026-09-30にパレット減光(frameのパレット16色を暗くする「ソフト輝度」)から置き換えた**。
  旧方式は実機の消費電力が変わらず、`CanvasRaster`等の自前パレットも対象外だったため。
  今は描画内容に一切触れないので、全ウィジェットが一様に暗くなり、設定直後の全画面再描画も要らない。
  - **LovyanGFXのrp2040の`Light_PWM`はPWMのwrapを100に固定している**(clkdiv 50、RP2350の150MHzで
    約30kHz。可聴域の外)。だから明るさ0〜100がそのままデューティ比(%)になる。
    レベル100はwrap100でも「101段中の100段(約99%)」なので、**百分率の100だけ255を渡して常時HIGH**にする
    (`SetBrightness()`)。
  - **`GFX_Functions::Setup()`で`TFT_LED`へ`pinMode()`/`digitalWrite()`をしてはいけない**(端子がPWMからSIOへ戻り
    調光が効かなくなる。以前あった`digitalWrite(22, HIGH)`は削除済み)。GP22のPWMスライス3
    (GP22/GP23)を他でPWMに使わないこと(wrap/clkdivがスライス単位のため。今はI2SがPIOで、PWMの利用者は他に無い)。
  - **PC/Webビルド**: `Panel_sdl_SpiWait::setBrightness()`が同じ0〜100を受け取り、表示用SDLテクスチャの色の
    掛け率(`SDL_SetTextureColorMod`)にする。`readRect()`(`--shot`)には写らないので、`main_pc.cpp`の
    `writeScreenshot()`が同じ掛け率を掛けて書き出す(バックライトは液晶のピクセルの中身ではないため)。
    テクスチャは描画スレッドが作り直すことがあるので、書き込みのたびに軽い比較で当て直している。
- **`DisplayFunctions`が「今どの明るさを見せるか」の方針を持ち、`PICO_GFX::SetBrightness()`は
  値をバックライトへ適用するだけの機構**という役割分担(`Sound_Functions`が方針、`ChipSynth`が機構、
  という分け方と同じ形)。
- 設定は`/sys/display.cfg`(無くてよい): `brightness = 0〜100`(既定100)、`auto-dim = true|false`(既定true)。
  `SettingsScene`が音量(`sound.cfg`)と全く同じ流儀(ドラッグ中は`SetBrightness()`で即反映、
  指を離したときに1回だけ`SetValue()`で書く)で編集できる。**画面が真っ黒になり操作不能になるのを
  防ぐため、`kMinBrightness=10`未満には設定できない**(スライダーの最小値もここに合わせてある)。
- **自動調光**: `OSData::isTouched`と`PadFunctions::IsDown(kAllButtons)`のどちらも
  `kIdleTimeoutMs`(既定30秒)の間ずっと無ければ`kDimBrightness`(既定40。パレット減光の時代に
  「15だと真っ黒にしか見えない」との指摘を受けて上げた値をそのまま引き継いだ。バックライトのPWMは
  光量そのものを絞るので同じ数値でもパレット減光より明るく見えるはずで、**暗さが足りなければ
  実機で見て下げる**)まで即座に暗くする
  (フェードはしない。`ClocksScene`と同じ`millis()`差分の考え方)。触れる/ボタンを押すと
  即座に通常の明るさへ戻る。設定側の`SettingsScene`の自動調光チェックボックスは
  `Checkbox`の既存の当たり判定の都合上(`causeOnPressStart()`がアイコン部分の24px幅しか
  見ない。`Checkbox.cpp`参照)、ラベルではなくチェックの四角そのものをタップする必要がある
  (他の`run_test_checkbox`等、既存の全チェックボックスと共通の制約で、この機能で新たに
  作ったものではない)。
- ホストテストは無し(`GFX_Functions`/`Touch_Functions`と同じく実描画・実タッチに強く依存するため、
  この2つと同様ASanホストテストの対象外にしてある)。検証はPCビルドの`--shot`/`--tap`で行った:
  明るさスライダーで画面全体が実際に暗くなること、無操作からの自動調光・タッチでの復帰、
  `display.cfg`への書き込みと再起動後の読み込みを確認済み。`--shot`は`readRect()`にバックライトが
  写らないため`main_pc.cpp`が掛け率を掛けて書き出す(明るさ30で平均輝度237→約71、自動調光後は約95=40%を確認)。
  **実機(RP2350の`Light_PWM`でGP22を駆動)で確認済み(2026-10-19)**: 実機ビルドが通り、スライダーで暗くなる・
  低い明るさでちらつかない・自動調光(30秒放置)の明るさ40で暗さが足りる。今後
  `lgfx::Light_PWM`の未解決や、明るさが効かない/ちらつく場合は、まずここ(pin_bl/pwm_channel、
  他コードがGP22のPWMスライスを使っていないか)を疑うこと。

## スリープ(省電力)(`src/services/Power_Functions`) (2026-09-30)

SUMMARY.md未掲載(小粒の機能のため新規の大項目は起こさず、この節にだけ残す)。
自動調光(バックライトを絞るだけ)の先に、消費電力そのものを下げる段階を足した。

```
Active → (30秒: kIdleTimeoutMs) → Dim(DisplayFunctions) → (sleep-timeout。既定120秒) → Sleep
```
タイムアウトは**最後の操作からの通算**(自動調光が切れていてもスリープは別に効く)。Sleepに入ると:

1. **バックライトを消し、液晶パネルもSLPINで休ませる**(`DisplayFunctions::SetSleeping()`。`lcd->sleep()/wakeup()`)。
   起きるとき**120ms待つ**(ILI9341/ST7789はSLPOUTから次のコマンドまで必要。LovyanGFXの`Panel_LCD::setSleep()`は待たない)。
   スリープ中は`ApplyEffectiveBrightness()`が何もしない(起きるときに今の明るさへ戻す)。表示内容(GRAM)は保たれるので再描画は要らない。
2. **Wi-Fiを積極的な省電力へ**(`NetworkFunctions::SetLowPower()` = `WiFi.aggressiveLowPowerMode()`、起きると`defaultLowPowerMode()`)。接続は保つので
   生存確認(5秒ごと)や再接続はそのまま動く。再接続で設定が戻るかもしれないので、接続が確立するたびに掛け直す。
3. **何も鳴っていなければI2Sとアンプ(休止端子)を止め、2コア目をゆっくり回す**(`SoundFunctions::SetPowerSave()`。
   鳴っているものが無い間だけ`want`を偽にする。要求が来れば次の周回で動き直す。2コア目の休みは1ms→`kPowerSaveIdleDelayMs`=20ms)。
4. **1コア目のloop()を間引く**(`IdleWait()`が`delay(kSleepLoopDelayMs=30)`。タッチで起きるまでの最大の遅れ)。
   CPUクロックは変えていない(SPI/I2S/CYW43のPIOの分周が`clk_sys`基準で、変えると全部の再初期化が要るため。効果を実測してから検討)。

**スリープへ入らない条件**: 各画面が**毎フレーム`PowerFunctions::KeepAwake()`を呼ぶ**(ヘッダの`inline`関数で、フラグを立てるだけ。
前のフレームに呼ばれたかで判断するので、**呼び忘れても「スリープに入る」だけで戻し忘れの事故が無い**。`Power_Functions.cpp`をリンクせずに
画面のホストテストを通せる)/ 音・曲が鳴っている / Wi-Fiへ接続中。スリープ中に`KeepAwake()`が来たら起きる
(ClocksScene: タイマーが鳴って`Finished`になったとき)。

KeepAwakeを呼んでいる画面: ゲームボーイ・SSH・チャット(常時)、カレンダー(取得中)、Markdown(取得/検索中)、Lua(`pico.http_request`中。`LuaEngine::HttpBusy()`)、
設定(Wi-Fiスキャン中)、時計(タイマーが鳴っているとき)。**計測中のタイマー/ストップウォッチはスリープしてよい**(`millis()`の差分で積むので、間引いても進む)。
新しく「操作が無くても動き続ける画面」を作るときは`onUpdate()`で呼ぶこと。

**スリープから起こしたタッチは画面へ渡さない**(暗い画面の見えないボタンを押さないよう。`swallow_touch`)。指を離すまで
`OSData::isTouched`等を毎フレーム下ろし続ける(Touch_Functionsは`isTouched`が偽なら新しいタッチとみなすので、離した瞬間に自然に終わる)。
コントローラーのボタンで起きた場合は握りつぶさない。

- 設定は`/sys/display.cfg`の`sleep-timeout = 秒`(0で無効、既定120)。`SettingsScene`の「スリープ」ドロップダウン(しない/1/2/5/10/30分)で編集する
  (一覧に無い値は一番近い項目を選んで見せる)。**設定画面は10行になった**(`ROW_H` 26→24。2026-10-12にタブを分けて`ROW_H`は34へ戻した。ドロップダウンの箱は行より背が高いので、
  箱の下端を行の下端へ揃えて下の行への食い込みを防いでいる。タイムゾーンも同じ)。
- ホストテスト: `power_test`(run.sh。入り方・タッチで起きて指を離すまで握りつぶす・コントローラーで起きる・KeepAwake/音/Wi-Fi接続中は入らない・
  スリープ中のKeepAwakeで起きる・0で無効)、`sound_test`(省電力でI2Sとアンプが止まり、要求で動き直し、解けば戻る)。
  依存する窓口は偽物へ差し替えている(`stubs/Arduino.h`に`delay()`、`stubs/WiFi.h`/`pc/compat/WiFi.h`に省電力モードの記録を足した)。
- PCビルドの検証: `display.cfg`に`sleep-timeout = 2`を置いて`--shot 1000`でスリープへ入る/`--tap`で復帰しそのタッチでアプリが開かない
  (2回目のタッチで開く)ことをログで確認。**PCの`--shot`はスリープ中の暗転が写らない**(`lcd->sleep()`は`PICO_GFX::SetBrightness()`を通らず、
  `main_pc.cpp`の掛け率が動かないため)。**起動から10秒はWi-Fi接続中扱い(PCは疎通が無いと`TRYING_CONNECT`のまま)でスリープに入らない**ので、
  確かめるときは10秒以上(1000フレーム程度)回すこと。
- **実機で確認済み**: `WiFi.aggressiveLowPowerMode()/defaultLowPowerMode()`の名前(実機ビルドが通る、2026-10-19)・スリープ→タッチで復帰(2026-10-23)。
  復帰の120msについては、復帰直後に画面化けすることがあるが、ほとんどが部分的で日常用途では問題ない範囲(2026-10-23、利用者の判断)。
- **実機では未確認**: `delay()`が実際にCPUを寝かせるか(arduino-picoの`delay()`はWFE/sleep系のはずだが未確認)・
  消費電流の実測(LiPo/USBの電流計で、スリープ前後を比べること)(Todoist参照)。

## 時刻の手動設定・電源メニュー (`TimeFunctions::SetClock()` / `PowerMenu_Functions` / `SettingsScene`) (2026-10-11)

- **時刻の手動設定**: 設定アプリの「時刻」タブの4行目([変更])。日付(`PickerDialog`のDate)→1フレーム空けて時刻(Time)を聞き、
  `TimeFunctions::SetLocalDateTime(y, mon, d, h, mi, s)`(今のタイムゾーンで`mktime`、存在しない日・範囲外はfalse)→`SetClock(epoch)`。
  NTP同期前(1970年)のままだとアラーム・通知・TLS・カレンダーが全部止まる(どれも2020年=`TimeFunctions::kValidYear`より前を「時計が合っていない」と見る)ため。
  後でNTPに同期できればそちらで上書きされる。`TimeFunctions::IsClockSet()`も足した。
  - **実機は`settimeofday()`**(arduino-picoがtime()/gettimeofday()のずれとして持つ。NTPも同じ口)。
  - **PC/Webは母艦の時計を変えない**(rootで動いていると本当に変わるため)。`pc/compat/PicoPcClock.h`が「ずれ」だけを持ち、
    **ネイティブのPCビルドは`main_pc.cpp`がlibcの`time()`を差し替えてずれを足す**(src/は`time(nullptr)`しか使っていないので、
    時計・アラーム・通知・TLSの有効期限が全部合わせた時刻を見る)。Webとホストテストは`time()`を差し替えないので、ずれは記録されるだけ
    (ホストテストは`PicoPcClock::Now()`で確かめる。`script/host_test/stubs/PicoPcClock.h`はpc/compatのものを取り込むだけ)。
  - `PickerDialog`のTimeの`DurationPicker`が高さ60pxで▲▼が描かれていなかった(数字48px+上下24px)ので104pxにした(Luaの`pico.show_time`も同じ)。
- **電源メニュー**(`PowerMenuFunctions::Open()`、`PickerDialog`のChoice「スリープ / 再起動」): ステータスバーを
  `kLongPressMs`(600ms)押し続けたとき(`main.cpp`が`status->is_pressing`を`UpdateAll()`の後に見張る。開いたら離したときのタップ=通知センター/
  スクリーンショットは無視)と、設定アプリの上の段の右端「電源」から開く。開いているメニューは`WidgetId`で持ち、二重に開かない。
  - **スリープ** = `PowerFunctions::SleepNow()`。指/ボタンを離した次のフレームで入る(押したまま入るとその操作で起きるため)。
    **自分で入ったスリープ(`forced`)は`KeepAwake()`を呼ぶ画面(SSH・ゲームボーイ等)・音・Wi-Fiの接続中でも続き**、タッチ・ボタン・打鍵でだけ起きる。
  - **再起動** = `RequestReboot()`→次のフレームの`PowerMenuFunctions::Update()`(`loop()`の`SceneFunctions::Update()`の直後。
    ウィジェットのコールバックの外)で、今の画面の`onExit()`(アラームの編集の保存・ゲームボーイの`.sav`・接続を閉じる等。
    上に積まれて隠れている画面は隠れたときに`onExit()`済み)→`NotificationFunctions::SaveNow()`→`LogFunctions::Flush()`→
    **`CrashDumpFunctions::Reboot()`**: パンくず・RAMの記録のmagicとスクラッチレジスタ0〜3を消してから`watchdog_reboot(0,0,0)`
    (pc=0なので`watchdog_enable_caused_reboot()`は偽。次の起動でクラッシュダンプは書かれない)。
    PCは`pc_reboot_hook`(ネイティブ: シグナルのマスクを外して`execv("/proc/self/exe")`で同じ引数のまま自分を実行し直す。
    `--tap`も付いたままなので、試すと再起動を繰り返す。Web: `location.reload()`)。
- 検証: `power_test`(SleepNow: 離してから入る・KeepAwake/音/Wi-Fi接続中でも入って続く・タッチで起きる。時刻: 2/30・13月・24時を断る、JSTの9時がUTCの0時)、
  PCビルドの`--tap`/`--shot`(時刻タブから日付→時刻→ステータスバーが変わり、母艦の`date`は変わらない。ステータスバー長押し→スリープ→タッチで復帰し、
  そのタッチでアプリが開かない。再起動で起動ログが繰り返され、`/crash/`に何も書かれない)。**実機では未確認**(arduino-picoの`settimeofday()`・
  `watchdog_reboot()`での再起動の後に`hw boot`の reason・SDの書きかけ)。
