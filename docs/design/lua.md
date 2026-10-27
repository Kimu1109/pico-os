# Luaバインディングの土台(LuaEngine・LuaScene・安全網・自動登録)

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## Luaバインディング (`src/lua/LuaEngine`) (2026-09-19着手)

Lua<->C++を繋ぐ実行エンジン。**1インスタンス=1つのlua_State=1つのLuaアプリ**という対応で、
既存の受け皿(`WidgetFactory`/`WidgetRegistry`/`WidgetProperty`/`ErrorFunctions`)を
薄く橋渡しするだけの設計にしてある(新規に持つ状態は「pico.*」というAPI表と、
コールバック中継用の小さな対応表だけ)。**まだSDからスクリプトを読んで実行する
`LuaScene`は無く、`LuaEngine`単体をホストテスト(`script/host_test/lua_engine_test.cpp`)
から動かして検証した段階。**

### ライフサイクルとメモリ

- コンストラクタで`lua_newstate()`にカスタムアロケータ(`BudgetAlloc`)を渡し、
  確保量に`budget_bytes`の上限を課す。超過時は`lua_newstate`自体がnullptrを返すか、
  `LUA_ERRMEM`として安全に失敗する(`lua_alloc_budget_test.cpp`で確認済みの挙動)。
  **`luaL_openlibs()`とAPI登録は`InitTrampoline`という1つのC関数へまとめ、必ず`lua_pcall`
  越しに呼ぶ**。保護せずに直接呼ぶと、初期化中のOOMがLuaの仕様上そのまま`abort()`する
  ため(これも`lua_alloc_budget_test.cpp`で踏んで学んだ)。
- `valid()`が`false`の場合、構築失敗(予算不足)なので呼び出し側はアプリの起動自体を諦める。
- `Run(script, chunkname)`: 読み込み(`luaL_loadbuffer`)と実行(`lua_pcall`)の両方を1関数でこなし、
  構文エラー・実行時エラーはどちらも捕捉して`ErrorFunctions::ShowFatal()`へ渡してから`false`を返す。
  呼び出し側は追加のエラー処理をしなくてよい。

### `pico.*` API(Lua側から見える面)

