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
if(root.matches("input")){root.checked=true;document.querySelectorAll(".sheet:popover-open").forEach(function(o){try{o.hidePopover()}catch(e){}});
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
/* Lune Touch binder — live data + save hooks for the Home / sheet / System page
   (Lune Design System 2.3, DESIGN.md 15). Progressive enhancement only.
   Dirty/save, autosave, sheets, tabs and deep links come from LDS lune-forms.js,
   which build_ui.py places before this file in /ui.js; this file listens to
   lune:save and talks to /api/lune-touch/v1. */
(function () {
  'use strict';

  var BASE = '/api/lune-touch/v1';
  var POLL_MS = 20000;
  var i18n = {};
  try {
    var el = document.getElementById('i18n');
    if (el) i18n = JSON.parse(el.textContent || '{}');
  } catch (e) {}

  var dec = i18n._dec || '.';
  var state = {
    rooms: [], zones: [], nodes: [], overview: null, forecast: null, heat: null,
    settings: null, diagnostics: null, strategy: null, scanFound: [], scanDone: false,
    roomSlots: [], slotOf: {}, byM: {}, chartLoaded: {},
    fwInstalled: '', fwLatest: null, fwAsset: null
  };
  var THERMO_MIN = 15, THERMO_MAX = 25;

  var RELEASE_LATEST_API = 'https://api.github.com/repos/birkemosen/lune-coordinator/releases/latest';
  var OTA_UPLOAD_PATH = '/update';
  var BACKUP_TYPE = 'lune-touch-settings';
  var BACKUP_VERSION = 1;

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }

  var PENCIL = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 20h4L18 10l-4-4L4 16v4z"/><path d="M13 7l4 4"/></svg>';

  function num(x, digits) {
    if (x == null || x !== x) return '—';
    var n = Number(x);
    if (!isFinite(n)) return '—';
    var d = digits == null ? 1 : digits;
    return n.toFixed(d).replace('.', dec);
  }

  function finite(x) { return x != null && x !== '' && isFinite(Number(x)); }

  function t(key, vars) {
    var s = i18n[key] || key;
    if (!vars) return s;
    return s.replace(/\{(\w+)\}/g, function (_, k) { return vars[k] != null ? vars[k] : ''; });
  }

  function qs(sel, root) { return (root || document).querySelector(sel); }
  function qsa(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }

  function setBind(key, html) {
    qsa('[data-bind="' + key + '"]').forEach(function (n) { n.innerHTML = html; });
  }

  function setText(key, text) {
    qsa('[data-bind="' + key + '"]').forEach(function (n) { n.textContent = text; });
  }

  function setShow(key, on) {
    qsa('[data-bind-show="' + key + '"]').forEach(function (n) { n.hidden = !on; });
  }

  // Slot-local fields (room and controller sheets): data-f inside one sheet.
  function fText(root, f, text) { qsa('[data-f="' + f + '"]', root).forEach(function (n) { n.textContent = text; }); }
  function fHtml(root, f, html) { qsa('[data-f="' + f + '"]', root).forEach(function (n) { n.innerHTML = html; }); }
  function fEl(root, f) { return qs('[data-f="' + f + '"]', root); }

  function formObj(fd) {
    var o = {};
    fd.forEach(function (v, k) {
      if (o[k] !== undefined) {
        if (!Array.isArray(o[k])) o[k] = [o[k]];
        o[k].push(v);
      } else o[k] = v;
    });
    return o;
  }

  function queryUrl(path, body) {
    var q = Object.keys(body || {}).map(function (k) {
      return encodeURIComponent(k) + '=' + encodeURIComponent(body[k] == null ? '' : body[k]);
    }).join('&');
    return BASE + path + (q ? '?' + q : '');
  }

  async function get(path) {
    if (window.LUNE_TOUCH_MOCK) return mockGet(path);
    var res = await fetch(BASE + path, { headers: { Accept: 'application/json' } });
    var json = await res.json();
    if (!json || json.ok === false) throw new Error((json && json.error && json.error.message) || 'request failed');
    return json.data != null ? json.data : json;
  }

  async function post(path, body) {
    if (window.LUNE_TOUCH_MOCK) {
      // Preview only: keep a log of what would be sent (used by headless checks).
      (window.__lunePosts = window.__lunePosts || []).push({ path: path, body: JSON.parse(JSON.stringify(body || {})) });
      return mockPost(path, body || {});
    }
    var payload = body || {};
    var res;
    try {
      res = await fetch(BASE + path, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: Object.keys(payload).map(function (k) {
          return encodeURIComponent(k) + '=' + encodeURIComponent(payload[k] == null ? '' : payload[k]);
        }).join('&')
      });
    } catch (e) {
      res = await fetch(queryUrl(path, payload), { method: 'POST', body: '' });
    }
    var json = await res.json();
    if (!json || json.ok === false) throw new Error((json && json.error && json.error.message) || 'save failed');
    return json.data != null ? json.data : json;
  }

  /* ---- Preview mock (window.LUNE_TOUCH_MOCK, dist/preview-*.html) ----------
     Three V6 controllers (the third is offline), one motor fault (Gang), one
     room without data (Lager), one grouped room (Stue Z5–6). */
  var mockHeat = {
    type: 'asgard',
    enabled: true,
    host: 'asgard.local',
    port: 80,
    weighted_temperature_variable: 'temperature_feedback_z1',
    push_interval_s: 60,
    write_url_template: '',
    read_url_template: '',
    climate_entity: 'Virtual Thermostat z1',
    target_sync_enabled: true,
    house_balance: { enabled: false, boards: [
      { node_id: 'lune-v6-cbe680', name: 'Stue', worst_kpa: 17.6, scale: 0.74, applied: 1 },
      { node_id: 'lune-v6-cbe67c', name: '1. sal', worst_kpa: 32.1, scale: 1, applied: 1 }] },
    odin_plan_enabled: true,
    odin_host: 'odin.local',
    physical_house_temperature_c: 21.6,
    house_comfort_target_c: 21.5,
    house_target: { available: true, value_c: 21.5 },
    send_preview: { available: true, value_c: 21.6, target_setpoint_c: 21.5, target_available: true, mode: 'active' },
    push: {
      has_result: true, status: 'confirmed', http_status: 200,
      requested_value_c: 21.6, confirmed_value_c: 21.6,
      write_age_s: 42, confirmation_age_s: 42, failure_streak: 0, last_error: ''
    },
    target_sync: { last_written_c: 21.5, last_confirmed_c: 21.5, failure_streak: 0, write_age_s: 42 },
    heat_pump: { available: true, feed_c: 36.0, return_c: 29.0, compressor_on: true, compressor_hz: 38 },
    circulation: {
      host: 'alpha2go.local', port: 80, flow_m3h: 1.34, head_m: 2.9, power_w: 38,
      flow_entity: 'pump_flow', head_entity: 'pump_head_pressure', power_entity: 'pump_power',
      mixing: { state: 'risk', ratio: 1.31, primary_l_min: 17.0, secondary_l_min: 22.3, age_s: 120 }
    }
  };

  var mockWifi = { ssid: 'Hjemme', connected: true, ap_active: false, 'switch': 'none', target_ssid: '' };
  var mockMqtt = { enabled: false, host: '', port: 1883, username: '', topic_prefix: '', hp_id: '', password_set: false, connected: false };
  var mockUptime = 540000;
  var mockNodeList = [
    { id: 'v6-teknik', name: 'Teknikrum', device_name: 'Teknikrum', hostname: 'lune-v6-teknik.local', ip: '192.168.20.106', firmware: '6.4.2',
      reachable: true, trust_label: 'trusted', lease: 'granted', last_seen_ms: (mockUptime - 4) * 1000,
      health: { mapped_zones: 5 }, runtime: { flow_c: 36.0, return_c: 29.0 } },
    { id: 'v6-1sal', name: '1. sal', device_name: '1. sal', hostname: 'lune-v6-1sal.local', ip: '192.168.20.107', firmware: '6.4.2',
      reachable: true, trust_label: 'trusted', lease: 'granted', last_seen_ms: (mockUptime - 6) * 1000,
      health: { mapped_zones: 5 }, runtime: { flow_c: 33.2, return_c: 29.6 } },
    { id: 'v6-anneks', name: '', device_name: 'Anneks', hostname: 'lune-v6-anneks.local', ip: '192.168.20.108', firmware: '6.4.1',
      reachable: false, trust_label: 'trusted', lease: 'granted', last_seen_ms: (mockUptime - 7200) * 1000,
      health: { mapped_zones: 2 }, runtime: { flow_c: 30.5, return_c: 27.8 } }
  ];
  // room_id, name, node, zone, temp, setpoint, status, valve, area, walls, include, weight, wind, solar
  var mockRoomRows = [
    ['room-01', 'Kontor', 0, 0, 22.9, 22.0, 'idle', 18, 12, 4, true, 1, 0.3, 0.5],
    ['room-02', 'Hobbyrum', 0, 1, 23.0, 22.0, 'idle', 22, 16, 0, true, 1, 0.2, 0.2],
    ['room-03', 'Bryggers', 0, 2, 21.6, 21.0, 'idle', 4, 8, 1, false, 0.5, 0.5, 0],
    ['room-04', 'Køkken', 0, 3, 21.7, 21.0, 'idle', 0, 14, 2, true, 1, 0.4, 0.3],
    ['room-05', 'Stue', 0, 4, 21.4, 21.5, 'calling', 54, 48, 12, true, 1.5, 0.6, 0.7],
    ['room-05', 'Stue', 0, 5, 21.4, 21.5, 'calling', 50, 0, 0, true, 1.5, 0.6, 0.7],
    ['room-06', 'Josephine', 1, 0, 21.4, 22.0, 'calling', 62, 12, 12, true, 1, 0.7, 0.4],
    ['room-07', 'Laura', 1, 1, 22.2, 22.0, 'idle', 35, 11, 3, true, 1, 0.5, 0.3],
    ['room-08', 'Toilet', 1, 2, 22.7, 22.0, 'idle', 14, 4.5, 0, false, 0.25, 0, 0],
    ['room-09', 'Gang', 1, 3, 19.1, 20.0, 'fault', 0, 10, 1, true, 0.75, 0.3, 0],
    ['room-10', 'Bad', 1, 4, 23.3, 22.0, 'idle', 48, 8, 0, true, 0.5, 0, 0],
    ['room-11', 'Værksted', 2, 0, 16.8, 18.0, 'idle', 20, 22, 8, false, 0.5, 0.5, 0.2],
    ['room-12', 'Lager', 2, 1, null, 15.0, 'unknown', null, 18, 0, false, 0.25, 0.3, 0]
  ];
  var mockRoomStore = {};
  function mockZones() {
    var seen = {};
    var zones = mockRoomRows.map(function (r) {
      var rid = r[0];
      var st = mockRoomStore[rid] || (mockRoomStore[rid] = { include: r[10], weight: r[11], wind: r[12], solar: r[13], revision: 3 });
      var secondary = !!seen[rid];
      seen[rid] = true;
      var members = rid === 'room-05' ? [5, 6] : [];
      var hasT = r[4] != null;
      return {
        room_id: rid, name: r[1], node_index: r[2], zone_index: r[3],
        temperature_c: r[4], setpoint_c: r[5], status: r[6], valve_pct: r[7], fresh: hasT && r[2] !== 2,
        is_group_secondary: secondary, unassigned: false, group_members: members,
        room: { revision: st.revision, total_area_m2: r[8] || 48, physical_weight: st.weight, ua_w_per_k: 0,
          include_in_house_temperature: st.include, wind_exposure: st.wind, solar_gain: st.solar, floor_unset: false },
        comfort: { setpoint_c: r[5], bias_c: 0, effective_setpoint_c: r[5], priority: 1 },
        schedule: { enabled: false, day_mask: 127, start_min: 360, end_min: 1320, setpoint_c: r[5] },
        history: hasT ? { samples: 288, calling_samples: r[6] === 'calling' ? 140 : 40, avg_temp_c: r[4] - 0.2,
          min_temp_c: r[4] - 0.9, max_temp_c: r[4] + 0.4 } : { samples: 0, calling_samples: 0, avg_temp_c: null, min_temp_c: null, max_temp_c: null },
        forecast: { exterior_walls: r[9], wind_exposure: st.wind, solar_gain: st.solar, thermal_lead_h: 4, max_offset_c: 1.5 }
      };
    });
    var rooms = [];
    var rs = {};
    mockRoomRows.forEach(function (r) {
      if (rs[r[0]]) return;
      rs[r[0]] = 1;
      rooms.push({ room_id: r[0], name: r[1], loop_count: r[0] === 'room-05' ? 2 : 1, area_m2: r[8],
        include_in_house_temperature: mockRoomStore[r[0]] ? mockRoomStore[r[0]].include : r[10] });
    });
    return { rooms: rooms, zones: zones };
  }

  // DK1 2026-10-06 from Energi Data Service (spot DKK/kWh, hourly means of 15-min prices).
  var mockPrices = (function () {
    var spot = [1.0687, 1.1007, 1.1199, 1.1162, 1.1288, 1.2298, 1.5392, 1.7849, 1.8631, 1.5656, 1.3057, 1.0951,
      1.0252, 0.9895, 1.0431, 1.1543, 1.4143, 1.8572, 2.5288, 2.85, 2.2129, 1.8561, 1.6844, 1.5644];
    var grid = [], sys = [], total = [];
    for (var h = 0; h < 24; h++) {
      grid.push(h < 6 ? 0.0769 : (h >= 17 && h < 21 ? 0.6922 : 0.2307));
      sys.push(0.115);
      total.push(Math.round((spot[h] + grid[h] + sys[h] + 0.008) * 1.25 * 10000) / 10000);
    }
    var d = new Date();
    var p2 = function (n) { return n < 10 ? '0' + n : String(n); };
    var now = Math.floor(Date.now() / 1000);
    return {
      available: true, enabled: true, model: 'touch', zone: 'DK1', dk: true,
      spot: { source: 'eds', fixed_eur: 0.1 }, currency: 'DKK', fx: 7.46,
      grid: { source: 'datahub', gln: '5790000610976', code: 'TNT1009',
        schedule: [{ h: 0, v: 0.077 }, { h: 6, v: 0.231 }, { h: 17, v: 0.692 }, { h: 21, v: 0.231 }] },
      system: { source: 'datahub', fixed: 0.115 },
      energy_tax: 0.008, markup: 0, vat_pct: 25, entsoe_token_set: false, odin_host_set: true,
      odin: { mode: 'dynamic', source: 'energy_charts', fixed_price: 0.25,
        current: { known: true, price_mode: 'dynamic', price_source: 'api', ec_bzn: 'DK1', fixed_price: 0.25, token_set: true, age_s: 60 } },
      status: { state: 'ok', pushes: true, reason: 'day_ahead', odin_source: 'api', odin_write_pending: false,
        last_fetch_age_s: 240, last_push_epoch: now - 240, last_attempt_epoch: now - 240, hours_pushed: 24, fx: 7.4736,
        spot_used: 'eds', spot_fallback: false, grid_from_cache: false, system_from_cache: false, last_error: '' },
      today: { date: d.getFullYear() + '-' + p2(d.getMonth() + 1) + '-' + p2(d.getDate()),
        spot: spot, grid: grid, system: sys, total: total },
      tomorrow: null
    };
  })();

  // Subset of energy_price.h's zone table for the preview's "Apply zone defaults".
  var mockZoneDefaults = {
    DK1: ['eds', 'DKK', 7.46, 0.008, 25, 'datahub', 'datahub', 0.115],
    DK2: ['eds', 'DKK', 7.46, 0.008, 25, 'datahub', 'datahub', 0.115],
    NL: ['energy_charts', 'EUR', 1, 0.109, 21, 'none', 'fixed', 0],
    'DE-LU': ['energy_charts', 'EUR', 1, 0.041, 19, 'none', 'fixed', 0],
    SE3: ['energy_charts', 'SEK', 11, 0, 25, 'none', 'fixed', 0],
    NO1: ['energy_charts', 'NOK', 11.7, 0, 25, 'none', 'fixed', 0]
  };
  var mockSettings = {
    coordinator: { name: 'Lune Touch', site_label: 'Huskoordinator', install_id: 'demo', install_mode: 'commissioning' },
    display: { idle_timeout_s: 300 },
    weather: { max_boost_c: 1.5 },
    forecast: { latitude: 55.6761, longitude: 12.5683, max_boost_c: 1.5 }
  };
  var mockControl = {
    demand: { route: 'odin_schedule', held_c: 0.6, target_uplift_c: 0 },
    odin: { enabled: true, max_lift_c: 1.5, status: 'lifted', reason: 'slab_charge', last_action: 'write_lift',
      user_schedule_known: true, yielded: false,
      wanted: { active: true, start_hour: 15, hours: 3, lift_c: 0.5, energy_kwh: 6.0 },
      applied: { active: true, start_hour: 15, hours: 3, lift_c: 0.5 } },
    link: { status: 'ok', alarm: false, odin_reachable: true, forwarder_known: true, forwarder_active: true, takeover: true,
      telemetry_age_s: 40, odin_room_c: 22.4 },
    generic: { applies: false, curve_gain: 2, curve_max_offset_c: 5, status: 'idle' }
  };
  var mockTarget = 21.5;

  function mockScan() {
    return {
      scan: 'lan', poll_pending: false, poll_generation: 2,
      found: [
        { source: 'known_node', node_id: 'v6-teknik', hostname: 'lune-v6-teknik.local', ip: '192.168.20.106', reachable: true },
        { source: 'lan_probe', node_id: 'v6-new', hostname: 'lune-v6-kaelder.local', ip: '192.168.20.130', reachable: true }
      ]
    };
  }

  function mockProbe(path) {
    var asgard = document.getElementById('hs-asgard');
    var type = asgard && asgard.checked ? 'asgard' : 'http';
    var hostEl = document.querySelector(type === 'asgard' ? '[name="asgard_host"]' : '[name="http_host"]');
    var portEl = document.querySelector(type === 'asgard' ? '[name="asgard_port"]' : '[name="http_port"]');
    var host = hostEl ? String(hostEl.value || '') : '';
    var port = portEl ? Number(portEl.value) : 80;
    var entity = type === 'asgard' ? 'temperature_feedback_z1' : 'house_temp';
    var base = { action: path.indexOf('push') !== -1 ? 'push' : 'read', host: host, port: port, entity: entity };
    if (/timeout/i.test(host)) return Object.assign(base, { result: 'failed', error: 'probe_timeout', http_status: 0 });
    if (port === 500 || /500/.test(host)) return Object.assign(base, { result: 'failed', error: 'read http 500', http_status: 500 });
    if (base.action === 'push') {
      return Object.assign(base, {
        result: 'ok', http_status: 200, requested_value_c: 21.6, confirmed_value_c: 21.6,
        write_url: 'http://' + (host || 'asgard.local') + ':' + port + '/number/' + entity + '/set?value=21.6'
      });
    }
    return Object.assign(base, { result: 'ok', http_status: 200, value_c: 21.6 });
  }

  function mockGet(path) {
    if (path === '/overview') {
      return { house_temp_c: 21.6, house_target_c: mockTarget, coverage_ratio: 0.92, expected_manifolds: 3,
        contributing_manifolds: 2, calling_rooms: 2, fault_boards: 0, authority: 'Touch' };
    }
    if (path === '/nodes') return { nodes: JSON.parse(JSON.stringify(mockNodeList)) };
    if (path === '/nodes/scan') return mockScan();
    if (path === '/wifi') return mockWifi;
    if (path === '/odin/mqtt') return mockMqtt;
    if (path === '/prices') return JSON.parse(JSON.stringify(mockPrices));
    var zd = path.match(/^\/prices\/zone-defaults\/(.+)$/);
    if (zd) {
      var zid = decodeURIComponent(zd[1]), r = mockZoneDefaults[zid];
      if (!r) return { zone: zid, known: false, spot_source: 'energy_charts', currency: 'EUR', fx: 1, energy_tax: 0, vat_pct: 21, grid_source: 'none', system_source: 'fixed', system_fixed: 0 };
      return { zone: zid, known: true, spot_source: r[0], currency: r[1], fx: r[2], energy_tax: r[3], vat_pct: r[4], grid_source: r[5], system_source: r[6], system_fixed: r[7] };
    }
    if (path === '/strategy') {
      return { weighting: { basis: 'area' }, physical_house_temperature_c: 21.6, house_comfort_target_c: mockTarget };
    }
    if (path === '/zones' || path === '/rooms') return mockZones();
    if (path.indexOf('/comfort-chart') !== -1) {
      var hours = 24;
      var expected = [], scheduled = [];
      var hr = new Date().getHours();
      for (var h = 0; h < hours; h++) {
        var hod = (hr + h) % 24;
        var sp = 21.5 + (hod >= 22 || hod < 6 ? -1 : 0);
        expected.push(sp - 0.2 + Math.sin(h / 4) * 0.3);
        scheduled.push(sp);
      }
      return { comfort_chart: { hours: hours, expected_temp_c: expected, scheduled_setpoint_c: scheduled } };
    }
    if (path === '/forecast') {
      var now = Math.floor(Date.now() / 1000);
      now -= now % 3600;
      return {
        status: 'ok', last_fetch_age_s: 600,
        cache: { min_temp_c: 4.9, max_wind_ms: 11, hours: 72, decision_start_index: 14, fetch_epoch_s: now - 600, provider_timezone: 'Europe/Copenhagen' },
        hours: Array.from({ length: 72 }, function (_, h) {
          return {
            h: h, timestamp_s: now - 14 * 3600 + h * 3600,
            temp_c: 9 + Math.sin((h - 9) / 24 * 2 * Math.PI) * 4, wind_ms: 4.5 + Math.sin(h / 9) * 1.5 + (h > 20 && h < 34 ? 4 : 0),
            wind_dir_deg: 250, solar_wm2: (h % 24 > 7 && h % 24 < 18) ? 200 : 0,
            precip_mm: (h >= 20 && h <= 28) ? 1.2 : 0, cloud_pct: 40
          };
        }),
        decisions: [
          { room_id: 'room-06', name: 'Josephine', node_index: 1, zone_index: 0, offset_c: 0.6, active: true, preload_start_h: 6, preload_end_h: 17 },
          { room_id: 'room-01', name: 'Kontor', node_index: 0, zone_index: 0, offset_c: 0.4, active: true, preload_start_h: 6, preload_end_h: 14,
            charge: { episode: true, now: true, insufficient: false, store_c: 1.0, deficit_kwh: 2.0 } }
        ],
        plan_vs_reality: Array.from({ length: 12 }, function (_, i) {
          return { h: i, planned_kw: 2.4 + i * 0.1, actual_kw: 2.1 + i * 0.08 + (i % 3 ? 0 : 0.3) };
        })
      };
    }
    if (path === '/heat-source') return mockHeat;
    if (path === '/heat-source/history/24h' || path === '/heat-source/history/7d') {
      var wk = /7d$/.test(path), hn = wk ? 84 : 96, hstep = wk ? 7200 : 900;
      var hf = [], hr2 = [];
      for (var hi = 0; hi < hn; hi++) {
        var ph = Math.sin(hi / hn * Math.PI * (wk ? 14 : 2) - 1.2);
        hf.push(Math.round((31.5 + 4.5 * ph + Math.sin(hi * 1.7) * 0.4) * 10) / 10);
        hr2.push(Math.round((26.5 + 3.2 * ph + Math.sin(hi * 2.3) * 0.5) * 10) / 10);
      }
      var mks = Array.from({ length: hn }, function (_, i) { return wk ? (i % 12 === 3 ? '1' : '0') : (i >= 26 && i <= 28) || (i >= 70 && i <= 72) ? '1' : '0'; }).join('');
      return { available: true, range: wk ? '7d' : '24h', from_ts: Math.floor(Date.now() / 1000) - hn * hstep, step_s: hstep, age_s: 30, feed_c: hf, return_c: hr2, marks: mks };
    }
    if (path === '/plan') {
      var hk = Array.from({ length: 24 }, function (_, i) { return (i >= 3 && i <= 5) ? 2.7 : (i === 14 ? 2.1 : 0); });
      return {
        available: true, clock: true, start_hour: new Date().getHours(), hours: 24,
        house: { target_c: mockTarget, temp_c: 21.6 },
        odin: { available: true, control: true, heat_kw: hk, energy_kwh: hk.map(function (v, i) { return i === 14 ? 1.5 : (i >= 18 && i <= 20 ? 0.8 : v / 4); }),
          mode: hk.map(function (v, i) { return i === 14 ? 1 : (i >= 18 && i <= 20 ? 6 : (v > 0 ? 2 : 0)); }),
          lift_applied: true, lift_c: Array.from({ length: 24 }, function (_, i) { return (i >= 2 && i <= 6) ? 0.5 : 0; }) },
        rooms: [
          { room_id: 'room-09', name: 'Gang', preload: null, charge: { from: 1, to: 8, store_c: 1.5, insufficient: true } },
          { room_id: 'room-01', name: 'Kontor', preload: { from: 0, to: 4, offset_c: 0.4 }, charge: null },
          { room_id: 'room-06', name: 'Josephine', preload: null, charge: { from: 2, to: 7, store_c: 0.8, insufficient: false } }
        ]
      };
    }
    if (path === '/heat-source/control') return mockControl;
    if (path === '/settings') return mockSettings;
    if (path === '/diagnostics') {
      return {
        heap: 'watching',
        polling: { last_error: '', fail: 0 },
        commissioning: { trusted_nodes: 3, reachable_trusted_nodes: 2 },
        ota: { state: 'valid' },
        network: {
          ip: '192.168.20.186', mac: 'D8:3B:DA:AA:BB:CC', ssid: 'Hjemme',
          version: 'v0.1.0-119', esphome: '2026.9.1', uptime_s: mockUptime
        }
      };
    }
    if (path === '/commands') {
      var e = Math.floor(Date.now() / 1000);
      return { commands: [
        { source: 'forecast', reason: 'fetched', result: 'accepted', created_at_epoch_s: e - 600 },
        { source: 'manual', reason: 'setpoint', result: 'accepted', created_at_epoch_s: e - 1200, room_id: 'room-01' }
      ] };
    }
    return {};
  }

  async function mockPost(path, body) {
    if (path === '/forecast/estimate-location') return { result: 'ok', source: 'network', latitude: 55.384697, longitude: 10.1402, city: 'Vissenbjerg', country: 'Denmark' };
    var bool = function (v) { return v === '1' || v === 1 || v === true; };
    if (path === '/heat-source/test-read' || path === '/heat-source/test-push') {
      await new Promise(function (r) { setTimeout(r, 450); });
      return mockProbe(path);
    }
    if (path === '/nodes/scan') return mockScan();
    if (path === '/nodes') {
      var raw = body.node_id || body.hostname || body.ip || 'v6-new';
      var id = String(raw).replace(/[^a-zA-Z0-9_-]/g, '-').slice(0, 24) || 'v6-new';
      mockNodeList.push({ id: id, name: '', hostname: body.hostname || '', ip: body.ip || '', trust_label: 'paired', reachable: true,
        health: { mapped_zones: 0 }, runtime: { flow_c: null, return_c: null } });
      return { result: 'stored', node_id: id };
    }
    var mh = path.match(/^\/nodes\/([^/]+)\/host$/);
    if (mh && body.host) {
      var isIp = /^\d{1,3}(\.\d{1,3}){3}$/.test(body.host);
      mockNodeList.forEach(function (n) {
        if (n.id !== decodeURIComponent(mh[1])) return;
        if (isIp) { n.ip = body.host; n.hostname = ''; } else n.hostname = body.host;
      });
      return { result: 'stored' };
    }
    var prof = path.match(/^\/nodes\/([^/]+)\/profile$/);
    if (prof && body.name) {
      mockNodeList.forEach(function (n) { if (n.id === decodeURIComponent(prof[1])) n.name = body.name; });
      return { result: 'stored' };
    }
    var rm = path.match(/^\/nodes\/([^/]+)\/remove$/);
    if (rm) {
      var rid = decodeURIComponent(rm[1]);
      mockNodeList = mockNodeList.filter(function (n) { return n.id !== rid; });
      return { result: 'stored' };
    }
    var room = path.match(/^\/zones\/([^/]+)\/room$/);
    if (room) {
      var st = mockRoomStore[decodeURIComponent(room[1])];
      if (!st) throw new Error('unknown room');
      if (Number(body.expected_revision) !== st.revision) throw new Error('revision_conflict');
      st.include = bool(body.include_in_house_temperature);
      st.weight = Number(body.physical_weight);
      st.wind = Number(body.wind_exposure);
      st.solar = Number(body.solar_gain);
      st.revision += 1;
      return { result: 'stored' };
    }
    if (path === '/strategy' && body.house_target_c != null) { mockTarget = Number(body.house_target_c); return { result: 'stored' }; }
    if (path === '/weather/settings') { mockSettings.weather.max_boost_c = Number(body.max_boost_c); return { result: 'saved' }; }
    if (path === '/forecast/settings') {
      mockSettings.forecast.latitude = Number(body.latitude);
      mockSettings.forecast.longitude = Number(body.longitude);
      return { result: 'stored' };
    }
    if (path === '/settings') {
      if (body.name) mockSettings.coordinator.name = body.name;
      if (body.display_idle_timeout_s != null) mockSettings.display.idle_timeout_s = Number(body.display_idle_timeout_s);
      return { result: 'stored' };
    }
    if (path === '/circulation/settings') {
      ['host', 'flow_entity', 'head_entity', 'power_entity'].forEach(function (k) { if (body[k] != null) mockHeat.circulation[k] = body[k]; });
      if (body.port) mockHeat.circulation.port = Number(body.port);
      return { result: 'stored' };
    }
    if (path === '/wifi' && body.ssid) {
      mockWifi = { ssid: mockWifi.ssid, connected: true, ap_active: false, 'switch': 'pending', target_ssid: body.ssid };
      setTimeout(function () { mockWifi = { ssid: body.ssid, connected: true, ap_active: false, 'switch': 'connected', target_ssid: body.ssid }; }, 1500);
      return mockWifi;
    }
    if (path === '/prices/settings') {
      var mp = mockPrices, wasPush = mp.enabled && mp.model === 'touch';
      var str = function (k) { return body[k] != null && body[k] !== '' ? String(body[k]) : null; };
      if (body.enabled != null) mp.enabled = bool(body.enabled);
      if (str('model')) mp.model = str('model');
      if (str('zone')) { mp.zone = str('zone'); mp.dk = /^DK[12]$/.test(mp.zone); }
      if (str('spot_source')) mp.spot.source = str('spot_source');
      if (str('spot_fixed_eur')) mp.spot.fixed_eur = Number(body.spot_fixed_eur);
      if (str('currency')) mp.currency = str('currency');
      if (str('fx')) mp.fx = Number(body.fx);
      if (str('grid_source')) mp.grid.source = str('grid_source');
      if (str('grid_gln')) mp.grid.gln = str('grid_gln');
      if (str('grid_code')) mp.grid.code = str('grid_code');
      if (str('grid_schedule')) { try { mp.grid.schedule = JSON.parse(body.grid_schedule); } catch (e) {} }
      if (str('system_source')) mp.system.source = str('system_source');
      if (str('system_fixed')) mp.system.fixed = Number(body.system_fixed);
      ['energy_tax', 'markup', 'vat_pct'].forEach(function (k) { if (str(k)) mp[k] = Number(body[k]); });
      if (str('odin_mode')) mp.odin.mode = str('odin_mode');
      if (str('odin_source')) mp.odin.source = str('odin_source');
      if (str('odin_fixed_price')) mp.odin.fixed_price = Number(body.odin_fixed_price);
      if (str('entsoe_token')) mp.entsoe_token_set = true;
      var pushes = mp.enabled && mp.model === 'touch';
      if (mp.enabled && mp.model === 'odin') {
        mp.odin.current = { known: true, price_mode: mp.odin.mode, price_source: mp.odin.source, ec_bzn: mp.zone,
          fixed_price: mp.odin.fixed_price, token_set: mp.odin.current.token_set || !!str('entsoe_token'), age_s: 1 };
      } else if (pushes) mp.odin.current.price_source = 'api';
      else if (wasPush) mp.odin.current.price_source = 'energy_charts';
      mp.status.state = !mp.enabled ? 'disabled' : (pushes ? 'ok' : 'odin');
      mp.status.pushes = pushes;
      return { result: 'saved', enabled: mp.enabled, model: mp.model, push_queued: pushes };
    }
    if (path === '/prices/push') {
      if (!mockPrices.enabled) throw new Error('disabled');
      var st0 = mockPrices.status;
      st0.state = 'running';
      setTimeout(function () {
        st0.state = mockPrices.model === 'touch' ? 'ok' : 'odin';
        st0.reason = 'request';
        st0.last_attempt_epoch = Math.floor(Date.now() / 1000);
        if (mockPrices.model === 'touch') st0.last_push_epoch = st0.last_attempt_epoch;
        st0.odin_write_pending = false;
      }, 1800);
      return { result: 'queued' };
    }
    if (path === '/heat-source/settings') {
      // Same rule as the firmware: absent keys are unchanged.
      if (body.type) mockHeat.type = body.type === 'generic_http' ? 'generic_http' : 'asgard';
      if (body.host) mockHeat.host = body.host;
      if (body.port) mockHeat.port = Number(body.port);
      if (body.push_interval_s) mockHeat.push_interval_s = Number(body.push_interval_s);
      if (body.weighted_temperature_variable) mockHeat.weighted_temperature_variable = body.weighted_temperature_variable;
      if (body.enabled != null) mockHeat.enabled = bool(body.enabled);
      if (body.target_sync_enabled != null) mockHeat.target_sync_enabled = bool(body.target_sync_enabled);
      if (body.odin_plan_enabled != null) mockHeat.odin_plan_enabled = bool(body.odin_plan_enabled);
      if (body.house_balance_enabled != null) mockHeat.house_balance.enabled = bool(body.house_balance_enabled);
      if (body.climate_entity) mockHeat.climate_entity = body.climate_entity;
      if (body.write_url_template) mockHeat.write_url_template = body.write_url_template;
      if (body.read_url_template) mockHeat.read_url_template = body.read_url_template;
      if (body.odin_host) mockHeat.odin_host = body.odin_host;
      if (body.mqtt_enabled != null) mockMqtt.enabled = bool(body.mqtt_enabled);
      ['host', 'username', 'topic_prefix', 'hp_id'].forEach(function (k) { if (body['mqtt_' + k]) mockMqtt[k] = body['mqtt_' + k]; });
      if (body.mqtt_port) mockMqtt.port = Number(body.mqtt_port);
      if (body.mqtt_password) mockMqtt.password_set = true;
      return { result: 'stored' };
    }
    if (path === '/heat-source/control') {
      if (body.odin_enabled != null) mockControl.odin.enabled = bool(body.odin_enabled);
      if (body.odin_max_lift_c != null) mockControl.odin.max_lift_c = Number(body.odin_max_lift_c);
      ['target_url_template', 'heat_request_url_template', 'curve_offset_url_template'].forEach(function (k) {
        if (body[k]) mockControl.generic[k] = body[k];
      });
      if (body.curve_gain != null) mockControl.generic.curve_gain = Number(body.curve_gain);
      if (body.curve_max_offset_c != null) mockControl.generic.curve_max_offset_c = Number(body.curve_max_offset_c);
      return { result: 'stored', control: mockControl };
    }
    return { ok: true, mock: true, path: path, body: body };
  }

  /* ---- Shared helpers ------------------------------------------------------ */
  function mapStatus(s) {
    if (s === 'calling' || s === 'call' || s === 'heat' || s === 'preheat') return 'calling';
    if (s === 'fault' || s === 'motor_fault') return 'fault';
    if (s === 'off' || s === 'unused') return 'off';
    return 'idle';
  }

  function statusRank(s) {
    s = mapStatus(s);
    if (s === 'fault') return 3;
    if (s === 'calling') return 2;
    if (s === 'off') return 0;
    return 1;
  }

  function compassDir(deg) {
    if (deg == null || deg !== deg) return '';
    var labels = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
    var i = Math.round((((Number(deg) % 360) + 360) % 360) / 22.5) % 16;
    // Localise the compass letters (da: Ø/V) from the catalogue's _walls.
    var w = i18n._walls || {};
    var map = { N: w.n || 'N', E: w.e || 'E', S: w.s || 'S', W: w.w || 'W' };
    return labels[i].replace(/[NESW]/g, function (c) { return map[c]; });
  }

  function seriesPoints(values, width, height, yMin, yMax) {
    if (!values || !values.length) return '';
    var n = values.length;
    var span = Math.max(yMax - yMin, 0.1);
    return values.map(function (v, i) {
      var x = n === 1 ? 0 : (i * width / (n - 1));
      var y = height - ((Number(v) - yMin) / span) * height;
      if (!isFinite(y)) y = height / 2;
      return x.toFixed(1) + ',' + y.toFixed(1);
    }).join(' ');
  }

  function stepPoints(values, width, height, yMin, yMax) {
    if (!values || !values.length) return '';
    var n = values.length;
    var span = Math.max(yMax - yMin, 0.1);
    var pts = [];
    values.forEach(function (v, i) {
      var x = n === 1 ? 0 : (i * width / (n - 1));
      var y = height - ((Number(v) - yMin) / span) * height;
      if (!isFinite(y)) y = height / 2;
      if (i && Number(v) !== Number(values[i - 1])) {
        var prevY = height - ((Number(values[i - 1]) - yMin) / span) * height;
        pts.push(x.toFixed(1) + ',' + prevY.toFixed(1));
      }
      pts.push(x.toFixed(1) + ',' + y.toFixed(1));
    });
    return pts.join(' ');
  }

  function axisRange(seriesList, minSpan) {
    var lo = Infinity, hi = -Infinity;
    seriesList.forEach(function (arr) {
      (arr || []).forEach(function (v) {
        var n = Number(v);
        if (!isFinite(n)) return;
        if (n < lo) lo = n;
        if (n > hi) hi = n;
      });
    });
    if (!isFinite(lo) || !isFinite(hi)) return { lo: 18, hi: 22 };
    var mid = (lo + hi) / 2;
    var span = Math.max(hi - lo + 0.6, minSpan || 3);
    return { lo: mid - span / 2, hi: mid + span / 2 };
  }

  // Temperature line (.t) + target as a step curve (.g); axis at least 3 °C (AGENTS.md).
  function updateSvgSeries(svg, tempSeries, targetSeries) {
    if (!svg) return;
    var vb = (svg.getAttribute('viewBox') || '0 0 240 40').split(/\s+/).map(Number);
    var W = vb[2] || 240, H = vb[3] || 40;
    var range = axisRange([tempSeries, targetSeries], 3);
    var polyT = svg.querySelector('polyline.t');
    var polyG = svg.querySelector('polyline.g');
    if (polyT) polyT.setAttribute('points', seriesPoints(tempSeries, W, H, range.lo, range.hi));
    if (polyG) polyG.setAttribute('points', stepPoints(targetSeries || [], W, H, range.lo, range.hi));
  }

  function axisHtml(labels) {
    return labels.map(function (l) { return '<span>' + esc(l) + '</span>'; }).join('');
  }

  // Live zone data: fresh, a real temperature and a known status. Without it
  // the zone shows "—" and unlit level bars — never a guessed level.
  function zoneHasData(z) {
    return !!z && z.fresh !== false && z.status !== 'unknown' &&
      z.temperature_c != null && isFinite(Number(z.temperature_c)) &&
      z.valve_pct != null && isFinite(Number(z.valve_pct));
  }

  // A poll must not overwrite a value the user is changing.
  function editingTarget(input) {
    var f = input.form;
    return document.activeElement === input || !!(f && (f._autoT || f.dataset.state === 'saving'));
  }

  function formBusy(f) {
    return !!f && (f.dataset.dirty != null || f.dataset.state === 'saving');
  }

  // Set a value as the form's new baseline, so Fortryd (reset) returns to the
  // value the device reported — not to the build-time default.
  function putVal(el, v) {
    if (!el || v == null || document.activeElement === el) return;
    if (el.type === 'checkbox') { el.checked = !!v; el.defaultChecked = !!v; return; }
    if (el.tagName === 'SELECT') {
      el.value = String(v);
      Array.prototype.forEach.call(el.options, function (o) { o.defaultSelected = o.value === String(v); });
      return;
    }
    el.value = v;
    el.defaultValue = String(v);
  }

  function putRadio(form, name, val) {
    qsa('input[name="' + name + '"]', form).forEach(function (r) {
      r.checked = r.value === String(val);
      r.defaultChecked = r.checked;
    });
  }

  function deg(x) {
    var s = num(x);
    return s === '—' ? s : s + '°';
  }

  function mzOf(z) {
    // Firmware/API indexes are 0-based; UI ids are M1–M4 / Z1–Z6.
    var m = (z.node_index != null ? Number(z.node_index) : 0) + 1;
    var zi = (z.zone_index != null ? Number(z.zone_index) : 0) + 1;
    return { m: m, z: zi };
  }

  function v6Base(node) {
    var addr = (node && (node.ip || node.hostname)) || '';
    if (!addr) return '';
    return addr.indexOf('http') === 0 ? addr.replace(/\/?$/, '/') : 'http://' + addr + '/';
  }

  // Hours since the controller last answered (last_seen_ms against Touch's uptime).
  function hoursSince(node) {
    var up = state.diagnostics && state.diagnostics.network && state.diagnostics.network.uptime_s;
    var seen = node && Number(node.last_seen_ms);
    if (!finite(up) || !(seen > 0)) return null;
    var h = (Number(up) * 1000 - seen) / 3600000;
    return h >= 0 ? Math.max(1, Math.round(h)) : null;
  }

  function syncHsType(type) {
    type = type === 'generic_http' ? 'http' : (type || 'asgard');
    var radio = qs('#hs-' + type);
    var f = connForm();
    if (radio && !formBusy(f)) { radio.checked = true; putRadio(f, 'hs_type', type); }
    qsa('[data-hs-type]').forEach(function (el) { el.setAttribute('data-hs-type', type); });
    syncHsTypedFields();
  }

  // One form in the Heat sheet: behaviour + the connection under «Connection ›».
  function connForm() { return qs('form[data-save="heat_source"]'); }
  function behaviorForm() { return connForm(); }

  function syncHsTypedFields() {
    var form = connForm();
    if (!form) return;
    var checked = form.querySelector('input[name="hs_type"]:checked');
    var type = checked ? checked.value : 'asgard';
    form.querySelectorAll('fieldset.hs-fields').forEach(function (fs) {
      var on = fs.getAttribute('data-type') === type;
      // Disable inactive fields so HTML5 validation and FormData ignore them.
      fs.querySelectorAll('input, select, textarea').forEach(function (el) { el.disabled = !on; });
    });
  }

  /* ---- Rooms, controllers, heat map --------------------------------------- */
  function mergeRooms(payload) {
    var meta = (payload && payload.rooms) || [];
    var zones = (payload && payload.zones) || [];
    var by = {};
    zones.forEach(function (z) {
      if (!z || !z.room_id || z.is_group_secondary || z.unassigned) return;
      var cur = by[z.room_id];
      if (!cur) {
        by[z.room_id] = {
          room_id: z.room_id, name: z.name, area_m2: z.room && z.room.total_area_m2,
          include_in_house_temperature: z.room ? z.room.include_in_house_temperature : true
        };
      }
    });
    meta.forEach(function (r) {
      if (!r || !r.room_id) return;
      if (!by[r.room_id]) by[r.room_id] = { room_id: r.room_id };
      if (r.name) by[r.room_id].name = r.name;
      if (r.area_m2 != null) by[r.room_id].area_m2 = r.area_m2;
      if (r.include_in_house_temperature != null) by[r.room_id].include_in_house_temperature = r.include_in_house_temperature;
    });
    return Object.keys(by).map(function (k) { return by[k]; });
  }

  /* Room and controller sheets are made from <template id="tpl-room"/"tpl-mani">
     (build_ui.py, texts from the i18n catalogue) once the data says how many
     there are. lune-forms binds their forms via window.luneForms.scan. */
  function ensureSheets(prefix, tplId, count) {
    var tpl = document.getElementById(tplId);
    var app = qs('.app');
    if (!tpl || !app) return 0;
    var made = 0;
    for (var k = 1; k <= count; k++) {
      if (document.getElementById('sheet-' + prefix + k)) continue;
      var box = document.createElement('div');
      box.innerHTML = tpl.innerHTML.split('__N__').join(String(k));
      while (box.firstElementChild) app.appendChild(box.firstElementChild);
      made++;
    }
    if (made && window.luneForms) window.luneForms.scan(app);
    return made;
  }

  // A deep link (#r5/indstillinger) may name a sheet that only exists after the
  // first data fetch: let lune-forms read the address again.
  function rereadHash() {
    var h = decodeURIComponent((location.hash || '').replace(/^#/, '')).split('/')[0];
    if (!h || !/^[rm]\d+$/.test(h)) return;
    var sh = document.getElementById('sheet-' + h);
    if (sh && !sh.matches(':popover-open')) window.dispatchEvent(new HashChangeEvent('hashchange'));
  }

  // One entry per room (primary loop first). Each gets a sheet r1, r2, … in data order.
  function buildRoomSlots() {
    var order = [], byRoom = {};
    (state.zones || []).forEach(function (z) {
      if (!z || !z.room_id || z.unassigned) return;
      if (!byRoom[z.room_id]) { byRoom[z.room_id] = { room_id: z.room_id, primary: null, loops: [] }; order.push(z.room_id); }
      var r = byRoom[z.room_id];
      r.loops.push(z);
      if (!z.is_group_secondary && !r.primary) r.primary = z;
    });
    state.roomSlots = [];
    state.slotOf = {};
    order.forEach(function (rid) {
      var r = byRoom[rid];
      if (!r.primary) r.primary = r.loops[0];
      var valve = null, st = r.primary.status;
      r.loops.forEach(function (z) {
        if (finite(z.valve_pct) && (valve == null || Number(z.valve_pct) > valve)) valve = Number(z.valve_pct);
        if (statusRank(z.status) > statusRank(st)) st = z.status;
      });
      r.valve = valve;
      r.status = st;
      state.roomSlots.push(r);
      state.slotOf[rid] = state.roomSlots.length;
    });
  }

  function roomSetpoint(z) {
    var sp = z.setpoint_c;
    if (sp == null && z.comfort && z.comfort.effective_setpoint_c != null) sp = z.comfort.effective_setpoint_c;
    return sp;
  }

  // Deviation from target as a 5-step chip (DESIGN.md 15.8).
  function devChip(z) {
    if (!zoneHasData(z)) return '';
    var st = mapStatus(z.status);
    var sp = roomSetpoint(z);
    if (st === 'fault' || st === 'off' || !finite(sp)) return '';
    var d = Number(z.temperature_c) - Number(sp);
    var k = d <= -1 ? 1 : d <= -0.3 ? 2 : d < 0.3 ? 3 : d < 1 ? 4 : 5;
    var sign = d > 0.05 ? '+' : (d < -0.05 ? '−' : '±');
    return '<span class="tile-dev" data-dev="' + k + '">' + sign + num(Math.abs(d)) + '°</span>';
  }

  function tileLevel(z, valve) {
    var st = mapStatus(z.status);
    if (st === 'fault' || st === 'off' || !zoneHasData(z)) return 0;
    var v = Number(valve) || 0;
    if (v <= 0) return 0;   // closed valve: unlit, orange means heat
    return Math.max(1, Math.min(10, Math.ceil(v / 10)));
  }

  function chargeFor(nodeIndex, zoneIndex) {
    var ds = (state.forecast && state.forecast.decisions) || [];
    for (var i = 0; i < ds.length; i++) {
      var d = ds[i];
      if (d && Number(d.node_index) === nodeIndex && Number(d.zone_index) === zoneIndex && d.charge && d.charge.now) return d.charge;
    }
    return null;
  }

  function zoneIdLabel(z) {
    var zi = mzOf(z).z;
    var members = Array.isArray(z.group_members) ? z.group_members.map(Number).filter(isFinite) : [];
    if (!z.is_group_secondary && members.length > 1) {
      var ids = members.slice().sort(function (a, b) { return a - b; });
      var run = ids.every(function (v, i) { return i === 0 || v === ids[i - 1] + 1; });
      return { id: run ? 'Z' + ids[0] + '–' + ids[ids.length - 1] : 'Z' + ids.join('+'), role: 'primary' };
    }
    return { id: 'Z' + zi, role: z.is_group_secondary ? 'member' : '' };
  }

  function roomValText(z) {
    var st = mapStatus(z.status);
    if (st === 'fault') return t('tile.fault');
    if (st === 'off') return t('tile.off');
    return zoneHasData(z) ? deg(z.temperature_c) : '—';
  }

  // Varmekort (DESIGN.md 15.10): rooms per controller; the head opens the
  // controller's sheet, each tile the room's sheet.
  // Hjem: huset + ét kort pr. styring med én søjle pr. zone (ventil i 5 trin).
  function renderScopes(list) {
    var host = qs('[data-bind-scopes]');
    if (!host) return;
    qsa('button.scope', host).forEach(function (b) { b.remove(); });
    host.insertAdjacentHTML('beforeend', list.map(function (m) {
      var node = state.nodes[m - 1];
      var name = nodeLabel(node) || (t('common.unnamed') + ' M' + m);
      var zones = (state.byM[m] || []).filter(function (z) { return !z.is_group_secondary; })
        .sort(function (a, b) { return mzOf(a).z - mzOf(b).z; });
      var bars = zones.map(function (z) {
        if (mapStatus(z.status) === 'fault') return '<i data-state="fault"></i>';
        var lvl = tileLevel(z, z.valve_pct);
        return '<i data-level="' + (lvl === 0 ? 0 : Math.max(1, Math.min(5, Math.ceil(Number(z.valve_pct) / 20)))) + '"></i>';
      }).join('');
      var isOff = node && node.reachable === false;
      var off = isOff ? ' data-offline' : '';
      var open = document.getElementById('sheet-m' + m) ? ' popovertarget="sheet-m' + m + '"' : ' disabled';
      return '<button class="scope" type="button"' + open + off + ' aria-label="' + esc(t('common.open', { x: name })) + '">' +
        '<small>M' + m + (isOff ? ' · <span>' + esc(t('status.offline')) + '</span>' : '') + '</small><b>' + esc(name) + '</b><span class="mini" aria-hidden="true">' + bars + '</span></button>';
    }).join(''));
  }

  function renderHeatmap() {
    var host = qs('[data-bind-heatmap]');
    if (!host) return;
    var ms = {};
    Object.keys(state.byM).forEach(function (m) { ms[m] = 1; });
    (state.nodes || []).forEach(function (n, i) { ms[i + 1] = 1; });
    var list = Object.keys(ms).map(Number).sort(function (a, b) { return a - b; });
    if (!list.length) return;
    renderScopes(list);
    host.innerHTML = list.map(function (m) {
      var node = state.nodes[m - 1];
      var name = nodeLabel(node) || (t('common.unnamed') + ' M' + m);
      var rt = (node && node.runtime) || {};
      var offline = node && node.reachable === false;
      var temps = finite(rt.flow_c) || finite(rt.return_c) ? ' · ' + deg(rt.flow_c) + ' → ' + deg(rt.return_c) : '';
      var head = document.getElementById('sheet-m' + m)
        ? '<button class="room-group-head" type="button" popovertarget="sheet-m' + m + '">' + esc(name) + ' <small>M' + m + esc(temps) + '</small></button>'
        : '<p class="room-group-head">' + esc(name) + ' <small>M' + m + esc(temps) + '</small></p>';
      var h = offline ? hoursSince(node) : null;
      var note = offline ? '<p class="offline-note">' + esc(h != null ? t('v6.offline', { h: h }) : t('v6.offlineNoTime')) + '</p>' : '';
      var zones = (state.byM[m] || []).filter(function (z) { return !z.is_group_secondary; })
        .sort(function (a, b) { return mzOf(a).z - mzOf(b).z; });
      var tiles = zones.map(function (z) {
        var mz = mzOf(z);
        var slot = state.slotOf[z.room_id];
        var room = slot ? state.roomSlots[slot - 1] : null;
        var valve = room && room.primary === z ? room.valve : z.valve_pct;
        var st = mapStatus(room && room.primary === z ? room.status : z.status);
        var lvl = tileLevel(z, valve);
        var open5 = lvl === 0 ? 0 : Math.max(1, Math.min(5, Math.ceil(Number(valve) / 20)));
        var area = Number((z.room && z.room.total_area_m2) || (room && room.area_m2)) || 12;   // tile width follows the room's area
        var idl = zoneIdLabel(z);
        var val = roomValText(z);
        var ch = chargeFor(m - 1, mz.z - 1);
        var chargeAttr = ch ? ' data-charge="' + (ch.insufficient ? 'insufficient' : 'now') + '" title="' +
          esc(t(ch.insufficient ? 'strip.charge.insufficient' : 'strip.charge.now', { c: num(ch.store_c) })) + '"' : '';
        var nm = z.name || idl.id;
        var aria = t('tile.room.aria', { name: nm, state: t('state.' + st), temp: val });
        var tag = slot ? 'button' : 'div';
        var open = slot ? ' type="button" popovertarget="sheet-r' + slot + '"' : '';
        return '<' + tag + ' class="tile"' + open + chargeAttr + ' data-state="' + st + '" data-open="' + open5 + '"' +
          ' style="--area:' + Math.round(area) + '"' + (idl.role ? ' data-group="' + idl.role + '"' : '') + ' aria-label="' + esc(aria) + '">' +
          '<span class="lvl" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></span>' +
          '<span class="tile-pct">' + (lvl === 0 && !zoneHasData(z) ? '—' : (lvl === 0 ? t('tile.closed') : t('tile.open', { p: num(valve, 0) }))) + '</span>' +
          '<span class="tile-id">' + esc(idl.id) + '</span><span class="tile-name">' + esc(nm) + '</span>' +
          devChip(z) + '<span class="tile-val">' + esc(val) + '</span></' + tag + '>';
      }).join('');
      return '<section class="room-group"' + (offline ? ' data-offline' : '') + ' aria-label="' + esc(name) + '">' + head + note +
        '<div class="room-grid">' + tiles + '</div></section>';
    }).join('');
  }

  // Controller sheets m1, m2, … (one per registered V6).
  function fillManifoldSheets() {
    for (var m = 1; m <= state.nodes.length; m++) {
      var sh = qs('#sheet-m' + m);
      if (!sh) continue;
      var node = state.nodes[m - 1];
      if (!node) continue;
      var name = nodeLabel(node) || (t('common.unnamed') + ' M' + m);
      var rt = node.runtime || {};
      var zones = (state.byM[m] || []).filter(function (z) { return !z.is_group_secondary; })
        .sort(function (a, b) { return mzOf(a).z - mzOf(b).z; });
      var offline = node.reachable === false;
      var h = offline ? hoursSince(node) : null;
      fText(sh, 'name', name);
      fText(sh, 'status', offline ? (h != null ? t('v6.offline', { h: h }) : t('v6.offlineNoTime'))
        : t('sheet.manifoldStatus', { flow: num(rt.flow_c), ret: num(rt.return_c), n: zones.length }));
      var off = fEl(sh, 'offline');
      if (off) off.hidden = !offline;
      fText(sh, 'offlineBody', offline && h != null ? t('v6.offlineBody', { h: h }) : '');
      fHtml(sh, 'flow', esc(num(rt.flow_c)) + ' <small>°C</small>');
      fHtml(sh, 'ret', esc(num(rt.return_c)) + ' <small>°C</small>');
      var badge = offline ? '<span class="badge warn">' + esc(t('status.offline')) + '</span>' : '<span class="badge ok">' + esc(t('status.online')) + '</span>';
      fHtml(sh, 'state', badge);
      fHtml(sh, 'host', '<span class="mono">' + esc(node.hostname || '—') + '</span>');
      fHtml(sh, 'ip', '<span class="mono">' + esc(node.ip || '—') + '</span>');
      fText(sh, 'fw', node.firmware || '—');
      var link = fEl(sh, 'link');
      var base = v6Base(node);
      if (link) { link.href = base || '#'; link.hidden = !base; }
      fHtml(sh, 'zones', zones.map(function (z) {
        var slot = state.slotOf[z.room_id];
        var st = mapStatus(z.status);
        var sp = roomSetpoint(z);
        var val = st === 'fault' ? '<b class="bad">' + esc(t('state.fault')) + '</b>' : '<b>' + esc(roomValText(z)) + '</b> / ' + esc(deg(sp));
        var inner = '<span class="id">' + esc(zoneIdLabel(z).id) + '</span><span class="name">' + esc(z.name || ('Z' + mzOf(z).z)) +
          '</span><span class="val">' + val + '</span>';
        return slot ? '<button type="button" popovertarget="sheet-r' + slot + '" data-state="' + st + '">' + inner + '</button>'
          : '<div data-state="' + st + '">' + inner + '</div>';
      }).join(''));
    }
  }

  function wallsText(mask) {
    var full = i18n._walls_full || {};
    var names = [];
    ['n', 'e', 's', 'w'].forEach(function (k, i) { if (Number(mask) & (1 << i)) names.push(full[k] || k.toUpperCase()); });
    return names.length ? names.join(' · ') : t('room.wallsNone');
  }

  // Room sheets: overview, history and the settings Touch owns.
  function fillRoomSheets() {
    state.roomSlots.forEach(function (r, i) {
      var sh = qs('#sheet-r' + (i + 1));
      if (!sh) return;
      var z = r.primary;
      var mz = mzOf(z);
      var node = state.nodes[mz.m - 1];
      var mName = nodeLabel(node) || ('M' + mz.m);
      var st = mapStatus(r.status);
      var has = zoneHasData(z);
      var sp = roomSetpoint(z);
      var open = has && finite(r.valve) ? Math.round(Number(r.valve)) : null;
      var name = z.name || r.room_id;
      fText(sh, 'name', name);
      var n24 = fEl(sh, 'next24');
      var n24svg = n24 && qs('svg', n24);
      if (n24svg) n24svg.setAttribute('aria-label', t('room.next24Aria', { name: name }));
      fText(qs('.sheet-head', sh), 'status', t('sheet.roomStatus', {
        m: mName, z: mz.z, temp: has ? deg(z.temperature_c) : '—', open: open == null ? '—' : open, state: t('state.' + st)
      }));
      var fault = fEl(sh, 'fault');
      if (fault) fault.hidden = st !== 'fault';
      var base = v6Base(node);
      var rl = fEl(sh, 'resetLink');
      if (rl) rl.href = base ? base + '#z' + mz.z : '#';
      var nd = fEl(sh, 'nodata');
      if (nd) nd.hidden = has || st === 'fault';
      fHtml(sh, 'temp', esc(has ? num(z.temperature_c) : '—') + ' <small>°C</small>');
      fHtml(sh, 'target', esc(num(sp)) + ' <small>°C</small>');
      fHtml(sh, 'valve', esc(open == null ? '—' : String(open)) + ' <small>%</small>');
      var bar = fEl(sh, 'bar');
      if (bar) bar.style.setProperty('--v', (open || 0) + '%');
      fText(sh, 'loopsTitle', t('room.loops', { m: mName }));
      fHtml(sh, 'loops', r.loops.map(function (c) {
        var cz = mzOf(c);
        var lbl = (cz.m !== mz.m ? 'M' + cz.m + ' ' : '') + 'Z' + cz.z;
        var v = finite(c.valve_pct) && zoneHasData(c) ? t('room.valveShort', { v: num(c.valve_pct, 0) }) : '—';
        return '<div><dt>' + esc(lbl) + '</dt><dd>' + esc(v) + '</dd></div>';
      }).join(''));
      // History summary from /zones (no ring buffer of samples on the device).
      var hi = z.history || {};
      var hb = fEl(sh, 'hist');
      var hasHist = Number(hi.samples) > 0 && finite(hi.avg_temp_c);
      if (hb) hb.toggleAttribute('data-empty', !hasHist);
      if (hasHist) {
        fText(sh, 'hAvg', num(hi.avg_temp_c) + ' °C');
        fText(sh, 'hMin', num(hi.min_temp_c) + ' °C');
        fText(sh, 'hMax', num(hi.max_temp_c) + ' °C');
        fText(sh, 'hCall', t('room.histCallingVal', { pct: num(100 * Number(hi.calling_samples || 0) / Number(hi.samples), 0) }));
        fText(sh, 'hN', String(hi.samples));
      }
      // Fra V6: read-only values, edited on the V6 itself.
      var room = z.room || {};
      var fc = z.forecast || {};
      fText(sh, 'fromV6', t('room.fromV6', { m: mName, z: mz.z }));
      fText(sh, 'area', Number(room.total_area_m2) > 0 ? num(room.total_area_m2, 1) + ' m²' : '—');
      fText(sh, 'walls', wallsText(fc.exterior_walls != null ? fc.exterior_walls : room.exterior_walls_union));
      var ed = fEl(sh, 'editLink');
      if (ed) { ed.href = base ? base + '#z' + mz.z + '/' + t('hash.settings') : '#'; ed.hidden = !base; }
      var v6 = fEl(sh, 'v6');
      var offline = node && node.reachable === false;
      if (v6) v6.toggleAttribute('data-offline', !!offline);
      var on = fEl(sh, 'offlineNote');
      if (on) {
        var h = offline ? hoursSince(node) : null;
        on.hidden = !offline;
        on.textContent = offline ? (h != null ? t('v6.offline', { h: h }) : t('v6.offlineNoTime')) : '';
      }
      // Settings (rooms, PATCH): never overwrite unsaved edits.
      var f = qs('form[data-save="rooms"]', sh);
      if (f && !formBusy(f)) {
        var k = 'room:r' + (i + 1) + ':';
        putVal(f.querySelector('[name="' + k + 'include"]'), room.include_in_house_temperature !== false);
        putVal(f.querySelector('[name="' + k + 'weight"]'), Number(room.physical_weight != null ? room.physical_weight : 1).toFixed(2));
        putVal(f.querySelector('[name="' + k + 'wind"]'), Number(fc.wind_exposure != null ? fc.wind_exposure : (room.wind_exposure != null ? room.wind_exposure : 0.5)).toFixed(2));
        putVal(f.querySelector('[name="' + k + 'solar"]'), Number(fc.solar_gain != null ? fc.solar_gain : (room.solar_gain != null ? room.solar_gain : 0.3)).toFixed(2));
        if (f.luneResnap) f.luneResnap();
      }
      if (sh.matches(':popover-open')) loadComfortChart(i + 1);
    });
  }

  // Expected temperature and target for the next 24 h; fetched when a room sheet opens.
  async function loadComfortChart(slot) {
    var r = state.roomSlots[slot - 1];
    var sh = qs('#sheet-r' + slot);
    if (!r || !sh) return;
    var box = fEl(sh, 'next24');
    if (!box || state.chartLoaded[r.room_id] && Date.now() - state.chartLoaded[r.room_id] < 300000) return;
    state.chartLoaded[r.room_id] = Date.now();
    try {
      var data = await get('/zones/' + encodeURIComponent(r.room_id) + '/comfort-chart');
      var chart = data && data.comfort_chart;
      var exp = (chart && chart.expected_temp_c) || [];
      var real = exp.filter(finite).length;
      box.toggleAttribute('data-empty', real < 2);
      if (real < 2) return;
      var svg = qs('svg', box);
      if (svg) svg.setAttribute('aria-label', t('room.next24Aria', { name: (r.primary && r.primary.name) || r.room_id }));
      updateSvgSeries(svg, exp, chart.scheduled_setpoint_c || []);
      var n = exp.length;
      fHtml(box, 'axis', axisHtml([t('trend.now'), '+' + Math.round(n / 2) + ' ' + (i18n._h || 'h'), '+' + n + ' ' + (i18n._h || 'h')]));
    } catch (e) { box.setAttribute('data-empty', ''); }
  }

  // Heat sheet: flow/return from Asgard's own history (the Touch averages it into
  // 96 × 15 min and 84 × 2 h, GET /heat-source/history/24h|7d). Loaded when the sheet opens.
  async function loadHeatHistory() {
    var box = qs('[data-bind-hchart]');
    if (state.heatHistAt && Date.now() - state.heatHistAt < 300000) return;
    state.heatHistAt = Date.now();
    ['24h', '7d'].forEach(async function (rng) {
      var panel = box && qs('[data-f="h' + rng + '"]', box);
      try {
        var h = await get('/heat-source/history/' + rng);
        drawHeatHistory(panel, rng, h);
        if (rng === '24h') drawHeatTile(h);
      }
      catch (e) { if (panel) panel.setAttribute('data-empty', ''); }
    });
  }

  // Home's Heat tile: the same 24 h, drawn small. Page samples are only the fallback.
  function drawHeatTile(h) {
    var fs = (h && h.feed_c) || [], rs = (h && h.return_c) || [];
    if (!(h && h.available && fs.filter(finite).length >= 2)) return;
    state.heatTileFromHistory = true;
    var viz = qs('[data-bind-viz="heat"]'), tile = qs('[data-tile="heat"]');
    if (tile) tile.removeAttribute('data-empty');
    if (!viz) return;
    var r = axisRange([fs, rs], 3), n = fs.length;
    var pts = function (arr) {
      return arr.map(function (v, i) { return finite(v) ? (i * 240 / (n - 1)).toFixed(1) + ',' + (64 - (Number(v) - r.lo) / (r.hi - r.lo) * 64).toFixed(1) : null; })
        .filter(Boolean);
    };
    var fp = pts(fs);
    viz.querySelector('polyline.f').setAttribute('points', fp.join(' '));
    viz.querySelector('polyline.r').setAttribute('points', pts(rs).join(' '));
    viz.querySelector('path.a').setAttribute('d', 'M' + fp[0].split(',')[0] + ',64 L' + fp.join(' L') + ' L' + fp[fp.length - 1].split(',')[0] + ',64Z');
  }

  function drawHeatHistory(panel, rng, h) {
    if (!panel) return;
    var fs = (h && h.feed_c) || [], rs = (h && h.return_c) || [];
    var ok = h && h.available && fs.filter(finite).length >= 2;
    panel.toggleAttribute('data-empty', !ok);
    if (!ok) return;
    var n = Math.max(fs.length, rs.length), W = 240, H = 100;
    var r = axisRange([fs, rs], 6);
    var lo = Math.floor(r.lo), hi = Math.ceil(r.hi);
    var pts = function (arr) {
      var out = [];
      arr.forEach(function (v, i) {
        if (!finite(v)) return;
        out.push((n === 1 ? 0 : i * W / (n - 1)).toFixed(1) + ',' + (H - (Number(v) - lo) / (hi - lo) * H).toFixed(1));
      });
      return out;
    };
    var fp = pts(fs), rp = pts(rs);
    var svg = qs('svg', panel);
    svg.querySelector('polyline.f').setAttribute('points', fp.join(' '));
    svg.querySelector('polyline.r').setAttribute('points', rp.join(' '));
    svg.querySelector('path.dt').setAttribute('d', 'M' + fp[0].split(',')[0] + ',' + H + ' L' + fp.join(' L') + ' L' + fp[fp.length - 1].split(',')[0] + ',' + H + 'Z');
    fHtml(panel, 'y', [hi, Math.round((hi + lo) / 2), lo].map(function (v) { return '<span>' + esc(v) + '°</span>'; }).join(''));
    // Hot water / legionella from Asgard's mode: hatched bands behind the lines.
    var marks = String((h && h.marks) || ''), bw = W / Math.max(n - 1, 1), mh = '', anyD = false, anyL = false;
    for (var mi = 0; mi < marks.length; mi++) {
      var mk = Number(marks.charAt(mi)) || 0;
      if (!mk) continue;
      var x0 = Math.max(0, mi * bw - bw / 2);
      if (mk & 1) { anyD = true; mh += '<rect class="dhw" x="' + x0.toFixed(1) + '" y="0" width="' + bw.toFixed(1) + '" height="' + H + '"/>'; }
      if (mk & 2) { anyL = true; mh += '<rect class="leg" x="' + x0.toFixed(1) + '" y="0" width="' + bw.toFixed(1) + '" height="' + H + '"/>'; }
    }
    var mg = svg.querySelector('g.marks');
    if (mg) mg.innerHTML = mh;
    var hm = state.heatMarks || (state.heatMarks = {});
    hm[rng] = { d: anyD, l: anyL };
    setShow('hchart.dhw', Object.keys(hm).some(function (k) { return hm[k].d; }));
    setShow('hchart.leg', Object.keys(hm).some(function (k) { return hm[k].l; }));
    // x: four ticks + "now" (24 h: hour of day; 7 d: weekday in the page language).
    var from = Number(h.from_ts) || 0, span = n * (Number(h.step_s) || 0), lang = document.documentElement.lang || 'en';
    var ticks = [0, .25, .5, .75].map(function (f) {
      var d = new Date((from + f * span) * 1000);
      return '<span>' + esc(rng === '7d' ? d.toLocaleDateString(lang, { weekday: 'short' }) : String(d.getHours()).padStart(2, '0')) + '</span>';
    });
    fHtml(panel, 'x', ticks.join('') + '<span><b>' + esc(t('trend.now')) + '</b></span>');
  }

  function renderAlerts() {
    var host = qs('[data-bind-alerts]');
    if (!host) return;
    var faults = state.roomSlots.map(function (r, i) { return { r: r, slot: i + 1 }; })
      .filter(function (x) { return mapStatus(x.r.status) === 'fault'; }).slice(0, 3);
    host.innerHTML = faults.map(function (x) {
      var z = x.r.primary, mz = mzOf(z);
      var name = z.name || x.r.room_id;
      var mName = nodeLabel(state.nodes[mz.m - 1]) || ('M' + mz.m);
      return '<div class="panel alert"><div class="panel-head"><h3>' + esc(t('alert.roomFault', { name: name, m: mName, z: mz.z })) + '</h3></div>' +
        '<p class="note">' + esc(t('alert.roomFaultBody')) + '</p>' +
        '<div class="panel-foot" style="justify-content:flex-start"><button class="btn" type="button" popovertarget="sheet-r' + x.slot + '">' +
        esc(t('common.open', { x: name })) + '</button></div></div>';
    }).join('');
  }

  function applyHierarchy(payload, nodes) {
    state.zones = (payload && payload.zones) || [];
    state.rooms = mergeRooms(payload || {});
    state.nodes = nodes || state.nodes || [];
    state.byM = {};
    state.zones.forEach(function (z) {
      if (!z) return;
      var mz = mzOf(z);
      if (!(mz.m >= 1) || !(mz.z >= 1)) return;
      (state.byM[mz.m] = state.byM[mz.m] || []).push(z);
    });
    buildRoomSlots();
    var made = ensureSheets('m', 'tpl-mani', state.nodes.length) + ensureSheets('r', 'tpl-room', state.roomSlots.length);
    renderHeatmap();
    fillManifoldSheets();
    fillRoomSheets();
    renderAlerts();
    renderDist();
    renderHome();
    if (made) rereadHash();
  }

  function applyOverview(ov) {
    if (!ov) return;
    state.overview = Object.assign({}, state.overview || {}, ov);
    var o = state.overview;
    var sum = o.summary || {};
    var contrib = o.contributing_manifolds != null ? o.contributing_manifolds : (sum.nodes != null ? sum.nodes : null);
    var expected = o.expected_manifolds != null ? o.expected_manifolds : (sum.nodes != null ? sum.nodes : null);
    if (contrib != null || expected != null) setText('house.coverage', (contrib != null ? contrib : 0) + ' / ' + (expected != null ? expected : 0));
    if (o.authority) setText('house.authority', o.authority);
    var target = o.house_target_c != null ? o.house_target_c
      : (o.house_comfort_target_c != null ? o.house_comfort_target_c : (o.house_target && o.house_target.value_c));
    var tgt = qs('#house_target');
    if (tgt && !editingTarget(tgt) && finite(target)) {
      putVal(tgt, num(target));
      if (tgt.form && tgt.form.luneResnap) tgt.form.luneResnap();
    }
    renderHome();
  }

  function houseTemp() {
    var o = state.overview || {};
    var ht = o.house_temp_c != null ? o.house_temp_c : (o.temperature_c != null ? o.temperature_c : o.physical_house_temperature_c);
    if (!finite(ht) && state.heat) ht = state.heat.physical_house_temperature_c;
    if (!finite(ht)) {
      // Fallback: plain average of rooms counted in the house temperature.
      var sum = 0, n = 0;
      (state.zones || []).forEach(function (z) {
        if (!z || z.is_group_secondary || !zoneHasData(z)) return;
        if (z.room && z.room.include_in_house_temperature === false) return;
        sum += Number(z.temperature_c); n++;
      });
      ht = n ? sum / n : null;
    }
    return finite(ht) ? Number(ht) : null;
  }

  // The house target is a text field so it shows the page language's decimal sign.
  function parseNum(v) { return Number(String(v == null ? '' : v).trim().replace(',', '.')); }

  function houseTarget() {
    var tgt = qs('#house_target');
    var v = tgt ? parseNum(tgt.value) : NaN;
    return isFinite(v) ? v : null;
  }

  // Home: one sentence about the house, the thermostat ring and the four tiles.
  function renderHome() {
    var hr = new Date().getHours();
    // morgen 5–10 · formiddag 10–12 · eftermiddag 12–18 · aften 18–22 · nat
    setText('home.greeting', t(hr < 5 ? 'home.greeting.night' : hr < 10 ? 'home.greeting.morning' : hr < 12 ? 'home.greeting.forenoon'
      : hr < 18 ? 'home.greeting.day' : hr < 22 ? 'home.greeting.evening' : 'home.greeting.night'));
    var ht = houseTemp(), tg = houseTarget();
    var primaries = (state.zones || []).filter(function (z) { return z && !z.is_group_secondary && !z.unassigned; });
    var calling = primaries.filter(function (z) { return mapStatus(z.status) === 'calling'; }).length;
    var faults = primaries.filter(function (z) { return mapStatus(z.status) === 'fault'; }).length;
    var rel = ht == null || tg == null ? 'none' : (ht - tg < -0.2 ? 'below' : (ht - tg > 0.2 ? 'above' : 'at'));
    setText('home.headline', t('home.headline.' + (faults ? 'fault' : rel)));
    var hpx = (state.heat && state.heat.heat_pump) || {};
    setText('home.headline2', hpx.available && hpx.compressor_on != null ? t(hpx.compressor_on ? 'home.hp.on' : 'home.hp.off') : '');
    setText('house.outdoor', finite(state.outdoorC) ? t('thermo.outside', { t: deg(state.outdoorC) }) : '');
    setText('home.sentence', t('home.sentence.' + rel, { d: ht != null && tg != null ? num(Math.abs(ht - tg)) : '—', n: calling }));
    setBind('house.temp', ht == null ? '—' : esc(num(ht)) + '<small>°</small>');
    setText('scope.house', deg(ht));
    var ring = qs('[data-bind-thermo]');
    if (ring) {
      var pct = function (v) { return Math.max(0, Math.min(100, Math.round((v - THERMO_MIN) / (THERMO_MAX - THERMO_MIN) * 100))); };
      if (tg != null) ring.style.setProperty('--v', pct(tg));
      ring.style.setProperty('--now', ht == null ? 0 : pct(ht));
      if (ht != null && tg != null) ring.setAttribute('aria-label', t('thermo.aria', { t: num(ht), g: num(tg) }));
    }
    // Heat tile: heat pump flow → return, else the controllers' average.
    var hp = (state.heat && state.heat.heat_pump) || {};
    var fl = null, rt = null;
    if (hp.available && finite(hp.feed_c) && finite(hp.return_c)) { fl = hp.feed_c; rt = hp.return_c; }
    else {
      var fs = 0, fn = 0, rs = 0, rn = 0;
      (state.nodes || []).forEach(function (n) {
        var r0 = (n && n.reachable !== false && n.runtime) || {};
        if (finite(r0.flow_c)) { fs += Number(r0.flow_c); fn++; }
        if (finite(r0.return_c)) { rs += Number(r0.return_c); rn++; }
      });
      if (fn) fl = fs / fn;
      if (rn) rt = rs / rn;
    }
    var heatVal = fl == null && rt == null ? '—' : deg(fl) + ' → ' + deg(rt);
    setText('tile.heatVal', heatVal);
    // Hero shortcut: heat source (flow → return) and its compressor state.
    setShow('hero.heat', heatVal !== '—');
    setText('hero.heatLabel', hp.available ? t('hero.heatPump') : t('tile.heat'));
    setText('hero.heatVal', heatVal);
    setText('hero.heatSub', hp.available && hp.compressor_on != null
      ? (hp.compressor_on ? t('hp.compOn', { hz: num(hp.compressor_hz, 0) }) : t('hp.compOff')) : (qs('[data-bind="heat.badgeText"]') || {}).textContent || '');
    pushHeatSample(fl, rt);
  }

  // The device keeps no flow/return history, so the Heat tile draws what this
  // page has seen: one sample a minute, up to 24 h.
  var HEAT_HIST_MAX = 1440;
  function pushHeatSample(fl, rt) {
    if (!finite(fl) || !finite(rt)) return;
    var hist = state.heatHist || (state.heatHist = []);
    var now = Date.now();
    if (hist.length && now - hist[hist.length - 1].at < 60000) return;
    hist.push({ at: now, f: Number(fl), r: Number(rt) });
    if (hist.length > HEAT_HIST_MAX) hist.shift();
    if (state.heatTileFromHistory) return;
    var viz = qs('[data-bind-viz="heat"]');
    var tile = qs('[data-tile="heat"]');
    var ok = hist.length >= 3;
    if (tile) tile.toggleAttribute('data-empty', !ok);
    if (!viz || !ok) return;
    var fs = hist.map(function (h) { return h.f; }), rs = hist.map(function (h) { return h.r; });
    var r = axisRange([fs, rs], 3);
    var fp = seriesPoints(fs, 240, 64, r.lo, r.hi);
    viz.querySelector('polyline.f').setAttribute('points', fp);
    viz.querySelector('polyline.r').setAttribute('points', seriesPoints(rs, 240, 64, r.lo, r.hi));
    viz.querySelector('path.a').setAttribute('d', 'M0,64 L' + fp.split(' ').join(' L') + ' L240,64Z');
  }

  /* ---- Controllers (System › Controllers) --------------------------------- */
  // Display name: the Touch-side name if set, else the label the V6 reports
  // (its Device identity name, or its location), else null ("Unnamed").
  function nodeLabel(n) {
    if (!n) return null;
    if (!nodeUnnamed(n)) return n.name;
    var dev = String(n.device_name || '').trim();
    return dev || null;
  }

  function nodeUnnamed(n) {
    var name = String((n && n.name) || '').trim();
    if (!name) return true;
    if (n.hostname && name === n.hostname) return true;
    if (n.id && name === n.id) return true;
    return false;
  }

  function nodeBadge(n) {
    if (n && n.reachable === false) return { cls: 'bad', key: 'status.unreachable' };
    var trust = (n && n.trust_label) || '';
    if (trust === 'trusted') return { cls: 'ok', key: 'status.trusted' };
    if (trust === 'paired' || trust === 'unpaired' || trust === '') return { cls: '', key: 'status.waiting' };
    return { cls: '', key: 'status.unknown' };
  }

  function statusLabel(raw) {
    var key = 'status.' + String(raw || '');
    return i18n[key] ? t(key) : t('status.unknown');
  }

  function relAge(age) {
    if (age == null || !isFinite(Number(age))) return '—';
    var n = Math.round(Number(age));
    if (n < 60) return t('rt.secondsAgo', { n: n });
    return t('rt.minutesAgo', { n: Math.round(n / 60) });
  }

  function whenText(age, status) {
    var rel = relAge(age);
    var label = status ? statusLabel(status) : '';
    if (rel === '—' && !label) return '—';
    if (!label || label === t('status.unknown') && !i18n['status.' + status]) return rel === '—' ? t('status.unknown') : rel;
    if (rel === '—') return label;
    return rel + ' · ' + label;
  }

  function zoneCountFor(n, index) {
    if (n && n.health && n.health.mapped_zones != null) return n.health.mapped_zones;
    if (n && n.health && n.health.imported_zones != null) return n.health.imported_zones;
    return (state.zones || []).filter(function (z) {
      return z && z.node_index === index && !z.is_group_secondary && !z.unassigned;
    }).length;
  }

  // Found controllers: one grouped-list row each (System › Controllers › Find).
  function renderScan(found) {
    var box = qs('[data-bind-scan]');
    if (!box) return;
    var rows = (found || []).filter(function (f) { return f && f.source === 'lan_probe'; });
    if (!rows.length) {
      box.hidden = !state.scanDone;
      box.innerHTML = state.scanDone ? '<div class="setting"><div class="setting-label"><span class="muted">' + esc(t('ctrl.scanNone')) + '</span></div></div>' : '';
      return;
    }
    box.hidden = false;
    box.innerHTML = rows.map(function (f) {
      var host = f.hostname || f.ip || '';
      return '<div class="setting"><div class="setting-label"><span class="mono">' + esc(host) + '</span><small>' + esc(t('ctrl.found')) +
        (f.ip && f.hostname ? ' · ' + esc(f.ip) : '') + '</small></div><div class="setting-control">' +
        '<button class="btn" type="button" data-action="add-found" data-host="' + esc(host) + '" data-fp="' + esc(f.pairing_fingerprint || '') + '">' +
        esc(t('ctrl.addRow')) + '</button></div></div>';
    }).join('');
  }

  // Device menu: this Touch and every registered V6 (link, IP, reachability).
  function renderDeviceMenu() {
    var nav = qs('[data-bind-devices]');
    if (!nav) return;
    var own = state.ownIp || '';
    var item = function (href, name, sub, current, offline) {
      return '<a href="' + esc(href) + '"' + (current ? ' aria-current="page"' : '') + (offline ? ' data-offline' : '') +
        '><i></i>' + esc(name) + '<small>' + esc(sub) + '</small></a>';
    };
    var html = item('/', 'Lune Touch', own ? own + ' · ' + t('device.this') : t('device.this'), true, false);
    (state.nodes || []).forEach(function (n) {
      var ip = n.ip || n.hostname || '';
      if (!ip) return;
      html += item(v6Base(n), nodeLabel(n) || 'Lune V6', ip, false, n.reachable === false);
    });
    if (nav.innerHTML !== html) nav.innerHTML = html;
  }

  function applyNodes(nodes) {
    state.nodes = nodes || [];
    renderDeviceMenu();
    var bad = state.nodes.find(function (n) { return n.reachable === false; });
    var alert = qs('[data-bind-alert="board"]');
    if (alert) {
      alert.hidden = !bad;
      if (bad) setText('alert.boardTitle', t('alert.boardFault', { board: nodeLabel(bad) || t('common.unnamed') }));
    }
    var tbody = qs('[data-bind-nodes] tbody');
    if (!tbody) return;
    tbody.innerHTML = state.nodes.map(function (n, index) {
      var badge = nodeBadge(n);
      var unnamed = !nodeLabel(n);
      var label = nodeLabel(n) || t('common.unnamed');
      var addr = n.ip || n.hostname || '';
      var pop = 'confirm-rm-' + String(n.id).replace(/[^a-zA-Z0-9_-]/g, '');
      var nameCell = '<div class="name-edit">' +
        '<span' + (unnamed ? ' class="muted"' : '') + ' data-name>' + esc(label) + '</span>' +
        '<button type="button" class="icon-btn" data-action="edit-node-name" data-id="' + esc(n.id) + '" aria-label="' + esc(t('ctrl.editName')) + '" title="' + esc(t('ctrl.editName')) + '">' + PENCIL + '</button>' +
        '<input class="input w-sm" hidden value="' + esc(nodeUnnamed(n) ? '' : n.name) + '" placeholder="' + esc(n.device_name || '') + '" data-rename="' + esc(n.id) + '" aria-label="' + esc(t('ctrl.editName')) + '">' +
        '</div>';
      var remove = '<button class="btn danger" type="button" popovertarget="' + pop + '">' + esc(t('ctrl.removeBtn')) + '</button>' +
        '<div class="confirm-pop" id="' + pop + '" popover role="alertdialog" aria-labelledby="' + pop + '-t">' +
        '<h4 id="' + pop + '-t">' + esc(t('ctrl.removeAsk', { name: label })) + '</h4>' +
        '<p>' + esc(t('ctrl.removeConfirm', { name: label })) + '</p>' +
        '<div class="actions">' +
        '<button class="btn" type="button" popovertarget="' + pop + '" popovertargetaction="hide" autofocus>' + esc(t('common.cancel')) + '</button>' +
        '<button class="btn danger-solid" type="button" data-action="remove-node" data-id="' + esc(n.id) + '">' + esc(t('ctrl.removeDo')) + '</button>' +
        '</div></div>';
      // Address is editable: a V6 that got a new IP (DHCP, new router, WiFi
      // password) keeps its trust and zones when it is moved here.
      var addrCell = '<div class="name-edit">' +
        '<span class="mono" data-name>' + esc(addr || '—') + '</span>' +
        '<button type="button" class="icon-btn" data-action="edit-node-host" data-id="' + esc(n.id) + '" aria-label="' + esc(t('ctrl.editHost')) + '" title="' + esc(t('ctrl.editHost')) + '">' + PENCIL + '</button>' +
        '<input class="input mono w-md" hidden value="' + esc(addr) + '" placeholder="192.168.1.50" data-rehost="' + esc(n.id) + '" aria-label="' + esc(t('ctrl.editHost')) + '" autocomplete="off" spellcheck="false">' +
        '</div>';
      return '<tr data-node="' + esc(n.id) + '"><td>' + nameCell + '</td><td>' + addrCell +
        '</td><td><span class="badge' + (badge.cls ? ' ' + badge.cls : '') + '">' + esc(t(badge.key)) + '</span></td><td class="num">' +
        esc(String(zoneCountFor(n, index))) + '</td><td>' + remove + '</td></tr>';
    }).join('') || '<tr><td colspan="5" class="muted">' + esc(t('ctrl.empty')) + '</td></tr>';
    renderScan(state.scanFound);
  }

  /* ---- Heat source --------------------------------------------------------- */
  function heatSyncOn(hs) {
    var el = qs('input[name="target_sync_enabled"]');
    var f = behaviorForm();
    if (el && formBusy(f)) return !!el.checked;
    if (hs && hs.target_sync_enabled != null) return !!hs.target_sync_enabled;
    return !!(el && el.checked);
  }

  function fillTemplate(tpl, host, port, entity, value) {
    if (!tpl) return '';
    return String(tpl)
      .replace(/\{host\}/g, host || '')
      .replace(/\{port\}/g, port == null ? '' : String(port))
      .replace(/\{entity\}/g, entity || '')
      .replace(/\{value\}/g, value == null ? '' : String(value));
  }

  function sentUrl(hs, entity, value) {
    var host = hs.host || '';
    var port = hs.port != null ? hs.port : 80;
    if (hs.type === 'generic_http') {
      // TODO: generic_http has no default URL when the write template is empty.
      return fillTemplate(hs.write_url_template || '', host, port, entity, value) || '—';
    }
    if (!host || !entity) return '—';
    return 'http://' + host + ':' + port + '/number/' + encodeURIComponent(entity) + '/set?value=' +
      (value == null || !isFinite(Number(value)) ? '' : Number(value).toFixed(1));
  }

  function paintHeatBadge(hs) {
    var typeName = (hs.type === 'generic_http') ? t('hs.typeHttp') : t('hs.typeAsgard');
    var cls = '';
    var label;
    var st = (hs.push && hs.push.status) || '';
    if (hs.enabled === false) {
      label = typeName + ' · ' + t('heat.badge.off');
    } else if (!hs.push || hs.push.has_result === false || !st) {
      label = typeName + ' · ' + t('status.waiting');
    } else {
      label = typeName + ' · ' + statusLabel(st);
      if (st === 'confirmed' || st === 'sent') cls = 'ok';
      else if (st === 'unreachable') cls = 'bad';
      else if (st === 'mismatch' || st === 'blocked') cls = 'warn';
    }
    qsa('[data-bind="heat.badge"]').forEach(function (n) {
      n.textContent = label;
      n.className = 'badge' + (cls ? ' ' + cls : '');
    });
    setText('heat.badgeText', label);
    var alert = qs('[data-bind-alert="heat"]');
    if (alert) alert.hidden = !(hs.enabled !== false && st === 'unreachable');
  }

  function renderHeatDelivery(hs) {
    if (!hs) return;
    var push = hs.push || {};
    var preview = hs.send_preview || {};
    var weighted = hs.physical_house_temperature_c != null ? hs.physical_house_temperature_c
      : (preview.value_c != null ? preview.value_c : null);
    var target = hs.house_comfort_target_c != null ? hs.house_comfort_target_c
      : (hs.house_target && hs.house_target.value_c != null ? hs.house_target.value_c
        : (preview.target_setpoint_c != null ? preview.target_setpoint_c : null));
    var entity = hs.weighted_temperature_variable || '—';
    var age = push.confirmation_age_s != null ? Number(push.confirmation_age_s)
      : (push.write_age_s != null ? Number(push.write_age_s) : null);
    // No push yet and a V6 refuses Touch's lease → say why instead of "—".
    var leaseMissing = (state.nodes || []).some(function (n) {
      return n && n.trust_label === 'trusted' && n.lease && n.lease !== 'granted';
    });
    var tempWhen = push.has_result ? whenText(age, push.status) : (leaseMissing ? t('hs.notSentLease') : '—');
    var tempTxt = finite(weighted) ? num(weighted) + ' °C' : '—';
    var setTxt = finite(target) ? num(target) + ' °C' : '—';
    setText('hs.http.temp', tempTxt);
    setText('hs.asgard.temp', tempTxt);
    setText('hs.http.entity', entity);
    setText('hs.asgard.entity', entity);
    setText('hs.http.when', tempWhen);
    setText('hs.asgard.when', tempWhen);
    setText('hs.asgard.setpoint', setTxt);
    var valueToken = finite(weighted) ? Number(weighted).toFixed(1) : '';
    setText('hs.sent.url', sentUrl(hs, entity === '—' ? '' : entity, valueToken));
    var sync = heatSyncOn(hs);
    qsa('[data-bind="hs.asgard.setpointWhen"]').forEach(function (n) {
      if (!sync) {
        n.textContent = t('hs.notSent');
        n.classList.add('muted');
      } else {
        var ts = hs.target_sync || {};
        var written = finite(ts.last_written_c);
        var tsAge = written && ts.write_age_s != null ? Number(ts.write_age_s) : null;
        var tsStatus = '';
        if (written) {
          if (ts.failure_streak > 0) tsStatus = 'unreachable';
          else if (finite(ts.last_confirmed_c)) tsStatus = 'confirmed';
          else tsStatus = 'sent';
        }
        n.textContent = written ? whenText(tsAge, tsStatus) : '—';
        n.classList.remove('muted');
      }
    });
    qsa('[data-bind="heat.sentNote"]').forEach(function (n) {
      var show = !sync && hs.type !== 'generic_http';
      n.hidden = !show;
      n.textContent = show ? t('hs.notSent') : '';
    });
  }

  function weightParts(z, basis) {
    var room = (z && z.room) || {};
    var listed = null;
    (state.rooms || []).some(function (r) {
      if (r && z && r.room_id === z.room_id) { listed = r; return true; }
      return false;
    });
    if (listed) {
      if (room.total_area_m2 == null && listed.area_m2 != null) room = Object.assign({ total_area_m2: listed.area_m2 }, room);
      if (room.include_in_house_temperature == null && listed.include_in_house_temperature != null)
        room.include_in_house_temperature = listed.include_in_house_temperature;
    }
    var mult = Number(room.physical_weight);
    if (!(mult > 0)) mult = 1;
    var area = Number(room.total_area_m2 != null ? room.total_area_m2 : room.area_m2);
    if (!(area > 0)) area = 1;
    var ua = Number(room.ua_w_per_k);
    if ((basis === 'ua' || basis === 'mixed') && ua > 0) return { w: ua * mult, kind: 'ua', area: area, ua: ua };
    return { w: area * mult, kind: 'area', area: area, ua: ua };
  }

  function renderWeightRows(hs) {
    // One table per adapter type (Asgard / generic HTTP) — fill all of them.
    var bodies = qsa('[data-bind-weight-rows]');
    if (!bodies.length) return;
    var put = function (v) { bodies.forEach(function (b) { b.innerHTML = v; }); };
    var basis = (state.strategy && state.strategy.weighting && state.strategy.weighting.basis) || 'area';
    var seen = {};
    var rows = [];
    (state.zones || []).forEach(function (z) {
      if (!z || z.is_group_secondary || z.unassigned) return;
      if (!z.room_id || seen[z.room_id]) return;
      var incl = z.room && z.room.include_in_house_temperature;
      if (incl == null) {
        var listed = (state.rooms || []).filter(function (r) { return r.room_id === z.room_id; })[0];
        if (listed) incl = listed.include_in_house_temperature;
      }
      if (incl === false) return;
      if (z.fresh === false) return;
      if (!finite(z.temperature_c)) return;
      seen[z.room_id] = 1;
      rows.push({ name: z.name || z.room_id, temp: Number(z.temperature_c), part: weightParts(z, basis) });
    });
    var sumW = rows.reduce(function (s, r) { return s + r.part.w; }, 0);
    var shown = hs && hs.physical_house_temperature_c != null ? Number(hs.physical_house_temperature_c)
      : (hs && hs.send_preview && hs.send_preview.value_c != null ? Number(hs.send_preview.value_c) : NaN);
    var computed = sumW > 0 ? rows.reduce(function (s, r) { return s + r.temp * r.part.w; }, 0) / sumW : NaN;
    // Commissioning preview is an equal average of fresh temperatures. Match that
    // shown number when a physical (area/UA) temperature is not available.
    if (!(hs && hs.physical_house_temperature_c != null) && isFinite(shown) && (!isFinite(computed) || Math.abs(computed - shown) > 0.15)) {
      rows.forEach(function (r) { r.part = { w: 1, kind: 'equal', area: r.part.area, ua: r.part.ua }; });
      sumW = rows.length;
    }
    if (!rows.length || !(sumW > 0)) { put('<tr><td colspan="4" class="muted">—</td></tr>'); return; }
    put(rows.map(function (r) {
      var pct = (100 * r.part.w / sumW);
      var weight = r.part.kind === 'ua'
        ? t('hs.calcWeightUa', { ua: num(r.part.ua, 1), pct: num(pct, 0) })
        : t('hs.calcWeightArea', { area: num(r.part.area, 0), pct: num(pct, 0) });
      var contrib = r.temp * r.part.w / sumW;
      return '<tr><td>' + esc(r.name) + '</td><td class="num">' + esc(num(r.temp)) + ' °C</td><td>' +
        esc(weight) + '</td><td class="num">' + esc(num(contrib)) + ' °C</td></tr>';
    }).join(''));
  }

  function applyHeat(hs) {
    if (!hs) return;
    state.heat = hs;
    var type = hs.type || 'asgard';
    syncHsType(type);
    var uiType = type === 'generic_http' ? 'http' : 'asgard';
    var conn = hs.connection || {};
    var push = hs.push || {};
    var preview = hs.send_preview || {};
    var age = push.confirmation_age_s != null ? Number(push.confirmation_age_s)
      : (push.write_age_s != null ? Number(push.write_age_s) : (push.age_s != null ? Number(push.age_s) : null));
    // Status lives in the badge; the kv row is only "when".
    setText('heat.lastPush', relAge(push.has_result === false ? null : age));
    var weighted = hs.physical_house_temperature_c != null ? hs.physical_house_temperature_c
      : (preview.value_c != null ? preview.value_c
        : (hs.weighted_temperature && hs.weighted_temperature.value_c != null ? hs.weighted_temperature.value_c : null));
    var target = hs.house_comfort_target_c != null ? hs.house_comfort_target_c
      : (hs.house_target && hs.house_target.value_c != null ? hs.house_target.value_c
        : (preview.target_setpoint_c != null ? preview.target_setpoint_c : null));
    setBind('heat.weighted', esc(num(weighted)) + ' <small>°C</small>');
    setBind('heat.setpoint', esc(num(target)) + ' <small>°C</small>');
    paintHeatBadge(hs);
    renderHeatDelivery(hs);
    paintTargetRole();
    var hp = hs.heat_pump || {};
    if (hs.type === 'asgard') loadHeatHistory();   // throttled to every 5 min
    // Heat sheet chart head: flow, return, ΔT and the compressor as a pill.
    var hpOk = hp.available && finite(hp.feed_c) && finite(hp.return_c);
    setBind('hp.feed', hpOk ? esc(deg(hp.feed_c)) : '—');
    setBind('hp.ret', hpOk ? esc(deg(hp.return_c)) : '—');
    setBind('hp.dt', hpOk ? esc(num(Number(hp.feed_c) - Number(hp.return_c))) : '—');
    qsa('[data-bind="hp.pill"]').forEach(function (b) {
      b.hidden = !(hp.available && hp.compressor_on != null);
      b.textContent = t(hp.compressor_on ? 'hp.pillOn' : 'hp.pillOff');
      b.className = 'badge' + (hp.compressor_on ? ' ok' : '');
    });
    setText('dash.hpTemps', hp.available && hp.feed_c != null && hp.return_c != null
      ? num(hp.feed_c) + ' → ' + num(hp.return_c) + ' °C · ' +
        (hp.compressor_on ? t('hp.compOn', { hz: num(hp.compressor_hz, 0) }) : t('hp.compOff'))
      : '—');
    renderWeightRows(hs);
    if (finite(weighted)) applyOverview({ house_temp_c: weighted, house_target_c: target != null ? target : undefined });
    // Connection (System › Heat source) and behaviour (Heat sheet): leave unsaved edits alone.
    var cf = connForm();
    if (cf && !formBusy(cf)) {
      var prefix = uiType === 'http' ? 'http_' : 'asgard_';
      var put = function (name, v) { putVal(cf.querySelector('[name="' + name + '"]'), v); };
      put(prefix + 'host', conn.host || hs.host || '');
      put(prefix + 'port', conn.port != null ? conn.port : (hs.port != null ? hs.port : 80));
      put(prefix + 'push_interval_s', hs.push_interval_s != null ? hs.push_interval_s : 60);
      put(prefix + 'weighted_temperature_variable', hs.weighted_temperature_variable || 'temperature_feedback_z1');
      put('write_url_template', hs.write_url_template || '');
      put('read_url_template', hs.read_url_template || '');
      put('odin_host', hs.odin_host || '');
      put('enabled', !!(conn.enabled != null ? conn.enabled : hs.enabled));
      if (cf.luneResnap) cf.luneResnap();
    }
    var bf = behaviorForm();
    if (bf && !formBusy(bf)) {
      putVal(bf.querySelector('[name="climate_entity"]'), hs.climate_entity || '');
      putVal(bf.querySelector('[name="target_sync_enabled"]'), !!(hs.target_sync_enabled || (hs.asgard && hs.asgard.sync_enabled)));
      putVal(bf.querySelector('[name="odin_plan_enabled"]'), !!hs.odin_plan_enabled);
      if (bf.luneResnap) bf.luneResnap();
    }
    var hbf = qs('form[data-save="circulation"]');
    if (hbf && !formBusy(hbf)) {
      putVal(hbf.querySelector('[name="house_balance_enabled"]'), !!(hs.house_balance || {}).enabled);
      if (hbf.luneResnap) hbf.luneResnap();
    }
    drawHouseBalance(hs.house_balance);
    var circ = hs.circulation || {};
    state.circ = circ;
    var lpm = finite(circ.flow_m3h) ? Number(circ.flow_m3h) * 1000 / 60 : null;
    setBind('pump.flow', esc(num(lpm, 1)) + ' <small>l/min</small>');
    setText('pump.flowM3h', finite(circ.flow_m3h) ? num(circ.flow_m3h, 2) + ' m³/h' : '—');
    setBind('pump.head', esc(num(circ.head_m)) + ' <small>m</small>');
    setBind('pump.power', esc(num(circ.power_w, 0)) + ' <small>W</small>');
    setText('pump.host', circ.host || '—');
    setText('tile.pumpVal', finite(circ.flow_m3h) ? num(circ.flow_m3h, 1) + ' m³/h' : '—');
    setText('tile.pumpSub', finite(circ.power_w) ? num(circ.power_w, 0) + ' W' : '');
    // Mixing in the buffer tank: secondary flow above primary while heating (Touch judges it).
    var mx = circ.mixing || {};
    var risk = mx.state === 'risk';
    var mxVars = { sec: num(mx.secondary_l_min, 1), pri: num(mx.primary_l_min, 1),
      pct: finite(mx.ratio) ? num(Number(mx.ratio) * 100, 0) : '—' };
    var pumpTile = qs('[data-tile="pump"]');
    if (pumpTile) {
      if (risk) pumpTile.setAttribute('data-state', 'warn'); else pumpTile.removeAttribute('data-state');
      var chip = pumpTile.querySelector('.chip-icon');
      if (chip) chip.setAttribute('data-tone', risk ? 'warn' : 'water');
    }
    setShow('pump.mixing', risk);
    setText('pump.mixBody', risk ? t('pump.mixBody', mxVars) : '');
    setText('pump.mixRatio', finite(mx.ratio) ? t('pump.mixRatioVal', mxVars) : '—');
    setText('tile.pumpStatus', risk ? t('tile.pumpMixing')
      : lpm != null && lpm > 0 ? t('tile.pumpStatus') : t('tile.pumpNone'));
    if (state.zones) renderDist();
    applyCirculation(circ);
    renderHome();
  }

  // Odin comfort control, Asgard→Odin link health and generic heat-source levers.
  function applyHeatControl(ctl) {
    if (!ctl) return;
    state.heatControl = ctl;
    var o = ctl.odin || {};
    var link = ctl.link || {};
    var d = ctl.demand || {};
    var g = ctl.generic || {};
    var ls = link.status || 'unknown';
    var linkText = t('hs.link.' + ls, { s: link.telemetry_age_s != null ? Math.round(link.telemetry_age_s / 60) : '—' });
    if (link.mqtt_telemetry_age_s != null) linkText += ' · MQTT';
    var st = o.status || 'disabled';
    var a = o.applied || {};
    var w = o.wanted || {};
    var stText = st === 'lifted'
      ? t('hs.odinStatus.lifted', { c: num(a.lift_c), h: a.start_hour, n: a.hours })
      : t('hs.odinStatus.' + st);
    if (st !== 'lifted' && w.active) stText += ' · ' + t('hs.odinWanted', { c: num(w.lift_c), h: w.start_hour, n: w.hours });
    setText('dash.odinPlan', stText);
    setText('dash.odinLink', linkText);
    var hsNow = state.heat || {};
    setShow('dash.odin', !!(o.enabled || hsNow.odin_host));
    var tk = link.takeover;
    setText('dash.odinDriver', tk === true ? t('driver.odin') : (tk === false ? t('driver.asgard') : '—'));
    paintTargetRole();
    var route = d.route || 'none';
    var routeText = t('hs.route.' + route, { c: num(d.target_uplift_c) });
    setText('odin.route', routeText);
    setText('levers.state', g.status && g.status !== 'idle'
      ? t('hs.leverState', { t: num(g.last_target_c), r: g.last_heat_request ? '1' : '0', c: num(g.last_curve_offset_c) })
      : routeText);
    var bf = behaviorForm();
    if (bf && !formBusy(bf)) {
      putVal(bf.querySelector('[name="odin_control_enabled"]'), !!o.enabled);
      putVal(bf.querySelector('[name="odin_max_lift_c"]'), Number(o.max_lift_c != null ? o.max_lift_c : 1.5).toFixed(1));
      if (bf.luneResnap) bf.luneResnap();
    }
    var cf = connForm();
    if (cf && !formBusy(cf)) {
      var put = function (name, v) { putVal(cf.querySelector('[name="' + name + '"]'), v); };
      put('target_url_template', g.target_url_template || '');
      put('heat_request_url_template', g.heat_request_url_template || '');
      put('curve_offset_url_template', g.curve_offset_url_template || '');
      put('curve_gain', Number(g.curve_gain != null ? g.curve_gain : 2).toFixed(1));
      put('curve_max_offset_c', Number(g.curve_max_offset_c != null ? g.curve_max_offset_c : 5).toFixed(1));
      if (cf.luneResnap) cf.luneResnap();
    }
  }

  function applyOdinMqtt(mq) {
    if (!mq) return;
    var cf = connForm();
    if (cf && !formBusy(cf)) {
      var put = function (name, v) { putVal(cf.querySelector('[name="' + name + '"]'), v); };
      put('mqtt_enabled', !!mq.enabled);
      put('mqtt_host', mq.host || '');
      put('mqtt_port', mq.port || 1883);
      put('mqtt_username', mq.username || '');
      put('mqtt_topic_prefix', mq.topic_prefix || '');
      put('mqtt_hp_id', mq.hp_id || '');
      if (cf.luneResnap) cf.luneResnap();
    }
    var pw = qs('#mqtt_password');
    if (pw) pw.placeholder = mq.password_set ? t('hs.mqttPasswordSet') : '';
    setText('hs.mqttShort', t(mq.enabled ? 'common.on' : 'common.off'));
    // Saved is not connected: show what the client actually does.
    setText('hs.mqttStatus', !mq.enabled ? t('hs.mqttOff')
      : t('hs.mqttState', { s: mq.connected ? t('hs.mqttConnected')
        : t('hs.mqttDisconnected') + (mq.last_error ? ' (' + mq.last_error + ')' : '') }));
  }

  /* ---- Electricity price to Odin (System › Power price) -------------------- */
  function priceForm() { return qs('form[data-save="prices"]'); }

  function priceCur() {
    var sel = qs('#price_currency');
    return (sel && sel.value) || ((state.prices || {}).currency) || 'DKK';
  }

  function segText(form, name) {
    var r = form && form.querySelector('input[name="' + name + '"]:checked');
    var lab = r && form.querySelector('label[for="' + r.id + '"] span');
    return lab ? lab.textContent : '';
  }

  // One-line summaries on the sub-page rows (Spot · Taxes · Grid · System).
  function priceSummaries() {
    var f = priceForm();
    if (!f) return;
    setText('price.sumSpot', segText(f, 'spot_source'));
    var vat = f.querySelector('[name="vat_pct"]');
    setText('price.sumTaxes', priceCur() + (vat ? ' · ' + t('price.vat') + ' ' + num(vat.value, 0) + ' %' : ''));
    setText('price.sumGrid', segText(f, 'grid_source'));
    setText('price.sumSystem', segText(f, 'system_source'));
  }

  // Units that follow the calculation currency; DK-only sources (Energi Data
  // Service, DataHub) only exist for DK1/DK2.
  function priceSyncForm() {
    var f = priceForm();
    if (!f) return;
    var unit = priceCur() + '/kWh';
    qsa('[data-price-unit]', f).forEach(function (u) { u.textContent = unit; });
    qsa('[data-price-cur]', f).forEach(function (u) { u.textContent = priceCur(); });
    var zone = (qs('#price_zone') || {}).value || 'DK1';
    var dk = /^DK[12]$/.test(zone);
    qsa('[data-dk-only]', f).forEach(function (lab) {
      lab.hidden = !dk;
      var r = document.getElementById(lab.getAttribute('for'));
      if (!dk && r && r.checked) {
        var alt = { 'ss-eds': 'ss-energy_charts', 'gt-datahub': 'gt-schedule', 'en-datahub': 'en-fixed' }[r.id];
        var a = alt && document.getElementById(alt);
        if (a) { a.checked = true; a.dispatchEvent(new Event('change', { bubbles: true })); }
      }
    });
    var dkNote = qs('[data-bind="price.dkNote"]');
    if (dkNote) dkNote.hidden = !dk;
    priceSummaries();
  }

  function priceSchedRows(list) {
    var body = qs('[data-bind-price-sched]');
    if (!body) return;
    body.innerHTML = (list || []).map(function (b) {
      return '<tr><td><input class="input w-xs" type="number" inputmode="numeric" min="0" max="23" step="1" data-sched="h" value="' +
        esc(b.h) + '" aria-label="' + esc(t('price.schedHourAria')) + '"></td>' +
        '<td class="num"><input class="input w-sm" type="number" inputmode="decimal" min="-5" max="20" step="0.001" data-sched="v" value="' +
        esc(Number(b.v).toFixed(3)) + '" aria-label="' + esc(t('price.schedValueAria')) + '"></td>' +
        '<td><button class="btn" type="button" data-action="price-sched-remove">' + esc(t('price.schedRemove')) + '</button></td></tr>';
    }).join('');
  }

  function priceSchedRead() {
    var out = [];
    qsa('[data-bind-price-sched] tr').forEach(function (tr) {
      var h = Number((qs('[data-sched="h"]', tr) || {}).value);
      var v = Number((qs('[data-sched="v"]', tr) || {}).value);
      if (isFinite(h) && h >= 0 && h <= 23 && isFinite(v)) out.push({ h: Math.round(h), v: Math.round(v * 10000) / 10000 });
    });
    out.sort(function (a, b) { return a.h - b.h; });
    return out.filter(function (b, i) { return i === 0 || b.h !== out[i - 1].h; });
  }

  // The rows have no name; the hidden grid_schedule field carries them so the
  // normal clean/dirty + save path sees the change.
  function priceSchedSync() {
    var f = priceForm();
    var hidden = f && f.querySelector('input[name="grid_schedule"]');
    if (!hidden) return;
    var json = JSON.stringify(priceSchedRead());
    if (hidden.value === json) return;
    hidden.value = json;
    hidden.dispatchEvent(new Event('input', { bubbles: true }));
  }

  function priceErrText(err) {
    var e = String(err || '');
    if (!e) return '';
    if (e === 'spot_incomplete') return t('price.err.spot_incomplete');
    if (e === 'no_odin_host') return t('price.err.no_odin_host');
    if (e === 'clock_invalid') return t('price.err.clock');
    if (/datahub_dk_only|eds_dk_only/.test(e)) return t('price.err.datahub_dk_only');
    if (/no_token/.test(e)) return t('price.err.no_token');
    if (/^spot_/.test(e)) return t('price.err.spot');
    if (/^grid_/.test(e)) return t('price.err.grid');
    if (/^system_/.test(e)) return t('price.err.energinet');
    if (/^odin_|_http_/.test(e)) return t('price.err.odin');
    return t('price.err.other');
  }

  function priceWhen(epoch) {
    if (!epoch) return '—';
    var lang = (i18n._lang || document.documentElement.lang || 'en');
    return new Date(epoch * 1000).toLocaleString(lang, { weekday: 'short', hour: '2-digit', minute: '2-digit' });
  }

  function priceSourceText(src) {
    if (src === 'api' || src === 'energy_charts' || src === 'entsoe' || src === 'fixed') return t('price.odinMode.' + src);
    return t('price.odinMode.unknown');
  }

  function applyPrices(p) {
    if (!p || p.available === false) return;
    state.prices = p;
    var st = (p.status || {});
    var stKey = st.state || (p.enabled ? 'waiting' : 'disabled');
    qsa('[data-bind="price.badge"]').forEach(function (b) {
      b.textContent = t('price.state.' + stKey);
      b.className = stKey === 'ok' || stKey === 'odin' ? 'badge ok' : (stKey === 'error' ? 'badge bad' : (stKey === 'disabled' ? 'badge' : 'badge warn'));
    });
    setText('price.lastPush', st.last_push_epoch ? t('price.lastPushValue', { time: priceWhen(st.last_push_epoch) }) : '—');
    setText('price.hours', st.hours_pushed ? t('price.hoursValue', { n: st.hours_pushed }) : '—');
    var used = st.spot_used ? t('price.spotUsed.' + st.spot_used) : '—';
    setText('price.spotUsed', st.spot_used && st.spot_fallback ? t('price.spotFallback', { src: used }) : used);
    setText('price.odinMode', st.odin_write_pending ? t('price.odinMode.pending') : priceSourceText(st.odin_source));
    setText('price.problem', stKey === 'error' ? priceErrText(st.last_error) : '—');
    var notes = [];
    if (!p.odin_host_set) notes.push(t('price.noOdinHost'));
    if (st.grid_from_cache || st.system_from_cache) notes.push(t('price.cached'));
    setText('price.note', notes.join(' '));

    // Odin's own settings as Odin reports them (its token only as set / not set).
    var oc = ((p.odin || {}).current) || {};
    setText('price.odin.mode', !oc.known ? '—' : (oc.price_mode === 'fixed' ? t('price.odinMode.fixed') : priceSourceText(oc.price_source)));
    setText('price.odin.zone', oc.known && oc.ec_bzn ? oc.ec_bzn : '—');
    setBind('price.odin.fixed', oc.known && oc.fixed_price != null ? esc(num(oc.fixed_price, 3)) + ' <small>€/kWh</small>' : '—');
    setText('price.odin.token', oc.known ? t(oc.token_set ? 'price.tokenSet' : 'price.tokenUnset') : '—');

    // Today's all-in price (Next heating sheet): muted bars (price is not heat) + figures in HTML.
    var day = p.today;
    var cur = p.currency || 'DKK';
    var plot = qs('[data-bind-price-bars]');
    var total = (day && day.total) || [];
    if (plot) {
      var max = 0;
      total.forEach(function (v) { if (Number(v) > max) max = Number(v); });
      plot.innerHTML = total.length ? total.map(function (v, h) {
        var pct = max > 0 ? Math.max(2, Math.round(Math.max(0, Number(v)) / max * 100)) : 0;
        return '<div class="col"><i class="plan" style="--plan:' + pct + '" title="' +
          esc(t('price.atHour', { v: num(v, 2) + ' ' + cur + '/kWh', h: String(h).padStart(2, '0') })) + '"></i></div>';
      }).join('') : '';
    }
    var today = qs('[data-bind-price-today]');
    if (today) today.hidden = !total.length;
    setText('price.inclAll', t('price.inclAll', { cur: cur }));
    var unit = ' <small>' + esc(cur) + '/kWh</small>';
    var showTile = false;
    if (total.length === 24) {
      var lo = 0, hi = 0, peak = 0;
      for (var h = 1; h < 24; h++) {
        if (total[h] < total[lo]) lo = h;
        if (total[h] > total[hi]) hi = h;
      }
      for (var k = 17; k < 21; k++) peak += Number(total[k]) / 4;
      var hh = function (x) { return String(x).padStart(2, '0'); };
      var nowV = Number(total[new Date().getHours()]);
      setBind('price.now', esc(num(nowV, 2)) + unit);
      setBind('price.nowShort', esc(num(nowV, 2)) + unit);
      setText('price.min', t('price.atHour', { v: num(total[lo], 2) + ' ' + cur + '/kWh', h: hh(lo) }));
      setText('price.max', t('price.atHour', { v: num(total[hi], 2) + ' ' + cur + '/kWh', h: hh(hi) }));
      setBind('price.peak', esc(num(peak, 2)) + unit);
      // Home › Next heating: the price now as one line with a 5-step good/bad chip.
      var below = total.filter(function (v) { return Number(v) < nowV; }).length;
      var scale = Math.max(1, Math.min(5, 1 + Math.floor(below / total.length * 5)));
      setText('tile.priceText', t('tile.price', { p: num(nowV, 2), cur: cur }));
      qsa('[data-bind-scale]').forEach(function (c) { c.setAttribute('data-scale', String(scale)); c.textContent = t('price.scale.' + scale); });
      showTile = !!p.enabled;
    } else {
      setText('price.now', t('price.noData'));
      setBind('price.nowShort', '—');
      ['price.min', 'price.max', 'price.peak'].forEach(function (k2) { setText(k2, '—'); });
    }
    setShow('tile.price', showTile);
    var tok = qs('#price_token');
    if (tok) tok.placeholder = p.entsoe_token_set ? t('price.tokenSaved') : '';

    // Settings: never overwrite what the user is editing.
    var f = priceForm();
    if (!f || formBusy(f)) return;
    fillPriceForm(f, p);
  }

  function fillPriceForm(f, p) {
    var g = p.grid || {}, sy = p.system || {}, sp = p.spot || {}, o = p.odin || {};
    var put = function (name, val, d) {
      if (val == null) return;
      putVal(f.querySelector('[name="' + name + '"]'), d != null ? Number(val).toFixed(d) : val);
    };
    put('enabled', !!p.enabled);
    putRadio(f, 'model', p.model || 'touch');
    put('zone', p.zone || 'DK1');
    putRadio(f, 'spot_source', sp.source || 'eds');
    put('spot_fixed_eur', sp.fixed_eur, 3);
    put('currency', p.currency || 'DKK');
    put('fx', p.fx != null ? Math.round(p.fx * 10000) / 10000 : null);
    putRadio(f, 'grid_source', g.source || 'datahub');
    putRadio(f, 'system_source', sy.source || 'datahub');
    put('grid_gln', g.gln || '');
    put('grid_code', g.code || '');
    put('system_fixed', sy.fixed, 3);
    put('energy_tax', p.energy_tax, 3);
    put('markup', p.markup, 3);
    put('vat_pct', p.vat_pct, 1);
    putRadio(f, 'odin_mode', o.mode || 'dynamic');
    put('odin_source', o.source || 'energy_charts');
    put('odin_fixed_price', o.fixed_price, 3);
    var tok = f.querySelector('input[name="entsoe_token"]');
    if (tok) { tok.value = ''; tok.defaultValue = ''; }
    var sched = g.schedule || [];
    var hidden = f.querySelector('input[name="grid_schedule"]');
    if (hidden) { hidden.value = JSON.stringify(sched.map(function (b) { return { h: b.h, v: b.v }; })); hidden.defaultValue = hidden.value; }
    priceSchedRows(sched);
    priceSyncForm();
    if (f.luneResnap) f.luneResnap();
  }

  async function applyZoneDefaults() {
    var f = priceForm();
    if (!f) return;
    var zone = (qs('#price_zone') || {}).value || 'DK1';
    var d = await get('/prices/zone-defaults/' + encodeURIComponent(zone));
    function check(name, val) {
      var r = f.querySelector('input[name="' + name + '"][value="' + val + '"]');
      if (r && !r.checked) { r.checked = true; r.dispatchEvent(new Event('change', { bubbles: true })); }
    }
    function put(name, val, dd) {
      var el2 = f.querySelector('[name="' + name + '"]');
      if (!el2 || val == null) return;
      el2.value = dd != null ? Number(val).toFixed(dd) : val;
      el2.dispatchEvent(new Event('input', { bubbles: true }));
    }
    check('spot_source', d.spot_source);
    put('currency', d.currency);
    put('fx', Math.round(d.fx * 10000) / 10000);
    put('energy_tax', d.energy_tax, 3);
    put('vat_pct', d.vat_pct, 1);
    check('grid_source', d.grid_source);
    check('system_source', d.system_source);
    if (d.system_source === 'fixed') put('system_fixed', d.system_fixed, 3);
    priceSyncForm();
    setText('price.defaultsNote', t(d.known ? 'price.defaultsApplied' : 'price.defaultsUnknown', { zone: zone }));
  }

  // Push (Touch model) or write Odin's settings (Odin model), then follow GET /prices.
  async function runPricePush(btn, odinWrite) {
    var box = qs(odinWrite ? '[data-bind-price-odin-result]' : '[data-bind-price-result]');
    function paint(ok, l1, l2) {
      if (!box) return;
      box.removeAttribute('data-state');
      box.innerHTML = '<div class="msg ' + (ok ? 'ok' : 'bad') + '"><span><b>' + esc(l1) + '</b> ' + esc(l2) + '</span></div>';
    }
    var fail = odinWrite ? 'price.odinWriteFail' : 'price.pushFail';
    if (box) { box.setAttribute('data-state', 'running'); box.textContent = t('price.state.running'); }
    btn.setAttribute('aria-busy', 'true');
    var before = ((state.prices || {}).status || {}).last_attempt_epoch || 0;
    try {
      try {
        await post('/prices/push', {});
      } catch (e) {
        var p0 = state.prices || {};
        paint(false, t(fail, { time: clockNow() }),
          !p0.enabled ? t('price.pushDisabled') : (!p0.odin_host_set ? t('price.noOdinHost') : t('price.err.other')));
        return;
      }
      for (var i = 0; i < 25; i++) {
        await new Promise(function (r) { setTimeout(r, 2000); });
        var p = await get('/prices').catch(function () { return null; });
        if (!p) continue;
        var s2 = p.status || {};
        if (odinWrite) {
          if (s2.odin_write_pending) continue;
          applyPrices(p);
          paint(true, t('price.odinWriteOk', { time: clockNow() }), priceSourceText(((p.odin || {}).current || {}).price_source));
          return;
        }
        if (s2.state === 'running' || !(s2.last_attempt_epoch > before)) continue;
        applyPrices(p);
        if (s2.state === 'ok') paint(true, t('price.pushOk', { time: clockNow() }), t('price.pushOkBody', { n: s2.hours_pushed || 24 }));
        else paint(false, t(fail, { time: clockNow() }), priceErrText(s2.last_error) || t('price.err.other'));
        return;
      }
      paint(false, t(fail, { time: clockNow() }), t('price.pushPending'));
    } finally {
      btn.removeAttribute('aria-busy');
    }
  }

  function applyWifi(w) {
    if (!w) return;
    setText('wifi.current', w.connected && w.ssid ? w.ssid : '—');
    var sw = w['switch'];
    var status = (sw && sw !== 'none')
      ? t('wifi.switch.' + sw, { ssid: w.target_ssid || '' })
      : (w.connected ? t('wifi.connectedTo') : (w.ap_active ? t('wifi.apActive') : t('wifi.notConnected')));
    setText('wifi.status', status);
    var ssidEl = qs('#wifi_ssid');
    var f = ssidEl && ssidEl.form;
    if (ssidEl && !formBusy(f) && !ssidEl.value && w.ssid) {
      putVal(ssidEl, w.ssid);
      if (f.luneResnap) f.luneResnap();
    }
  }

  // What the comfort target sent to Asgard does right now — explicit, because
  // with Odin in control it is a backup, not the thing that drives heating.
  function paintTargetRole() {
    var hs = state.heat || {};
    var ctl = state.heatControl || {};
    var link = ctl.link || {};
    var d = ctl.demand || {};
    var sync = heatSyncOn(hs);
    var odin = d.route === 'odin_schedule' || !!link.forwarder_active || !!((ctl.odin || {}).enabled);
    var txt;
    if (hs.type === 'generic_http') txt = '—';
    else if (odin) {
      if (!sync) txt = t('role.offOdin');
      else if (link.takeover === true) txt = t('role.reserveNow');
      else if (link.takeover === false) txt = t('role.asgardNow');
      else txt = t('role.reserve');
    } else txt = sync ? t('role.drives', { c: num(d.target_uplift_c || 0) }) : t('role.off');
    setText('dash.targetRole', txt);
  }

  /* ---- Next heating: Odin's plan + Touch's preheating (GET /plan) ---------- */
  function applyHeatPlan(p) {
    var host = qs('[data-bind-plan]');
    if (!p) return;
    state.plan = p;
    var odin = p.odin || {};
    var heat = odin.heat_kw || [];
    var mode = odin.mode || [];
    var H = Number(p.hours) || 24;
    var start = Number(p.start_hour) || 0;
    var now = '—';
    if (odin.available && heat.length) {
      var kw = Number(heat[0]);
      now = Number(mode[0]) === 1 ? t('now.dhw') : (kw > 0.05 ? t('now.heat', { kw: num(kw) }) : t('now.off'));
    }
    setText('dash.odinNow', now);
    // Tile + metrics: the next block of space heat and its energy.
    var hh = function (x) { return String((start + x) % 24).padStart(2, '0'); };
    var a = -1, b = -1, blockKwh = 0, totalKwh = 0;
    for (var i = 0; i < Math.min(H, heat.length); i++) {
      var v = Number(heat[i]) || 0;
      if (Number(mode[i]) !== 1 && Number(mode[i]) !== 6) totalKwh += v;
      if (v > 0.05 && a < 0) a = i;
      if (a >= 0 && b < 0 && !(v > 0.05)) b = i;
      if (a >= 0 && b < 0) blockKwh += v;
    }
    if (a >= 0 && b < 0) b = Math.min(H, heat.length);
    var tile = qs('[data-tile="plan"]');
    if (odin.available && a >= 0) {
      setText('tile.planVal', a === 0 ? t('tile.planNow') : t('tile.planVal', { a: hh(a), b: hh(b) }));
      setText('tile.planKwh', t('tile.planKwh', { v: num(blockKwh) }));
    } else {
      setText('tile.planVal', '—');
      setText('tile.planKwh', '');
    }
    var planVal = (qs('[data-bind="tile.planVal"]') || {}).textContent || '—';
    setShow('hero.plan', planVal !== '—');
    setText('hero.planVal', planVal);
    setText('hero.planSub', (qs('[data-bind="tile.planKwh"]') || {}).textContent || '');
    setText('tile.planStatus', odin.available ? t('tile.planStatusOdin') : (p.rooms && p.rooms.length ? t('planG.noOdin') : t('tile.planNone')));
    setBind('plan.energyKwh', odin.available ? esc(num(totalKwh)) + ' <small>kWh</small>' : '—');
    var viz = qs('[data-bind-viz="plan"]');
    if (viz) {
      var max = 0;
      heat.forEach(function (x) { if (Number(x) > max) max = Number(x); });
      viz.innerHTML = odin.available && max > 0 ? '<line class="base" x1="0" y1="63.5" x2="240" y2="63.5"/>' + heat.slice(0, 24).map(function (x, k) {
        if (!(Number(x) > 0.05)) return '';
        var h2 = Math.max(4, Number(x) / max * 60);
        return '<rect class="col on" x="' + (k * 10 + 2) + '" y="' + (64 - h2).toFixed(1) + '" width="6" height="' + h2.toFixed(1) + '" rx="3"/>';
      }).join('') : '';
      if (tile) tile.toggleAttribute('data-empty', !(odin.available && max > 0));
    }
    if (!host) return;
    var rooms = (p.rooms || []).filter(function (r) { return r && (r.preload || r.charge); });
    var empty = !odin.available && !rooms.length;
    setShow('planG.empty', empty);
    host.hidden = empty;
    qsa('[data-panel="plan"] .fc-legend').forEach(function (l) { l.hidden = empty; });
    if (empty) { host.innerHTML = ''; return; }
    var clamp = function (v) { return Math.max(0, Math.min(H, Number(v) || 0)); };
    var span = function (cls, x0, x1, extra, tip) {
      x0 = clamp(x0); x1 = clamp(x1);
      if (x1 <= x0) return '';
      return '<span class="' + cls + '" style="--a:' + x0 + ';--b:' + x1 + '"' + (extra || '') +
        (tip ? ' title="' + esc(tip) + '"' : '') + '></span>';
    };
    var lang = (i18n._lang || document.documentElement.lang || 'en');
    var x = '';
    for (var j = 0; j <= H; j++) {
      var hr = (start + j) % 24;
      if (j === 0) { x += '<span style="left:0%">' + esc(t('fc.now')) + '</span>'; continue; }
      if (hr % 3 !== 0 || j < 2 || j > H - 2) continue;  // keep clear of the "Now" label
      var left = (j / H * 100).toFixed(2) + '%';
      x += hr === 0
        ? '<span class="d" style="left:' + left + '">' + esc(new Date(Date.now() + j * 3600000).toLocaleDateString(lang, { weekday: 'short' })) + '</span>'
        : '<span' + (hr % 6 ? ' class="m"' : '') + ' style="left:' + left + '">' + String(hr).padStart(2, '0') + '</span>';
    }
    var html = '<span></span><div class="plan-x">' + x + '</div>';
    // Odin lane: planned energy per hour + Touch's lift of Odin's comfort band.
    // Space heat = kWh heat (heat_kw); DHW / legionella hours carry no space
    // heat, so their bar is Odin's planned electricity (kWh el).
    if (odin.available) {
      var energy = odin.energy_kwh || [];
      var vals = [];
      var kinds = [];
      for (var q = 0; q < H; q++) {
        var m = Number(mode[q]);
        var kind = m === 1 ? 'dhw' : (m === 6 ? 'legionella' : 'heat');
        kinds.push(kind);
        vals.push(kind === 'heat' ? (Number(heat[q]) || 0) : (Number(energy[q]) || 0));
      }
      var vmax = 0;
      vals.forEach(function (v2) { if (v2 > vmax) vmax = v2; });
      // Round the axis up to 1 / 2 / 2.5 / 5 × 10^n.
      var top = 1;
      if (vmax > 0) {
        var mag = Math.pow(10, Math.floor(Math.log10(vmax)));
        top = [1, 2, 2.5, 5, 10].map(function (f) { return f * mag; }).filter(function (v3) { return v3 >= vmax; })[0];
      }
      var bars = '';
      for (var h = 0; h < H; h++) {
        var pct = Math.round(vals[h] / top * 100);
        var label = String((start + h) % 24).padStart(2, '0') + ':00';
        bars += '<i class="plan-bar"' + (kinds[h] !== 'heat' ? ' data-mode="' + kinds[h] + '"' : '') + ' style="--v:' + pct + '" title="' +
          esc(t('planG.tip.' + kinds[h], { h: label, kwh: num(vals[h]) })) + '"></i>';
      }
      var lifts = '';
      var lc = odin.lift_c || [];
      for (var a2 = 0; a2 < H; a2++) {
        if (!(Number(lc[a2]) > 0)) continue;
        var b2 = a2;
        while (b2 < H && Number(lc[b2]) > 0) b2++;
        lifts += span('plan-lift', a2, b2, '', t('planG.tipLift', { c: num(lc[a2]) }));
        a2 = b2;
      }
      var ticks = '<span class="plan-y"><i>' + esc(num(top)) + ' kWh</i><i>' + esc(num(top / 2)) + '</i><i>0</i></span>';
      html += '<span class="plan-lab plan-lab--y"><b>' + esc(t('planG.odin')) + '</b>' + ticks + '</span>' +
        '<div class="plan-lane plan-lane--odin">' + bars + lifts + '</div>';
    } else {
      html += '<span class="plan-lab">' + esc(t('planG.odin')) + '</span><span class="plan-empty-lane">' + esc(t('planG.noOdin')) + '</span>';
    }
    rooms.forEach(function (r) {
      var segs = '';
      if (r.preload) segs += span('plan-seg', r.preload.from, r.preload.to, ' data-kind="preload"', t('planG.tipPre', { c: num(r.preload.offset_c) }));
      if (r.charge) {
        var ins = !!r.charge.insufficient;
        segs += span('plan-seg', r.charge.from, r.charge.to, ' data-kind="charge"' + (ins ? ' data-insufficient' : ''),
          t(ins ? 'planG.tipInsufficient' : 'planG.tipCharge', { c: num(r.charge.store_c) }));
      }
      html += '<span class="plan-lab" title="' + esc(r.name) + '">' + esc(r.name) + '</span><div class="plan-lane">' + segs + '</div>';
    });
    host.innerHTML = html;
  }

  // Approximate split of the pump flow per manifold (LDS .dist): each loop's
  // conductance is taken as its valve opening; a manifold's share is the sum of
  // its open loops over all open loops. Pipe runs to each manifold are ignored.
  function renderDist() {
    var host = qs('[data-bind-dist]');
    if (!host) return;
    var zones = (state.zones || []).filter(function (z) { return z && !z.unassigned; });
    var byNode = {};
    var total = 0;
    zones.forEach(function (z) {
      var n = Number(z.node_index) || 0;
      var v = Math.max(0, Math.min(100, Number(z.valve_pct) || 0));
      if (!byNode[n]) byNode[n] = { kv: 0, zones: [] };
      byNode[n].kv += v / 100;
      byNode[n].zones.push({ name: z.name || ('Z' + ((Number(z.zone_index) || 0) + 1)), v: v });
      total += v / 100;
    });
    var circ = (state.heat && state.heat.circulation) || {};
    var lpmTotal = finite(circ.flow_m3h) ? Number(circ.flow_m3h) * 1000 / 60 : null;
    var nodes = Object.keys(byNode).map(Number).sort(function (a, b) { return a - b; });
    if (!nodes.length || !(total > 0)) {
      host.innerHTML = '<div class="dist-bar"></div><p class="dist-note">' + esc(t('flow.none')) + '</p>';
      var td0 = qs('[data-bind-tiledist]');
      if (td0) td0.hidden = true;
      return;
    }
    var bar = '';
    var rows = '';
    nodes.forEach(function (n, i) {
      var g = byNode[n];
      var pct = g.kv / total * 100;
      var name = nodeLabel(state.nodes[n]) || ('M' + (n + 1));
      var di = ' data-i="' + (i % 4) + '"';
      bar += '<span class="dist-seg"' + di + ' style="--w:' + pct.toFixed(1) + '" title="' + esc(name + ' · ' + num(pct, 0) + ' %') + '"></span>';
      var zbar = '';
      g.zones.forEach(function (z) {
        var zp = g.kv > 0 ? (z.v / 100) / g.kv * 100 : 0;
        if (zp <= 0) return;
        zbar += '<span class="dist-seg"' + di + ' style="--w:' + zp.toFixed(1) + '" title="' +
          esc(t('flow.zoneTip', { zone: z.name, v: num(z.v, 0), pct: num(zp, 0) })) + '"></span>';
      });
      rows += '<i class="dist-key"' + di + '></i><span class="dist-name">' + esc(name) + '</span>' +
        '<span class="num">' + esc(num(pct, 0)) + ' %</span>' +
        '<span class="num">' + (lpmTotal != null ? esc(num(lpmTotal * pct / 100, 1)) + ' l/min' : '') + '</span>' +
        '<div class="dist-bar dist-bar--thin">' + zbar + '</div>';
    });
    host.innerHTML = '<div class="dist-bar">' + bar + '</div><div class="dist-rows">' + rows + '</div>' +
      '<p class="dist-note">' + esc(t(lpmTotal != null ? 'flow.noteLpm' : 'flow.note')) + '</p>';
    setText('svc.dist', nodes.map(function (n) { return num(byNode[n].kv / total * 100, 0); }).join(' · ') + ' %');
    // Home's Circulation tile: the same split as a donut + legend.
    var td = qs('[data-bind-tiledist]');
    if (td) {
      var cum = 0, gap = nodes.length > 1 ? 1.5 : 0, segs = '', legend = '';
      nodes.forEach(function (n, i) {
        var pct = byNode[n].kv / total * 100;
        var name = nodeLabel(state.nodes[n]) || ('M' + (n + 1));
        segs += '<circle data-i="' + (i % 4) + '" cx="22" cy="22" r="17" pathLength="100" stroke-dasharray="' +
          Math.max(0, pct - gap).toFixed(2) + ' 100" stroke-dashoffset="' + (-cum).toFixed(2) + '"/>';
        cum += pct;
        legend += '<li><i data-i="' + (i % 4) + '"></i><span>' + esc(name) + '</span> <b>' + esc(num(pct, 0)) + ' %</b></li>';
      });
      td.innerHTML = '<svg viewBox="0 0 44 44" role="img" aria-label="' + esc(t('pump.dist')) + '"><circle class="trk" cx="22" cy="22" r="17"/>' + segs + '</svg><ul>' + legend + '</ul>';
      td.hidden = false;
    }
  }

  function applyCirculation(circ) {
    if (!circ) return;
    var f = qs('form[data-save="circulation"]');
    if (!f || formBusy(f)) return;
    var put = function (id, val) { if (val != null && val !== '') putVal(qs('#' + id), val); };
    put('pump_host', circ.host || '');
    put('pump_port', circ.port != null ? circ.port : 80);
    put('pump_flow_entity', circ.flow_entity || 'pump_flow');
    put('pump_head_entity', circ.head_entity || 'pump_head_pressure');
    put('pump_power_entity', circ.power_entity || 'pump_power');
    if (f.luneResnap) f.luneResnap();
  }

  /* ---- Weather ------------------------------------------------------------- */
  function forecastSky(hour) {
    var hod = 12;
    if (hour.timestamp_s) hod = new Date(hour.timestamp_s * 1000).getHours();
    var cloud = hour.cloud_pct || 0;
    // Precipitation at or below ~0 °C falls as snow (no weather code in the feed).
    if ((hour.precip_mm || 0) > 0.2) return (hour.temp_c != null && hour.temp_c <= 0.5) ? 'snow' : 'rain';
    if (cloud > 70) return 'cloud';
    // Night: no sun above the horizon (falls back to the clock without irradiance).
    var night = hour.solar_wm2 != null ? hour.solar_wm2 < 5 : (hod < 6 || hod >= 20);
    if (night) return 'moon';
    if (cloud < 35) return 'sun';
    return 'partly';
  }

  // Slider over the forecast: a dashed line through both plots that follows the
  // pointer (mouse or finger), with the values for that hour beside it.
  function bindForecastScrub() {
    var root = qs('.fc');
    if (!root || root.dataset.scrubBound) return;
    root.dataset.scrubBound = '1';
    var line = qs('.fc-scrub', root);
    var box = qs('.fc-readout', root);
    var plot = qs('.fc-temp', root);
    if (!line || !box || !plot) return;
    var hide = function () {
      line.hidden = true; box.hidden = true;
      root.removeAttribute('data-scrubbing');
    };
    var show = function (ev) {
      var hours = state.fcHours || [];
      if (hours.length < 2) return;
      var pr = plot.getBoundingClientRect();
      var rr = root.getBoundingClientRect();
      var frac = (ev.clientX - pr.left) / pr.width;
      if (!(frac >= 0 && frac <= 1)) { hide(); return; }
      var i = Math.round(frac * (hours.length - 1));
      var h = hours[i] || {};
      var x = pr.left - rr.left + (i / (hours.length - 1)) * pr.width;
      line.style.left = x + 'px';
      var sky = forecastSky(h);
      var lang = (i18n._lang || document.documentElement.lang || 'en');
      var when = h.timestamp_s
        ? new Date(h.timestamp_s * 1000).toLocaleString(lang, { weekday: 'short', hour: '2-digit', minute: '2-digit' })
        : '';
      var dir = finite(h.wind_dir_deg) ? ' ' + t('fc.windFrom', { dir: compassDir(Number(h.wind_dir_deg)) }) : '';
      box.innerHTML = '<span><b>' + esc(when) + '</b> · ' + esc(t('fc.sky.' + sky)) + '</span>' +
        '<span class="c-temp">' + esc(num(h.temp_c)) + ' °C</span>' +
        '<span class="c-sun">' + esc(num(h.solar_wm2, 0)) + ' W/m²</span>' +
        '<span class="c-wind">' + esc(num(h.wind_ms, 1)) + ' m/s' + esc(dir) + '</span>';
      box.style.left = x + 'px';
      box.classList.toggle('flip', x > rr.width * 0.66);
      line.hidden = false; box.hidden = false;
      root.setAttribute('data-scrubbing', '');
    };
    qsa('.fc-plot', root).forEach(function (p) {
      p.addEventListener('pointermove', show);
      p.addEventListener('pointerdown', show);
      p.addEventListener('pointerleave', hide);
    });
  }

  function applyForecastCharts(fc) {
    var hours = fc.hours || [];
    // No forecast → the chart collapses to one .empty line (DESIGN.md 5.9).
    var real = hours.filter(function (h) { return h && finite(h.temp_c); }).length;
    var fcPanel = qs('[data-panel="forecast"]');
    if (fcPanel) fcPanel.toggleAttribute('data-empty', real < 2);
    if (real < 2) return;
    state.fcHours = hours;
    bindForecastScrub();
    var start = (fc.cache && fc.cache.decision_start_index >= 0) ? fc.cache.decision_start_index : 0;
    if (start >= hours.length) start = 0;
    var temps = hours.map(function (h) { return h.temp_c; });
    var winds = hours.map(function (h) { return h.wind_ms; });
    var n = Math.max(temps.length - 1, 1);
    var W = 720;
    var tMin = 6, tMax = 20, wMax = 14;
    temps.forEach(function (v) {
      if (!isFinite(v)) return;
      if (v < tMin) tMin = Math.floor(v);
      if (v > tMax) tMax = Math.ceil(v);
    });
    if (tMax - tMin < 8) {
      var mid = (tMin + tMax) / 2;
      tMin = mid - 4;
      tMax = mid + 4;
    }
    winds.forEach(function (v) { if (isFinite(v) && v > wMax) wMax = Math.ceil(v / 2) * 2; });
    var tp = seriesPoints(temps, W, 100, tMin, tMax);
    var solar = hours.map(function (h) { return h.solar_wm2 != null ? Number(h.solar_wm2) : 0; });
    var sPeak = solar.reduce(function (m, v) { return isFinite(v) && v > m ? v : m; }, 0);
    var sMax = Math.max(200, Math.ceil(sPeak / 100) * 100);
    var sunSvg = qs('[data-bind-fc="sun"]');   // stacked layout: the sun has its own row
    var sH = sunSvg ? 60 : 100;
    var sp = seriesPoints(solar, W, sH, 0, sMax);
    var wp = seriesPoints(winds, W, 60, 0, wMax);
    var nowX = (start / n) * W;
    var svgT = qs('[data-bind-fc="temp"]');
    var svgW = qs('[data-bind-fc="wind"]');
    var setLine = function (ln, x) { if (ln) { ln.setAttribute('x1', String(x)); ln.setAttribute('x2', String(x)); } };
    if (svgT) {
      var tl = svgT.querySelector('polyline.tl');
      if (tl) tl.setAttribute('points', tp);
      var sa = svgT.querySelector('polygon.sa');
      if (sa) sa.setAttribute('points', '0,100 ' + sp + ' ' + W + ',100');
      var sl = svgT.querySelector('polyline.sl');
      if (sl) sl.setAttribute('points', sp);
      var past = svgT.querySelector('rect.past');
      if (past) past.setAttribute('width', String(nowX));
      setLine(svgT.querySelector('line.now'), nowX);
    }
    if (sunSvg) {
      var sa2 = sunSvg.querySelector('polygon.sa');
      if (sa2) sa2.setAttribute('points', '0,60 ' + sp + ' ' + W + ',60');
      var sl2 = sunSvg.querySelector('polyline.sl');
      if (sl2) sl2.setAttribute('points', sp);
      var pastS = sunSvg.querySelector('rect.past');
      if (pastS) pastS.setAttribute('width', String(nowX));
      setLine(sunSvg.querySelector('line.now'), nowX);
    }
    // Row labels in the stacked layout: the span each small chart covers.
    var rng = function (k, txt) { qsa('[data-fc-range="' + k + '"]').forEach(function (el) { el.textContent = txt; }); };
    rng('temp', Math.round(tMin) + '–' + Math.round(tMax) + ' °C');
    rng('sun', '0–' + sMax + ' W/m²');
    rng('wind', '0–' + Math.round(wMax) + ' m/s');
    if (svgW) {
      var wl = svgW.querySelector('polyline.wl');
      if (wl) wl.setAttribute('points', wp);
      var wa = svgW.querySelector('polygon.wa');
      if (wa) wa.setAttribute('points', '0,60 ' + wp + ' ' + W + ',60');
      var pastW = svgW.querySelector('rect.past');
      if (pastW) pastW.setAttribute('width', String(nowX));
      setLine(svgW.querySelector('line.now'), nowX);
    }
    var fcRoot = qs('.fc');
    if (fcRoot) fcRoot.style.setProperty('--now', ((start / Math.max(hours.length, 1)) * 100).toFixed(3) + '%');
    var yTemp = qs('.fc-temp') && qs('.fc-temp').previousElementSibling;
    if (yTemp && yTemp.classList.contains('fc-y')) {
      yTemp.innerHTML = '<span>' + Math.round(tMax) + '°</span><span>' + Math.round((tMin + tMax) / 2) + '°</span><span>' + Math.round(tMin) + '°</span>';
    }
    var yWind = qs('.fc-wind') && qs('.fc-wind').previousElementSibling;
    if (yWind && yWind.classList.contains('fc-y')) {
      yWind.innerHTML = '<span>' + Math.round(wMax) + '</span><span>' + Math.round(wMax / 2) + '</span><span>0</span>';
    }
    var y2 = qs('.fc-temp') && qs('.fc-temp').nextElementSibling;
    if (y2 && y2.classList.contains('fc-y2')) {
      y2.innerHTML = '<span>' + sMax + ' W/m²</span><span>' + (sMax / 2) + '</span><span>0</span>';
    }
    // Header: one weather icon per hour (every 3rd on narrow screens, CSS).
    var icons = qs('.fc-icons');
    if (icons) {
      var step = icons.hasAttribute('data-hourly') ? 1 : 3;
      icons.style.setProperty('--fc-cols', String(Math.ceil(hours.length / step)));
      var html = '';
      for (var i = 0; i < hours.length; i += step) {
        var sky = forecastSky(hours[i]);
        html += '<svg class="ic ' + sky + '"><title>' + esc(t('fc.sky.' + sky)) + '</title><use href="#i-' + sky + '"/></svg>';
      }
      icons.innerHTML = html;
    }
    var clock = function (h) {
      if (!h || !h.timestamp_s) return '';
      return String(new Date(h.timestamp_s * 1000).getHours()).padStart(2, '0') + ':00';
    };
    // Wind direction every 3 h. Meteorological degrees say where the wind comes
    // FROM; the arrow points where it blows TO.
    var dirs = qs('.fc-dirs');
    if (dirs) {
      dirs.style.setProperty('--fc-dirs', String(Math.ceil(hours.length / 3)));
      var dh = '';
      for (var k = 0; k < hours.length; k += 3) {
        var h = hours[k];
        var dg = h.wind_dir_deg != null ? Number(h.wind_dir_deg) : NaN;
        if (!isFinite(dg)) { dh += '<span></span>'; continue; }
        var tip = t('fc.dirTitle', { time: clock(h), speed: num(h.wind_ms, 0), dir: compassDir(dg) });
        dh += '<svg class="dir" viewBox="0 0 24 24" style="--deg:' + ((dg + 180) % 360) + 'deg"><title>' + esc(tip) + '</title><use href="#i-arrow"/></svg>';
      }
      dirs.innerHTML = dh;
    }
    // x-axis on the clock every 3 h: weekday at midnight, hour otherwise.
    // 03/09/15/21 are minor ticks (.m) that narrow screens hide.
    var xaxis = qs('.fc-x');
    if (xaxis && hours[0] && hours[0].timestamp_s) {
      var lang = (i18n._lang || document.documentElement.lang || 'en');
      var t0 = hours[0].timestamp_s;
      var spanH = Math.max(hours.length - 1, 1);
      var h0 = new Date(t0 * 1000).getHours();
      var xs = '';
      for (var j = (3 - (h0 % 3)) % 3; j <= spanH; j += 3) {
        var dd = new Date((t0 + j * 3600) * 1000);
        var hh = dd.getHours();
        var left = (j / spanH * 100).toFixed(2) + '%';
        xs += hh === 0
          ? '<span class="d" style="left:' + left + '">' + esc(dd.toLocaleDateString(lang, { weekday: 'short' })) + '</span>'
          : '<span' + (hh % 6 ? ' class="m"' : '') + ' style="left:' + left + '">' + String(hh).padStart(2, '0') + '</span>';
      }
      xaxis.classList.add('fc-x--abs');
      xaxis.innerHTML = xs;
    }
    // Preload window from active decisions (hours relative to decision start).
    var pre = preloadWindow(fc, start);
    qsa('.fc .pre').forEach(function (rect) {
      if (!pre) { rect.setAttribute('width', '0'); return; }
      rect.setAttribute('x', String((pre[0] / n) * W));
      rect.setAttribute('width', String(Math.max(0, ((pre[1] - pre[0]) / n) * W)));
    });
  }

  function preloadWindow(fc, start) {
    var a = null, b = null;
    (fc.decisions || []).forEach(function (d) {
      if (!d || !d.active) return;
      var x0 = start + (d.preload_start_h || 0);
      var x1 = start + (d.preload_end_h != null ? d.preload_end_h : (d.preload_start_h || 0) + 1);
      if (a == null || x0 < a) a = x0;
      if (b == null || x1 > b) b = x1;
    });
    return a == null || b == null || b <= a ? null : [a, b];
  }

  // Next heating › History: plan vs. reality (forecast.plan_vs_reality).
  function applyPvr(fc) {
    var plan = (fc && fc.plan_vs_reality) || [];
    var box = qs('[data-f="pvr"]');
    if (box) box.toggleAttribute('data-empty', !plan.length);
    var kw = function (v) { var x = num(v); return x === '—' ? x : esc(x) + ' <small>kW</small>'; };
    var last = plan[plan.length - 1] || {};
    setBind('plan.planned', plan.length ? kw(last.planned_kw) : '—');
    setBind('plan.actual', plan.length ? kw(last.actual_kw) : '—');
    var plot = qs('[data-bind-pvr]');
    if (!plot) return;
    var max = 0;
    plan.forEach(function (p) { max = Math.max(max, Math.abs(Number(p.planned_kw) || 0), Math.abs(Number(p.actual_kw) || 0)); });
    if (max <= 0) max = 1;
    plot.parentNode.style.setProperty('--bars-n', String(Math.max(plan.length, 1)));
    plot.innerHTML = plan.map(function (p) {
      return '<div class="col"><i class="plan" style="--plan:' + Math.round(Math.abs(Number(p.planned_kw) || 0) / max * 100) + '"></i>' +
        '<i class="act" style="--act:' + Math.round(Math.abs(Number(p.actual_kw) || 0) / max * 100) + '"></i></div>';
    }).join('');
  }

  function applyForecast(fc) {
    if (!fc) return;
    state.forecast = fc;
    renderHeatmap();  // charge badges follow the forecast
    var hours = fc.hours || [];
    var start = (fc.cache && fc.cache.decision_start_index >= 0) ? fc.cache.decision_start_index : 0;
    var nowHour = hours[start] || hours[0];
    if (nowHour) {
      setBind('forecast.temp', esc(num(nowHour.temp_c)) + ' <small>°C</small>');
      if (finite(nowHour.temp_c)) setText('house.outdoor', num(nowHour.temp_c) + ' °C');
      setText('tile.weatherVal', deg(nowHour.temp_c));
      state.outdoorC = finite(nowHour.temp_c) ? Number(nowHour.temp_c) : null;
      setText('tile.weatherSub', finite(nowHour.wind_ms) ? t('tile.weatherSub', { w: num(nowHour.wind_ms, 0) }) : '');
    }
    if (fc.cache) {
      setBind('forecast.windmax', esc(num(fc.cache.max_wind_ms, 0)) + ' <small>m/s</small>');
      setBind('forecast.tmin', esc(num(fc.cache.min_temp_c)) + ' <small>°C</small>');
      var ageMin = fc.last_fetch_age_s != null ? Math.round(Number(fc.last_fetch_age_s) / 60) : null;
      setText('fc.sub', t('fc.sub', { model: fc.cache.provider_timezone || 'Open-Meteo', time: ageMin != null ? (ageMin + ' min') : '—' }));
    }
    var active = (fc.decisions || []).filter(function (d) { return d && d.active && d.offset_c > 0; });
    setShow('fc.preload', active.length > 0);
    setText('tile.weatherStatus', active.length ? t('tile.weatherStatus', { n: active.length }) : t('tile.weatherStatusNone'));
    if (active.length) {
      var maxOff = active.reduce(function (m, d) { return Math.max(m, Number(d.offset_c) || 0); }, 0);
      var names = active.slice(0, 4).map(function (d) { return d.name || d.room_id; }).join(', ');
      setBind('fc.preloadMsg', '<b>' + esc(t('fc.preloadMsg', { v: num(maxOff) })) + '</b> ' + esc(names));
    }
    // Home tile: the next 24 hours from now, with the preload window hatched.
    var viz = qs('[data-bind-viz="weather"]');
    var next = hours.slice(start, start + 25).map(function (h) { return h.temp_c; });
    var ok = next.filter(finite).length >= 2;
    var tile = qs('[data-tile="weather"]');
    if (tile) tile.toggleAttribute('data-empty', !ok);
    if (viz && ok) {
      var r = axisRange([next], 3);
      var pl = viz.querySelector('polyline.t');
      var wp = seriesPoints(next, 240, 64, r.lo, r.hi);
      if (pl) pl.setAttribute('points', wp);
      var wa = viz.querySelector('path.wa');
      if (wa) wa.setAttribute('d', 'M0,64 L' + wp.split(' ').join(' L') + ' L' + wp.split(' ').pop().split(',')[0] + ',64Z');
      var pre = preloadWindow(fc, 0);
      var rect = viz.querySelector('rect.pre');
      if (rect) {
        var n2 = Math.max(next.length - 1, 1);
        var x0 = pre ? Math.max(0, Math.min(n2, pre[0])) : 0, x1 = pre ? Math.max(0, Math.min(n2, pre[1])) : 0;
        rect.setAttribute('x', String(x0 / n2 * 240));
        rect.setAttribute('width', String(Math.max(0, (x1 - x0) / n2 * 240)));
      }
    }
    // Weather › History: the hours before now that the forecast still carries.
    var pastBox = qs('[data-f="wxpast"]');
    var past = hours.slice(0, start + 1).map(function (h) { return h.temp_c; });
    var hasPast = past.filter(finite).length >= 2;
    if (pastBox) {
      pastBox.toggleAttribute('data-empty', !hasPast);
      if (hasPast) {
        fText(pastBox, 'ttl', t('wx.pastN', { n: past.length - 1 }));
        updateSvgSeries(qs('svg', pastBox), past, []);
        var hU = i18n._h || 'h';
        fHtml(pastBox, 'axis', axisHtml(['−' + (past.length - 1) + ' ' + hU, '−' + Math.round((past.length - 1) / 2) + ' ' + hU, t('trend.now')]));
      }
    }
    applyForecastCharts(fc);
    applyPvr(fc);
  }

  function applyCommands(data) {
    var commands = (data && data.commands) || [];
    var log = qs('[data-bind="log"]');
    if (!log) return;
    if (!commands.length) { log.textContent = '—'; return; }
    log.innerHTML = commands.slice(0, 12).map(function (c) {
      var ts = '—';
      if (c.created_at_epoch_s) {
        var d = new Date(Number(c.created_at_epoch_s) * 1000);
        ts = String(d.getHours()).padStart(2, '0') + ':' + String(d.getMinutes()).padStart(2, '0');
      }
      var kind = c.source || 'cmd';
      var detail = c.reason || c.result || '';
      if (c.room_id) detail += (detail ? ' · ' : '') + c.room_id;
      var cls = kind === 'forecast' ? 'info' : (c.result && String(c.result).indexOf('fail') === 0 ? 'bad' : '');
      return ts + '  <span class="' + cls + '">' + esc(kind) + '</span> ' + esc(detail);
    }).join('\n');
  }

  function applySettings(s) {
    if (!s) return;
    state.settings = s;
    var coord = s.coordinator || {};
    var name = coord.name || s.device_name || s.name || 'Lune Touch';
    var place = coord.site_label || s.location || s.place || name;
    setText('device.about.name', name);
    setText('device.about.place', place);
    var df = qs('form[data-save="settings"]');
    if (df && !formBusy(df)) {
      putVal(qs('#dev_name'), name);
      var idleS = (s.display && s.display.idle_timeout_s != null) ? Number(s.display.idle_timeout_s)
        : (s.display_idle_min != null ? Number(s.display_idle_min) * 60 : NaN);
      if (isFinite(idleS)) putVal(qs('#dev_idle'), String(Math.round(idleS / 60)));
      if (df.luneResnap) df.luneResnap();
    }
    var wx = s.weather || s.forecast || {};
    var wf = qs('form[data-save="weather"]');
    if (wf && !formBusy(wf)) {
      if (s.forecast && s.forecast.latitude != null) putVal(qs('#wx_lat'), s.forecast.latitude);
      if (s.forecast && s.forecast.longitude != null) putVal(qs('#wx_lon'), s.forecast.longitude);
      if (wx.max_boost_c != null) putVal(qs('#wx_boost'), Number(wx.max_boost_c).toFixed(1));
      if (wf.luneResnap) wf.luneResnap();
    }
  }

  function formatUptime(sec) {
    if (sec == null || sec !== sec) return '—';
    var s = Math.max(0, Math.floor(Number(sec)));
    var d = Math.floor(s / 86400);
    if (d >= 1) return d + ' <small>' + t('common.days') + '</small>';
    var h = Math.floor(s / 3600);
    if (h >= 1) return h + ' <small>h</small>';
    return Math.floor(s / 60) + ' <small>min</small>';
  }

  function applyDiagnostics(d) {
    if (!d) return;
    state.diagnostics = d;
    var net = d.network || d;
    // Never show raw firmware strings (DESIGN.md 6.8): translate, keep the raw
    // error only as a tooltip for installers.
    var com = d.commissioning || {};
    setText('diag.nodes', com.trusted_nodes != null
      ? String(com.reachable_trusted_nodes || 0) + ' / ' + String(com.trusted_nodes) : '—');
    var poll = d.polling || {};
    var pollEl = qs('[data-bind="diag.poll"]');
    if (pollEl) {
      var fails = Number(poll.fail) || 0;
      pollEl.textContent = poll.fail == null ? '—' : (fails ? t('diag.pollFail', { n: fails }) : t('diag.pollOk'));
      pollEl.classList.toggle('c-warn', !!fails);
      if (poll.last_error) pollEl.title = poll.last_error; else pollEl.removeAttribute('title');
    }
    var ota = d.ota;
    var otaState = typeof ota === 'string' ? ota : (ota && ota.state);
    setText('diag.ota', otaState ? (i18n['diag.ota.' + otaState] || t('status.unknown')) : '—');
    if (net.ip) {
      setText('device.about.ip', net.ip);
      setText('svc.hostIp', location.host && location.host !== net.ip ? location.host + ' · ' + net.ip : net.ip);
      if (state.ownIp !== net.ip) { state.ownIp = net.ip; renderDeviceMenu(); }
    }
    if (net.mac) setText('device.about.mac', net.mac);
    if (net.version || net.firmware) {
      var fw = net.version || net.firmware;
      state.fwInstalled = fw;
      setText('device.about.firmware', fw);
      setText('fw.installed', fw);
    }
    if (net.esphome) setText('device.about.esphome', net.esphome);
    if (net.uptime_s != null || net.uptime != null) {
      setBind('device.about.uptime', formatUptime(net.uptime_s != null ? net.uptime_s : net.uptime));
    }
  }

  function copyDiagnostics(btn) {
    var lines = [];
    qsa('.device-about .kv div').forEach(function (row) {
      var dt = row.querySelector('dt');
      var dd = row.querySelector('dd');
      if (dt && dd) lines.push(dt.textContent + ': ' + dd.textContent.replace(/\s+/g, ' ').trim());
    });
    var text = lines.join('\n');
    function done() {
      if (!btn) return;
      var prev = btn.textContent;
      btn.textContent = t('device.copied');
      setTimeout(function () { btn.textContent = prev; }, 1600);
    }
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(done).catch(function () { window.prompt(t('device.copyDiag'), text); });
    } else {
      window.prompt(t('device.copyDiag'), text);
      done();
    }
  }

  async function refresh() {
    try {
      var paths = ['/overview', '/nodes', '/zones', '/strategy', '/forecast', '/plan', '/heat-source', '/heat-source/control', '/odin/mqtt', '/prices', '/settings', '/diagnostics', '/commands', '/wifi'];
      var results = await Promise.all(paths.map(function (p) {
        return get(p).then(function (v) { return { p: p, v: v }; }).catch(function () { return { p: p, v: null }; });
      }));
      var map = {};
      results.forEach(function (r) { map[r.p] = r.v; });
      applyDiagnostics(map['/diagnostics']);   // first: uptime for "last seen"
      applyOverview(map['/overview']);
      state.strategy = map['/strategy'] || state.strategy;
      var nodes = (map['/nodes'] && map['/nodes'].nodes) || [];
      applyNodes(nodes);
      applyHierarchy(map['/zones'] || {}, nodes);
      applyForecast(map['/forecast']);
      applyHeat(map['/heat-source']);
      applyHeatPlan(map['/plan']);
      applyHeatControl(map['/heat-source/control']);
      applyOdinMqtt(map['/odin/mqtt']);
      applyPrices(map['/prices']);
      applyWifi(map['/wifi']);
      applySettings(map['/settings']);
      applyCommands(map['/commands']);
      renderHome();
    } catch (e) {
      console.warn('refresh failed', e);
    }
  }

  /* ---- Firmware and backup -------------------------------------------------- */
  function formStatus(form, msg, ok) {
    var el2 = form && form.querySelector('[data-form-status]');
    if (!el2) return;
    el2.textContent = msg || '';
    el2.className = 'note' + (ok === false ? ' c-bad' : (ok ? ' c-ok' : ''));
  }

  function normVer(s) { return String(s || '').replace(/^v/i, '').trim(); }

  function verNewer(latest, installed) {
    var a = normVer(latest).split(/[.+-]/);
    var b = normVer(installed).split(/[.+-]/);
    var n = Math.max(a.length, b.length);
    for (var i = 0; i < n; i++) {
      var x = parseInt(a[i], 10); var y = parseInt(b[i], 10);
      if (!isFinite(x)) x = 0;
      if (!isFinite(y)) y = 0;
      if (x > y) return true;
      if (x < y) return false;
    }
    return normVer(latest) !== normVer(installed) && String(latest || '').length > 0;
  }

  function pickReleaseAsset(assets, tag) {
    var list = Array.isArray(assets) ? assets : [];
    function named(re) {
      for (var i = 0; i < list.length; i++) {
        if (re.test(String(list[i] && list[i].name || ''))) return list[i];
      }
      return null;
    }
    var asset = named(/^lune-touch.*\.ota\.bin$/i) || named(/\.ota\.bin$/i) || named(/\.bin$/i);
    if (asset && asset.browser_download_url) return { name: String(asset.name), url: String(asset.browser_download_url) };
    var name = 'lune-touch-' + (tag || 'latest') + '.ota.bin';
    return { name: name, url: 'https://github.com/birkemosen/lune-coordinator/releases/latest/download/' + name };
  }

  async function fetchLatestRelease() {
    if (window.LUNE_TOUCH_MOCK) return { tag: 'v0.2.0', asset: { name: 'lune-touch-v0.2.0.ota.bin', url: 'mock://lune-touch-v0.2.0.ota.bin' } };
    var res = await fetch(RELEASE_LATEST_API, { cache: 'no-store', headers: { Accept: 'application/vnd.github+json' } });
    if (res.status === 404) { var err404 = new Error('no_releases'); err404.code = 'no_releases'; throw err404; }
    if (!res.ok) { var err = new Error('http_' + res.status); err.code = 'http'; throw err; }
    var payload = await res.json();
    var tag = String(payload && payload.tag_name || '');
    if (!tag) { var empty = new Error('no_releases'); empty.code = 'no_releases'; throw empty; }
    return { tag: tag, asset: pickReleaseAsset(payload.assets, tag) };
  }

  // State-dependent actions (DESIGN.md 6.1b): install only when a newer
  // release is known; upload/import only with a chosen file.
  function syncFwBackupButtons() {
    var ota = qs('#ota_file');
    var up = qs('form[data-save="firmware"] button[value="upload"]');
    if (up) up.disabled = !(ota && ota.files && ota.files[0]);
    var bf = qs('#backup_file');
    var imp = qs('form[data-save="backup"] [popovertarget="cf-import"]');
    if (imp) imp.disabled = !(bf && bf.files && bf.files[0]);
    var inst = qs('form[data-save="firmware"] button[value="install"]');
    if (inst) inst.hidden = !(state.fwAsset && state.fwAsset.url && verNewer(state.fwLatest, state.fwInstalled));
  }

  function uploadFirmware(file, onProgress) {
    if (window.LUNE_TOUCH_MOCK) {
      return new Promise(function (resolve) {
        var pct = 0;
        var step = setInterval(function () {
          pct = Math.min(100, pct + 25);
          if (onProgress) onProgress(pct);
          if (pct >= 100) { clearInterval(step); resolve('ok'); }
        }, 120);
      });
    }
    return new Promise(function (resolve, reject) {
      var body = new FormData();
      body.append('update', file, file.name || 'firmware.bin');
      var req = new XMLHttpRequest();
      req.open('POST', OTA_UPLOAD_PATH);
      req.upload.onprogress = function (ev) {
        if (onProgress && ev.lengthComputable) onProgress(Math.min(100, Math.round(ev.loaded / ev.total * 100)));
      };
      req.onload = function () {
        var text = String(req.responseText || '');
        if (req.status >= 200 && req.status < 300 && !/fail/i.test(text)) resolve(text || 'ok');
        else reject(new Error('OTA upload rejected: ' + req.status + ' ' + text));
      };
      req.onerror = function () { reject(new Error('OTA upload connection lost')); };
      req.send(body);
    });
  }

  async function buildBackupEnvelope() {
    var paths = ['/settings', '/heat-source', '/circulation', '/forecast'];
    var parts = await Promise.all(paths.map(function (p) {
      return get(p).then(function (v) { return { p: p, v: v }; }).catch(function () { return { p: p, v: null }; });
    }));
    var map = {};
    parts.forEach(function (r) { map[r.p] = r.v; });
    return {
      _type: BACKUP_TYPE,
      _version: BACKUP_VERSION,
      exported_at: new Date().toISOString(),
      firmware: state.fwInstalled || '',
      settings: map['/settings'] || {},
      heat_source: map['/heat-source'] || {},
      circulation: map['/circulation'] || {},
      forecast: map['/forecast'] || {}
    };
  }

  function downloadJson(filename, obj) {
    var blob = new Blob([JSON.stringify(obj, null, 2)], { type: 'application/json' });
    var a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = filename;
    a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); }, 1000);
  }

  function unwrapBackup(payload) {
    if (payload && payload._type) return payload;
    if (payload && payload.data && payload.data._type) return payload.data;
    return payload && payload.data ? payload.data : payload;
  }

  async function applyBackupEnvelope(envelope) {
    var env = unwrapBackup(envelope);
    if (!env || env._type !== BACKUP_TYPE) throw new Error('not_a_lune_backup');
    var applied = 0;
    var s = env.settings || {};
    var coord = s.coordinator || {};
    var display = s.display || {};
    if (coord.name != null || display.idle_timeout_s != null) {
      await post('/settings', {
        name: coord.name || '',
        display_idle_timeout_s: display.idle_timeout_s != null ? display.idle_timeout_s : 300
      });
      applied++;
    }
    var hs = env.heat_source || {};
    if (hs && (hs.type || hs.host || (hs.asgard && hs.asgard.host))) {
      var uiType = (hs.type === 'generic_http' || hs.type === 'http') ? 'generic_http' : 'asgard';
      var payload = {
        type: uiType,
        enabled: hs.enabled ? '1' : '0',
        host: hs.host || (hs.asgard && hs.asgard.host) || '',
        port: hs.port || 80,
        weighted_temperature_variable: hs.weighted_temperature_variable || '',
        push_interval_s: hs.push_interval_s || 60
      };
      if (uiType === 'generic_http') {
        payload.write_url_template = hs.write_url_template || '';
        payload.read_url_template = hs.read_url_template || '';
      } else {
        payload.climate_entity = hs.climate_entity || '';
        payload.target_sync_enabled = hs.target_sync_enabled ? '1' : '0';
        payload.odin_plan_enabled = hs.odin_plan_enabled ? '1' : '0';
      }
      if (payload.host) { await post('/heat-source/settings', payload); applied++; }
    }
    var circ = env.circulation || {};
    if (circ.host) {
      await post('/circulation/settings', {
        enabled: '1', host: circ.host || '', port: circ.port || 80,
        flow_entity: circ.flow_entity || '', head_entity: circ.head_entity || '', power_entity: circ.power_entity || ''
      });
      applied++;
    }
    var fc = env.forecast || {};
    var sf = (env.settings && env.settings.forecast) || {};
    var lat = fc.latitude != null ? fc.latitude : sf.latitude;
    var lon = fc.longitude != null ? fc.longitude : sf.longitude;
    if (lat != null && lon != null) {
      await post('/forecast/settings', { latitude: lat, longitude: lon });
      applied++;
    }
    // Max preload boost has its own endpoint (POST /weather/settings).
    var wx = (env.settings && env.settings.weather) || {};
    var boost = wx.max_boost_c != null ? wx.max_boost_c : fc.max_boost_c;
    if (boost != null) { await post('/weather/settings', { max_boost_c: boost }); applied++; }
    return { applied: applied };
  }

  /* ---- Save (lune:save from lune-forms.js) ----------------------------------
     Forms with data-patch send only the changed fields (detail.changed). The
     firmware has no PATCH method (GET/POST only); its POST endpoints treat
     absent keys as unchanged, so a "patch" is a POST with only those keys plus
     what the endpoint requires. /zones/{room}/room and /forecast/settings need
     the whole object: there the changes are merged into the known values. */
  function bit(v) { return v === true || v === '1' || v === 'on' ? '1' : '0'; }

  function has(ch, k) { return !!ch && Object.prototype.hasOwnProperty.call(ch, k); }

  function fval(form, name) {
    var el2 = form.querySelector('[name="' + name + '"]');
    if (!el2) return undefined;
    if (el2.type === 'checkbox') return el2.checked;
    if (el2.type === 'radio') { var r = form.querySelector('[name="' + name + '"]:checked'); return r ? r.value : undefined; }
    return el2.value;
  }

  async function saveHeatConnection(form, ch) {
    syncHsTypedFields();
    var uiType = fval(form, 'hs_type') || 'asgard';
    var api = uiType === 'http' ? 'generic_http' : 'asgard';
    var pre = uiType === 'http' ? 'http_' : 'asgard_';
    // A new type needs its whole connection (the device keeps one host/port).
    var all = has(ch, 'hs_type');
    var want = function (k) { return all || has(ch, k); };
    var p = {}, c = {};
    if (want('enabled')) p.enabled = bit(fval(form, 'enabled'));
    [['host', 'host'], ['port', 'port'], ['push_interval_s', 'push_interval_s'],
      ['weighted_temperature_variable', 'weighted_temperature_variable']].forEach(function (m) {
      if (want(pre + m[0])) p[m[1]] = String(fval(form, pre + m[0]) || '').trim();
    });
    if (all && !p.host) throw new Error(t('hs.host') + ': —');
    if (api === 'generic_http') {
      if (want('write_url_template') || want('read_url_template') || Object.keys(p).length) {
        p.write_url_template = String(fval(form, 'write_url_template') || '').trim();
        p.read_url_template = String(fval(form, 'read_url_template') || '').trim();
        if (!p.write_url_template || !p.read_url_template) throw new Error(t('hs.writeUrl') + ' / ' + t('hs.readUrl'));
      }
      ['target_url_template', 'heat_request_url_template', 'curve_offset_url_template', 'curve_gain', 'curve_max_offset_c'].forEach(function (k) {
        if (has(ch, k)) c[k] = fval(form, k);
      });
    } else {
      if (has(ch, 'odin_host')) p.odin_host = String(fval(form, 'odin_host') || '').trim();
      if (has(ch, 'mqtt_enabled')) p.mqtt_enabled = bit(fval(form, 'mqtt_enabled'));
      ['mqtt_host', 'mqtt_port', 'mqtt_username', 'mqtt_topic_prefix', 'mqtt_hp_id'].forEach(function (k) {
        if (has(ch, k)) p[k] = String(fval(form, k) || '').trim();
      });
      // Empty password field = keep the stored one (never echoed back).
      if (has(ch, 'mqtt_password') && fval(form, 'mqtt_password')) p.mqtt_password = String(fval(form, 'mqtt_password'));
    }
    if (Object.keys(p).length) {
      // The endpoint rejects a body without one of its core keys ("settings_required");
      // the type is always safe to repeat.
      p.type = api;
      await post('/heat-source/settings', p);
    }
    if (Object.keys(c).length) {
      var saved = await post('/heat-source/control', c);
      if (saved && saved.control) applyHeatControl(saved.control);
    }
    var pw = form.querySelector('[name="mqtt_password"]');
    if (pw && has(ch, 'mqtt_password')) { pw.value = ''; pw.defaultValue = ''; }
  }

  // House balance: one read-value row per V6 board (scale Touch sends) under the switch.
  function drawHouseBalance(hb) {
    var sw = qs('input[name="house_balance_enabled"]');
    var body = sw && sw.closest('.gated') && sw.closest('.gated').querySelector('.gated-body');
    if (!body) return;
    var boards = (hb && hb.boards) || [];
    body.innerHTML = boards.map(function (b) {
      var none = b.worst_kpa == null;
      var hint = none ? t('heat.houseBalanceNone') : t('heat.houseBalanceRow', { kpa: num(b.worst_kpa, 1) });
      return '<div class="setting"><div class="setting-label"><span>' + esc(b.name || b.node_id || '—') +
        '</span><small>' + hint + '</small></div><div class="setting-control"><span class="setting-value">' +
        (none ? '—' : num(b.scale, 2)) + '</span></div></div>';
    }).join('');
  }

  async function saveHeatBehavior(form, ch) {
    var p = {}, c = {};
    if (has(ch, 'target_sync_enabled')) p.target_sync_enabled = bit(fval(form, 'target_sync_enabled'));
    if (has(ch, 'climate_entity')) p.climate_entity = String(fval(form, 'climate_entity') || '').trim();
    if (has(ch, 'odin_plan_enabled')) p.odin_plan_enabled = bit(fval(form, 'odin_plan_enabled'));
    if (has(ch, 'house_balance_enabled')) p.house_balance_enabled = bit(fval(form, 'house_balance_enabled'));
    if (has(ch, 'odin_control_enabled')) c.odin_enabled = bit(fval(form, 'odin_control_enabled'));
    if (has(ch, 'odin_max_lift_c')) c.odin_max_lift_c = fval(form, 'odin_max_lift_c');
    if (Object.keys(p).length) await post('/heat-source/settings', p);
    if (Object.keys(c).length) {
      var saved = await post('/heat-source/control', c);
      if (saved && saved.control) applyHeatControl(saved.control);
    }
  }

  // Full atomic room update (POST /zones/{room}/room): the changed fields merged
  // into everything else the device knows about the room.
  async function saveRoom(form, ch) {
    var slot = Number(form.getAttribute('data-room-slot'));
    var r = state.roomSlots[slot - 1];
    if (!r) throw new Error(t('status.unknown'));
    var z = r.primary;
    var room = z.room || {}, f = z.forecast || {}, c = z.comfort || {}, sc = z.schedule || {};
    var k = 'room:r' + slot + ':';
    var pick = function (field, cur) { return has(ch, k + field) ? fval(form, k + field) : cur; };
    var include = pick('include', room.include_in_house_temperature !== false);
    var body = {
      expected_revision: room.revision || 0,
      total_area_m2: room.total_area_m2 || 0,
      physical_weight: Number(pick('weight', room.physical_weight != null ? room.physical_weight : 1)),
      include_in_house_temperature: include === true || include === 'on' ? 1 : 0,
      comfort_setpoint_c: c.setpoint_c != null ? c.setpoint_c : (z.setpoint_c != null ? z.setpoint_c : 21),
      comfort_bias_c: c.bias_c || 0,
      priority: c.priority || 0,
      schedule_enabled: sc.enabled ? 1 : 0,
      schedule_day_mask: sc.day_mask || 0,
      schedule_start_min: sc.start_min || 0,
      schedule_end_min: sc.end_min || 0,
      schedule_setpoint_c: sc.setpoint_c != null ? sc.setpoint_c : 21,
      exterior_walls: f.exterior_walls || 0,
      wind_exposure: Number(pick('wind', f.wind_exposure != null ? f.wind_exposure : 0.5)),
      solar_gain: Number(pick('solar', f.solar_gain != null ? f.solar_gain : 0.3)),
      thermal_lead_h: f.thermal_lead_h || 4,
      max_offset_c: f.max_offset_c != null ? f.max_offset_c : 1.5
    };
    await post('/zones/' + encodeURIComponent(r.room_id) + '/room', body);
  }

  async function onSave(detail) {
    var form = detail.form;
    var key = detail.key || form.getAttribute('data-save');
    var data = formObj(detail.data || new FormData(form));
    var ch = detail.changed || {};
    var submitter = detail.submitter || null;
    try {
      if (key === 'house-target') {
        await post('/strategy', { house_target_c: parseNum(data.house_target) });
        renderHome();
      } else if (key === 'rooms') {
        await saveRoom(form, ch);
      } else if (key === 'heat_source') {
        await saveHeatConnection(form, ch);
        await saveHeatBehavior(form, ch);
      } else if (key === 'weather') {
        var wxAll = !Object.keys(ch).length;   // no patch info: save both parts
        if (wxAll || has(ch, 'wx_boost')) await post('/weather/settings', { max_boost_c: fval(form, 'wx_boost') });
        // Latitude and longitude are both required by the endpoint.
        if (wxAll || has(ch, 'latitude') || has(ch, 'longitude'))
          await post('/forecast/settings', { latitude: fval(form, 'latitude'), longitude: fval(form, 'longitude') });
      } else if (key === 'add-node') {
        var addr = String(data.host || '').trim();
        if (!addr) throw new Error(t('ctrl.host'));
        var nodePayload = /^\d{1,3}(\.\d{1,3}){3}$/.test(addr) ? { ip: addr } : { hostname: addr };
        var storedNode = await post('/nodes', nodePayload);
        var newId = storedNode && storedNode.node_id;
        if (data.name && newId) await post('/nodes/' + encodeURIComponent(newId) + '/profile', { name: String(data.name).trim() });
        form.reset();
        formStatus(form, t('rt.savedOk'), true);
      } else if (key === 'circulation') {
        if (has(ch, 'house_balance_enabled'))
          await post('/heat-source/settings', { house_balance_enabled: bit(fval(form, 'house_balance_enabled')) });
        var balanceOnly = Object.keys(ch).length === 1 && has(ch, 'house_balance_enabled');
        if (!balanceOnly) await post('/circulation/settings', {
          enabled: '1',
          host: data.host || '',
          port: data.pump_port || data.port || 80,
          flow_entity: data.flow_entity || '',
          head_entity: data.head_entity || '',
          power_entity: data.power_entity || ''
        });
      } else if (key === 'prices') {
        priceSchedSync();
        var hiddenSched = form.querySelector('input[name="grid_schedule"]');
        var pricePayload = {
          enabled: data.enabled ? '1' : '0',
          model: data.model || 'touch',
          zone: data.zone || 'DK1',
          spot_source: data.spot_source || 'eds',
          spot_fixed_eur: data.spot_fixed_eur,
          currency: data.currency || 'DKK',
          fx: data.fx,
          grid_source: data.grid_source || 'none',
          grid_gln: String(data.grid_gln || '').trim(),
          grid_code: String(data.grid_code || '').trim(),
          grid_schedule: hiddenSched ? hiddenSched.value : '',
          system_source: data.system_source || 'fixed',
          system_fixed: data.system_fixed,
          energy_tax: data.energy_tax,
          markup: data.markup,
          vat_pct: data.vat_pct,
          odin_mode: data.odin_mode || 'dynamic',
          odin_source: data.odin_source || 'energy_charts',
          odin_fixed_price: data.odin_fixed_price
        };
        var token = String(data.entsoe_token || '').trim();
        if (token) pricePayload.entsoe_token = token;
        await post('/prices/settings', pricePayload);
        var tokEl = form.querySelector('input[name="entsoe_token"]');
        if (tokEl) tokEl.value = '';
      } else if (key === 'wifi') {
        var ssid = String(data.ssid || '').trim();
        if (!ssid) throw new Error(t('wifi.needSsid'));
        applyWifi(await post('/wifi', { ssid: ssid, password: data.password || '' }));
        var pwEl = qs('#wifi_password');
        if (pwEl) pwEl.value = '';
        setText('wifi.status', t('wifi.sent'));
        // Follow the switch; the page may drop while the radio reconnects.
        var tries = 0;
        var follow = setInterval(function () {
          get('/wifi').then(function (w) {
            applyWifi(w);
            if (!w || w['switch'] !== 'pending' || ++tries > 30) clearInterval(follow);
          }).catch(function () { if (++tries > 30) clearInterval(follow); });
        }, 2000);
      } else if (key === 'settings') {
        var idleMin = Number(data.dev_idle);
        await post('/settings', { name: data.name || '', display_idle_timeout_s: isFinite(idleMin) ? Math.round(idleMin * 60) : 300 });
      } else if (key === 'firmware') {
        var action = data.action || (submitter && submitter.value) || '';
        if (action === 'check') {
          formStatus(form, t('csys.fwChecking'));
          try {
            var info = await fetchLatestRelease();
            state.fwLatest = info.tag;
            state.fwAsset = info.asset;
            setText('fw.latest', info.tag || '—');
            syncFwBackupButtons();
            formStatus(form, verNewer(info.tag, state.fwInstalled) ? t('csys.fwAvailable') : t('csys.fwUpToDate'), true);
          } catch (ce) {
            state.fwLatest = null;
            state.fwAsset = null;
            setText('fw.latest', '—');
            syncFwBackupButtons();
            formStatus(form, ce && ce.code === 'no_releases' ? t('csys.fwNoReleases') : t('csys.fwCheckFailed'), false);
          }
          return;
        }
        if (action === 'install') {
          if (!state.fwAsset || !state.fwAsset.url) throw new Error(t('csys.fwCheckFailed'));
          formStatus(form, t('csys.fwInstalling'));
          var bin;
          if (window.LUNE_TOUCH_MOCK) {
            bin = new Blob(['mock-ota'], { type: 'application/octet-stream' });
            bin.name = state.fwAsset.name || 'firmware.bin';
          } else {
            var binRes = await fetch(state.fwAsset.url, { cache: 'no-store' });
            if (!binRes.ok) throw new Error(t('csys.fwUploadFailed'));
            bin = await binRes.blob();
            bin.name = state.fwAsset.name || 'firmware.bin';
          }
          await uploadFirmware(bin, function (pct) { formStatus(form, t('csys.fwUploading') + ' ' + pct + '%'); });
          formStatus(form, t('csys.fwUploadDone'), true);
          return;
        }
        if (action === 'upload') {
          var fileInput = qs('#ota_file');
          var file = fileInput && fileInput.files && fileInput.files[0];
          if (!file) throw new Error(t('csys.fwNoFile'));
          formStatus(form, t('csys.fwUploading'));
          await uploadFirmware(file, function (pct) { formStatus(form, t('csys.fwUploading') + ' ' + pct + '%'); });
          formStatus(form, t('csys.fwUploadDone'), true);
        }
        return;
      } else if (key === 'backup') {
        var bakAction = data.action || (submitter && submitter.value) || '';
        if (bakAction === 'export') {
          formStatus(form, t('csys.backupExporting'));
          var envelope = await buildBackupEnvelope();
          var stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
          downloadJson('lune-touch-settings-' + stamp + '.json', envelope);
          formStatus(form, t('csys.backupExported'), true);
          return;
        }
        if (bakAction === 'import') {
          var bakInput = qs('#backup_file');
          var bakFile = bakInput && bakInput.files && bakInput.files[0];
          if (!bakFile) throw new Error(t('csys.backupInvalid'));
          formStatus(form, t('csys.backupImporting'));
          await applyBackupEnvelope(JSON.parse(await bakFile.text()));
          formStatus(form, t('csys.backupImported'), true);
          refresh();
        }
        return;
      }
      if (form.luneSaved) form.luneSaved(true);
      refresh();
    } catch (e) {
      if (key === 'firmware' || key === 'backup' || key === 'add-node') {
        formStatus(form, (e && e.message) || t(key === 'firmware' ? 'csys.fwUploadFailed' : 'rt.saveFailed'), false);
        if (form.luneSaved && key === 'add-node') form.luneSaved(false, (e && e.message) || undefined);
        return;
      }
      if (form.luneSaved) form.luneSaved(false, (e && e.message) || undefined);
    }
  }

  /* A switch that saves at once sends only itself (changed = {name: value}).
     Afterwards only the switch's entry in lune-forms' snapshot (f._snap, keyed
     "name:value" for checkboxes) and its default are moved to the new value,
     so the form's other unsaved fields stay dirty and keep their own baseline;
     an input event lets lune-forms repaint the save bar. On failure the switch
     goes back and the save bar says why. */
  async function saveSwitchNow(sw) {
    var form = sw.form;
    var key = form.getAttribute('data-save');
    var on = sw.checked;
    var ch = {};
    ch[sw.name] = on;
    var repaint = function () { sw.dispatchEvent(new Event('input', { bubbles: true })); };
    try {
      if (key === 'heat_source') { await saveHeatConnection(form, ch); await saveHeatBehavior(form, ch); }
      else if (key === 'circulation' && sw.name === 'house_balance_enabled')
        await post('/heat-source/settings', { house_balance_enabled: on ? 1 : 0 });
      else if (key === 'rooms') await saveRoom(form, ch);
      else if (key === 'prices' && sw.name === 'enabled') await post('/prices/settings', { enabled: on ? '1' : '0' });  // absent keys = unchanged
      else return;   // no single-field path: the switch waits for the save bar
      if (form._snap) form._snap[sw.name + ':' + sw.value] = on;
      sw.defaultChecked = on;
      repaint();
      refresh();
    } catch (e) {
      sw.checked = !on;
      repaint();
      var st = form.querySelector('.save-status');
      if (st) st.textContent = (e && e.message) || t('rt.saveFailed');
    }
  }

  /* ---- Actions (data-action) ------------------------------------------------ */
  async function onAction(btn) {
    var action = btn.getAttribute('data-action');
    var id = btn.getAttribute('data-id');
    // preventDefault on the click also stops popovertargetaction="hide": close
    // the confirmation ourselves once the action is accepted.
    var pop = btn.closest('.confirm-pop');
    if (pop && pop.matches(':popover-open')) pop.hidePopover();
    try {
      if (action === 'price-push') { await runPricePush(btn, false); return; }
      if (action === 'price-odin-write') { await runPricePush(btn, true); return; }
      if (action === 'price-zone-defaults') { await applyZoneDefaults(); return; }
      if (action === 'price-sched-add') {
        var cur = priceSchedRead();
        var used = {};
        cur.forEach(function (b) { used[b.h] = true; });
        var nh = 0;
        while (used[nh] && nh < 23) nh++;
        if (used[nh]) return;
        cur.push({ h: nh, v: cur.length ? cur[cur.length - 1].v : 0 });
        cur.sort(function (a, b) { return a.h - b.h; });
        priceSchedRows(cur);
        priceSchedSync();
        return;
      }
      if (action === 'price-sched-remove') {
        var tr = btn.closest('tr');
        if (tr && qsa('[data-bind-price-sched] tr').length > 1) tr.remove();
        priceSchedSync();
        return;
      }
      if (action === 'copy-diag') { copyDiagnostics(btn); return; }
      if (action === 'edit-node-name' || action === 'edit-node-host') { beginRename(btn); return; }
      if (action === 'scan-nodes') {
        btn.setAttribute('aria-busy', 'true');
        try {
          await post('/nodes/scan', {});
          var scan = await get('/nodes/scan');
          var guard = 0;
          while (scan && scan.poll_pending && guard < 8) {
            await new Promise(function (r) { setTimeout(r, 400); });
            scan = await get('/nodes/scan');
            guard++;
          }
          state.scanFound = (scan && scan.found) || [];
          state.scanDone = true;
          renderScan(state.scanFound);
        } finally {
          btn.removeAttribute('aria-busy');
        }
        return;
      }
      if (action === 'add-found') {
        var host = btn.getAttribute('data-host') || '';
        var foundPayload = /^\d{1,3}(\.\d{1,3}){3}$/.test(host) ? { ip: host } : { hostname: host };
        // With the fingerprint the Touch moves an already paired V6 instead of adding it twice.
        var fp = btn.getAttribute('data-fp');
        if (fp) foundPayload.pairing_fingerprint = fp;
        await post('/nodes', foundPayload);
        state.scanFound = (state.scanFound || []).filter(function (f) { return (f.hostname || f.ip) !== host; });
        refresh();
        return;
      }
      else if (action === 'trust-node') await post('/nodes/' + encodeURIComponent(id) + '/trust', { trust: 2, confirm: id });
      else if (action === 'remove-node') await post('/nodes/' + encodeURIComponent(id) + '/remove', { confirm: id });
      else if (action === 'hs-test-read' || action === 'hs-test-push') { await runHeatTest(btn, action); return; }
      else if (action === 'wx-geo') {
        // The device only estimates; the fields are filled and the form turns
        // dirty, so the person saves (or discards) the new location themselves.
        btn.setAttribute('aria-busy', 'true');
        try {
          var g = await post('/forecast/estimate-location', {});
          if (!g || !finite(g.latitude) || !finite(g.longitude)) throw new Error('no location');
          [['#wx_lat', g.latitude], ['#wx_lon', g.longitude]].forEach(function (p) {
            var el = qs(p[0]);
            if (!el) return;
            el.value = Number(p[1]).toFixed(4);
            el.dispatchEvent(new Event('input', { bubbles: true }));
            el.dispatchEvent(new Event('change', { bubbles: true }));
          });
          setText('wx.geoResult', t('wx.geoFound', { place: [g.city, g.country].filter(Boolean).join(', ') || '—' }));
        } catch (ge) {
          setText('wx.geoResult', t('wx.geoFail'));
        }
        btn.removeAttribute('aria-busy');
        return;
      }
      else if (action === 'reset-registry') await post('/recovery/reset-registry', { confirm: 'reset' });
      refresh();
    } catch (e) {
      if (action === 'hs-test-read' || action === 'hs-test-push') {
        paintTestResult(false, t('hs.testFailLine', { status: t('hs.testStatus.failed'), time: clockNow(), reason: t('status.unknown') }), t('hs.testCheckHost'));
        btn.removeAttribute('aria-busy');
      }
      console.warn(action, e);
    }
  }

  function clockNow() {
    var d = new Date();
    function p(n) { return n < 10 ? '0' + n : String(n); }
    return p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds());
  }

  function testBox() { return qs('form[data-save="heat_source"] .test-result'); }

  function paintTestRunning() {
    var box = testBox();
    if (!box) return;
    box.setAttribute('data-state', 'running');
    box.textContent = t('hs.testing');
  }

  function paintTestResult(ok, line1, line2) {
    var box = testBox();
    if (!box) return;
    box.removeAttribute('data-state');
    box.innerHTML = '<div class="msg ' + (ok ? 'ok' : 'bad') + '"><span><b>' + esc(line1) + '</b> ' + esc(line2) + '</span></div>';
  }

  function probeReason(r) {
    var err = String((r && r.error) || '');
    var http = r && r.http_status;
    if (/timeout/i.test(err) || err === 'read unreachable' || err === 'probe_timeout') return { reason: t('hs.testReason.timeout'), check: t('hs.testCheckHost') };
    if (/dns|resolve|ENOTFOUND/i.test(err)) return { reason: t('hs.testReason.dns'), check: t('hs.testCheckDns') };
    var m = /http\s+(\d+)/i.exec(err);
    var code = m ? m[1] : (http && Number(http) >= 400 ? String(http) : '');
    if (code) return { reason: t('hs.testReason.http', { code: code }), check: t('hs.testCheckHttp') };
    if (/address_missing/.test(err)) return { reason: t('hs.testReason.missing'), check: t('hs.testCheckHost') };
    if (/network_down/.test(err)) return { reason: t('hs.testReason.network'), check: t('hs.testCheckHost') };
    if (/invalid_|endpoint|value missing/.test(err)) return { reason: t('hs.testReason.endpoint'), check: t('hs.testCheckHttp') };
    if (/not_ready/.test(err)) return { reason: t('hs.testReason.notReady'), check: t('hs.testCheckHost') };
    if (/busy/.test(err)) return { reason: t('hs.testReason.busy'), check: t('hs.testCheckHost') };
    if (r && r.result === 'rejected') return { reason: t('hs.testReason.rejected'), check: t('hs.testCheckHost') };
    return { reason: t('status.unknown'), check: t('hs.testCheckHost') };
  }

  async function runHeatTest(btn, action) {
    paintTestRunning();
    btn.setAttribute('aria-busy', 'true');
    var started = (window.performance && performance.now) ? performance.now() : Date.now();
    try {
      var result = await post(action === 'hs-test-read' ? '/heat-source/test-read' : '/heat-source/test-push', {});
      var elapsed = Math.max(0, Math.round(((window.performance && performance.now) ? performance.now() : Date.now()) - started));
      var time = clockNow();
      var code = result && result.http_status ? result.http_status : (result && result.result === 'ok' ? 200 : 0);
      var read = action === 'hs-test-read';
      if (result && result.result === 'ok') {
        var entity = result.entity || '—';
        var value = read ? result.value_c : (result.confirmed_value_c != null ? result.confirmed_value_c : result.requested_value_c);
        // TODO: test-read does not return flow, return or operating state — only value_c and entity.
        paintTestResult(true,
          t(read ? 'hs.testReadOk' : 'hs.testPushOk', { time: time, code: code, ms: elapsed }),
          t(read ? 'hs.testReadBody' : 'hs.testPushBody', { value: num(value), entity: entity }));
      } else {
        var why = probeReason(result || {});
        paintTestResult(false, t('hs.testFailLine', { status: t('hs.testStatus.failed'), time: time, reason: why.reason }), why.check);
      }
    } finally {
      btn.removeAttribute('aria-busy');
    }
  }

  function beginRename(btn) {
    var cell = btn.closest('.name-edit');
    if (!cell) return;
    var input = cell.querySelector('input');
    var label = cell.querySelector('[data-name]');
    if (!input) return;
    input.hidden = false;
    if (label) label.hidden = true;
    btn.hidden = true;
    input.focus();
    input.select();
  }

  async function commitRename(input) {
    if (!input || input.dataset.saving) return;
    var id = input.getAttribute('data-rename');
    var name = String(input.value || '').trim();
    if (!name) { applyNodes(state.nodes); return; }
    input.dataset.saving = '1';
    try {
      await post('/nodes/' + encodeURIComponent(id) + '/profile', { name: name });
      refresh();
    } catch (err) {
      delete input.dataset.saving;
    }
  }

  async function commitHost(input) {
    if (!input || input.dataset.saving) return;
    var id = input.getAttribute('data-rehost');
    var host = String(input.value || '').trim();
    var node = state.nodes.find(function (n) { return String(n.id) === id; });
    if (!host || (node && host === (node.ip || node.hostname || ''))) { applyNodes(state.nodes); return; }
    input.dataset.saving = '1';
    try {
      await post('/nodes/' + encodeURIComponent(id) + '/host', { host: host });
      refresh();
    } catch (err) {
      delete input.dataset.saving;
      input.setAttribute('aria-invalid', 'true');
      input.title = t('ctrl.hostInvalid');
    }
  }

  /* ---- Events --------------------------------------------------------------- */
  // Help popovers sit next to their "?" (no CSS anchor positioning everywhere yet).
  var helpBtn = null;
  function placeHelp(pop, btn) {
    if (!pop || !btn || window.matchMedia('(max-width:599.98px)').matches) return;
    var r = btn.getBoundingClientRect(), gap = 8;
    pop.style.position = 'fixed';
    pop.style.inset = 'unset';
    pop.style.right = 'auto';
    pop.style.bottom = 'auto';
    pop.style.margin = '0';
    pop.style.top = '0px';
    pop.style.left = '0px';
    var w = pop.offsetWidth || 280, h = pop.offsetHeight || 120;
    var left = Math.min(Math.max(8, r.left), Math.max(8, window.innerWidth - w - 8));
    var top = r.bottom + gap;
    if (top + h > window.innerHeight - 8) top = Math.max(8, r.top - h - gap);
    pop.style.top = top + 'px';
    pop.style.left = left + 'px';
  }

  document.addEventListener('pointerdown', function (e) {
    var h = e.target.closest && e.target.closest('.help-btn');
    if (h) helpBtn = h;
  }, true);

  document.addEventListener('toggle', function (e) {
    var tg = e.target;
    if (e.newState !== 'open' || !tg.classList) return;
    if (tg.classList.contains('help-pop')) {
      var btn = (helpBtn && helpBtn.getAttribute('popovertarget') === tg.id) ? helpBtn : document.querySelector('[popovertarget="' + tg.id + '"]');
      if (btn) placeHelp(tg, btn);
    }
    // A room sheet fetches its 24 h comfort chart when it opens.
    var m = /^sheet-r(\d+)$/.exec(tg.id || '');
    if (m) loadComfortChart(Number(m[1]));
    if (tg.id === 'sheet-heat') loadHeatHistory();
  }, true);

  document.addEventListener('click', function (e) {
    var b = e.target.closest('[data-step]');
    if (b && !b.disabled) {
      var i = b.parentNode.querySelector('input');
      if (i && i.type === 'number') {
        if (Number(b.dataset.step) > 0) i.stepUp(); else i.stepDown();
        i.dispatchEvent(new Event('change', { bubbles: true }));
      } else if (i) {
        // Text field with data-step-size/min/max (house target): step and format in the page language.
        var sz = Number(i.dataset.stepSize) || 0.5, cur = parseNum(i.value);
        var nv = (isFinite(cur) ? Math.round(cur / sz) * sz : Number(i.dataset.min) || 0) + (Number(b.dataset.step) > 0 ? sz : -sz);
        nv = Math.max(Number(i.dataset.min), Math.min(Number(i.dataset.max), nv));
        i.value = num(nv);
        i.dispatchEvent(new Event('input', { bubbles: true }));
        i.dispatchEvent(new Event('change', { bubbles: true }));
      }
    }
    var d = document.querySelector('.device[open]');
    if (d && !d.contains(e.target)) d.open = false;
    var act = e.target.closest('[data-action]');
    if (act) {
      e.preventDefault();
      onAction(act);
    }
  });

  document.addEventListener('focusout', function (e) {
    var input = e.target;
    if (!input || !input.getAttribute || input.hidden) return;
    if (input.getAttribute('data-rename')) commitRename(input);
    else if (input.getAttribute('data-rehost')) commitHost(input);
  });
  document.addEventListener('keydown', function (e) {
    var input = e.target;
    if (!input || !input.getAttribute) return;
    var rename = input.getAttribute('data-rename'), rehost = input.getAttribute('data-rehost');
    if (!rename && !rehost) return;
    if (e.key === 'Enter') { e.preventDefault(); if (rename) commitRename(input); else commitHost(input); }
    if (e.key === 'Escape') { e.preventDefault(); applyNodes(state.nodes); }
  });

  document.addEventListener('input', function (e) {
    if (e.target && e.target.closest && e.target.closest('[data-bind-price-sched]')) priceSchedSync();
    if (e.target && e.target.id === 'house_target') renderHome();
  });

  document.addEventListener('change', function (e) {
    var inp = e.target;
    if (!inp) return;
    if (inp.name === 'hs_type') {
      syncHsType(inp.value);
      if (state.heat) {
        state.heat.type = inp.value === 'http' ? 'generic_http' : 'asgard';
        renderHeatDelivery(state.heat);
        paintHeatBadge(state.heat);
      }
    }
    if (inp.id === 'house_target') renderHome();
    if (inp.id === 'price_zone' || inp.id === 'price_currency') {
      if (inp.id === 'price_currency') {
        // A new currency starts from its default rate (still editable).
        var opt = inp.selectedOptions && inp.selectedOptions[0];
        var fx = qs('#fx');
        if (opt && fx && opt.getAttribute('data-fx')) {
          fx.value = Number(opt.getAttribute('data-fx'));
          fx.dispatchEvent(new Event('input', { bubbles: true }));
        }
      }
      priceSyncForm();
    }
    if (inp.form && inp.form === priceForm()) priceSummaries();
    // Switches in a settings form save at once (DESIGN.md 6.1) — only the switch,
    // never the form's other pending edits. data-save-now="false" (fx MQTT)
    // waits for the save bar like any other field.
    if (inp.getAttribute && inp.getAttribute('role') === 'switch' && inp.getAttribute('data-save-now') !== 'false' &&
        inp.form && inp.form.matches('form[data-save]') && inp.form.querySelector('.savebar') && !inp.disabled) {
      if (inp.name === 'target_sync_enabled' && state.heat) renderHeatDelivery(state.heat);
      saveSwitchNow(inp);
    }
    if (inp.type === 'file') {
      var lab = inp.closest('label.file');
      var name = lab && lab.querySelector('.file-name');
      if (name) name.textContent = (inp.files && inp.files[0]) ? inp.files[0].name : (name.dataset.empty || '');
      syncFwBackupButtons();
    }
  });

  // Fortryd: the baseline is the device's values (putVal sets the defaults);
  // the schedule rows have no name, so redraw them from the hidden field.
  document.addEventListener('reset', function (e) {
    var f = e.target;
    if (f === priceForm()) setTimeout(function () {
      var hidden = f.querySelector('input[name="grid_schedule"]');
      try { priceSchedRows(JSON.parse(hidden.value)); } catch (err) {}
      priceSyncForm();
    }, 0);
    if (f === connForm()) setTimeout(function () {
      syncHsType((f.querySelector('input[name="hs_type"]:checked') || {}).value || 'asgard');
    }, 0);
    if (f && f.matches && f.matches('form[data-save="house-target"]')) setTimeout(renderHome, 0);
  });

  document.addEventListener('lune:save', function (e) { onSave(e.detail || {}); });

  // Remember language cookie when visiting /en/ or /da/
  var lm = location.pathname.match(/^\/(en|da)\/?/);
  if (lm) document.cookie = 'lune_lang=' + lm[1] + ';path=/;max-age=31536000';

  syncHsType((qs('input[name="hs_type"]:checked') || {}).value || 'asgard');
  syncFwBackupButtons();
  priceSummaries();
  renderHome();
  refresh();
  setInterval(function () {
    // Do not redraw under the user: open menus, confirmations, focused fields, unsaved forms.
    if (document.querySelector('details.device[open], .confirm-pop:popover-open, .help-pop:popover-open')) return;
    if (document.activeElement && /^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName)) return;
    if (qs('form[data-save][data-dirty], form[data-save][data-state="saving"]')) return;
    refresh();
  }, POLL_MS);
})();
