#include "net/File_Share.hpp"

// Wi-Fi転送のページ(ブラウザで開く1枚)。取り決めは net/File_Share.hpp の先頭。
// 中身はSDの名前を表示するだけなので、名前は必ず textContent で入れる(innerHTML へ入れない)。
// キーはクッキー pk に持ち、書き換える要求には X-Pico-Key 見出しでも送る。
// アップロードは1つずつ順に送る(本体は同時に1本しか相手をしない)。進み具合が要るので fetch ではなく XHR。
// フォルダごと(選ぶ・落とす)は中のファイルを「フォルダ/中/名前」で ?mkdirs=1 を付けて送り、本体が親のフォルダを作る。
// 表示の言語はブラウザの言語で決める(ja 以外は英語。本体の言語設定は見ない: 開く人はPCの前にいる)。
// 文は日本語の原文を T() で包み、英語は EN の表から引く(本体が返す誤りの文も同じ表で訳す)。
// 静的なところは data-t の付いた要素の文字を書き換える(textContent。innerHTML は使わない)。
namespace FileShare {

const char kPageHtml[] = R"PAGE(<!doctype html>
<html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title data-t>pico-os ファイル</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#fff1e8;color:#1d2b53}
header{background:#1d2b53;color:#fff;padding:10px 16px;font-weight:bold}
main{padding:12px 16px;max-width:900px;margin:auto}
button,label.btn{font:inherit;border:1px solid #1d2b53;background:#fff;border-radius:6px;padding:5px 10px;cursor:pointer;color:#1d2b53}
button.danger{border-color:#ff004d;color:#ff004d}
#bar{display:flex;gap:6px;flex-wrap:wrap;margin:8px 0}
#crumb a{color:#29adff;text-decoration:none}
table{width:100%;border-collapse:collapse;background:#fff}
td{padding:6px;border-bottom:1px solid #c2c3c7;word-break:break-all}
td.size{text-align:right;white-space:nowrap;color:#5f574f;width:6em}
td.ops{white-space:nowrap;width:1%}
td a{color:#1d2b53}
#msg{min-height:1.4em;margin:6px 0;color:#5f574f}
#msg.err{color:#ff004d}
#queue{font-size:90%;color:#5f574f;min-height:1.6em}
#login{display:none;background:#fff;padding:16px;border-radius:8px}
#drop{outline:3px dashed transparent}
#drop.over{outline-color:#29adff}
input[type=text]{font:inherit;padding:5px;width:12em}
</style></head><body>
<header id="title">pico-os</header>
<main>
<form id="login"><p data-t>本体の画面に出ているキーを入れてください。</p>
<input type="text" id="key" autocomplete="off" autocapitalize="off" spellcheck="false" maxlength="8">
<button data-t>開く</button></form>
<div id="app" style="display:none">
<div id="crumb"></div>
<div id="bar">
<label class="btn"><span data-t>アップロード</span><input type="file" id="files" multiple hidden></label>
<label class="btn"><span data-t>フォルダをアップロード</span><input type="file" id="dirs" webkitdirectory multiple hidden></label>
<button id="mkdir" data-t>新しいフォルダ</button>
<button id="reload" data-t>更新</button>
</div>
<div id="msg"></div>
<div id="queue"><span id="qline"></span> <button id="cancel" style="display:none" data-t>やめる</button></div>
<div id="drop"><table><tbody id="list"></tbody></table></div>
</div>
</main>
<script>
"use strict";
const $=id=>document.getElementById(id);
const L=/^ja\b/i.test(navigator.language||"")?"ja":"en";
const EN={
"pico-os ファイル":"pico-os files","本体の画面に出ているキーを入れてください。":"Enter the key shown on the device screen.",
"開く":"Open","アップロード":"Upload","フォルダをアップロード":"Upload folder","新しいフォルダ":"New folder","更新":"Reload",
"やめる":"Cancel","キーが違います":"Wrong key","エラー %s":"Error %s","%s のファイル":"Files on %s","↑ 上へ":"↑ Up",
"名前":"Rename","削除":"Delete","%s 個":"%s items","新しい名前":"New name","「%s」を消しますか?":"Delete \"%s\"?",
"「%s」(フォルダの中身も全部)を消しますか?":"Delete \"%s\" and everything in it?","フォルダの名前":"Folder name",
"フォルダを読めませんでした":"Could not read the folder","送れませんでした":"Upload failed","やめました":"Cancelled",
"/sys には触れません":"/sys is off limits","Content-Length が要ります":"Content-Length is required",
"SDの全体は動かせません":"Cannot move the whole SD","SDの全体は消せません":"Cannot delete the whole SD",
"SDカードが使えません":"SD card unavailable","SDカードに書けませんでした":"Could not write to the SD card",
"SDカードに書けませんでした(空きが足りないかもしれません)":"Could not write to the SD card (maybe out of space)",
"SDカードを読めません":"Cannot read the SD card",
"この名前では開けません(本体の名前かIPアドレスで開いてください)":"Cannot open with this name (use the device name or IP address)",
"その方法では使えません":"Method not allowed","キーを何度も間違えたので、しばらく待ってください":"Too many wrong keys. Wait a while",
"パスがありません":"No path","パスが長すぎます":"Path too long","フォルダがありません":"Folder not found",
"フォルダはダウンロードできません":"Folders cannot be downloaded","フォルダを自分の中へは移せません":"Cannot move a folder into itself",
"作れませんでした":"Could not create","使えない名前です":"Invalid name","使っているので動かせません":"In use; cannot move",
"使っているので消せません":"In use; cannot delete","使っているファイルなので書き換えられません":"File in use; cannot overwrite",
"同じ名前があります":"That name already exists","同じ名前のフォルダがあります":"A folder with that name exists",
"名前を変えられませんでした":"Could not rename","書き換えには X-Pico-Key の見出しが要ります":"Changes need the X-Pico-Key header",
"本体の画面ロックを解いてください":"Unlock the device screen","消せませんでした":"Could not delete",
"移す先のフォルダがありません":"Destination folder not found","置き換えられませんでした":"Could not replace",
"要求が大きすぎます":"Request too large","要求を読めません":"Cannot read the request","見つかりません":"Not found",
"親のフォルダがありません":"Parent folder not found","送り先のフォルダがありません":"Upload folder not found",
"送り先のフォルダを作れません(同じ名前のファイルがあります)":"Cannot create the upload folder (a file has that name)"
};
// 原文 s の今の言語の文。%s は後ろの引数で順に埋める
function T(s,...a){const t=(L==="en"&&EN[s])||s;let i=0;return t.replace(/%s/g,()=>String(a[i++]))}
document.documentElement.lang=L;
for(const e of document.querySelectorAll("[data-t]"))e.textContent=T(e.textContent);
let cwd="/",busy=null,sent=0,total=0;
const queue=[];
function key(){const m=document.cookie.match(/(?:^|; )pk=([^;]*)/);return m?m[1]:""}
function setKey(k){document.cookie="pk="+encodeURIComponent(k)+"; path=/; SameSite=Strict"}
function enc(p){return p.split("/").map(encodeURIComponent).join("/")}
function join(d,n){return (d==="/"?"":d)+"/"+n}
function msg(t,err){const m=$("msg");m.textContent=t||"";m.className=err?"err":""}
function size(n){if(n<1024)return n+" B";if(n<1048576)return (n/1024).toFixed(1)+" KB";return (n/1048576).toFixed(1)+" MB"}
async function api(path,opt){
  opt=opt||{};opt.headers=Object.assign({"X-Pico-Key":key()},opt.headers||{});
  const r=await fetch(path,opt);
  if(r.status===401){showLogin();throw new Error(T("キーが違います"))}
  if(!r.ok){const t=await r.text();throw new Error(t?T(t):T("エラー %s",r.status))}
  return r;
}
function showLogin(){$("app").style.display="none";$("login").style.display="block";$("key").focus()}
$("login").onsubmit=e=>{e.preventDefault();setKey($("key").value.trim().toLowerCase());start()};
async function start(){
  try{
    const info=await (await api("/api/info")).json();
    $("title").textContent=T("%s のファイル",info.name);document.title=T("%s のファイル",info.name);
    $("login").style.display="none";$("app").style.display="block";
    await load(cwd);
  }catch(e){if($("app").style.display!=="none")msg(e.message,true)}
}
async function load(dir){
  let text;
  try{text=await (await api("/api/ls?p="+encodeURIComponent(dir))).text()}catch(e){msg(e.message,true);return}
  cwd=dir;
  const rows=text.split("\n").filter(l=>l).map(l=>{const c=l.split("\t");return {name:c[0],dir:c[1]==="d",size:+c[2]}});
  rows.sort((a,b)=>a.dir!==b.dir?(a.dir?-1:1):a.name.localeCompare(b.name,"ja"));
  const crumb=$("crumb");crumb.textContent="";
  const parts=dir.split("/").filter(s=>s);let acc="";
  const link=(label,p)=>{const a=document.createElement("a");a.href="#";a.textContent=label;a.onclick=e=>{e.preventDefault();load(p)};crumb.appendChild(a)};
  link("SD","/");
  for(const s of parts){acc+="/"+s;crumb.appendChild(document.createTextNode(" / "));link(s,acc)}
  const list=$("list");list.textContent="";
  if(dir!=="/"){const tr=list.insertRow();const td=tr.insertCell();td.colSpan=3;const a=document.createElement("a");a.href="#";a.textContent=T("↑ 上へ");a.onclick=e=>{e.preventDefault();load(dir.replace(/\/[^/]*$/,"")||"/")};td.appendChild(a)}
  for(const r of rows){
    const tr=list.insertRow();const p=join(dir,r.name);
    const td=tr.insertCell();const a=document.createElement("a");
    a.textContent=(r.dir?"📁 ":"")+r.name;
    if(r.dir){a.href="#";a.onclick=e=>{e.preventDefault();load(p)}}
    else{a.href="/f"+enc(p)+"?dl=1"}
    td.appendChild(a);
    const sz=tr.insertCell();sz.className="size";sz.textContent=r.dir?"":size(r.size);
    const ops=tr.insertCell();ops.className="ops";
    const mv=document.createElement("button");mv.textContent=T("名前");mv.onclick=()=>rename(p,r.name);ops.appendChild(mv);
    ops.appendChild(document.createTextNode(" "));
    const rm=document.createElement("button");rm.textContent=T("削除");rm.className="danger";rm.onclick=()=>remove(p,r.name,r.dir);ops.appendChild(rm);
  }
  msg(T("%s 個",rows.length));
}
async function post(what,q){await api("/api/"+what+"?"+q,{method:"POST"})}
async function rename(p,name){
  const n=prompt(T("新しい名前"),name);if(!n||n===name)return;
  try{await post("mv","p="+encodeURIComponent(p)+"&to="+encodeURIComponent(join(cwd,n)));await load(cwd)}catch(e){msg(e.message,true)}
}
async function remove(p,name,dir){
  if(!confirm(dir?T("「%s」(フォルダの中身も全部)を消しますか?",name):T("「%s」を消しますか?",name)))return;
  try{await post("rm","p="+encodeURIComponent(p));await load(cwd)}catch(e){msg(e.message,true)}
}
$("mkdir").onclick=async()=>{
  const n=prompt(T("フォルダの名前"));if(!n)return;
  try{await post("mkdir","p="+encodeURIComponent(join(cwd,n)));await load(cwd)}catch(e){msg(e.message,true)}
};
$("reload").onclick=()=>load(cwd);
// rel はフォルダごとのときの「フォルダ/中/名前」(無ければ名前だけ)
function enqueue(items){for(const it of items){queue.push({file:it.file,dir:cwd,rel:it.rel||it.file.name});total++}pump()}
$("files").onchange=e=>{enqueue([...e.target.files].map(f=>({file:f})));e.target.value=""};
$("dirs").onchange=e=>{enqueue([...e.target.files].map(f=>({file:f,rel:f.webkitRelativePath||f.name})));e.target.value=""};
// 落とされたフォルダは中を辿る(readEntries は少しずつ返すので空になるまで読む)
async function walk(entry,prefix,out){
  if(entry.isFile){out.push({file:await new Promise((ok,ng)=>entry.file(ok,ng)),rel:prefix+entry.name});return}
  if(!entry.isDirectory)return;
  const rd=entry.createReader();
  for(;;){const list=await new Promise((ok,ng)=>rd.readEntries(ok,ng));if(!list.length)break;for(const c of list)await walk(c,prefix+entry.name+"/",out)}
}
const drop=$("drop");
drop.ondragover=e=>{e.preventDefault();drop.classList.add("over")};
drop.ondragleave=()=>drop.classList.remove("over");
drop.ondrop=async e=>{
  e.preventDefault();drop.classList.remove("over");
  const items=[...(e.dataTransfer.items||[])];
  const entries=items.map(i=>i.webkitGetAsEntry&&i.webkitGetAsEntry()).filter(x=>x);
  if(!entries.length){enqueue([...e.dataTransfer.files].map(f=>({file:f})));return}
  const out=[];
  try{for(const en of entries)await walk(en,"",out)}catch(err){msg(T("フォルダを読めませんでした"),true);return}
  enqueue(out);
};
$("cancel").onclick=()=>{queue.length=0;if(busy)busy.abort()};
function pump(){
  const line=$("qline"),cancel=$("cancel");
  if(busy)return;
  const job=queue.shift();
  if(!job){line.textContent="";cancel.style.display="none";sent=total=0;load(cwd);return}
  cancel.style.display="";
  const head=()=>(total>1?(sent+1)+"/"+total+" ":"")+job.rel;
  line.textContent=head()+" 0%";
  const x=new XMLHttpRequest();busy=x;
  x.open("PUT","/f"+enc(join(job.dir,job.rel))+(job.rel.includes("/")?"?mkdirs=1":""));
  x.setRequestHeader("X-Pico-Key",key());
  x.upload.onprogress=e=>{if(e.lengthComputable)line.textContent=head()+" "+Math.floor(e.loaded*100/e.total)+"%"};
  const done=err=>{busy=null;sent++;if(err)msg(job.rel+": "+err,true);pump()};
  x.onload=()=>{if(x.status===401){queue.length=0;busy=null;showLogin();return}done(x.status<300?"":(x.responseText?T(x.responseText):T("エラー %s",x.status)))};
  x.onerror=()=>done(T("送れませんでした"));
  x.onabort=()=>done(T("やめました"));
  x.send(job.file);
}
const k=new URLSearchParams(location.search).get("k");
if(k){setKey(k);history.replaceState(null,"","/")}
if(key())start();else showLogin();
</script></body></html>
)PAGE";

size_t PageHtmlLength(){ return sizeof(kPageHtml) - 1; }

}