| 関数 | 内容 |
|---|---|
| `pico.create(type_name)` | `WidgetFactory::TypeFromName()`→`Create()`。生成物は即`WidgetFunctions::Add()`で登録し、`WidgetId`(整数)を返す |
| `pico.destroy(id)` | コールバック登録を`PruneCallbacksFor()`で外してから`WidgetFunctions::DestroyLater()`(フレーム境界での遅延削除) |
| `pico.set(id, name, value)` / `pico.get(id, name)` | `WidgetProperty::IdFromName()`→`Set()`/`Get()`。プロパティ名は`snake_case`の文字列 |
| (追加分) | 2026-10-05に `off` `text_set` `set_dots` `set_name/find/parent/children/get_rect` `bring_to_front/send_to_back` `scroll_to` `list_*` `tab_*` `show_choice/date/time/number/progress` `draw_text_wrapped` `measure_text` `get_pixel` `image_create/target/clear` `app_dir` `path_join` `time` `wifi_status` `url_*` `base64_*` `settings_*` `memory_info` `toast` `on_back/go_back` `encrypt/decrypt/is_encrypted/hash/random_bytes` を足した。一覧は下記「Lua APIの追加(2026-10-05 その2)」 |
| `pico.on(id, event_name, fn)` | 4種の共通イベント(`press_start`/`press_end`/`press_move`/`press_out`。**`fn(id, x, y, lx, ly, dx, dy)`でタッチ座標も届く**、下記「Lua APIの追加(2026-10-05)」)+`render`(`Canvas`限定、下記「直接描画」参照)+ウィジェット固有4種(`checked_changed`/`value_changed`/`select_item`/`tab_changed`、下記「ウィジェット固有イベント」参照)+`closed`(ダイアログ限定)に対応(下記) |
| `pico.add_child(container_id, child_id)` | `LayoutContainer`/`GridContainer`/`ScrollContainer`のみ対応 |
| `pico.remove_child(container_id, child_id)` | `add_child`の逆。破棄せず取り外す。取り外した子はフラットリストへ独立したルートとして戻る(下記「コンテナからの取り外し」参照)(2026-09-21追加) |
| `pico.list_add(id, text)` / `pico.list_clear(id)` | `ScrollList`/`DropdownMenu`へ項目を足す/全消しする(下記「リストへの項目追加」参照)(2026-09-21追加) |
| `pico.tab_add(id, label)` | `TabBar`へタブを足す。`kMaxTabs`(4)超過なら`false`(下記「リストへの項目追加」参照)(2026-09-21追加) |
| `pico.log(msg)` | `LOG_APP_MSG` |
| `pico.show_error(msg)` | `ErrorFunctions::ShowFatal()` |
| `pico.pop([result])` | `SceneFunctions::Pop()`。`LuaScene`から起動されたアプリがランチャへ戻るためのもの。`result`は`push_scene`した親の`on_result(result)`に届く(2026-09-19追加) |
| `pico.content_rect()` | `Scene::contentRect()`を`x,y,w,h`の4値で返す。ステータスバー分を避けた配置に使う(2026-09-19追加) |
| `pico.draw_pixel(x,y,color)` / `draw_line(x0,y0,x1,y1,color)` / `draw_rect(x,y,w,h,color)` / `fill_rect(...)` / `draw_circle(x,y,r,color)` / `fill_circle(...)` / `clear_rect(x,y,w,h[,color])` / `draw_text(x,y,text[,color[,font_size]])` | `OSData::frame`へ直接描く。**`Canvas`の`render`コールバック内で使うこと**(下記「直接描画」参照)(2026-09-20追加) |
| `pico.invalidate(id)` | 対象ウィジェットの画面矩形を`needsRender()`でdirty化(次のFlushDirty()で`render()`が呼ばれる)。`Canvas`に限らず任意のウィジェットに使える汎用API(2026-09-20追加) |
| `pico.mark_dirty(x,y,w,h)` | `PICO_GFX::MarkDirty()`の生の下請け。任意の矩形を直接dirty化したいとき向けの低レベルAPI(2026-09-20追加) |
| `pico.set_palette(index,r,g,b)` / `pico.get_palette(index)` / `pico.reset_palette()` | パレットの1〜14番(黒0・白15は固定)の色を差し替える(2026-10-07)。実体は`PICO_GFX::COLORS`(RGB565、可変)+`paletteRevision`で、`FlushDirty()`がframeのパレットを合わせて全画面を描き直す(行ハッシュも無効化)。`CanvasRaster`/`ImageView`は`render()`で、Luaの画像は`set_palette`時に追従。**`LuaEngine`のデストラクタが既定へ戻す**(アプリ終了・画面遷移)。ホストテストは`lua_ext_test`、PCビルドの`--shot`で確認、実機は未確認 |
| `pico.get_draw_area()` | 今のクリップ矩形を`x,y,w,h`で返す(無ければw/hが0)。`render`の中では「Canvasとdirty矩形の重なり」なので、部品の多い絵で描き直しが要る部分だけを描ける(テトリスの盤面)(2026-09-26追加) |
| `pico.set_draw_area(x,y,w,h)` / `pico.clear_draw_area()` | `OSData::frame->setClipRect()`/`clearClipRect()`。以降の`pico.draw_*`をこの矩形の内側だけに制限する/解除する(下記「直接描画エリア」参照)(2026-09-20追加) |
| `pico.sd_exists(path)` | `OSData::SD.exists()`。`bool`を返す(2026-09-20追加) |
| `pico.sd_read(path)` | ファイル全体を文字列で返す。無い/開けない/上限超過は`nil`(下記「SDカードアクセス」参照)(2026-09-20追加) |
| `pico.sd_write(path, content[, append])` | 新規作成+上書き(既定)、または`append=true`で追記。成否を`bool`で返す(2026-09-20追加) |
| `pico.sd_remove(path)` | ファイルなら`SD.remove()`、ディレクトリなら`PICO_IO::removeRecursive()`(`FileExplorer`の削除と同じ判断)(2026-09-20追加) |
| `pico.sd_mkdir(path)` | `OSData::SD.mkdir()`(2026-09-20追加) |
| `pico.sd_list(path)` | ディレクトリを列挙し`{ {name=..., is_dir=...}, ... }`の配列を返す。パスが無い/ディレクトリでないなら`nil`(2026-09-20追加) |
| `pico.config_read(path)` / `pico.config_get(path, key)` / `pico.config_write(path, key, value)` | `key=value`形式の設定ファイル(`PICO_Config`と同じ書式)の読み書き。値は文字列で返す。書き込みは`PICO_Config::SetValue()`(1キー差し替え)。**`app.cfg`へは書けない**(下記「設定ファイルとapp.cfgの書き込み制限」参照)(2026-09-30追加) |
| `pico.image_load(path)` | `.pimg`をデコードして整数ハンドルを返す。失敗(SD無し/パス不正/不正な`.pimg`/上限超過)は`nil`(下記「画像」参照)(2026-09-21追加) |
| `pico.image_size(handle)` | 読み込んだ画像の`width, height`を返す。無効なハンドルはエラー(2026-09-21追加) |
| `pico.draw_image(handle, x, y)` | 画像を描く。他の`pico.draw_*`と同じく**`Canvas`の`render`コールバック内で使うこと**。無効なハンドルはエラー(2026-09-21追加) |
| `pico.image_free(handle)` | 画像を明示的に解放する。無効/解放済みハンドルは`pico.destroy`と同じく黙って無視(2026-09-21追加) |
| `pico.draw_image_part(handle, x, y, sx, sy, w, h)` | 画像の一部だけを描く(スプライトシートからの切り出し。画像は4枚までなので部品の多い絵は1枚にまとめる)。今のクリップと描き先の重なりへクリップを一時的に狭めてから画像全体をずらして`pushSprite()`し、クリップは元へ戻す(2026-09-26追加) |
| `pico.canvas_clear(id)` | `CanvasRaster`(`pico.create("CanvasRaster")`)を白紙(`PICO_WHITE`)へ戻す。対象がCanvasRaster以外/無効なIDはエラー(下記「ラスタキャンバスの保存/読み込み」参照)(2026-09-23追加) |
| `pico.canvas_save(id, path)` | `CanvasRaster`の中身を`.pimg`としてSDへ書き出す。成否を`bool`で返す(SD無し/権限外/書き込み失敗はfalse。対象種別/IDが不正ならエラー)(2026-09-23追加) |
| `pico.canvas_load(id, path[, keep_size])` | `.pimg`を読み込み`CanvasRaster`へ反映する。**読み込んだ画像のサイズへキャンバス自体もリサイズされる**(内容は消える)。`keep_size=true`なら大きさを変えず白紙にしてから左上に合わせて読む(2026-09-24追加)。成否を`bool`で返す(2026-09-23追加) |
| `pico.canvas_undo(id)` | 1段だけの「元に戻す」(もう一度でやり直し)。`undo_enabled`が有効なときだけ効く(下記「ペイント」参照)(2026-09-24追加) |
| `pico.push_scene(path [, args])` / `pico.change_scene(path [, args])` | 別のLuaスクリプトへ`SceneFunctions::Push/Change`する。`args`は子の`pico.args()`で受け取る(下記「シーン制御」「Lua APIの追加(2026-10-05)」参照)(2026-09-21追加) |
| `pico.launch_app(name)` | `AppFunctions::LaunchByName()`経由で登録簿の任意のアプリ(C++製含む)へ`Push`する。見つかれば`true`、無ければ`false`(下記「シーン制御」参照)(2026-09-21追加) |
| `pico.show_message(text, cancel_text, ok_text)` | `MsgDialog`を表示する。閉じた結果は`pico.on(id,"closed",fn)`で受ける(下記「ダイアログ」参照)(2026-09-21追加) |
| `pico.show_input(label, initial_text, is_single_line)` | `InputDialog`を表示する。入力文字列は`pico.get(id,"text")`で読む(2026-09-21追加) |
| `pico.show_file_save(start_dir[, default_name])` / `pico.show_file_select(start_dir)` | `FileSaveDialog`/`FileSelectDialog`を表示する。選択パスは`pico.get(id,"path")`で読む(未選択は`nil`)(2026-09-21追加) |
| `pico.show_color()` | `ColorDialog`(4×4パレット)を表示する。選択色は`pico.get(id,"value")`で読む(未選択は`-1`)(2026-09-21追加) |
| `pico.http_request(method, url, body, content_type, callback [, opts])` | 非同期HTTPリクエスト(GET/POST/PUT/PATCH/DELETE)。走るのは1本、2本目以降は順番待ち(最大4本)。戻り値はリクエストID。`opts={headers=, save_to=}`、`callback(ok, status_code, body_or_nil, error_or_nil, headers, info)`(下記「ネットワーク」と「Lua APIの追加(2026-10-05)」参照)(2026-09-21追加、2026-10-05拡張) |
| `pico.http_cancel([id])` | `pico.http_request()`を取り消す(idなしは走っているものも順番待ちも全部)(2026-09-21追加) |
| `pico.get_time()` | `TimeFunctions::timeinfo`を`{year, month, day, hour, min, sec, wday}`のテーブルで返す(下記「時刻取得」参照)(2026-09-21追加) |
| `pico.get_touch()` | 現在(直近)のタッチ位置を`x, y, is_touched`の3値で返す。`pico.draw_*`と同じ絶対スクリーン座標(下記「タップ位置の取得」参照)(2026-09-21追加) |

