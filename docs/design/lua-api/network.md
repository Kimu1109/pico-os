# Lua API の設計記録(pico.*): ネットワーク

> `CLAUDE.md`から分けた設計記録(2026-10-20)。2026-11-06に `lua-api.md` から節ごとに分けた(目次は同じフォルダの `README.md`)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`grep -rn '^#.*節の名前' docs/design/` で節の名前から探すこと。

## ネットワーク(2026-09-21実装)

`pico.http_request(method, url, body, content_type, callback)` / `pico.http_cancel()`。
既存の`Http_Get`(`src/net/Http_Get.hpp`)はMarkdownブラウザのキャッシュ用途に
特化していて汎用のHTTPクライアントとしては使えなかった(GET専用でメソッドが
`sendRequestLine()`にリテラル`"GET "`で埋め込まれている/送信ボディの概念が無い/
`HttpBodyGate`が`statusCode()==200`以外の本文を黙って捨てる/200・304以外を
一律`FAILED`扱いにする)ため、新設の`HttpRequest`(`src/net/Http_Request.hpp/.cpp`)を
別クラスとして用意した。

- **`HttpGet`とコード骨格(接続/送信/受信ループ・タイムアウト・リダイレクト追跡)は
  ほぼ同じにしてある**(実績のある形をそのまま踏襲。差分だけ抜き出す共通基底への
  リファクタは、Markdownブラウザの動いているHTTP経路を壊すリスクの方が大きいと
  判断して見送った)。**違うのは以下3点だけ**:
  1. メソッドが`enum class Method { GET, POST, PUT, PATCH, Delete }`で選べる
  2. 送信ボディ+`Content-Type`を指定でき、`Content-Length`ヘッダを自分で付けて
     ボディを送る(`HttpResponse`が読む*応答*の`Content-Length`とは別物)
  3. **`HttpBodyGate`を挟まない**(`res.reset(sink_)`で直結)ため、
     ステータスコードによらず本文がsinkへ渡る。Task自体の成功/失敗も「応答を
     最後まで読めたか」だけで判定し、ステータスコードの意味(2xx/4xx/5xx)は
     呼び出し側(Luaスクリプト)へ渡す。**Markdownブラウザ側の判断(200/304だけ
     成功、他は本文を捨てる)はそのまま`HttpGet`に残しており、この変更の影響は
     受けない**
- **GETだけリダイレクトを自動で追う。** POST等はボディを送り直すべきか
  (RFCの解釈も一律ではない)を決め打ちせず、3xxが返ってきてもそのまま
  呼び出し側(Luaスクリプト)へ渡す判断にした。
- **`HttpGet`/`HttpRequest`はどちらも`PICO_Task`の全体リストには登録されない。**
  実際の利用者(`DocFetch`/`DocSearch`)は`HttpGet`を値メンバとして持ち、
  自分の`update()`から毎フレーム`http.update()`を呼ぶ設計になっている
  (`NetworkScan`だけが`PICO_Task::Add()`でグローバルリストに乗る例外)。
  同じ流儀で、`LuaEngine`が`HttpRequest`を1本(`http_`、後述の理由で遅延`new`)
  保持し、**`LuaScene::onUpdate()`から毎フレーム`engine->UpdateHttp()`を呼んで
  進める**(`setup()`/`loop()`の定義有無に関わらず無条件に呼ぶ。`CallLoop()`
  とは別の独立した呼び出しにしてあり、`pico.*`のネットワーク進行を
  Arduino風`loop(dt)`の意味論と混ぜていない)。
- **同時に走るリクエストは1本まで(2026-10-05からは2本目以降を最大4本まで順番待ちにする。下記「Lua APIの追加(2026-10-05)」)。** `HttpState::callback_ref`が
  `LUA_NOREF`かどうかで「進行中か」を判定する。進行中に`pico.http_request()`を
  呼んでも`luaL_error`にはせず`false`を返すだけ(SD無し等と同じ「実行時の状態」
  枠)。完了時は**Lua側コールバックを呼ぶ前に`callback_ref`を`LUA_NOREF`へ戻す**
  ことで、コールバック自身の中から次の`pico.http_request()`を呼べるようにしてある
  (チェイン可能)。
- **`http_`(`HttpRequest`+受信バッファ+送信ボディの控え。受信/送信とも
  `kMaxHttpResponseBytes`/`kMaxHttpBodyBytes`=16KiBで頭打ち)は初回の
  `pico.http_request()`呼び出しまで`new`しない。** ネットワークを使わない
  Lua アプリ(大半を占める見込み)に32KiB(16KiB×2)の固定バッファを常時
  負担させたくないための遅延生成(`struct HttpState`はヘッダでは前方宣言のみ)。
- **送信ボディ/Content-Typeは呼び出し時にLuaEngine側の`FixedString`へコピーする。**
  `HttpRequest::begin()`は`const void* body`/`const char* content_type`を
  非所有ポインタ(`IHttpSink*`と同じ約束)で受け取るが、Luaスタック上の一時的な
  文字列をそのまま渡すと、`Connecting`フェーズが次のフレームへ回る(`HttpGet`と
  同じ設計。フレーム時間をならすため)間にポインタが無効になりかねない。
  `HttpState::body_buf`/`content_type_buf`へ一度コピーしてから渡すことで解決した。
- **受信本文はNULを含み得るため、Luaへ渡す際は`lua_pushlstring()`(長さ明示)を使い、
  `lua_pushstring()`(strlen前提)にしていない。**
- ホストテストは`lua_engine_test.cpp`に追加。**実ソケットに一切触れない範囲
  (未知のメソッド/不正なURL/送信ボディの上限超過/同時実行数の上限)での
  早期拒否がすべて`false`(またはpcall経由でエラー)になることだけを確認**しており
  (`HttpGet`と同様、実際の通信を伴う検証は`run.sh`(ASan、ネットワーク無し)の
  対象外。`run_net.sh`と同じ「本物のソケット」区分に属する)、送信ボディの上限
  テストで16KiB超の文字列を作る都合上、それまでの全テストで積み上がった共有
  `engine`の予算と衝突しないよう専用の`LuaEngine`を使っている。
  **実際の通信はPCビルドで、使い捨てのローカルHTTPサーバ(`http.server`)を相手に
  動作確認した**(このリポジトリには含めていない検証用スクリプト): GET成功
  (status=200、本文が読める)、POST(送信ボディ+Content-Typeが実際にサーバへ届き、
  エコーされた応答本文が読める)、**404応答でも本文が読めること**
  (`HttpGet`の`HttpBodyGate`なら本文が捨てられていたはずの経路で、`HttpRequest`が
  正しく本文を渡せていることの確認)の3パターンを確認済み。
