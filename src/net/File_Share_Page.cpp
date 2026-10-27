#include "net/File_Share.hpp"

// Wi-Fi転送のページ(ブラウザで開く1枚)。取り決めは net/File_Share.hpp の先頭。
// 中身はSDの名前を表示するだけなので、名前は必ず textContent で入れる(innerHTML へ入れない)。
// キーはクッキー pk に持ち、書き換える要求には X-Pico-Key 見出しでも送る。
// アップロードは1つずつ順に送る(本体は同時に1本しか相手をしない)。進み具合が要るので fetch ではなく XHR。
namespace FileShare {

const char kPageHtml[] = R"PAGE(<!doctype html>
<html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>pico-os ファイル</title>
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
#queue div{font-size:90%;color:#5f574f}
#login{display:none;background:#fff;padding:16px;border-radius:8px}
#drop{outline:3px dashed transparent}
#drop.over{outline-color:#29adff}
input[type=text]{font:inherit;padding:5px;width:12em}
</style></head><body>
<header id="title">pico-os</header>
<main>
<form id="login"><p>本体の画面に出ているキーを入れてください。</p>
<input type="text" id="key" autocomplete="off" autocapitalize="off" spellcheck="false" maxlength="8">
<button>開く</button></form>
<div id="app" style="display:none">
<div id="crumb"></div>
<div id="bar">
<label class="btn">アップロード<input type="file" id="files" multiple hidden></label>
<button id="mkdir">新しいフォルダ</button>
<button id="reload">更新</button>
</div>
<div id="msg"></div>
<div id="queue"></div>
<div id="drop"><table><tbody id="list"></tbody></table></div>
</div>
</main>
<script>
"use strict";
const $=id=>document.getElementById(id);
let cwd="/",busy=false;
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
  if(r.status===401){showLogin();throw new Error("キーが違います")}
  if(!r.ok){throw new Error((await r.text())||("エラー "+r.status))}
  return r;
}
function showLogin(){$("app").style.display="none";$("login").style.display="block";$("key").focus()}
$("login").onsubmit=e=>{e.preventDefault();setKey($("key").value.trim().toLowerCase());start()};
async function start(){
  try{
    const info=await (await api("/api/info")).json();
    $("title").textContent=info.name+" のファイル";document.title=info.name+" のファイル";
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
  if(dir!=="/"){const tr=list.insertRow();const td=tr.insertCell();td.colSpan=3;const a=document.createElement("a");a.href="#";a.textContent="↑ 上へ";a.onclick=e=>{e.preventDefault();load(dir.replace(/\/[^/]*$/,"")||"/")};td.appendChild(a)}
  for(const r of rows){
    const tr=list.insertRow();const p=join(dir,r.name);
    const td=tr.insertCell();const a=document.createElement("a");
    a.textContent=(r.dir?"📁 ":"")+r.name;
    if(r.dir){a.href="#";a.onclick=e=>{e.preventDefault();load(p)}}
    else{a.href="/f"+enc(p)+"?dl=1"}
    td.appendChild(a);
    const sz=tr.insertCell();sz.className="size";sz.textContent=r.dir?"":size(r.size);
    const ops=tr.insertCell();ops.className="ops";
    const mv=document.createElement("button");mv.textContent="名前";mv.onclick=()=>rename(p,r.name);ops.appendChild(mv);
    ops.appendChild(document.createTextNode(" "));
    const rm=document.createElement("button");rm.textContent="削除";rm.className="danger";rm.onclick=()=>remove(p,r.name,r.dir);ops.appendChild(rm);
  }
  msg(rows.length+" 個");
}
async function post(what,q){await api("/api/"+what+"?"+q,{method:"POST"})}
async function rename(p,name){
  const n=prompt("新しい名前",name);if(!n||n===name)return;
  try{await post("mv","p="+encodeURIComponent(p)+"&to="+encodeURIComponent(join(cwd,n)));await load(cwd)}catch(e){msg(e.message,true)}
}
async function remove(p,name,dir){
  if(!confirm("「"+name+"」"+(dir?"(フォルダの中身も全部)":"")+"を消しますか?"))return;
  try{await post("rm","p="+encodeURIComponent(p));await load(cwd)}catch(e){msg(e.message,true)}
}
$("mkdir").onclick=async()=>{
  const n=prompt("フォルダの名前");if(!n)return;
  try{await post("mkdir","p="+encodeURIComponent(join(cwd,n)));await load(cwd)}catch(e){msg(e.message,true)}
};
$("reload").onclick=()=>load(cwd);
function enqueue(files){for(const f of files)queue.push({file:f,dir:cwd});pump()}
$("files").onchange=e=>{enqueue(e.target.files);e.target.value=""};
const drop=$("drop");
drop.ondragover=e=>{e.preventDefault();drop.classList.add("over")};
drop.ondragleave=()=>drop.classList.remove("over");
drop.ondrop=e=>{e.preventDefault();drop.classList.remove("over");enqueue(e.dataTransfer.files)};
function pump(){
  const q=$("queue");
  if(busy)return;
  const job=queue.shift();
  if(!job){q.textContent="";load(cwd);return}
  busy=true;
  const line=document.createElement("div");q.textContent="";q.appendChild(line);
  const left=()=>queue.length?"(あと"+queue.length+"個)":"";
  line.textContent=job.file.name+" 0% "+left();
  const x=new XMLHttpRequest();
  x.open("PUT","/f"+enc(join(job.dir,job.file.name)));
  x.setRequestHeader("X-Pico-Key",key());
  x.upload.onprogress=e=>{if(e.lengthComputable)line.textContent=job.file.name+" "+Math.floor(e.loaded*100/e.total)+"% "+left()};
  const done=err=>{busy=false;if(err)msg(job.file.name+": "+err,true);pump()};
  x.onload=()=>{if(x.status===401){queue.length=0;busy=false;showLogin();return}done(x.status<300?"":(x.responseText||"エラー "+x.status))};
  x.onerror=()=>done("送れませんでした");
  x.send(job.file);
}
const k=new URLSearchParams(location.search).get("k");
if(k){setKey(k);history.replaceState(null,"","/")}
if(key())start();else showLogin();
</script></body></html>
)PAGE";

size_t PageHtmlLength(){ return sizeof(kPageHtml) - 1; }

}