- **プロパティ名・種別名は文字列(snake_case/PascalCase)にした**(数値定数にしなかった)。
  Lua側の書きやすさを優先した判断で、毎回文字列比較が挟まるが、UI操作程度の頻度なら実害は無いはず。
  対応表は`WidgetProperty::IdFromName()`/`WidgetFactory::TypeFromName()`に集約してあるので、
  プロパティ/種別を増やす際はそこへ1行足すだけでよい(片方だけ更新すると「Luaから見えない」
  というずれ方をするので、`WidgetProperty.cpp`のコメントで注意喚起してある)。
- **数値プロパティはLua側の1/1.0の書き分けを吸収する**(`l_set`)。`WidgetProperty`側は
  プロパティごとに期待する型(Int/Float)が固定だが、Lua側に「整数リテラルで書け」
  「浮動小数点数で書け」を強制するとミスの元になるため、整数値に見えるならまずIntとして試し、
  ダメならFloatとして試す。
- **未対応の値(型不一致・非対応プロパティ)は`pico.set`側はエラー、`pico.get`側はプロパティ名が
  有効ならnil**(名前自体が無効ならどちらもエラー)。「setは黙って失敗させない」
  「getは無ければnilというLuaの慣習に合わせる」を使い分けてある。

### 権限(LuaPermissions、2026-09-21実装)

Luaアプリの権限管理の第一歩として、`network`/`sd_outside_app_dir`の2値(`src/lua/LuaPermissions.hpp`)
だけを見る粗い実装にした。パスのホワイトリストやホスト単位の制限のような細かい制御はまだ無い。

- **`LuaEngine`のコンストラクタが`LuaPermissions`と`app_dir`(文字列)を追加で受け取る**
  (どちらもデフォルト引数があるので、権限を意識しない既存の呼び出し元
  (ホストテスト等)は今まで通り`LuaEngine(budget)`のままで良い)。`app_dir`は
  `PICO_IO::normalize()`して`app_dir_`へ持つ。**既定値の`"/"`は「制限なし」に相当する**
  (ルート配下=あらゆる絶対パスが該当するため)。`LuaScene`を介さず`LuaEngine`を
  直接使う場面(ホストテスト等)はこの既定のままなので、今回の変更で挙動は変わらない。
- **実際に効くのは`LuaScene`経由の場合だけ**。`LuaScene::onEnter()`が
  `PICO_IO::parent(script_path)`でスクリプト自身の親ディレクトリを毎回計算し直して
  `app_dir`として渡す(`push_scene`/`change_scene`で別ファイルへ移った場合、
  そのファイル自身の場所を見るのが正しいため、`LuaScene`のメンバへ固定して
  持ち回したりはしない)。
- **ゲートしているのは`pico.http_request`(network)と、`pico.sd_exists/read/write/remove/mkdir/list`
  +`pico.image_load`(sd_outside_app_dir)**。いずれも拒否時は`luaL_error`にはせず
  `false`/`nil`を返すだけ(SD無し等、既存の「実行時の状態」枠と同じ扱い。
  プログラマの書き間違いだけでなく、想定通り動くスクリプトが試しうる経路でもあるため)。
  ただし`LOG_APP_WARN`は出す(SD無しのような日常的な状態とは違い、権限の壁に
  当たったことは開発者が気づけるようにしておきたいため)。
  判定は`LuaEngine::SdPathAllowed()`の1箇所に集約してある:
  `sd_outside_app_dir==true`なら常に許可、`false`なら`path`を`PICO_IO::normalize()`した
  上で`app_dir_`自身か`app_dir_+"/"`始まりかを見る(`normalize()`が`..`によるルート越え
  自体は既に防いでいるので、ここでは「どのディレクトリ配下か」だけを見ればよい)。
- **権限は「スクリプトファイル単位」ではなく「アプリ単位」で決まるモデルにした**。
  `pico.push_scene()`/`pico.change_scene()`は呼び出し元の`LuaEngine`が持つ
  `LuaPermissions`をそのまま新しい`LuaScene`へ引き継ぐ(`LuaEngine::permissions()`)。
  複数画面のLuaアプリで2画面目以降だけ権限が既定値(最小権限)へ落ちてしまうと
  分かりにくいバグの元になるため。一方`pico.launch_app()`は対象アプリ自身の
  `AppEntry::create()`(=その`Register()`呼び出しが決めた権限)へ委ねるので、
  ここでは何も引き継がない(別アプリへの遷移なので独立した権限であるべき)。
- **権限の割り当ては現状、アプリ登録側(`App_List.cpp`)が`Register()`呼び出しごとに
  手書きする**。`AppFunctions::MakeSceneWithArg<LuaScene>`は`entry.arg`(パス)しか
  `LuaScene`へ渡さない汎用テンプレートのままにしてあり(他の`MakeSceneWithArg<T>`
  利用者に影響を与えたくないため)、権限が必要なアプリは専用の生成関数を書く
  (`App_List.cpp`の`MakeLuaHelloScene()`が実例。同梱デモ`hello.lua`は`/lua/`の外
  (`/img/hello.pimg`)を読むため`sd_outside_app_dir=true`を明示的に与えている)。
  **Luaアプリが増えて権限の組み合わせも増えたら、`AppEntry`へ権限フィールドを
  持たせる形へ一般化することを検討する**(今は登録されているLuaアプリが1つだけなので、
  汎用化は時期尚早と判断した)。**SDを走査して動的にLuaアプリを登録する仕組み
  (下記「Lua着手前の受け皿の状態」参照)ができた際は、その時点でこの割り当て方法を
  再設計する必要がある**(スキャンで見つけたスクリプトに対し、誰が何を根拠に
  権限を決めるかがまだ無い)。

### コールバック中継の設計(ヒープを使わない理由)

> 2026-10-27: ウィジェットのコールバックは `std::function` から `InplaceFunction`(箱に収まらないキャプチャはコンパイルエラー)へ替えた
> (`performance.md`「シーンアリーナ」)。下の中継の設計はそのまま、キャプチャ「`LuaEngine*`+`WidgetId`」が箱に収まる。

`Widget::on_press_start`等は`std::function<void()>`のままシグネチャを変えていない
(前回のセッションでは再設計を見送ると決めていた箇所)。素朴に`lua_State*`とLuaの
registry ref(関数への参照)をラムダへキャプチャすると16Bを超え、`std::function`の
小バッファ最適化(SBO)からあふれてヒープ確保が起きる。

