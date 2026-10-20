# Markdownブラウザと通信(HTTP/HTTPS)

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## MarkdownView 実装詳細
`MdBlockType`: H1/H2/H3/Paragraph/Image/Link/CodeBlock/ListItem/HorizontalRule/Quote/TableRow。`MdBlock`はオフセット/長さ参照方式(`srcOffset`/`srcLength`、`doc_text`をコピーせず範囲参照)。固定上限: `kMaxBlocks=128`, `kMdMaxSourceBytes=8192`, `kMdBlockTextBytes=512`(1ブロックの表示テキスト上限。日本語で約170文字), `kLabelPoolSize=16`, `kImagePoolSize=2`, `kMaxListLevels=6`, テーブル最大列`kMdTableMaxCols=4`。`kMdBlockTextBytes`と`kMdMaxSourceBytes`はクラス外定義(クラス外に書くメンバ関数定義の戻り値型はクラススコープより前に解決されるため)。上限に当たった場合は`load()`が警告ログを出す。**512Bを超える段落・コードブロックは`parseBlocks()`の`pushSplitBlock()`が行の境目(無ければUTF-8の文字の境目)で複数のブロックに分け、`MdBlock::joinNext`で間隔を空けずに並べる**(2026-10-10。以前は1ブロック目で切り詰められ、さらに高さを測る`measure_label`が`Label<PICO_STR_LL>`=192Bしか持てず長い段落の高さが足りずに次のブロックと重なっていた。**`measure_label`の容量は表示用のLabelと必ず揃えること**)。画像は`onRAM=false`でSDからストリーミング描画する(RAMに載せると占有量が開いた文書次第で青天井になるため)。テーブル/水平線/引用バーは`Label`を介さず`frame`へ直接描画(`renderDecorations()`)。リンクタップ用`on_link_tap`あり。フロントマターは`skipFrontMatter()`で読み飛ばし。**ヘッダー/フッター機能は現状なし。**

**文書内の参照(画像)は`doc_path`基準で解決する**(`resolveRef()`)。`load(path)`が`doc_path`を覚え、`layoutBlocks()`(画像ヘッダを読んで**ブロックの高さ**を決める)と`bindImageSlot()`(**表示用のパス**)の**2箇所**で使う。片方だけ直すと「高さは合うが表示されない」類のずれ方をするので必ず両方を通すこと。キャッシュがサーバのパスをミラーしているため、この1つの規則でローカルもリモートも足りる(View側にネットワークの知識は不要)。

## 文書キャッシュ (`src/apps/browser/Doc_Cache.hpp`)
サーバから取った文書をSDへ書き、次回以降はSDから読むための層(`docs/PROTOCOL.md`)。
**MarkdownViewは「SD上のファイルを開く」ことしか知らないままでよく、ネットワークとの境界がファイルシステムで切れる**のが狙い。

- 配置は `/cache/<正規化したホスト>/<サーバ上のパス>`。ハッシュ名にせずミラーするのは**FileExplorerでそのまま中身を覗ける**ようにするため。
- **ホスト名は`SanitizeHost()`で正規化してから使う**。FATでは`:`が使えず(ポート番号!)大小も区別しないため、小文字化して`: * ? < > | " \ /`と制御文字を`_`へ潰す。
- **`Writer`が本体**: 一時ファイル(`.part`)へ書き、`commit()`で初めて本来の名前へ差し替える。`commit()`せずに破棄されるとデストラクタが一時ファイルごと消す。**通信が途中で切れた半端なファイルを「正常なキャッシュ」として残さない**ためで、これがこのモジュールの存在理由。
- `kMaxEntryBytes=64KiB`で頭打ち。相手のサーバが何を返してきてもSDを埋め尽くさないようにする(`docs/PROTOCOL.md`の「巨大なレスポンス」対策)。
- 目録は `/cache/index.tsv`(行指向TSV、1行1文書): `host / path / validator / fetched_epoch / size`。書き換えは**元を読みながら一時ファイルへ書き写して最後に差し替える**ので、目録全体をRAMへ載せない(`Config_Functions::SetValue()`と同じ手順)。
- `fetched_epoch`は**参考値**。NTP同期前の時計は当てにならないので、鮮度の判断は`validator`(ETag)で行う。
- **追い出し(LRU等)は実装しない。** 1文書8KiBに対しSDはGB単位あり、1000件貯めても8MB程度なので枠を管理する対価に見合わない。全消去の`Clear()`だけ用意してある。
- **`Clear()`だけはホストテストで検証できていない**。ディレクトリの再帰削除に`isDir()`/`openNext()`が要るが、`script/host_test/stubs/SdFat.h`はパス→内容のフラットな`map`でディレクトリの実体が無いため。PCビルド(`pc/compat/SdFat.h`は実ファイルシステム)側で確かめること。

