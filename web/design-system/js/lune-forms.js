/* Lune dirty/save + autosave. Progressive enhancement. */
(function(){
var I={};try{I=JSON.parse((document.getElementById("i18n")||{}).textContent||"{}")}catch(e){}
function t(k,v){var s=I[k]||k;if(v)for(var n in v)s=s.split("{"+n+"}").join(String(v[n]));return s}
function snap(f){var o={};f.querySelectorAll("input,select,textarea").forEach(function(el){
if(!el.name||/^(submit|reset|button|file)$/.test(el.type))return;
var k=el.name+(el.type==="radio"||el.type==="checkbox"?":"+el.value:"");
o[k]=el.type==="checkbox"||el.type==="radio"?el.checked:el.value});return o}
function wrap(el){return el.closest(".setting,.field,.switch,.seg,.compass")}
function btn(f){return f.querySelector(':is(.panel-foot,.savebar) .btn.primary[type="submit"]')}
function st(f){return f.querySelector(".save-status")}
function auto(f){return!!f.querySelector(".climate")}
function track(f){return!!st(f)}
function cross(){
document.querySelectorAll('.tile[data-dirty],.mode label[data-dirty],.section-nav a[data-dirty],.section[data-dirty],details.section[data-dirty],.sys-nav label[data-dirty],[popovertarget][data-dirty]').forEach(function(el){
el.removeAttribute("data-dirty")});
var conf=false,sys=false;
document.querySelectorAll("form[data-save][data-dirty]").forEach(function(f){
var cat=f.closest(".sys-cat[data-cat]");if(cat){var cl=document.querySelector('.sys-nav label[for="c-'+cat.dataset.cat+'"]');if(cl)cl.setAttribute("data-dirty","")}
var sh=f.closest(".sheet[id]");if(sh)document.querySelectorAll('[popovertarget="'+sh.id+'"]:not(.sheet-close)').forEach(function(o){o.setAttribute("data-dirty","")});
var v=f.closest(".view");if(!v||!v.id)return;if(v.id==="v-sys"){sys=true;return}var m=/^v-(dash|conf|home)-(.+)$/.exec(v.id);if(!m)return;
if(m[1]==="conf")conf=true;var tile=document.querySelector('label.tile[for="s-'+m[2]+'"]');if(tile)tile.setAttribute("data-dirty","");
var sec=f.closest("section.section,details.section");
if(sec){sec.setAttribute("data-dirty","");
if(sec.id){var link=document.querySelector('.section-nav a[href="#'+sec.id+'"]');
if(link)link.setAttribute("data-dirty","")}}
});
if(conf||sys){var lab=document.querySelector('.mode label[for="m-'+(sys?"sys":"conf")+'"]');if(lab)lab.setAttribute("data-dirty","")}}
function openSectionFromHash(){
var id=(location.hash||"").replace(/^#/,"");if(!id)return;
var el=document.getElementById(id);
if(el&&el.matches&&el.matches("details.section"))el.open=true;
/* Deep link fra en anden enhed (fx Touch' V6-række): #s-z3 vælger omfanget. */
if(el&&el.matches&&el.matches("input.state[type=radio]")){el.checked=true;window.scrollTo(0,0)}}
window.addEventListener("hashchange",openSectionFromHash);
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",openSectionFromHash);
else openSectionFromHash();
function paint(f){
if(auto(f)||!track(f)||f.dataset.state==="saving"||f.dataset.state==="saved")return;
var s=f._snap||{},c=snap(f),n=0,seen={},sid=(st(f)||{}).id;
f.querySelectorAll("[data-dirty]").forEach(function(w){w.removeAttribute("data-dirty");if(w.getAttribute("aria-describedby")===sid)w.removeAttribute("aria-describedby")});
Object.keys(Object.assign({},s,c)).forEach(function(k){
if(s[k]===c[k])return;var name=k.split(":")[0];if(seen[name])return;seen[name]=1;n++;
var el=f.querySelector('[name="'+name+'"]'),w=el&&wrap(el);
if(w){w.setAttribute("data-dirty","");if(sid)w.setAttribute("aria-describedby",sid)}});
f.dataset.changes=String(n);var b=btn(f),sEl=st(f);
if(n){f.dataset.dirty="";if(b){b.removeAttribute("aria-disabled");b.removeAttribute("title")}
if(sEl&&f.dataset.state!=="error")sEl.textContent=n===1?t("rt.unsaved.one",{n:1}):t("rt.unsaved.other",{n:n})}
else{delete f.dataset.dirty;if(b){b.setAttribute("aria-disabled","true");b.title=t("rt.nothingToSave")}
if(sEl&&f.dataset.state!=="saved"&&f.dataset.state!=="error")sEl.textContent=""}
cross()}
/* Gør formularens nuværende værdier til standardværdier (reset/Fortryd vender tilbage hertil). */
function adopt(f){f.querySelectorAll("input,select,textarea").forEach(function(el){
if(el.type==="checkbox"||el.type==="radio")el.defaultChecked=el.checked;
else if(el.tagName==="SELECT")[].forEach.call(el.options,function(o){o.defaultSelected=o.selected});
else if(el.type!=="file")el.defaultValue=el.value})}
function bind(f){
if(f.dataset.js)return;
f.dataset.js="1";f._snap=snap(f);f._label=(btn(f)||{}).textContent||"";
f.luneResnap=function(){adopt(f);f._snap=snap(f);if(track(f)&&!auto(f))paint(f)};
f.luneSaved=function(ok,msg){
var b=btn(f),sEl=st(f),a=f.querySelector(".autosave"),sid=(sEl||{}).id;delete f.dataset.state;if(b)b.removeAttribute("aria-busy");
if(ok){adopt(f);f._snap=snap(f);if(auto(f)){if(a){a.textContent=t("rt.autoSaved");setTimeout(function(){if(a.textContent===t("rt.autoSaved"))a.textContent=""},2000)}return}
f.querySelectorAll("[data-dirty]").forEach(function(w){w.removeAttribute("data-dirty");if(sid&&w.getAttribute("aria-describedby")===sid)w.removeAttribute("aria-describedby")});
f.dataset.state="saved";if(b)b.textContent=t("rt.savedOk")+" ✓";if(sEl)sEl.textContent="";delete f.dataset.dirty;cross();
setTimeout(function(){delete f.dataset.state;if(b)b.textContent=f._label;paint(f)},3000)}
else if(auto(f)){if(a)a.innerHTML=t("rt.autoFailed")+' <button type="button" class="btn" data-autosave-retry>'+t("rt.retry")+"</button>"}
else{f.dataset.state="error";if(sEl)sEl.textContent=msg||t("rt.saveFailed");if(b)b.textContent=f._label;paint(f)}};
if(track(f)&&!auto(f))paint(f)}
document.querySelectorAll("form[data-save]").forEach(bind);
/* Til bindere, der tegner formularer efter indlæsning (fx ark pr. rum). */
window.luneForms={bind:bind,scan:function(root){(root||document).querySelectorAll("form[data-save]:not([data-js])").forEach(bind)},adopt:adopt};
function onEdit(e){
var f=e.target&&e.target.closest&&e.target.closest("form[data-save]");if(!f||!f.dataset.js)return;
if(auto(f)){if(e.target.disabled)return;var a=f.querySelector(".autosave");if(a)a.textContent=t("rt.autoSaving");
clearTimeout(f._autoT);f._autoT=setTimeout(function(){f._autoT=null;f.dataset.state="saving";
document.dispatchEvent(new CustomEvent("lune:save",{detail:{key:f.dataset.save,data:new FormData(f),auto:true,form:f}}))},1500)}
else if(track(f)){delete f.dataset.state;paint(f)}}
document.addEventListener("input",onEdit,true);document.addEventListener("change",onEdit,true);
document.addEventListener("reset",function(e){var f=e.target;if(f&&f.matches&&f.matches("form[data-save]"))setTimeout(function(){delete f.dataset.state;paint(f)},0)});
document.addEventListener("click",function(e){
var retry=e.target.closest&&e.target.closest("[data-autosave-retry]");
if(retry){var f=retry.closest("form[data-save]");if(f){var a=f.querySelector(".autosave");if(a)a.textContent=t("rt.autoSaving");
f.dataset.state="saving";
document.dispatchEvent(new CustomEvent("lune:save",{detail:{key:f.dataset.save,data:new FormData(f),auto:true,form:f}}))}return}
var copy=e.target.closest&&e.target.closest("[data-copy]");
if(copy){e.preventDefault();var sel=copy.getAttribute("data-copy"),src=sel?document.querySelector(sel):null;
var text=(src&&(src.textContent||src.value)||"").trim();if(!text||text==="—")return;
var done=function(){var prev=copy.textContent;copy.textContent=t("device.copied")||t("rt.copied")||"Copied";
setTimeout(function(){copy.textContent=prev},1600)};
if(navigator.clipboard&&navigator.clipboard.writeText)navigator.clipboard.writeText(text).then(done).catch(function(){
var ta=document.createElement("textarea");ta.value=text;document.body.appendChild(ta);ta.select();
try{document.execCommand("copy");done()}catch(err){}ta.remove()});return}
var b=e.target.closest&&e.target.closest('.btn.primary[type="submit"]');
if(b&&b.getAttribute("aria-disabled")==="true"&&b===btn(b.closest("form[data-save]"))){e.preventDefault();e.stopPropagation()}},true);
document.addEventListener("submit",function(e){
var f=e.target;if(!f||!f.matches||!f.matches("form[data-save]"))return;e.preventDefault();
var sub=e.submitter,b=btn(f),isSave=!sub||sub===b;
if(isSave&&b&&b.getAttribute("aria-disabled")==="true")return;
if(isSave&&f.dataset.state==="saving")return;
/* preventDefault slår popovertargetaction="hide" fra på submit-knappen. */
var pop=sub&&sub.closest&&sub.closest(".confirm-pop");
if(pop&&pop.matches(":popover-open"))pop.hidePopover();
if(isSave&&track(f)&&!auto(f)){f.dataset.state="saving";if(b){b.setAttribute("aria-busy","true");b.textContent=t("rt.saving")}}
/* Delvis gem = patch (DESIGN.md 6.1): form[data-patch] sender kun ændrede felter.
   key "heat_source.connection" → resource "heat_source", part "connection". */
var key=f.dataset.save,dot=key.indexOf("."),changed=null;
if(f.hasAttribute("data-patch")){changed={};var s0=f._snap||{},c0=snap(f);
Object.keys(Object.assign({},s0,c0)).forEach(function(k){if(s0[k]===c0[k])return;var nm=k.split(":")[0];
var el=f.querySelector('[name="'+nm+'"]');if(!el)return;
if(el.type==="radio"){var on=f.querySelector('[name="'+nm+'"]:checked');changed[nm]=on?on.value:null}
else if(el.type==="checkbox"){changed[nm]=f.querySelectorAll('[name="'+nm+'"]').length>1?[].map.call(f.querySelectorAll('[name="'+nm+'"]:checked'),function(x){return x.value}):el.checked}
else changed[nm]=el.value})}
document.dispatchEvent(new CustomEvent("lune:save",{detail:{key:key,resource:dot>0?key.slice(0,dot):key,part:dot>0?key.slice(dot+1):null,
method:changed?"PATCH":"POST",changed:changed,data:new FormData(f,sub),auto:false,form:f}}))});
/* ---- Ark, faner, System og deep links (DESIGN.md 15) ---- */
function dirtyIn(el){return!!(el&&el.querySelector("form[data-save][data-dirty]"))}
function sheetOpen(){return document.querySelector(".sheet:popover-open")}
function hashOf(el){return el?(el.getAttribute("data-hash")||(el.id||"").replace(/^(sheet-|c-)/,"")):""}
function writeHash(){
var sh=sheetOpen(),h="";
if(sh){h=hashOf(sh);var tb=sh.querySelector("input.tab:checked"),first=sh.querySelector("input.tab");if(tb&&tb!==first)h+="/"+hashOf(tb)}
else{var ms=document.getElementById("m-sys");if(ms&&ms.checked){h=hashOf(ms)||"system";var c=document.querySelector('input[name="syscat"]:checked');if(c&&c.id!=="c-none")h+="/"+hashOf(c)}}
var url=location.pathname+location.search+(h?"#"+h:"");
if(url!==location.pathname+location.search+location.hash)history.replaceState(null,"",url)}
function readHash(){
var parts=decodeURIComponent((location.hash||"").replace(/^#/,"")).split("/");if(!parts[0])return;
/* Første niveau: et ark eller en tilstand (#m-sys) — aldrig en fane eller System-kategori. */
var root=document.querySelector('[popover][data-hash="'+parts[0]+'"],input[name="mode"][data-hash="'+parts[0]+'"]')||document.getElementById("sheet-"+parts[0]);
if(!root)return;
if(root.matches("input")){root.checked=true;
if(parts[1]){var c=document.querySelector('input[name="syscat"][data-hash="'+parts[1]+'"]')||document.getElementById("c-"+parts[1]);if(c)c.checked=true}}
else if(root.matches("[popover]")){
if(parts[1]){var tb=root.querySelector('input.tab[data-hash="'+parts[1]+'"]')||root.querySelector('input.tab[value="'+parts[1]+'"]');if(tb)tb.checked=true}
if(!root.matches(":popover-open"))try{root.showPopover()}catch(e){}}}
window.addEventListener("hashchange",readHash);
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",readHash);else readHash();
/* data-tab på en trigger: åbn arket direkte på den fane. */
document.addEventListener("click",function(e){
var trg=e.target.closest&&e.target.closest("[popovertarget][data-tab]");if(!trg)return;
var sh=document.getElementById(trg.getAttribute("popovertarget")),tb=sh&&sh.querySelector('input.tab[value="'+trg.dataset.tab+'"]');if(tb)tb.checked=true},true);
/* Luk ark med ugemte ændringer: advar (uden JS lukker det bare). */
document.addEventListener("toggle",function(e){
var sh=e.target;if(!sh.classList||!sh.classList.contains("sheet"))return;
if(e.newState==="closed"&&dirtyIn(sh)&&!sh._leaving){
if(!window.confirm(t("rt.leaveUnsaved"))){try{sh.showPopover()}catch(err){}return}
sh.querySelectorAll("form[data-save][data-dirty]").forEach(function(f){f.reset()})}
writeHash()},true);
/* Skift mellem Hjem/System og systemkategorier: advar ved ugemte ændringer i det, man forlader. */
var prev={};
document.querySelectorAll('input.state[type=radio]:checked').forEach(function(r){prev[r.name]=r});
document.addEventListener("change",function(e){
var r=e.target;if(!r.matches||!r.matches("input[type=radio]"))return;
if(r.name==="mode"||r.name==="syscat"){
var old=prev[r.name],scope=old&&(r.name==="mode"?document.getElementById("v-sys"):document.querySelector('.sys-cat[data-cat="'+old.id.replace(/^c-/,"")+'"]'));
if(old&&old.id==="m-sys"&&r.name==="mode"||r.name==="syscat"){
if(scope&&dirtyIn(scope)&&!window.confirm(t("rt.leaveUnsaved"))){old.checked=true;return}}
prev[r.name]=r;writeHash()}
else if(r.matches("input.tab"))writeHash()},true);
window.addEventListener("beforeunload",function(e){if(document.querySelector("form[data-save][data-dirty]")){e.preventDefault();e.returnValue=t("rt.leaveUnsaved")}});
function placeConfirm(pop){
if(!pop)return;
if(window.matchMedia("(max-width:599.98px)").matches){
pop.style.position="";pop.style.inset="";pop.style.top="";pop.style.left="";
pop.style.right="";pop.style.bottom="";pop.style.margin="";pop.style.maxHeight="";
return}
var btn=document.querySelector('button.btn.danger[popovertarget="'+pop.id+'"]');
if(!btn)return;
var margin=8,gap=8,r=btn.getBoundingClientRect();
pop.style.position="fixed";
pop.style.inset="unset";
pop.style.right="auto";
pop.style.bottom="auto";
pop.style.margin="0";
pop.style.maxHeight="none";
pop.style.top="0px";
pop.style.left="0px";
var w=pop.offsetWidth||288,h=pop.offsetHeight||180;
var vw=window.innerWidth,vh=window.innerHeight,maxH=vh-margin*2;
if(h>maxH)h=maxH;
var left=r.left+(r.width-w)/2;
if(left<margin)left=margin;
if(left+w>vw-margin)left=Math.max(margin,vw-w-margin);
var below=r.bottom+gap,above=r.top-gap-h;
var top=(below+h<=vh-margin)?below:above;
if(top<margin)top=margin;
if(top+h>vh-margin)top=Math.max(margin,vh-h-margin);
pop.style.maxHeight=maxH+"px";
pop.style.top=top+"px";
pop.style.left=left+"px";
}
document.addEventListener("toggle",function(e){
if(e.newState!=="open"||!e.target.classList||!e.target.classList.contains("confirm-pop"))return;
placeConfirm(e.target);
requestAnimationFrame(function(){if(e.target.matches(":popover-open"))placeConfirm(e.target)});
},true);
window.addEventListener("resize",function(){
document.querySelectorAll(".confirm-pop:popover-open").forEach(placeConfirm);
});
})();