代わりに、`LuaEngine`が「`WidgetId`+イベント種別 → Lua registry ref」という対応表
(`callbacks_`、単純な`std::vector`の線形探索。1ウィジェットあたり高々4イベントなので
十分速い)を自分で持ち、ウィジェット側へ設定するラムダは**`this`(`LuaEngine*`)と
`WidgetId`(4B)だけをキャプチャする**ようにした。これなら合計12B程度でSBOに収まり、
`pico.on()`を何回呼んでもヒープ確保は増えない。実際に鳴らす際は
`Dispatch(id, kind)`が対応表からrefを引き、`lua_pcall`越しにLua関数を呼ぶ
(失敗時は`ErrorFunctions::ShowFatal()`)。

### 実装中に見つけて直した既存のバグ2件

Luaバインディングを実際に動かして初めて踏んだ、`LayoutContainer`/`GridContainer`が
「Luaアプリ向けに用意したが実コードでは一度も使われていなかった」ことに起因する
潜在バグ。どちらも`lua_engine_test.cpp`がASanで検出し、修正して回帰確認済み。

- **`pico.add_child`の重なり順**: `WidgetFunctions::widgets`は追加順=描画順(後が上)のフラットな
  配列で、`Widget::needs_children_update`が立った次のフレームにコンテナ経由で子孫を再スイープする
  仕組みは既にあった(`Widget_Functions.cpp::UpdateAll()`)。しかし生成順が「子→親」だと、
  子が配列内で親より手前(=下)の位置に残ったままになり、親の描画に隠れてしまう。
  対策: `pico.add_child()`は`container->add(child)`する前に一度`WidgetFunctions::Remove(child)`で
  フラットリストから外す。こうすると次フレームの再スイープで「まだ登録されていない子」として
  親の直後(=上)へ正しく入り直す。
- **`ScrollContainer`に`removeChild()`が無かった**: `LayoutContainer`/`GridContainer`は
  `removeChild()`をoverrideして`children_`から取り除くが、`ScrollContainer`だけ無く、
  基底の空実装のままだった。そのため子を`WidgetFunctions::Destroy()`等で個別に破棄すると、
  `children_`に残った破棄済みポインタを`~ScrollContainer()`がもう一度`delete`し二重解放になる。
  `pico.destroy(child_of_scroll_container)`で実際に踏める経路だったため、
  `LayoutContainer`と同じ形の`removeChild()`を追加した。

### `LuaScene`(`src/gui/scenes/LuaScene.hpp/.cpp`)(2026-09-19実装)

SD上のLuaスクリプトを1本読んで実行する画面。`AppEntry`の`MakeSceneWithArg<LuaScene>`パターンで
スクリプトパス("/lua/hello.lua"のようなSD絶対パス)を`arg`に渡して登録する
(同じ`LuaScene`型を別のargで何個でも登録できるので「Luaスクリプトごとに1タイル」が作れる)。

- **ライフサイクル**: `onEnter()`で`LuaEngine`を`new`(予算`kLuaBudgetBytes=200KB`)、
  スクリプトをSDから読んで`Run()`する。`onExit()`で`delete`。**Push()で背後へ退避される場合も
  `onExit()`は呼ばれる**(`Scene`のレイヤ説明通り)ので、Lua stateもウィジェットと同じく
  「シーンがアクティブな間だけ」の寿命にした。他のシーンのように状態をメンバへ退避して
  `onEnter()`で復元する形にはできない(Luaアプリの状態はスクリプト内のLua変数にあり、
  C++側から見えないため)ので、**`Pop()`で戻ってきたらスクリプトを最初から実行し直す**。
- **スクリプトの読み込み**は`MarkdownView::load()`と全く同じ手順(256Bのスタックチャンクで
  読み進めてメンバの`FixedString`へ追記。ヒープを使わない)。上限は`kMaxScriptBytes=16KiB`
  (`MarkdownView`の8KiBより大きくしてあるのは、`pico.*`呼び出しの羅列は文書より冗長になりがちなため)。
  超過分は警告ログを出して打ち切る。このバッファは`LuaScene`のメンバなので、`LuaScene`自体が
  `MarkdownScene`と同じく「シーン本体は数十バイト」の例外になる。
- **ランチャへ戻る手段はスクリプト側が自前で用意する**(`pico.create("Button")`+
  `pico.on(id, "press_start", ...)`で`pico.pop()`を呼ぶ。`ClocksScene`/`CalculatorScene`等、
  他のアプリの「戻る」ボタンと同じ考え方)。これに伴い`LuaEngine`へ2関数を追加した:
  - `pico.pop()`: `SceneFunctions::Pop()`を呼ぶだけ
  - `pico.content_rect()`: `Scene::contentRect()`を`x,y,w,h`の4値で返す。
    Luaアプリだけステータスバーの下に潜り込む、といったズレを防ぐ
- **動作サンプル**: `pc/sdcard/lua/hello.lua`(ボタンでカウンタが増える最小限の画面)を
  `App_List.cpp`に`Register("Lua Hello", IconID::AppBox, &MakeSceneWithArg<LuaScene>, "/lua/hello.lua")`
  として登録済み。PCビルドで`--tap`により実際にタップ→カウンタ更新→戻るまで動作確認済み
  (`pc/build/picoos_pc`)。
- ホストテストは`script/host_test/lua_scene_test.cpp`。`Scene_Functions.cpp`と組み合わせた
  結合テストで、SDからの読み込み・`pico.pop()`での実際のランチャ復帰・ファイル不在時の
  ダイアログ表示・大きすぎるスクリプトの打ち切り警告を確認している
  (`script/host_test/stubs/SdFat.h`の`HostSd::files`にスクリプトを登録して読ませる)。

### Luaは32bit(`LUA_32BITS`、2026-10-09)

`lib/lua/src/luaconf.h`の`LUA_32BITS`を1にした(vendorしたLuaへの唯一の改造。`lib/lua/README-pico-os.md`)。整数は32bit、小数は単精度。
RP2350は単精度しかハードウェアで計算できず、実機のゾンビTDで1フレームのLuaが5〜9ms(PCの約30倍。PCで測るとごみ集めはほぼ0で、
Luaの計算そのものが一様に遅かった)かかり、メモリも足りなかったため。実機では値1つが16→8バイト。
- 合わせて直したもの: `pico.json_decode`は32bitを超える整数を小数にする(以前は`strtoll`の値をそのまま詰めて化けた)、
  `json_encode`は単精度の小数を`%.7g`で書く、`pico.micros()`の差は`b - a`でよい(ゾンビTDの`us_since`)。
- Luaから見える違いはAPIドキュメントの`reference/limits.md`「数の範囲」。`WidgetId`(上位6bitが種類)は負の整数になりうるが、C++側は
  `(WidgetId)luaL_checkinteger()`でビット列のまま戻すので往復できる。