## HTTPクライアント (`util/Url.hpp` / `net/Http_Response` / `net/Http_Get`)
`docs/PROTOCOL.md` のHTTPを喋る側(http/https。HTTPSは下の「HTTPS」参照)。3つに割ってあるのは**テストできる形にするため**。

- **`Url`**: `http://host:port/path?query` を分解して持つ型。`"http://"` を各所で`strncmp`しないための入れ物で、**schemeをここに閉じ込めてある**(実際にHTTPS対応で触ったのは接続処理=`net/Http_Transport`だけで済んだ)。`path`は`PICO_STR_LL`(Googleカレンダーの非公開URLのパスが96Bを超えるため)。パスとクエリを分けて持つのは、相対解決(`PICO_IO::resolve`を再利用)がクエリ内の`/`まで畳んでしまわないようにするため。
- **`HttpResponse`**: **ソケットを持たない**増分パーサ。受信したバイト列を`feed()`へ渡すだけなので、ネットワーク無しに全経路をホストテストできる(`http_test.cpp`は1バイトずつ食わせた場合も同じ結果になることまで見ている)。見るヘッダは`Content-Length`/`ETag`/`Last-Modified`/`Location`/`Transfer-Encoding`だけ。**chunkedは解いて本文だけをシンクへ渡す**(2026-09-23。以前は検出したらエラーにしていたが、HTTPSで繋ぐ一般のサーバは動的な応答をchunkedで返す)。chunked以外の転送符号化(gzip等)はエラー。**読まないヘッダは長すぎても読み飛ばす**(Googleは`Set-Cookie`/CSPで`kMaxLineLen=256`を平気で超える)。中身を使うヘッダ(Location等)とステータス行だけは切れていたら失敗にする。本文の行き先は`IHttpSink`で差し替える。
- **`HttpGet`**: `Task`派生。`Connection: close`を送り、`Accept-Encoding`は送らない。リダイレクト最大3回、全体10秒で打ち切り。手元の検証子を渡すと条件付きGETになる(`GMT`を含むかで`If-None-Match`と`If-Modified-Since`を出し分ける — 目録が「どちらのヘッダで来たか」を覚えていないための割り切り)。**3xx/4xxの本文はシンクへ流さない**(`BodyGate`が200を見てから開く)のでキャッシュが汚れない。
- **接続(`connect`)だけは同期的**。到達しない相手を指すと最大`kConnectTimeoutMs=3000`ぶん画面が止まる。受信は全てポーリングなので、繋がってしまえばフレームは止まらない。非同期接続にはlwIPを直に叩く必要があり、別の段の仕事。
- PC側の`WiFiClient`は`pc/compat/WiFiClient_PC.h`にある(TLS版`WiFiClientSecure`はその派生で`WiFiClientSecure_PC.h`。そのため主要メソッドは仮想関数)。**Wi-Fiの「状態」(`pc/compat/WiFi.h`)は偽物のままだが、通信そのものは本物のソケット**。SDにもLovyanGFXにも依存しないので、ホストテスト(`script/host_test/stubs/WiFi.h`)からも**同じ実装**を使う(通信経路のテストで別物を使っては意味が無いため)。

## HTTPS (`src/net/Http_Transport` / `Tls_Roots_Data.hpp`) (2026-09-23)

`HttpGet`(文書/カレンダー)と`HttpRequest`(Luaの`pico.http_request`)の下の「繋ぐ」部分を
`HttpTransport`へ切り出し、URLが`https://`ならTLSで繋ぐ。schemeを見て分岐するのはここだけ。
Markdownブラウザ・Lua・カレンダーの3つとも、これで`https://`を扱える
(以前は`url.secure`を見て各所で「httpsは未対応」と断っていた)。

- **実機はarduino-pico同梱のBearSSL**(`BearSSL::WiFiClientSecure`/`X509List`)。新しいライブラリは足していない。
  **TLS 1.2まで**(BearSSLの上限)なので、TLS 1.3だけを受け付けるサーバには繋がらない。
- **PCはOpenSSL**(`pc/compat/WiFiClientSecure_PC.h`)。実機と同じ呼び方に揃え、**わざとTLS 1.2に固定**し、
  **信頼するのも同じルートだけ**(OSの証明書ストアを見ない)。「PCでは繋がるのに実機では繋がらない」を防ぐため。
  PCビルドは`libssl-dev`が要る(`find_package(OpenSSL)`)。**Webビルドは常に繋がらないスタブ**
  (生のソケットが無いので、そもそもTLSも無い)。
- **信頼するルート**は`script/generate_tls_roots.py`が母艦の`/etc/ssl/certs`から`src/net/Tls_Roots_Data.hpp`へ焼き込む
  (GTS Root R1/R4=Google、ISRG Root X1/X2=Let's Encrypt、DigiCert Global Root G2/CA、USERTrust RSA=Sectigo、
  Amazon Root CA 1=AWS(api.todoist.com。2026-10-01追加)、Starfield Root G2(accounts.spotify.com。Certainly/Fastlyの中間の上。2026-10-14追加)の9枚)。
  api.spotify.com は DigiCert Global Root G2 の下(足す前から通る)。どちらもTLS 1.2で繋がることを`openssl s_client -tls1_2 -CAfile <焼き込みの9枚>`と
  PC版の`HttpRequest`(OpenSSL・TLS 1.2固定・焼き込みのルートだけ)で確認した。**`ROOTS`は末尾へ足すこと** — 母艦のストアから消えたルート
  (DigiCert Global Root CAは既に無い)は、今焼き込まれている同じ位置の証明書を引き継いで生成し直す。
  足りない相手(自己署名の自前サーバ等)はSDの**`/sys/tls/ca.pem`**へPEMを置けば足される。
  **フラッシュに置くだけで、RAMへ展開するのは接続中だけ**(1枚あたり約1.5KB)。
- **TLSの道具一式は`connect()`で確保し`close()`で返す**。BearSSLは受信バッファ16KB+専用スタック6.4KB+
  ルートの展開等で**接続中だけ約40KB**を使う。常駐させると使わないアプリにまで負担させるため。
- **時計が合っていない(NTP同期前、2020年より前)と繋がない**(`Error::ClockNotSet`)。証明書の有効期限を
  確かめられず、実機では全ての証明書が「まだ有効でない」になって理由が分かりにくいため。
- **TLSのハンドシェイクは`connect()`の中で同期的に進む**(素のTCPの接続と同じ制約)。実機で1〜2秒程度の見込みで、
  その間は画面が止まる。**実機での実測はまだ**(このリモート環境にはRP2350のボード定義が無く、実機ビルドもできない)。
- 失敗の理由は`HttpGet::Fail::ClockNotSet`/`TlsFailed`で分かり、`failureToStr()`はTLSライブラリの理由も付けて返す。
- 検証は`run_net.sh`の`calendar_sync_test`(使い捨てのCAを`openssl`で作り、`script/host_test/tls_test_server.py`を
  相手に、chunked転送・ETag→304・信頼していないCA・名前の違う証明書を本物のTLSで確かめる)。

## 取得〜表示の配線 (`src/apps/browser/Doc_Fetch.hpp`)
「URLを1本取ってきて、SD上の開けるパスにする」係。`Http_Get`(取得)と`Doc_Cache`(保存)を繋ぐだけの薄い層だが、**ブラウザとして必要な判断はここに集めてある**。

- 手元にキャッシュがあれば検証子を添えて条件付きGETし、**304ならそのまま使う**(`Writer`は`abort()`するので本体に触らない)
- 200なら一時ファイル経由でキャッシュを差し替えてから使う
- **取得に失敗しても、古いキャッシュがあればそれを開く**(圏外でも読める)。黙って古い内容を出すと更新が反映されていないように見えるので、`Source::CacheAfterError`で呼び出し側へ伝え、`MarkdownScene`はフッタへ「オフライン表示(保存済み)」と出す
- キャッシュのキーは**ポートまで含めたホスト**(`host:port`)。ポートが違えば別のサーバとして扱う
- `MarkdownView`へ渡すのは常にSD上のパスなので、**View側はネットワークの存在を知らない**

## サーバ情報 (`src/apps/browser/Discovery.hpp`)
`GET /.well-known/pico-os` の中身(`ServerInfo`)。`docs/PROTOCOL.md`「サーバ情報」参照。

- **discoveryを特別扱いしない。** 置き場所が決め打ちなだけで、取得も保存も`Doc_Fetch`/`Doc_Cache`をそのまま通す。おかげで**条件付きGET(304)も、圏外のときに前回の内容を使うことも、何も書かずに手に入る**。
- **知らないキーは無視する**(`docs/PROTOCOL.md`の前方互換ルール)。サーバが将来キーを足しても古いpico-osが壊れない。`discovery_test.cpp`で固定してある。
- **`search`の行が無い = 検索非対応**。`hasSearch()`で判定し、対応していないサーバでは検索UIを無効にする。
- **404は正常な結果**。「素の静的ファイルサーバと分かった」という確定した答えなので、`checked`を立ててページごとに問い合わせ直さない。
- `MarkdownScene`は**ホストが変わったときだけ**問い合わせる。`home`が申告されていればヘッダの「ホーム」ボタンの行き先になる(無ければ`network.cfg`の`browser-home`)。

## 検索 (`src/apps/browser/Doc_Search.hpp`)
`GET <discoveryのsearch>?q=...&limit=10&offset=N` の応答(1行1件のTSV)を読む。`docs/PROTOCOL.md`「3. 検索」参照。