- 確認: ホストテスト全部(`lua_engine_test`に32bitとJSONの項目)、PCビルドのゾンビTD、実機ファームのビルド(`pio run`)。
- 実機の結果(ゾンビTD): Luaのメモリは約176KB → 約131〜159KB、ゾンビ0匹の始めのLuaは約5ms → 約0.4ms。ただ遊んでいる間は
  0匹でも4〜7ms、12〜18匹で8〜12ms(PCの約30倍のまま)。PCで区間ごとに測ると突出した所は無く、Luaの実行そのものの速さと見て、
  **ゾンビTDのゲームの計算を1秒に30回(`SIM_MS`=33ms)にまとめた**(描き直しと操作は毎フレーム)。PCで1フレーム0.37ms → 0.12ms。
- 同じときに見つけたメモリ: **曲の置き場を曲の大きさだけ確保するようにした**(`Sound_Functions.cpp`の`music_slots`。以前は最初に鳴らしたときに
  6KiB×2+読み取り係 約3.5KBを確保して持ち続け、ジングルを鳴らすだけのゾンビTDが約15KBを抱えていた。読み取り係と6KiBの作業場所は
  読む間だけ確保する)。`pico.iso`の「ごみを集めてやり直す」は1秒に1回まで(全部集めるのは実機で数十msかかり、兵士の数だけ続いて止まった)。

### Luaの確保がヒープの残りを守る(`LuaEngine::Alloc` / `MemFunctions::HeapTopRoom()`、2026-10-10)

実機のゾンビTDが「ウェーブ4以降にランダムで再起動する」報告への対策。**Luaの予算(200KB)はヒープ全体の空きより大きい**ので、
Luaが予算の内側でもヒープを使い切ることがあり、そのとき次のC++側の確保(`new`/`std::vector`/`std::string`/SD/Wi-Fi)が失敗する。
Luaの確保の失敗はエラーで済むが、C++の`new`の失敗はabortで本体ごと落ちる(再起動の候補として一番濃い。クラッシュダンプは未確認)。
- `MemFunctions::HeapTopRoom()`(`Mem_Functions.hpp`、inline): ヒープの末尾`sbrk(0)`から`__HeapLimit`(arduino-picoのリンカスクリプト。
  SRAMの終わり=0x20080000。弱いシンボル)までの未使用の量。O(1)。PC/ホストは`kRoomUnknown`で、テストは`heap_room_hook`で偽る。
  `Mem_Functions`の`stack_headroom`(= `pico.memory_info().heap_headroom`)もこれで測るようにした(以前はスタックポインタまでで、
  実機のスタックはSCRATCHにあるので大きく見えていた)。
- `LuaEngine::Alloc()`: 増やす確保で、残りが`kHeapReserveBytes`(20KB)+確保量を割りそうなときだけ`malloc`+コピーで確保し、
  前後で末尾が伸びて残りが20KBを割っていたら返して断る(空きブロックから取れたなら通す)。断るとLua本体はごみを集めて1回だけ頼み直し
  (lmem.cの`tryagain`)、それでも足りなければ「not enough memory」のエラー(再起動はしない)。断った回数は`pico.memory_info().heap_refused`、
  残りは`heap_room`。ゾンビTDの5秒ごとの`[TD]`の行に`refused=`を足した。
- 検証: `lua_engine_test`(差し替え口で「末尾を伸ばさない確保は通す」「伸ばして割る確保は断りエラーで止まる」「余裕が戻れば続く」)、
  実機ファームのビルド(maxgerhardtのplatform-raspberrypiで`pio run`。`__HeapLimit`が0x20080000に解決される)。**実機での効果は未確認**。

### pico.* の関数の置き方(Luaのメモリを減らす、2026-10-09)

ゾンビTDにコントローラーの操作を足してLuaの予算が足りなくなったのを受けて、全部のLuaアプリに効く形で減らした(PCで起動直後に約23KB減)。
- **`pico.*`は上位値の無い素のCの関数**(`lua_pushcfunction`)。`this`は`lua_getextraspace(L)`に置き(`lua_newstate`の直後)、各ファイルの`Self(L)`がそこから読む。
  コルーチンへは`lua_newthread()`が写す。以前は`this`を上位値に持つCのクロージャ約320個で、クロージャだけでPCで約11KBだった。
  `pcall`の包み・`coroutine.wrap`等、自分で`lua_pushcclosure`しているものは上位値のまま(上位値1が`this`)。
- **`pico`/`pico.iso`のテーブルは空で始まり、`__index`(`l_api_index`)が初めて使われた関数だけをテーブルへ入れる**。関数の一覧は
  `registerFn()`が`api_pico_`/`api_iso_`(`std::vector<luaL_Reg>`。名前は文字列リテラルを指すだけ。Luaの予算の外、実機で約2.5KB)へ積み、
  `finishApiTable()`が名前順に並べて(同じ名前は後から足したもの=差し替えを残す)メタテーブルを付ける。**`pairs(pico)`では使ったことのある関数しか見えない**。
  `json_null`・`iso`のような関数でない値は今までどおりテーブルに直接入る。
- **先読み(`preloadModules`)は読み込み済み(`pico_loaded`)のモジュールを飛ばす**。以前は`Run()`のたびに`require`しているモジュールを全部コンパイルし直して
  `pico_preload`へ置き、使われないまま残っていた(アプリは`Run()`1回なので実害は小さいが、テストや`pico.require`を書いた`Run()`を重ねると積もった)。
- `pico.game`の`g:tilemap{rows=}`は1行ぶんの表を使い回す(行ごとに作っていて、100x40のマップで一時的に約80KBのごみが出てテストの予算160KBに当たった)。

### 実行時間の安全網(暴走防止、2026-09-21実装)

OSは単一スレッドのポーリングループ(`main.cpp`の`loop()`)なので、Luaのコールバック
(`loop(dt)`/`press_start`/…)の中に`while true do end`のような終わらないループが
あると、`lua_pcall()`が戻ってこずOS全体が固まる。C++側の新規コードにはレビューが
あるが、Luaスクリプトは(このあとの「SDを走査したLuaアプリの自動登録」でますます)
書く人を選ばなくなるため、この種の事故を検出できる仕組みを入れた。

- **`lua_sethook(L, InstructionHook, LUA_MASKCOUNT, kHookInstructionInterval)`を
  コンストラクタで1回だけ設定**し、Luaバイトコードを`kHookInstructionInterval`
  (=1000)命令実行するたびに`InstructionHook()`を呼ぶ。外部から見える呼び出し
  (`Run`/`CallSetup`/`CallLoop`/各種`Dispatch`/HTTPコールバック、合計8箇所)は
  共通のprivateヘルパー`ProtectedCall(nargs)`を必ず経由し、そこで
  「この1回の呼び出しで消費してよい命令数」の残高(`instructions_remaining_`)を
  `kMaxInstructionsPerCall`(=200万、暫定値)へ積み直してから`lua_pcall()`する。
  `InstructionHook()`は毎回この残高を減らし、尽きたら`luaL_error()`で
  Luaのエラー機構(内部はlongjmp)経由に処理を戻す。
- **`lua_pcall()`から見れば通常の実行時エラーと区別が付かない**ため、`Run()`等の
  既存のエラーハンドリング(`ErrorFunctions::ShowFatal()`でログ+ダイアログ)を
  そのまま使い回せる。新しい状態は`instructions_remaining_`(1個のuint32_t)だけ。
- **Lua自身のバイトコード実行だけを数える。** `pico.sd_read`等のC関数の中
  (ファイルI/O等)ではフックは発火しない——C関数呼び出し中はインタプリタが
  バイトコードを進めていないため。「うっかり書いた無限ループ」を捕まえる用途に
  対し、C++側の正当な処理を巻き込まない。
- **時間ではなく命令数で打ち切る。** `millis()`ベースの時間打ち切りも検討したが、
  ホストテスト環境の`millis()`スタブが常に0を返すため時間ベースでは検証できず、
  実機の処理速度にも依存して閾値の意味が変わってしまう。命令数ならホストテストでも
  `while true do end`を実際に実行してエラーになることを確認でき(実測: ASan+UBSan
  付きでも約50ms程度で打ち切りに達する)、ハードウェアに依存しない決定的な基準になる。
  `kMaxInstructionsPerCall=200万`は暫定値(実機RP2350での実測は未実施。RAM/Flash予算の
  「200KB」と同種の「後で実機で確かめる」枠)。
- ~~既知の限界: Luaの`pcall`で自前でエラーを握り潰して繰り返す敵対的なスクリプトまでは
  防げない~~ → **解消済み(2026-10-03)**。下の「開発者向けの道具」の「Luaサンドボックスの強化」参照。
- ホストテストは`lua_engine_test.cpp`に追加。終わらないループを含むスクリプトが
  `Run()`/`CallLoop()`をハングさせず`false`で戻ること(テストプロセス自体がハング
  しないことが最大の確認点)、打ち切り時もダイアログが出ること、Lua側の`pcall`で
  捕まえれば普通に続行できること、上限内のループは邪魔されないこと、`loop()`内で
  打ち切られた場合は既存の`loop_broken_`安全弁と重ねて効く(以降`loop()`自体が
  呼ばれなくなる)ことを確認している。

### SDを走査したLuaアプリの自動登録(LuaAppScanner、2026-09-21実装)

`AppFunctions::Register()`はC++からしか呼べず、「SDにスクリプトを置くだけでランチャに
タイルが出る」というLua版の"アプリストア"的な使い方の土台が無かった
(`AppEntry`が`FixedString`で動的な名前/argを持てるようになった時点(2026-09-13)で
下地はできていたが、実際に走査してRegister()を呼ぶ側が無かった)。

- **`src/lua/LuaAppScanner.hpp/.cpp`が新設した唯一の関数は`LuaAppScanner::Scan()`。**
  `/lua/apps/<名前>/main.lua`という「サブディレクトリ1つ=アプリ1つ」の構成で
  `/lua/apps/`直下を走査し、`main.lua`があるサブディレクトリを見つけるたびその
  ディレクトリ名をそのままタイル名として`AppFunctions::Register()`する。
  `App_List.cpp::Setup()`の末尾(静的登録の後)で1回呼ぶだけで配線は完了する。
- **なぜサブディレクトリ単位か(`/lua/apps/`直下へフラットに`*.lua`を置く案は不採用)**:
  `LuaScene::onEnter()`はスクリプト自身の親ディレクトリを`app_dir`(SDアクセスの
  閉じ込め先)として使う。フラットに置くと複数アプリのapp_dirが全部`/lua/apps/`
  自身になり、見つかった複数のアプリが互いのファイルを読み書きできてしまう。
  サブディレクトリ単位にすることで、各アプリが自分の`main.lua`の親ディレクトリ
  (=自分専用のサブディレクトリ)だけに閉じ込められる。
- **権限は既定値(`LuaPermissions{}`、`network`/`sd_outside_app_dir`ともfalse)固定。**
  SDに置かれているだけで中身を検証していないスクリプトへ、走査した側が勝手に
  強い権限を与えないための判断。ネットワークやapp_dir外のSDアクセスがどうしても
  要るLuaアプリは、従来通り`App_List.cpp`へ専用の生成関数(`MakeLuaHelloScene()`と
  同じ形)を書いて手動登録する(このパスはスキャン対象外のまま)。
- **`/lua/`直下(`hello.lua`等の動作サンプル)とは別ディレクトリにしてある。**
  `/lua/`直下を走査すると、`hello_sub.lua`/`hello_sub2.lua`のような
  「他のスクリプトから`push_scene`/`change_scene`で遷移するためだけのサブ画面」まで
  誤って1タイルずつ登録してしまうため。
- **`App_Functions.cpp`自体は変更していない。** `LuaAppScanner.cpp`は`App_List.cpp`と
  同じ立ち位置(`AppFunctions::Register()`の1利用者)で、シーン実装
  (`LuaScene`)に依存するコードを`App_Functions.cpp`へ持ち込まない、という既存の
  分離方針(CLAUDE.md「アプリの枠組み」参照)をそのまま踏襲している。
- **名前の重複チェックはしていない。** 静的登録とスキャンで同名のアプリがあれば
  単純に2タイル並ぶ(後勝ちで上書きはしない)。今のところ登録されるLuaアプリの
  絶対数が少ないため、重複解決の優先度は低いと判断した。
- 動作サンプル: `pc/sdcard/lua/apps/スキャン確認/main.lua`。`App_List.cpp`には
  一切手を加えず、PCビルドの`--shot`でランチャに「スキャン確認」タイルが実際に
  現れてタップで起動できることを確認済み。
- ホストテストは`script/host_test/lua_app_scanner_test.cpp`。**完全な走査結果は
  ホストのSdFatスタブでは検証できない**(パス→内容のフラットな`map`でディレクトリの
  実体も`openNext()`の走査も無いため。`pico.sd_list`/`Doc_Cache::Clear()`と同じ制約)。
  SD無し/ディレクトリが存在しない場合に安全に0件を返すことのみASanで確認し、
  実際の走査結果はPCビルドの`--shot`(上記)で確認する、という役割分担にした。

### 現時点のスコープ外(次回以降)

- ~~コールバックは共通4種(press_start/end/move/out)のみ~~ → **解消済み(2026-09-21)**。
  `Checkbox::on_change_checked`/`NumberSlider::on_value_changed`/`ScrollList::on_selectitem`/
  `TabBar::on_changed`を`checked_changed`/`value_changed`/`select_item`/`tab_changed`として
  `pico.on()`から使えるようにした(詳細は上の「ウィジェット固有イベント」参照)。