- **検索だけは`Doc_Cache`を通さない。** キャッシュ上の置き場所は**URLのパスだけで決まりクエリを見ない**ため、
  通すと検索語違いの応答が同じファイルへ重なり、条件付きGETで別の検索語の304まで起きる。
  応答は高々20行なのでRAMへ載せる(`SearchHit`が1件160B × `kMaxHits=10`)。
- 読むのは**1列目(path)と2列目(title)だけ**。`version`/`snippet`は画面で使わないので読み飛ばす(前方互換)。
  **pathは`/`始まりのサーバ絶対パスでなければ捨てる**(何を基準に解決するか決まらないため)。
  pathが収まらない行も捨てる(別の文書を指してしまう)。titleは表示専用なので切り詰まってよい。
- 検索語は`UrlTools::EncodeComponent()`でパーセントエンコードする。**日本語1文字が9バイトになる**ので、
  `Url::query`は`PICO_STR_L`ではなく`PICO_STR_LL`にしてある(`RequestTarget()`の受けも`PICO_STR_256B`)。
- 総件数は返ってこないので、**要求ちょうどの件数が返ったら「続きがあるかもしれない」**と見なす
  (`mayHaveMore()`)。「次へ」は`offset += kMaxHits`で引き直す。
- **404は失敗**(discoveryは404が正常な結果だったが、こちらは違う)。
  `search`の行が無いサーバへは`begin()`が接続すら試さない。

UI側は`apps/browser/ui/SearchDialog`(状態1行 + `ScrollList` + 再検索/次へ/閉じる)。
**通信は一切せず**、何件目から取るかの判断も含めて`MarkdownScene`が持つ。結果は**2回タップで開く**
(`ScrollList`の流儀。1回目は選択)。

## マニフェスト (`src/apps/browser/Manifest.hpp`)
`GET <discoveryのmanifest>` の中身(1行 `path<TAB>version`、**pathの昇順**)。`docs/PROTOCOL.md`「4. マニフェスト」参照。

- **狙いは「開くたびの条件付きGETを省く」こと。** 手元の検証子とマニフェストのversionが一致すれば、
  その文書は**サーバへ何も聞かずに開ける**(304の往復すら要らない)。
  `DocFetch::begin()`が`Source::Manifest`で即`Ready`になる。
- 取得と保存はdiscoveryと同じく`Doc_Fetch`/`Doc_Cache`をそのまま通すので、**マニフェスト自体も304になる**。
  `MarkdownScene`はdiscoveryの直後(=ホストが変わったとき)と、**更新ボタンを押したとき**に引く。
- **一致しない/載っていない/サーバが非対応なら、今までどおり条件付きGETへ落ちる。**
  つまり**あれば速くなるだけ**で、無くても挙動は変わらない。
- 引き当ては`Manifest::VersionOf()`が**pathを追い越した時点で打ち切る**(昇順という取り決めの使いどころ)。
  **検証子が`FixedString`に収まらない行は「分からなかった」扱い**にする — 切り詰めて比べると別物を同じと見なす。
- **滞在中にサーバ側が更新されても気づけない**(次にそのホストへ来るまで引き直さないため)。
  そこを埋めるのが更新ボタンで、押すと`manifest_stale`が立ってマニフェストごと引き直す。
- `DocFetch::setManifest()`は**ホストごとの設定**なので`begin()`/`cancel()`では消えない。
  ホストが変わったとき(`openCurrent()`/`abortNavigation()`)に明示的に捨てる。

## ブラウザのヘッダー

`[<][>][ホーム][更新][検索]` … [終了]。左側は**各ボタンの実測幅で左から詰めて並べる**
(日本語の文字幅はフォント任せなので、`<`/`>`のように字が細いものへ最低幅を与えるだけにしてある)。
「終了」だけ右端。

- **更新** = キャッシュを無視して取り直す。検証子を送らないので200が返り、キャッシュごと差し替わる。
  **挿絵も引き直す**(文書だけ新しくて絵が古いままにならないように)。履歴は積まず、スクロール位置も保つ。
  `bypass_cache`フラグが`DocFetch::begin(url, bypass_cache)`まで届く仕組みで、`commit/abortNavigation()`で下りる。
- **検索** = 押せるのは「リモートの文書を開いていて、そのサーバがsearchを申告している」ときだけ
  (`currentServerCanSearch()`)。

**ダイアログからダイアログへ移るときは1フレーム空けること**(`MarkdownScene::Pending`)。
`PICO_GFX::FlushDirty()`は**TRANSLUCENTなウィジェットの下を描き直さない**(半透明の下は変わらない前提の最適化)
ため、同じフレームで次のダイアログを開くと、閉じたキーボードや前のダイアログの跡がその下に残ったままになる。
1フレーム空ければ「ダイアログが何も無い状態」で描き直される。