- ~~命令単位の実行時間制御(`lua_sethook`)は無い~~ → **解消済み(2026-09-21)**。
  詳細は上の「実行時間の安全網(暴走防止)」参照。`StepBudget`(時間で区切る土台)とは
  別物のまま(こちらはLua命令数、StepBudgetはTaskのフレーム分割用)で、統合はしていない。
- ~~1フレームごとにLua側の「update」関数を呼ぶ仕組みは無い~~ → **解消済み(2026-09-20)**。
  `LuaEngine::CallSetup()`/`CallLoop(dt_ms)`がArduino風の`setup()`/`loop(dt)`を
  呼ぶ(どちらも定義は任意)。`LuaScene::onEnter()`がRun()成功後に`CallSetup()`を
  1回、新設した`LuaScene::onUpdate()`が毎フレーム`CallLoop(dt)`を呼ぶ
  (dtは`millis()`差分。`ClocksScene`と同じ計測方法)。**loop()が一度エラーを
  出すと以降は自動的に呼ばれなくなる**(`LuaEngine`内の`loop_broken_`フラグ。
  毎フレーム同じエラーダイアログが積まれるのを防ぐ安全弁で、setup()側はRun()と
  同じく1回きりなので不要)。ホストテストは`lua_engine_test.cpp`(CallSetup/CallLoop単体)
  と`lua_scene_test.cpp`(LuaScene経由の結合テスト)。サンプル`pc/sdcard/lua/hello.lua`に
  経過秒数を表示するloop()の実例を追加した。
- ~~コンテナからの明示的な子の取り外し(`pico.remove_child`)は無い~~ → **解消済み
  (2026-09-21)**。詳細は上の「コンテナからの取り外し」参照。この節に挙げていた
  Lua APIの既知の穴はこれで全て埋まった(残るのはOS内部90箇所の
  OOM未対応など、コストに見合わないと判断して対象外にしたものだけ)。

## Lua着手前の受け皿の状態 (2026-09-19時点)

Lua向けの土台は「発行側・ファクトリ・プロパティ共通口・実行時間制御の土台・エラー表示導線・
ビルドへの組み込みまでは入って、Luaバインディング本体(`lua_State`を実際に生成してsrc/へ繋ぐ部分)
だけが空」の状態。着手時に必ず当たる穴を列挙しておく。

| 箇所 | 状態 |
|---|---|
| ~~`WidgetRegistry::Resolve()`~~ | **ホストテストで検証済み(2026-09-18)**。発行済みIDからの解決・type改ざん検出・破棄済みID(use-after-free)検出・スロット再利用時のgeneration不一致検出を`script/host_test/widget_factory_test.cpp`で確認。ただし**実コード中の呼び出し元はまだテストのみ**で、Luaバインディングを書いた時点で初めて実利用される |
| ~~ウィジェットのファクトリ~~ | **解消済み(2026-09-18)**。`WidgetFactory::Create(WidgetType)`(`src/gui/widgets/WidgetFactory.hpp/.cpp`)がwidgets/直下の汎用部品15種を生成する。widgets/apps・systems・dialogsの専用ウィジェットは対象外 |
| ~~プロパティのget/set共通口~~ | **解消済み(2026-09-19)**。`WidgetProperty::Get/Set()`(`src/gui/widgets/WidgetProperty.hpp/.cpp`)を参照。Lua側が「WidgetIdを`Resolve()`で引く→`WidgetProperty`で値を読み書きする」という2段構えを、Lua本体無しで既にホストテストまで確認できている |
| ~~`AppEntry`(`App_Functions.hpp`)~~ | **解消済み(2026-09-13、スキャン側も2026-09-21で解消)**。`create`が`Scene* (*)(const AppEntry&)`になり、`name`/`arg`は`FixedString`でコピー保持するようになった。「同じ`LuaScene`型 + 別スクリプトパス」も、寿命の短い文字列からの動的登録も表現できる。**スキャン処理そのもの(`LuaAppScanner::Scan()`)も実装済み**(下記「SDを走査したLuaアプリの自動登録」参照) |
| コールバック | **意図的に見送り(2026-09-19)**。`std::function<void()>`のまま。上記「メモリ計測の結論」が既に出している判断(「関数ポインタ+`void*`のDelegateへ替える効果は単体では5%程度、旨味が出るのはLuaのコールバックを大量に貼るようになってから」)をそのまま踏襲し、今回は手を入れなかった。今のシグネチャには「引数もコンテキストも無い」という実害(誰が押したか・どのウィジェットのIDかをコールバック側へ渡せない)もあるため、**再設計するならLuaバインディング本体を書く回でシグネチャを一度に決める**(引数無しのまま先にDelegate化だけ済ませても、Lua側の要求で結局signature変更が要る可能性が高く、二度手間になるため) |
| ~~実行時間の制御~~ | **解消済み(土台2026-09-19、命令単位の打ち切りは2026-09-21)**。`task/StepBudget.hpp`が「一定時間(マイクロ秒)働いたら次のフレームへ回す」ための時間区切りプリミティブを提供する(`Task::update()`内の作業ループを`StepBudget::ShouldContinue()`で区切る。ホストテストは`script/host_test/step_budget_test.cpp`)。**`LuaEngine`側は`lua_sethook(LUA_MASKCOUNT)`でLuaバイトコード命令数を数え、1回の外部呼び出しあたりの上限を超えたら`luaL_error()`で打ち切る**(`StepBudget`とは別物のまま、統合はしていない。詳細は上の「実行時間の安全網(暴走防止)」参照) |
| ~~確保失敗(OOM)~~ | **Lua向けの経路は解消、内部90箇所は対象外と決定(2026-09-19)**。`WidgetFactory::Create()`はtype非対応時もWidget::operator new失敗時も一貫してnullptrを返す設計になっており(ヘッダのコメントで明記済み)、**Luaが実際に触る唯一の生成経路はこの時点で既に安全**。一方、Scene/Dialog等OS内部の`new Button(...)`等(約90箇所)は個々にnullチェックしていないが、これらは実行時に増減しない固定・既知個数の生成で、「メモリ計測の結論」が示す通り実測で断片化もリークも無く十分な余裕がある。ここへ90箇所分のnullチェックを機械的に足す投資対効果は低いと判断し、**対象外とする**(Luaスクリプトが暴走してウィジェットを大量生成する経路は`WidgetFactory::Create()`1箇所に絞られているため、そこが安全なら実害は無い)。`lua_newstate`のカスタムallocでLuaに上限枠を切る話(下記RAM/Flash予算)は引き続き未着手 |
| ~~エラーの見せ方~~ | **解消済み(2026-09-19)**。`ErrorFunctions::ShowFatal(message)`(`src/gui/core/Error_Functions.hpp/.cpp`)がログ(`LOG_APP_FAIL`)とMsgDialog表示の両方を1呼び出しでこなす。`FileExplorer::on_press_delete()`等と同じ「生成→`AddDialog`→`setVisible`→`setOnClosed`で`DestroyLater`」の作法を関数内に閉じ込めてあるので、将来Luaの`pcall`エラーを拾った先はこれを呼ぶだけでよい。ホストテストは`script/host_test/error_functions_test.cpp` |
| RAM/Flash予算 | **暫定枠: Lua用に200KBを割り当てる方針(2026-09-19決定、`lua_newstate`のカスタムallocへ渡す上限)**。開発者が実機で計測した「OS側のヒープ使用量はピークでも150KB程度」を根拠に、RP2350の総SRAM 520KBから逆算した(150KB+200KB=350KBでも170KBの余裕)。**ただし2点未確認**: ①その150KBが`Mem_Functions`(mallinfoベースのヒープ)の値かどうか(フレームバッファ`frame`スプライトやWi-Fi/lwIPスタックがヒープ計測に乗らない確保だと実際の総使用量はもう少し上振れし得る)、②このリモート実行環境には実機もPlatformIOのRP2350ボード定義も無く追試できていない(`platform = raspberrypi`のPlatformIO公式パッケージ1.20.0にはrpipico2wのボード定義が同梱されていない)。**実機が使える時に、Wi-Fi接続中+一番重いシーン(Markdown/Dict)を開いた状態で`MemFunctions`のレポートを取り、200KB確保後も安全か確認すること。** Lua本体はflash 100KB超で、**stateだけでRAM 20〜30KBのオーダーという見積もりはPC上で実測して裏付けた**(空のstate+`luaL_openlibs()`一式でピーク約19.5KB。`script/host_test/lua_alloc_budget_test.cpp`のBudgetAlloc計測)。200KB枠はそこにユーザースクリプト+ウィジェットツリー分の余裕を見込んだ値。**カスタムallocによる予算制御そのものの安全性もPCで確認済み**(下記「ビルドの二重管理」参照)。実機での絶対値(mallinfoの150KBが本当に正しいか)はやはり未確認 |
| ~~ビルドの二重管理~~ | **LovyanGFXとは別方式で解消済み(2026-09-19)**。Lua 5.4.7本体(`lua.c`/`luac.c`を除く)を`lib/lua/`へvendorした(`lib/lua/README-pico-os.md`に経緯あり: Lua本体の`src/`には`main()`を持つ`lua.c`/`luac.c`が混在しており、`lib_deps`へ生のgit/tarballを指定するとPlatformIOの自動収集がそれも拾ってArduinoコア自身の`main()`と衝突するため、LovyanGFX方式(`lib_deps`+`pc/CMakeLists.txt`が同じタグをそれぞれ取得)は使えなかった)。vendor後は`lib/`配下がPlatformIOの「プロジェクト専用ライブラリ」として自動的にビルドされる(`platformio.ini`への追記は不要)ので、`pc/CMakeLists.txt`もこの同じ`lib/lua/src/`を参照するようにし、**実質1箇所の情報源**に落ち着いた。PCビルドで実際にリンクし、`lua_newstate`/`luaL_openlibs`/`luaL_dostring`/`lua_close`が動くことまで確認済み(`script/host_test/lua_smoke_test.cpp`、PC実行バイナリ`pc/build/picoos_pc`でも起動確認済み)。**言語機能・標準ライブラリの動作確認(2026-09-19追加)**: `script/host_test/lua_stdlib_test.cpp`で数値/文字列/テーブル/クロージャ/メタテーブル/pcall/コルーチン/GCが一通り動くことをASan/UBSan付きで確認(全てpc/CMakeLists.txtと同じ`LUA_USE_LINUX`無しのANSI構成でビルド)。**カスタムアロケータでの予算制御の安全性も確認済み**: `script/host_test/lua_alloc_budget_test.cpp`が`lua_newstate`へ確保量上限付きの`lua_Alloc`を渡し、予算超過時に(a)`lua_newstate`自体は素直に`nullptr`を返す、(b)`pcall`越しの`luaL_openlibs()`は`abort()`せず`LUA_ERRMEM`を返す(`pcall`で保護しないまま直接呼ぶとLuaの仕様上`abort()`する点に注意)、(c)いずれの場合も`lua_close`後は確保量が必ず0に戻る(リーク無し)ことを確認した。これは上記RAM/Flash予算の「200KBで上限を切る」方式が安全に実現できることの裏付けになる。**PlatformIO側(実機)は引き続き未検証**(このリモート実行環境にRP2350のボード定義が無いため。上記RAM/Flash予算の未確認点と同種の制約) |

**API仕様は「C++で標準アプリを1〜2本書いてみて、必要になったもの」から逆算するのが確実。** ランチャに載っているのは`MarkdownScene`(引数なしなら`network.cfg`の`browser-home`を開く)/ `ClocksScene`(時計・タイマー・ストップウォッチ)/ `InputTestScene`(部品の動作確認用)の3本で、バインディング設計の実例としてはまだ足りていない。

**2026-09-19追記: 上記②④⑤は`src/lua/LuaEngine`として実装・検証済み**(詳細は上の
「Luaバインディング」参照)。コールバックは結局シグネチャを変えずに済み、
`WidgetRegistry::Resolve()`/`WidgetProperty`は`pico.get`/`pico.set`/`pico.on`から
実際に呼ばれ、Luaのエラーは`pcall`で受けて`ErrorFunctions::ShowFatal()`へ渡るところまで
ホストテスト(`lua_engine_test.cpp`)で確認済み。**残っているのは①(`lua_newstate`を
`budget_bytes`付きで呼ぶ場所自体は`LuaEngine`のコンストラクタに決まったが、
「どの`Scene`/`Task`がいつ`LuaEngine`を`new`/`delete`するか」という置き場所はまだ無い
=`LuaScene`が無い)と③(命令単位の実行時間制御)**、および「Luaバインディング」章末尾の
「現時点のスコープ外」に挙げた項目(ウィジェット固有コールバック、毎フレームのupdate呼び出し等)。

**2026-09-21追記: ①③とも解消済み。** ①は`LuaScene`(上記)、③は`LuaEngine`の
`lua_sethook`ベースの安全網(上記「実行時間の安全網(暴走防止)」)。ウィジェット固有
コールバックも同日解消(上記「ウィジェット固有イベント」)。この節が挙げていた
`pico.remove_child`(コンテナからの子の明示的取り外し)も同日の細部穴埋めで追加し、
挙げていた穴は全て埋まった(詳細は上の「コンテナからの取り外し / リストへの項目追加」参照)。
