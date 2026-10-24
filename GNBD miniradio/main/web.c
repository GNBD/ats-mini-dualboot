/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Embedded web UI and HTTP API.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "esp_littlefs.h"
#include "audio.h"
#include "app.h"
#include "display.h"

/* HTTP handlers run sequentially in one task, so share a single page buffer. */
static char page[40960];

void json_escape(char *dst, size_t n, const char *src)
{
    size_t o = 0;
    if (n == 0) {
        return;
    }
    for (; src && *src && o + 2 < n; ++src) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') {
            dst[o++] = '\\';
            dst[o++] = (char)c;
        } else if (c < 0x20) {
            dst[o++] = ' ';
        } else {
            dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
}

void html_escape(char *dst, size_t n, const char *src)
{
    size_t o = 0;
    if (n == 0) {
        return;
    }
    for (; src && *src && o + 6 < n; ++src) {
        switch (*src) {
        case '&':  memcpy(dst + o, "&amp;", 5);  o += 5; break;
        case '<':  memcpy(dst + o, "&lt;", 4);   o += 4; break;
        case '>':  memcpy(dst + o, "&gt;", 4);   o += 4; break;
        case '"':  memcpy(dst + o, "&quot;", 6); o += 6; break;
        case '\'': memcpy(dst + o, "&#39;", 5);  o += 5; break;
        default:   dst[o++] = *src; break;
        }
    }
    dst[o] = '\0';
}

#define WEB_THEME_HEAD \
    "<meta name='color-scheme' content='light dark'>" \
    "<script>try{var t=localStorage.getItem('mrTheme');" \
    "if(t==='dark'||t==='light')document.documentElement.dataset.theme=t;}" \
    "catch(e){}</script>"

static const char WEB_STYLE[] =
    ":root{--bg:#eef1f7;--glow:#fff;--card:#fff;--text:#111827;--muted:#667085;"
    "--line:#e4e7ec;--accent:#0072de;--accent-soft:#e8f1fd;--danger:#d92d20;"
    "--danger-soft:#fee4e2;--chip:#f2f4f7;--round-text:#344054;"
    "--navbg:rgba(255,255,255,.92);--nav-muted:#98a2b3;"
    "--topbg:linear-gradient(180deg,rgba(238,241,247,.96),rgba(238,241,247,.72));"
    "--backdrop:rgba(16,24,40,.42);--handle:#d0d5dd;"
    "--iconbg:#f2f4f7;--icontext:#475467;"
    "--shadow:0 4px 20px rgba(16,24,40,.06);color-scheme:light}"
    "@media(prefers-color-scheme:dark){:root{--bg:#0b0f16;--glow:#151b26;"
    "--card:#161c26;--text:#e8edf5;--muted:#97a3b6;--line:#26303e;"
    "--accent:#4da3ff;--accent-soft:#12253d;--danger:#ff8a80;"
    "--danger-soft:#3a1d1d;--chip:#222b38;--round-text:#d5deea;"
    "--navbg:rgba(17,23,32,.94);--nav-muted:#7d8a9c;"
    "--topbg:linear-gradient(180deg,rgba(11,15,22,.96),rgba(11,15,22,.72));"
    "--backdrop:rgba(0,0,0,.58);--handle:#3a4553;--iconbg:#222b38;"
    "--icontext:#c7d2e0;--shadow:0 4px 20px rgba(0,0,0,.38);color-scheme:dark}}"
    ":root[data-theme=light]{--bg:#eef1f7;--glow:#fff;--card:#fff;"
    "--text:#111827;--muted:#667085;--line:#e4e7ec;--accent:#0072de;"
    "--accent-soft:#e8f1fd;--danger:#d92d20;--danger-soft:#fee4e2;"
    "--chip:#f2f4f7;--round-text:#344054;--navbg:rgba(255,255,255,.92);"
    "--nav-muted:#98a2b3;--topbg:linear-gradient(180deg,rgba(238,241,247,.96),"
    "rgba(238,241,247,.72));--backdrop:rgba(16,24,40,.42);--handle:#d0d5dd;"
    "--iconbg:#f2f4f7;--icontext:#475467;"
    "--shadow:0 4px 20px rgba(16,24,40,.06);color-scheme:light}"
    ":root[data-theme=dark]{--bg:#0b0f16;--glow:#151b26;--card:#161c26;"
    "--text:#e8edf5;--muted:#97a3b6;--line:#26303e;--accent:#4da3ff;"
    "--accent-soft:#12253d;--danger:#ff8a80;--danger-soft:#3a1d1d;"
    "--chip:#222b38;--round-text:#d5deea;--navbg:rgba(17,23,32,.94);"
    "--nav-muted:#7d8a9c;--topbg:linear-gradient(180deg,rgba(11,15,22,.96),"
    "rgba(11,15,22,.72));--backdrop:rgba(0,0,0,.58);--handle:#3a4553;"
    "--iconbg:#222b38;--icontext:#c7d2e0;--shadow:0 4px 20px rgba(0,0,0,.38);"
    "color-scheme:dark}"
    "*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}"
    "body{margin:0;padding:0 0 104px;color:var(--text);line-height:1.45;"
    "font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,"
    "'Noto Sans KR','Malgun Gothic',sans-serif;"
    "background:radial-gradient(120% 80% at 50% -10%,var(--glow) 0%,var(--bg) 62%) fixed}"
    "a{color:var(--accent);text-decoration:none}"
    ".top{position:sticky;top:0;z-index:9;display:flex;align-items:center;"
    "justify-content:space-between;padding:18px 20px 12px;"
    "background:var(--topbg);"
    "backdrop-filter:blur(18px);-webkit-backdrop-filter:blur(18px)}"
    ".brand{font-size:26px;font-weight:800;letter-spacing:-.7px}"
    ".pill{display:inline-flex;align-items:center;gap:6px;font-size:11px;"
    "font-weight:700;background:var(--accent-soft);color:var(--accent);"
    "border-radius:99px;padding:7px 12px;white-space:nowrap}"
    ".pill.gray{background:var(--chip);color:var(--muted)}"
    ".wrap{max-width:560px;margin:0 auto;padding:4px 16px 0}"
    ".card{background:var(--card);border-radius:28px;padding:22px;"
    "margin-bottom:14px;box-shadow:var(--shadow)}"
    ".hero{text-align:center;padding:28px 20px 24px}"
    ".freq{font-size:48px;font-weight:800;letter-spacing:-2px;line-height:1.1}"
    ".chips{display:flex;justify-content:center;gap:8px;flex-wrap:wrap;margin-top:12px}"
    ".chip{background:var(--accent-soft);color:var(--accent);font-size:12px;"
    "font-weight:700;padding:6px 12px;border-radius:99px}"
    ".chip.gray{background:var(--chip);color:var(--muted)}"
    ".station{min-height:18px;margin-top:12px;font-size:13px;font-weight:600;"
    "color:var(--muted)}"
    ".grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;text-align:center}"
    ".metric{padding:6px 2px}.metric .v{font-size:19px;font-weight:800;"
    "letter-spacing:-.3px}.metric .k{font-size:10px;font-weight:700;"
    "color:var(--muted);letter-spacing:.6px;margin-top:4px}"
    ".sec{font-size:12px;font-weight:800;color:var(--muted);"
    "letter-spacing:.4px;margin:0 0 12px}"
    ".tuner{display:flex;justify-content:center;gap:18px;margin:6px 0 16px}"
    ".round{width:78px;height:78px;border:0;border-radius:50%;background:var(--chip);"
    "color:var(--round-text);font-size:32px;font-weight:600;cursor:pointer;display:flex;"
    "align-items:center;justify-content:center;touch-action:manipulation;"
    "transition:transform .12s ease,background .12s ease}"
    ".round:active{transform:scale(.95);background:var(--accent-soft);color:var(--accent)}"
    ".btns{display:flex;flex-wrap:wrap;gap:8px}"
    ".btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;"
    "padding:12px 16px;border:0;border-radius:16px;background:var(--chip);"
    "color:var(--text);font-size:14px;font-weight:700;cursor:pointer;"
    "text-decoration:none;touch-action:manipulation}"
    ".btn.primary{background:var(--accent);color:#fff;"
    "box-shadow:0 6px 16px rgba(0,114,222,.22)}"
    ".btn.soft{background:var(--accent-soft);color:var(--accent)}"
    ".btn.danger{background:var(--danger-soft);color:var(--danger)}"
    ".btn:active,.chipbtn:active{transform:scale(.98)}"
    ".field{display:flex;gap:8px}"
    "input,select{width:100%;padding:13px 14px;border:0;border-radius:16px;"
    "background:var(--chip);color:var(--text);font-size:15px;outline:none;"
    "font-family:inherit}"
    "input:focus{box-shadow:0 0 0 3px rgba(0,114,222,.18)}"
    "input[type=range]{padding:0;background:transparent;accent-color:var(--accent);"
    "height:28px}input[type=radio]{width:20px;height:20px;flex:0 0 auto;"
    "padding:0;margin:0;accent-color:var(--accent)}"
    ".chipbtn{border:0;border-radius:14px;padding:10px 14px;background:var(--chip);"
    "color:var(--muted);font-size:13px;font-weight:700;cursor:pointer;"
    "font-family:inherit;touch-action:manipulation}"
    ".chipbtn.on{background:var(--accent);color:#fff;"
    "box-shadow:0 4px 12px rgba(0,114,222,.2)}"
    ".rowline{display:flex;align-items:center;justify-content:space-between;"
    "padding:13px 0;border-bottom:1px solid var(--line);font-size:14px}"
    ".rowline:last-child{border-bottom:0}.rowline .k{color:var(--muted);"
    "font-weight:600}.rowline .v{font-weight:700;text-align:right}"
    ".setrow{display:flex;align-items:center;justify-content:space-between;"
    "gap:12px;padding:9px 0}.setrow .k{color:var(--muted);font-weight:600;"
    "font-size:14px}.setrow .v{font-weight:700;font-size:14px}"
    ".setrow select{width:auto;min-width:112px;padding:9px 12px;"
    "border-radius:12px;font-size:14px;font-weight:700;text-align:right}"
    ".hint{color:var(--muted);font-size:12px;margin-top:8px}"
    ".switch{position:relative;display:inline-block;width:46px;height:26px;"
    "flex:0 0 auto}.switch input{opacity:0;width:0;height:0}"
    ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;"
    "background:#cbd5e1;border-radius:999px;transition:.2s}"
    ".slider:before{content:'';position:absolute;height:20px;width:20px;"
    "left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s;"
    "box-shadow:0 1px 3px rgba(0,0,0,.25)}"
    ".switch input:checked+.slider{background:#2563eb}"
    ".switch input:checked+.slider:before{transform:translateX(20px)}"
    ".fold{padding:0;overflow:hidden}"
    ".fold>summary{list-style:none;display:flex;align-items:center;"
    "justify-content:space-between;padding:20px 22px;cursor:pointer;"
    "-webkit-tap-highlight-color:transparent}"
    ".fold>summary::-webkit-details-marker{display:none}"
    ".fold>summary .sec{margin:0}.foldbody{padding:0 22px 22px}"
    ".chev{width:9px;height:9px;border-right:2px solid var(--nav-muted);"
    "border-bottom:2px solid var(--nav-muted);transform:rotate(45deg);"
    "margin:-4px 4px 0 0;transition:transform .2s ease}"
    ".fold[open] .chev{transform:rotate(-135deg);margin-top:4px}"
    ".item{display:flex;align-items:center;gap:12px;padding:14px 0;"
    "border-bottom:1px solid var(--line)}.item:last-child{border-bottom:0}"
    ".item .pick{display:flex;align-items:center;gap:12px;flex:1;"
    "min-width:0;color:inherit}"
    ".item .num{flex:0 0 44px;height:44px;border-radius:14px;"
    "background:var(--accent-soft);color:var(--accent);font-size:12px;"
    "font-weight:800;display:flex;align-items:center;justify-content:center}"
    ".item .txt{display:flex;flex-direction:column;min-width:0}"
    ".item .txt b{font-size:16px;letter-spacing:-.2px;white-space:nowrap;"
    "overflow:hidden;text-overflow:ellipsis}"
    ".item .txt small{color:var(--muted);font-size:12px;font-weight:600}"
    ".item .del{font-size:12px;font-weight:700;color:var(--danger);"
    "background:var(--danger-soft);border-radius:10px;padding:7px 10px}"
    ".ap{display:flex;align-items:center;gap:10px;padding:13px 0;"
    "border-bottom:1px solid var(--line);font-size:14px;font-weight:600}"
    ".ap:last-child{border-bottom:0}.ap .rssi{margin-left:auto;"
    "color:var(--muted);font-size:12px;font-weight:600}"
    ".sub{color:var(--muted);font-size:13px;margin-top:6px}"
    ".empty{padding:26px 0;text-align:center;color:var(--muted);font-size:14px}"
    ".bottom{position:fixed;left:0;right:0;bottom:0;z-index:20;display:flex;"
    "justify-content:space-around;background:var(--navbg);"
    "backdrop-filter:blur(18px);-webkit-backdrop-filter:blur(18px);"
    "border-top:1px solid var(--line);"
    "padding:8px 8px calc(8px + env(safe-area-inset-bottom))}"
    ".nav{display:flex;flex-direction:column;align-items:center;gap:3px;"
    "min-width:56px;padding:6px 8px;border-radius:16px;color:var(--nav-muted);"
    "font-size:10px;font-weight:700;text-decoration:none}"
    ".nav svg{width:24px;height:24px;fill:none;stroke:currentColor;"
    "stroke-width:1.9;stroke-linecap:round;stroke-linejoin:round}"
    ".nav.on{background:var(--accent-soft);color:var(--accent)}"
    ".modal{position:fixed;inset:0;z-index:40;display:none}"
    ".modal.open{display:block}"
    ".modal .backdrop{position:absolute;inset:0;background:var(--backdrop);"
    "opacity:0;transition:opacity .22s ease}"
    ".modal.open .backdrop{opacity:1}"
    ".sheet{position:absolute;left:0;right:0;bottom:0;max-height:88vh;"
    "overflow:auto;background:var(--card);border-radius:28px 28px 0 0;"
    "padding:10px 20px calc(26px + env(safe-area-inset-bottom));"
    "transform:translateY(100%);transition:transform .26s ease;"
    "-webkit-overflow-scrolling:touch}"
    ".modal.open .sheet{transform:translateY(0)}"
    ".handle{width:36px;height:4px;border-radius:99px;background:var(--handle);"
    "margin:6px auto 14px}.sheet-head{display:flex;align-items:center;"
    "justify-content:space-between;margin-bottom:8px}"
    ".sheet-head h2{font-size:21px;margin:0;letter-spacing:-.5px}"
    ".iconbtn{width:36px;height:36px;border:0;border-radius:50%;"
    "background:var(--iconbg);color:var(--icontext);font-size:21px;line-height:1;"
    "cursor:pointer;font-family:inherit}"
    ".netlist{margin-top:2px}"
    "@media(min-width:600px){.sheet{left:50%;right:auto;width:520px;"
    "transform:translate(-50%,100%)}.modal.open .sheet{transform:translate(-50%,0)}}"
    "@media(max-width:420px){.freq{font-size:40px}.round{width:70px;height:70px}"
    ".card{padding:18px;border-radius:24px}.brand{font-size:23px}}";

static const char WEB_ROOT_MAIN[] =
    "<div class='top'><div class='brand'>Radio</div>"
    "<span class='pill' id='wifi'>Offline</span></div>"
    "<main class='wrap'>"
    "<section class='card hero'><div class='freq' id='freq'>--</div>"
    "<div class='chips'><span class='chip' id='band'>--</span>"
    "<span class='chip gray' id='mode'>--</span>"
    "<span class='chip gray' id='stereo' style='display:none'>Stereo</span></div>"
    "<div class='station' id='station'></div></section>"
    "<section class='card'><div class='grid3'>"
    "<div class='metric'><div class='v' id='sig'>--</div><div class='k'>RSSI</div></div>"
    "<div class='metric'><div class='v' id='snr'>--</div><div class='k'>SNR</div></div>"
    "<div class='metric'><div class='v' id='batt'>--</div><div class='k'>BATTERY</div></div>"
    "</div></section>"
    "<section class='card'><div class='sec'>TUNING</div>"
    "<div class='tuner'>"
    "<button class='round' onclick='cmd(\"tune=down\")' aria-label='Tune down'>&minus;</button>"
    "<button class='round' onclick='cmd(\"tune=up\")' aria-label='Tune up'>+</button>"
    "</div>"
    "<form class='field' onsubmit='return goFreq(event)'>"
    "<input id='freqin' placeholder='87.5 MHz or 1200 kHz' inputmode='decimal'>"
    "<button class='btn primary'>Go</button></form></section>"
    "<section class='card'><div class='sec'>BAND</div>"
    "<div class='btns' id='bands'></div>"
    "<div class='sec' style='margin-top:18px'>MODE</div>"
    "<div class='btns' id='modes'></div></section>"
    "<details class='card fold'><summary><span class='sec'>RADIO SETTINGS</span>"
    "<span class='chev'></span></summary><div class='foldbody'>"
    "<div class='setrow'><span class='k'>Step</span>"
    "<select id='rstep' onchange='radioSet(\"step\",this.value)'></select></div>"
    "<div class='setrow'><span class='k'>Bandwidth</span>"
    "<select id='rbw' onchange='radioSet(\"bw\",this.value)'></select></div>"
    "<div class='setrow' id='rstereoRow'><span class='k'>Stereo</span>"
    "<select id='rstereo' onchange='radioSet(\"stereo\",this.value)'>"
    "<option value='0'>AUTO</option><option value='1'>MONO</option>"
    "</select></div>"
    "<div class='setrow' id='rrdsRow'><span class='k'>RDS</span>"
    "<select id='rrds' onchange='radioSet(\"rds\",this.value)'>"
    "<option value='1'>ON</option><option value='0'>OFF</option>"
    "</select></div>"
    "<div class='setrow' id='rdeemphRow'><span class='k'>De-emphasis</span>"
    "<select id='rdeemph' onchange='radioSet(\"deemph\",this.value)'>"
    "<option value='0'>50us</option><option value='1'>75us</option>"
    "</select></div>"
    "<div class='setrow'><span class='k'>AGC</span>"
    "<select id='ragc' onchange='radioSet(\"agc\",this.value)'>"
    "<option value='1'>ON</option><option value='0'>OFF</option>"
    "</select></div>"
    "<div class='setrow'><span class='k'>Squelch</span>"
    "<span class='v' id='rsqv'>OFF</span></div>"
    "<input class='rset' id='rsq' type='range' min='0' max='100' value='0'"
    " oninput='$(\"rsqv\").textContent=this.value==0?\"OFF\":this.value'"
    " onchange='radioSet(\"squelch\",this.value)'>"
    "<div class='setrow'><span class='k'>AVC gain</span>"
    "<span class='v' id='ravcv'>0 dB</span></div>"
    "<input class='rset' id='ravc' type='range' min='0' max='90' value='0'"
    " oninput='$(\"ravcv\").textContent=this.value+\" dB\"'"
    " onchange='radioSet(\"avc\",this.value)'>"
    "<div class='setrow'><span class='k'>Soft mute</span>"
    "<span class='v' id='rsmv'>0 dB</span></div>"
    "<input class='rset' id='rsm' type='range' min='0' max='63' value='0'"
    " oninput='$(\"rsmv\").textContent=this.value+\" dB\"'"
    " onchange='radioSet(\"softmute\",this.value)'>"
    "<div id='rbfoWrap' style='display:none'><div class='setrow'>"
    "<span class='k'>BFO</span><span class='v' id='rbfov'>0 Hz</span></div>"
    "<input class='rset' id='rbfo' type='range' min='-1000' max='1000' value='0'"
    " oninput='$(\"rbfov\").textContent=this.value+\" Hz\"'"
    " onchange='radioSet(\"bfo\",this.value)'></div>"
    "<div class='btns' style='margin-top:14px'>"
    "<button class='btn soft' onclick='radioSet(\"ats\",0)'>Run ATS</button>"
    "<a class='btn soft' href='/presets'>Favorites</a></div>"
    "<a class='btn soft' style='width:100%;margin-top:8px'"
    " href='/api/screenshot' download='miniradio.png'>Download screenshot</a>"
    "</div></details>"
    "<section class='card'><div class='rowline' style='border:0;padding-top:0'>"
    "<span class='k'>Volume</span><span class='v' id='vol'>--</span></div>"
    "<input id='volrange' type='range' min='0' max='63' value='32'"
    " oninput='$(\"vol\").textContent=this.value'"
    " onchange='cmd(\"volume=\"+this.value)'></section>"
    "<section class='card'><div class='rowline'><span class='k'>Preset</span>"
    "<span class='v' id='preset'>--</span></div>"
    "<a class='btn primary' style='width:100%;margin-top:4px'"
    " href='/api/cmd?save=1&back=/'>Save current</a></section>"
    "</main>";

static const char WEB_ROOT_JS[] =
    "const $=id=>document.getElementById(id);"
    "let bandsBuilt=false,dragging=false,rdrag=false;"
    "function cmd(q){fetch('/api/cmd?'+q,{cache:'no-store'}).then(tick);}"
    "function goFreq(e){e.preventDefault();const v=$('freqin').value.trim();"
    "if(v)cmd('freq='+encodeURIComponent(v));return false;}"
    "function buildBands(s){if(bandsBuilt)return;bandsBuilt=true;"
    "const b=$('bands');s.bands.forEach((n,i)=>{const x=document.createElement('button');"
    "x.className='chipbtn';x.textContent=n;x.onclick=()=>cmd('band='+i);"
    "b.appendChild(x);});}"
    "function buildModes(s){const m=$('modes');"
    "const list=(s.band==='FM')?['FM']:['AM','LSB','USB'];m.innerHTML='';"
    "list.forEach(n=>{const x=document.createElement('button');"
    "x.className='chipbtn'+(s.mode===n?' on':'');x.textContent=n;"
    "x.onclick=()=>cmd('mode='+n);m.appendChild(x);});}"
    "function fillSel(el,arr,v){const k=arr.join('|');"
    "if(el.dataset.k!==k){el.innerHTML='';arr.forEach((n,i)=>{"
    "const o=document.createElement('option');o.value=i;o.textContent=n;"
    "el.appendChild(o);});el.dataset.k=k;}el.value=v;}"
    "function radioSet(k,v){cmd('radio='+k+'&v='+encodeURIComponent(v));}"
    "async function tick(){try{"
    "const r=await fetch('/api/status',{cache:'no-store'});"
        "const s=await r.json();"
    "$('freq').textContent=s.freq;$('band').textContent=s.band;"
    "$('mode').textContent=s.mode;$('stereo').style.display=s.stereo?'':'none';"
    "$('station').textContent=s.station||(s.preset>=0?('Preset P'+(s.preset+1)):'');"
    "$('sig').textContent=s.rssi;$('snr').textContent=s.snr;"
    "$('batt').textContent=s.battery?s.battery+'%':'--';"
    "$('batt').title=s.mv?(s.mv/1000).toFixed(2)+' V':'';"
    "$('preset').textContent=s.preset>=0?('P'+(s.preset+1)):'None';"
    "$('wifi').textContent=s.wifi;$('wifi').className='pill';"
    "$('vol').textContent=s.volume;"
    "if(!dragging)$('volrange').value=s.volume;"
    "buildBands(s);buildModes(s);"
    "if(bandsBuilt){[...$('bands').children].forEach((x,i)=>"
    "x.className='chipbtn'+(i===s.band_idx?' on':''));}"
    "const fm=s.band==='FM';"
    "$('rstereoRow').style.display=fm?'':'none';"
    "$('rrdsRow').style.display=fm?'':'none';"
    "$('rdeemphRow').style.display=fm?'':'none';"
    "fillSel($('rstep'),s.steps,s.step);fillSel($('rbw'),s.bws,s.bw);"
    "$('rstereo').value=s.stereo_force?'1':'0';$('rrds').value=s.rds?'1':'0';"
    "$('rdeemph').value=s.deemph75?'1':'0';$('ragc').value=s.agc?'1':'0';"
    "if(!rdrag){$('rsq').value=s.squelch_on?s.squelch:0;"
    "$('ravc').value=s.avc;$('rsm').value=s.softmute;"
    "$('rbfo').min=-s.bfo_max;$('rbfo').max=s.bfo_max;$('rbfo').value=s.bfo;}"
    "$('rbfoWrap').style.display=s.ssb?'':'none';"
    "$('rsqv').textContent=s.squelch_on?s.squelch:'OFF';"
    "$('ravcv').textContent=s.avc+' dB';$('rsmv').textContent=s.softmute+' dB';"
    "$('rbfov').textContent=(s.bfo>0?'+':'')+s.bfo+' Hz';"
    "}catch(e){$('wifi').textContent='Offline';$('wifi').className='pill gray';}}"
    "$('volrange').addEventListener('pointerdown',()=>dragging=true);"
    "$('volrange').addEventListener('pointerup',()=>dragging=false);"
    "$('volrange').addEventListener('touchstart',()=>dragging=true);"
    "$('volrange').addEventListener('touchend',()=>dragging=false);"
    "document.querySelectorAll('.rset').forEach(r=>{"
    "r.addEventListener('pointerdown',()=>rdrag=true);"
    "r.addEventListener('pointerup',()=>rdrag=false);"
    "r.addEventListener('touchstart',()=>rdrag=true);"
    "r.addEventListener('touchend',()=>rdrag=false);});"
    "const wifiModal=$('wifiModal');"
    "function openWifi(){wifiModal.classList.add('open');"
    "wifiModal.setAttribute('aria-hidden','false');"
    "document.body.style.overflow='hidden';"
    "const n=document.querySelector('[data-wifi]');if(n)n.classList.add('on');"
    "loadWifi();}"
    "function closeWifi(){wifiModal.classList.remove('open');"
    "wifiModal.setAttribute('aria-hidden','true');"
    "document.body.style.overflow='';"
    "const n=document.querySelector('[data-wifi]');if(n)n.classList.remove('on');}"
    "function wifiCmd(q){fetch('/api/cmd?wifi='+q,{cache:'no-store'})"
    ".then(()=>setTimeout(loadWifi,800)).catch(()=>{});}"
    "async function loadWifi(){try{"
    "const r=await fetch('/api/wifi',{cache:'no-store'});const w=await r.json();"
    "$('wState').textContent=w.state;$('wIp').textContent=w.ip||'--';"
    "$('wSsidName').textContent=w.ssid||'--';"
    "$('wRssi').textContent=w.rssi?w.rssi+' dBm':'--';"
    "const L=$('wList');L.innerHTML='';"
    "if(w.scanning){L.textContent='Scanning...';setTimeout(loadWifi,1500);}"
    "else if(!w.networks||!w.networks.length){"
    "L.textContent='No networks. Tap Scan in STA mode.';}"
    "else{w.networks.forEach(n=>{const l=document.createElement('label');"
    "l.className='ap';const i=document.createElement('input');"
    "i.type='radio';i.name='wnet';"
    "i.onchange=()=>{$('wSsid').value=n.ssid;};"
    "const s=document.createElement('span');s.textContent=n.ssid;"
    "const q=document.createElement('span');q.className='rssi';"
    "q.textContent=n.rssi+' dBm'+(n.secure?' (locked)':'');"
    "l.appendChild(i);l.appendChild(s);l.appendChild(q);L.appendChild(l);});}"
    "}catch(e){$('wState').textContent='Offline';}}"
    "function wifiConnect(e){e.preventDefault();"
    "const ssid=$('wSsid').value.trim();if(!ssid)return false;"
    "const p=$('wPass').value;"
    "fetch('/api/cmd?wifi=sta&ssid='+encodeURIComponent(ssid)+"
    "'&pass='+encodeURIComponent(p),{cache:'no-store'})"
    ".then(()=>{$('wPass').value='';setTimeout(loadWifi,1500);})"
    ".catch(()=>{});return false;}"
    "document.querySelectorAll('[data-wifi]').forEach(a=>"
    "a.addEventListener('click',e=>{e.preventDefault();openWifi();}));"
    "document.addEventListener('keydown',e=>{if(e.key==='Escape')closeWifi();});"
    "if(location.search.indexOf('wifi=1')>=0){setTimeout(openWifi,120);}"
    "tick();setInterval(tick,1000);";

static const char WEB_WIFI_MODAL[] =
    "<div id='wifiModal' class='modal' aria-hidden='true'>"
    "<div class='backdrop' onclick='closeWifi()'></div>"
    "<section class='sheet' role='dialog' aria-modal='true'"
    " aria-labelledby='wifiTitle'><div class='handle'></div>"
    "<div class='sheet-head'><h2 id='wifiTitle'>Wi-Fi</h2>"
    "<button class='iconbtn' type='button' onclick='closeWifi()'"
    " aria-label='Close'>&times;</button></div>"
    "<div class='rowline'><span class='k'>State</span>"
    "<span class='v' id='wState'>--</span></div>"
    "<div class='rowline'><span class='k'>IP address</span>"
    "<span class='v' id='wIp'>--</span></div>"
    "<div class='rowline'><span class='k'>SSID</span>"
    "<span class='v' id='wSsidName'>--</span></div>"
    "<div class='rowline'><span class='k'>Signal</span>"
    "<span class='v' id='wRssi'>--</span></div>"
    "<div class='sec' style='margin-top:18px'>NETWORKS</div>"
    "<div id='wList' class='netlist sub'>Open to load networks.</div>"
    "<div class='btns' style='margin-top:12px'>"
    "<button class='btn soft' type='button' onclick='wifiCmd(\"scan\")'>Scan</button>"
    "</div>"
    "<div class='sec' style='margin-top:20px'>CONNECT</div>"
    "<form onsubmit='return wifiConnect(event)'>"
    "<input id='wSsid' placeholder='SSID'>"
    "<input id='wPass' type='password' placeholder='Password'>"
    "<button class='btn primary' style='width:100%;margin-top:8px'>"
    "Connect</button></form></section></div>";

int web_nav(char *dst, size_t n, int active)
{
    return snprintf(dst, n,
        "<div class='bottom'>"
        "<a class='nav%s' href='/'><svg viewBox='0 0 24 24'>"
        "<rect x='3' y='7' width='18' height='13' rx='4'/>"
        "<path d='M7 7l10-4'/><circle cx='8' cy='13' r='2'/>"
        "<path d='M15 12h3M15 16h3'/></svg><span>Radio</span></a>"
        "<a class='nav%s' href='/presets'><svg viewBox='0 0 24 24'>"
        "<path d='M12 3l2.6 5.6 6.1.8-4.5 4.3 1.1 6-5.3-2.9-5.3 2.9"
        " 1.1-6L3.3 9.4l6.1-.8z'/></svg><span>Presets</span></a>"
        "<a class='nav%s' data-wifi href='/?wifi=1'><svg viewBox='0 0 24 24'>"
        "<path d='M3 9a15 15 0 0 1 18 0'/><path d='M6.5 12.5a10 10 0 0 1 11 0'/>"
        "<path d='M10 16a4.5 4.5 0 0 1 4 0'/><circle cx='12' cy='19' r='1'/>"
        "</svg><span>Wi-Fi</span></a>"
        "<a class='nav%s' href='/settings'><svg viewBox='0 0 24 24'>"
        "<path d='M4 7h9M17 7h3M4 17h3M11 17h9'/>"
        "<circle cx='15' cy='7' r='2'/><circle cx='9' cy='17' r='2'/>"
        "</svg><span>Settings</span></a>"
        "<a class='nav%s' href='/info'><svg viewBox='0 0 24 24'>"
        "<circle cx='12' cy='12' r='9'/><path d='M12 11v6M12 7.5v.5'/>"
        "</svg><span>Info</span></a></div>",
        active == 0 ? " on" : "", active == 1 ? " on" : "",
        active == 2 ? " on" : "", active == 3 ? " on" : "",
        active == 4 ? " on" : "");
}

static bool web_authorized(httpd_req_t *req);
static esp_err_t web_require_auth(httpd_req_t *req);

esp_err_t http_root_handler(httpd_req_t *req)
{
    if (!s_setup_done) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/setup");
        return httpd_resp_send(req, "", 0);
    }
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    char nav[2048];
    web_nav(nav, sizeof(nav), 0);

    int n = snprintf(page, sizeof(page),
        "<!doctype html><html><head><meta charset='utf-8'>" WEB_THEME_HEAD
        "<meta name='viewport' content='width=device-width,initial-scale=1,"
        "viewport-fit=cover'><meta name='theme-color' content='#eef1f7'>"
        "<title>MINI-radio</title><style>%s</style></head><body>"
        "%s%s%s<script>%s</script></body></html>",
        WEB_STYLE, WEB_ROOT_MAIN, nav, WEB_WIFI_MODAL, WEB_ROOT_JS);
    if (n < 0) {
        n = 0;
    } else if (n >= (int)sizeof(page)) {
        n = (int)sizeof(page) - 1;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, n);
}

void do_save(void);
void wifi_off(void);
esp_err_t wifi_start_ap(void);
esp_err_t wifi_sta_connect(void);

void url_decode(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char h[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            s += 3;
        } else if (*s == '+') {
            *o++ = ' ';
            s++;
        } else {
            *o++ = *s++;
        }
    }
    *o = '\0';
}

static char s_web_user[16] = "admin";
static char s_web_pass[65];
static bool s_web_auth_on;

static void sha256_hex(const char *s, char *out)
{
    unsigned char d[32];
    mbedtls_sha256((const unsigned char *)s, strlen(s), d, 0);
    for (int i = 0; i < 32; ++i) {
        sprintf(out + i * 2, "%02x", d[i]);
    }
    out[64] = '\0';
}

static void web_auth_init(void)
{
    char u[16];
    char p[65];
    if (storage_load_web_auth(u, sizeof(u), p, sizeof(p)) == ESP_OK) {
        snprintf(s_web_user, sizeof(s_web_user), "%s", u);
        if (strlen(p) == 64) {
            snprintf(s_web_pass, sizeof(s_web_pass), "%s", p);
        } else {
            sha256_hex(p, s_web_pass);
            storage_save_web_auth(s_web_user, s_web_pass);
        }
    } else {
        sha256_hex("12345678", s_web_pass);
        storage_save_web_auth(s_web_user, s_web_pass);
    }
    storage_load_web_auth_on(&s_web_auth_on);
}

static bool basic_auth_ok(httpd_req_t *req)
{
    char hdr[160];
    if (!s_web_pass[0]) {
        return false;
    }
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) !=
        ESP_OK) {
        return false;
    }
    if (strncmp(hdr, "Basic ", 6) != 0) {
        return false;
    }
    unsigned char dec[160];
    size_t dlen = sizeof(dec) - 1;
    if (mbedtls_base64_decode(dec, sizeof(dec) - 1, &dlen,
                              (const unsigned char *)(hdr + 6),
                              strlen(hdr + 6)) != 0) {
        return false;
    }
    dec[dlen] = '\0';
    char *colon = strchr((char *)dec, ':');
    if (!colon) {
        return false;
    }
    *colon = '\0';
    if (strcmp((char *)dec, s_web_user) != 0) {
        return false;
    }
    char h[65];
    sha256_hex(colon + 1, h);
    return strcmp(h, s_web_pass) == 0;
}

static bool web_authorized(httpd_req_t *req)
{
    return !s_setup_done || !s_web_auth_on || basic_auth_ok(req);
}

static esp_err_t web_require_auth(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate",
                       "Basic realm=\"miniradio\", charset=\"UTF-8\"");
    if (strncmp(req->uri, "/api/", 5) == 0) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"error\":\"auth\"}");
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, "<h1>401</h1>Authentication required");
}

static void form_get(const char *body, const char *key, char *out, size_t n)
{
    out[0] = '\0';
    size_t kl = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
            const char *v = p + kl + 1;
            size_t i = 0;
            while (v[i] && v[i] != '&' && i + 1 < n) {
                out[i] = v[i];
                i++;
            }
            out[i] = '\0';
            url_decode(out);
            return;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }
}

esp_err_t http_webauth_handler(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    char q[64];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char on[8];
        if (httpd_query_key_value(q, "on", on, sizeof(on)) == ESP_OK) {
            s_web_auth_on = (on[0] == '1');
            storage_save_web_auth_on(s_web_auth_on);
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":true}");
        }
    }
    char body[192];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len > 0) {
        body[len] = '\0';
        char u[32];
        char p[64];
        form_get(body, "u", u, sizeof(u));
        form_get(body, "p", p, sizeof(p));
        if (u[0]) {
            snprintf(s_web_user, sizeof(s_web_user), "%.15s", u);
        }
        if (strlen(p) >= 4) {
            sha256_hex(p, s_web_pass);
        }
        storage_save_web_auth(s_web_user, s_web_pass);
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/settings");
    return httpd_resp_send(req, "OK", 2);
}

esp_err_t http_screenshot_handler(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    void *png = NULL;
    size_t len = 0;
    if (display_capture_png(&png, &len) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "capture failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"miniradio.png\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, (const char *)png, len);
    display_capture_free(png);
    return err;
}

static const char SETUP_JS[] =
    "const $=id=>document.getElementById(id);"
    "let wifiOk=0;"
    "function msg(t){$('msg').textContent=t;}"
    "$('utc').value=document.body.dataset.utc;"
    "$('unit').value=document.body.dataset.unit;"
    "$('batt').value=document.body.dataset.batt;"
    "function syncTime(){fetch('/api/cmd?set=time&v='+"
    "Math.floor(Date.now()/1000),{cache:'no-store'})"
    ".then(()=>msg('Browser time set'));}"
    "function scan(){fetch('/api/cmd?wifi=scan',{cache:'no-store'})"
    ".then(()=>setTimeout(loadNets,1200));}"
    "async function loadNets(){try{const r=await fetch('/api/wifi',"
    "{cache:'no-store'});const j=await r.json();const s=$('ssid');"
    "const cur=s.value;s.innerHTML='';(j.networks||[]).forEach(n=>{"
    "const o=document.createElement('option');o.value=n.ssid;"
    "o.textContent=n.ssid+' ('+n.rssi+')';s.appendChild(o);});"
    "if(cur)s.value=cur;$('wstat').textContent=(j.state||'')+"
    "(j.ip?' '+j.ip:'');if(j.scanning)setTimeout(loadNets,1200);}"
    "catch(e){}}"
    "async function connectWifi(){const ssid=$('ssid').value;"
    "const pass=$('pass').value;if(!ssid){msg('Select a network');return;}"
    "wifiOk=1;const b=new URLSearchParams({action:'wifi',ssid:ssid,pass:pass});"
    "const r=await fetch('/api/setup',{method:'POST',body:b});"
    "const j=await r.json();msg(j.ok?'Connecting...':(j.error||'Connect failed'));"
    "setTimeout(loadNets,1500);}"
    "function skipWifi(){wifiOk=0;msg('Wi-Fi skipped - offline mode');}"
    "async function finish(){const b=new URLSearchParams({action:'finish',"
    "wifi:wifiOk,utc:$('utc').value,unit:$('unit').value,batt:$('batt').value});"
    "const r=await fetch('/api/setup',{method:'POST',body:b});"
    "const j=await r.json();if(j.ok){msg('Saved. Rebooting...');"
    "document.querySelectorAll('button,select,input').forEach(x=>x.disabled=true);}"
    "else{msg(j.error||'Error');}}"
    "setTimeout(scan,400);setInterval(loadNets,3000);";

esp_err_t http_setup_handler(httpd_req_t *req)
{
    if (s_setup_done) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
        return httpd_resp_send(req, "", 0);
    }
    char utc_opts[1200];
    size_t o = 0;
    utc_opts[0] = '\0';
    for (int h = -12; h <= 14; ++h) {
        int w = snprintf(utc_opts + o, sizeof(utc_opts) - o,
                         "<option value='%d'>UTC%+d</option>", h, h);
        if (w < 0 || (size_t)w >= sizeof(utc_opts) - o) {
            break;
        }
        o += (size_t)w;
    }
    char wifi_esc[80];
    html_escape(wifi_esc, sizeof(wifi_esc),
                s_wifi_ssid[0] ? s_wifi_ssid : "-");
    int utc_h = s_utc_min / 60;
    if (utc_h < -12) utc_h = -12;
    if (utc_h > 14) utc_h = 14;

    int n = snprintf(page, sizeof(page),
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1,"
        "viewport-fit=cover'><title>Setup - MINI-radio</title>"
        "<style>%s</style></head><body data-utc='%d' data-unit='%d'"
        " data-batt='%d'><div class='top'><div class='brand'>Setup</div>"
        "<span class='pill gray'>GNBD</span></div><main class='wrap'>"
        "<section class='card'><div class='sec'>1  TIME</div>"
        "<div class='setrow'><span class='k'>UTC offset</span>"
        "<select id='utc'>%s</select></div>"
        "<button class='btn soft' style='width:100%%;margin-top:8px'"
        " type='button' onclick='syncTime()'>Use browser time</button>"
        "<div class='hint'>With Wi-Fi, time syncs automatically via NTP."
        " Without Wi-Fi, use browser time.</div></section>"
        "<section class='card'><div class='sec'>2  WI-FI</div>"
        "<div class='hint'>AP: MINI-radio / %s</div>"
        "<div class='setrow'><span class='k'>Network</span>"
        "<select id='ssid'></select></div>"
        "<input id='pass' type='password' maxlength='63'"
        " placeholder='Wi-Fi password'>"
        "<div class='btns' style='margin-top:8px'>"
        "<button class='btn soft' type='button' onclick='scan()'>Scan</button>"
        "<button class='btn primary' type='button' onclick='connectWifi()'>"
        "Connect</button>"
        "<button class='btn soft' type='button' onclick='skipWifi()'>"
        "Skip</button></div>"
        "<div class='hint' id='wstat'>Scanning...</div>"
        "<div class='hint'>Current: %s</div></section>"
        "<section class='card'><div class='sec'>3  DISPLAY</div>"
        "<div class='setrow'><span class='k'>Frequency unit</span>"
        "<select id='unit'><option value='0'>MHz</option>"
        "<option value='1'>kHz</option></select></div>"
        "<div class='setrow'><span class='k'>Battery display</span>"
        "<select id='batt'><option value='0'>ICON</option>"
        "<option value='1'>VOLT</option><option value='2'>PERCENT</option>"
        "<option value='3'>%% + ICON</option>"
        "<option value='4'>V + ICON</option></select></div></section>"
        "<section class='card'><button class='btn primary'"
        " style='width:100%%' type='button' onclick='finish()'>"
        "Finish setup</button><div class='hint' id='msg'></div></section>"
        "</main><script>%s</script></body></html>",
        WEB_STYLE, utc_h, s_unit_mode, s_batt_view, utc_opts, s_ap_pass,
        wifi_esc, SETUP_JS);
    if (n < 0) {
        n = 0;
    } else if (n >= (int)sizeof(page)) {
        n = (int)sizeof(page) - 1;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, n);
}

static esp_err_t setup_json(httpd_req_t *req, const char *status,
                            const char *body)
{
    if (status) {
        httpd_resp_set_status(req, status);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}

esp_err_t http_setup_api_handler(httpd_req_t *req)
{
    if (s_setup_done) {
        return setup_json(req, "403 Forbidden",
                          "{\"ok\":false,\"error\":\"setup done\"}");
    }
    char body[512];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        return setup_json(req, "400 Bad Request",
                          "{\"ok\":false,\"error\":\"empty\"}");
    }
    body[len] = '\0';
    char action[16];
    form_get(body, "action", action, sizeof(action));
    if (strcmp(action, "wifi") == 0) {
        char ssid[33];
        char pass[65];
        form_get(body, "ssid", ssid, sizeof(ssid));
        form_get(body, "pass", pass, sizeof(pass));
        if (!ssid[0]) {
            return setup_json(req, "400 Bad Request",
                              "{\"ok\":false,\"error\":\"select network\"}");
        }
        snprintf(s_wifi_ssid, sizeof(s_wifi_ssid), "%s", ssid);
        snprintf(s_wifi_pass, sizeof(s_wifi_pass), "%s", pass);
        if (wifi_setup_connect(s_wifi_ssid, s_wifi_pass) != ESP_OK) {
            return setup_json(req, "500 Internal Server Error",
                              "{\"ok\":false,\"error\":\"wifi failed\"}");
        }
        return setup_json(req, NULL, "{\"ok\":true}");
    }
    if (strcmp(action, "finish") == 0) {
        char v[24];
        form_get(body, "utc", v, sizeof(v));
        int utc_h = atoi(v);
        if (utc_h < -12) utc_h = -12;
        if (utc_h > 14) utc_h = 14;
        s_utc_min = (int16_t)(utc_h * 60);
        form_get(body, "unit", v, sizeof(v));
        s_unit_mode = atoi(v) ? 1 : 0;
        s_ui.unit_mode = s_unit_mode;
        form_get(body, "batt", v, sizeof(v));
        int batt = atoi(v);
        if (batt < 0) batt = 0;
        if (batt > 4) batt = 4;
        s_batt_view = (uint8_t)batt;
        s_ui.batt_view = s_batt_view;
        form_get(body, "wifi", v, sizeof(v));
        int want_wifi = atoi(v) != 0;
        if (want_wifi && !s_wifi_ssid[0]) {
            return setup_json(req, "400 Bad Request",
                              "{\"ok\":false,\"error\":\"no wifi\"}");
        }
        web_cmd_t c;
        memset(&c, 0, sizeof(c));
        c.type = WEB_CMD_SETUP_FINISH;
        c.a = want_wifi ? 1 : 0;
        if (!web_cmd_push(&c)) {
            return setup_json(req, "500 Internal Server Error",
                              "{\"ok\":false,\"error\":\"busy\"}");
        }
        return setup_json(req, NULL, "{\"ok\":true}");
    }
    return setup_json(req, "400 Bad Request",
                      "{\"ok\":false,\"error\":\"unknown action\"}");
}

esp_err_t http_licenses_handler(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    char nav[2048];
    web_nav(nav, sizeof(nav), -1);
    int n = snprintf(page, sizeof(page),
        "<!doctype html><html><head><meta charset='utf-8'>" WEB_THEME_HEAD
        "<meta name='viewport' content='width=device-width,initial-scale=1,"
        "viewport-fit=cover'><title>Licenses - MINI-radio</title>"
        "<style>%s</style></head><body>"
        "<div class='top'><div class='brand'>Licenses</div>"
        "<span class='pill gray'>Notices</span></div>"
        "<main class='wrap'>"
        "<section class='card'><div class='sec'>MINIRADIO</div>"
        "<div class='sub'>MIT License. Copyright (c) miniradio"
        " Contributors.</div>"
        "<div class='sub'>Radio firmware author: GNBD.</div></section>"
        "<section class='card'><div class='sec'>DISPLAY PORT</div>"
        "<div class='sub'>ESP32-SI4732 Radio / Max Arnold. MIT License."
        " ST7789/GC9307 LovyanGFX backend.</div></section>"
        "<section class='card'><div class='sec'>LOVYANGFX</div>"
        "<div class='sub'>BSD 2-Clause License. Includes TFT_eSPI derived"
        " code under the FreeBSD License. See components/lovyan_gfx/"
        "license.txt.</div></section>"
        "<section class='card'><div class='sec'>LITTLEFS</div>"
        "<div class='sub'>joltwallet LittleFS. MIT License. See"
        " managed_components/joltwallet__littlefs/LICENSE.</div></section>"
        "<section class='card'><div class='sec'>ESP-IDF / MBEDTLS</div>"
        "<div class='sub'>Apache License 2.0.</div></section>"
        "<section class='card'><div class='sec'>SI4732 SSB PATCH</div>"
        "<div class='sub'>Pu2CLR SI4735 Arduino Library SSB patch data."
        " Silicon Laboratories AN332. Copyright respective authors.</div>"
        "</section></main>%s</body></html>",
        WEB_STYLE, nav);
    if (n < 0) {
        n = 0;
    } else if (n >= (int)sizeof(page)) {
        n = (int)sizeof(page) - 1;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, n);
}

void fmt_freq_web(uint32_t hz, radio_mode_t mode, char *buf, size_t n)
{
    if (mode == RADIO_MODE_FM) {
        snprintf(buf, n, "%lu.%02lu MHz",
                 (unsigned long)(hz / 1000000),
                 (unsigned long)((hz / 10000) % 100));
    } else if (s_unit_mode == 1) {
        snprintf(buf, n, "%lu kHz", (unsigned long)(hz / 1000));
    } else {
        snprintf(buf, n, "%lu.%03lu MHz",
                 (unsigned long)(hz / 1000000),
                 (unsigned long)((hz / 1000) % 1000));
    }
}

const char *wifi_state_str(void)
{
    if (s_wifi_mode == 3) {
        return s_sta_connected ? "STA" : "AP";
    }
    if (s_wifi_mode == 1) {
        return "AP";
    }
    if (s_wifi_mode == 2) {
        return "STA";
    }
    if (s_ui.wifi_state == 3) {
        return "SCAN";
    }
    return "OFF";
}

const char *wifi_ip_str(void)
{
    if (s_wifi_mode == 3) {
        return s_sta_connected ? s_sta_ip : "192.168.4.1";
    }
    if (s_wifi_mode == 1) {
        return "192.168.4.1";
    }
    if (s_wifi_mode == 2 && s_sta_connected) {
        return s_sta_ip;
    }
    return "";
}

void local_datetime(int *y, int *mo, int *d, int *h, int *mi);
int bw_max(void);

esp_err_t http_status_handler(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    char freq[28];
    char bands[320];
    char station[160];
    char ssid[80];
    char steps[320];
    char bws[240];

    fmt_freq_web(s_ui.freq_hz, s_ui.mode, freq, sizeof(freq));
    json_escape(station, sizeof(station), s_ui.station ? s_ui.station : "");
    json_escape(ssid, sizeof(ssid), s_wifi_ssid);

    int slot = mode_slot(s_ui.mode);
    size_t so = 0;
    steps[0] = '\0';
    for (int i = 0; i <= step_count(slot); ++i) {
        char lbl[16];
        if (i == 0) {
            snprintf(lbl, sizeof(lbl), "BAND");
        } else {
            int hz = (int)step_at(slot, i - 1);
            if (hz % 1000 == 0) {
                snprintf(lbl, sizeof(lbl), "%dK", hz / 1000);
            } else {
                snprintf(lbl, sizeof(lbl), "%dH", hz);
            }
        }
        int w = snprintf(steps + so, sizeof(steps) - so, "%s\"%s\"",
                         i ? "," : "", lbl);
        if (w < 0) {
            break;
        }
        so += (size_t)w;
        if (so >= sizeof(steps)) {
            so = sizeof(steps) - 1;
            break;
        }
    }
    steps[so] = '\0';

    static const char *fm[]  = {"AUTO", "110K", "84K", "60K", "40K"};
    static const char *am[]  = {"6K", "4K", "3K", "2K", "1K", "1.8K", "2.5K"};
    static const char *ssb[] = {"1.2K", "2.2K", "3K", "4K", "500", "1K"};
    so = 0;
    bws[0] = '\0';
    for (int i = 0; i <= bw_max(); ++i) {
        const char *lbl = (slot == 0) ? fm[i] : (slot == 1 ? am[i] : ssb[i]);
        int w = snprintf(bws + so, sizeof(bws) - so, "%s\"%s\"",
                         i ? "," : "", lbl);
        if (w < 0) {
            break;
        }
        so += (size_t)w;
        if (so >= sizeof(bws)) {
            so = sizeof(bws) - 1;
            break;
        }
    }
    bws[so] = '\0';

    int yy, mo, dd, hh, mm;
    local_datetime(&yy, &mo, &dd, &hh, &mm);
    int sq = mode_dsp_slot(s_ui.mode);

    size_t bo = 0;
    bands[0] = '\0';
    for (size_t i = 0; i < band_count && bo < sizeof(bands); ++i) {
        char name[32];
        json_escape(name, sizeof(name), band_table[i].name);
        int w = snprintf(bands + bo, sizeof(bands) - bo, "%s\"%s\"",
                         i ? "," : "", name);
        if (w < 0) {
            break;
        }
        bo += (size_t)w;
        if (bo >= sizeof(bands)) {
            bo = sizeof(bands) - 1;
        }
    }
    bands[bo] = '\0';

    int n = snprintf(page, sizeof(page),
        "{\"freq\":\"%s\",\"freq_hz\":%lu,\"band\":\"%s\",\"band_idx\":%d,"
        "\"bands\":[%s],\"mode\":\"%s\",\"stereo\":%s,"
        "\"step\":%d,\"steps\":[%s],\"bw\":%d,\"bws\":[%s],"
        "\"bfo\":%ld,\"bfo_max\":%d,\"ssb\":%s,"
        "\"rds\":%s,\"stereo_force\":%s,\"deemph75\":%s,\"agc\":%s,"
        "\"avc\":%u,\"softmute\":%u,\"squelch_on\":%s,\"squelch\":%u,"
        "\"squelch_type\":\"%s\","
        "\"rssi\":%d,\"snr\":%d,\"volume\":%u,\"vmax\":%u,"
        "\"battery\":%u,\"mv\":%u,\"preset\":%d,"
        "\"wifi\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\","
        "\"utc_min\":%d,\"follow\":%s,"
        "\"date\":\"%04d-%02d-%02d\",\"clock\":\"%02d:%02d\","
        "\"seeking\":%s,\"radio\":%s,\"station\":\"%s\"}",
        freq, (unsigned long)s_ui.freq_hz, band_table[s_ui.band_idx].name,
        s_ui.band_idx, bands, mode_str(s_ui.mode),
        (s_ui.mode == RADIO_MODE_FM && s_ui.stereo) ? "true" : "false",
        (s_step_idx[slot] < 0) ? 0 : (s_step_idx[slot] + 1), steps,
        s_bw[slot], bws, (long)s_bfo, BFO_MAX_HZ,
        band_is_ssb(s_ui.mode) ? "true" : "false",
        s_rds_on ? "true" : "false",
        s_stereo_force ? "true" : "false",
        s_deemph_75 ? "true" : "false",
        s_agc_on ? "true" : "false",
        s_avc, s_softmute,
        s_squelch_on ? "true" : "false",
        s_squelch_on ? s_squelch_thr[sq] : 0,
        s_squelch_type[sq] ? "SNR" : "RSSI",
        s_ui.have_rsq ? s_ui.rsq.rssi : 0,
        s_ui.have_rsq ? s_ui.rsq.snr : 0,
        s_ui.volume, (unsigned)AUDIO_VOLUME_MAX,
        s_ui.have_battery ? s_ui.battery.percent : 0,
        s_ui.have_battery ? s_ui.battery.millivolt : 0,
        s_ui.preset_slot, wifi_state_str(), wifi_ip_str(), ssid,
        s_utc_min, s_follow_browser ? "true" : "false",
        yy, mo, dd, hh, mm,
        s_ui.seeking ? "true" : "false", s_ui.radio_ok ? "true" : "false",
        station);
    if (n < 0) {
        n = 0;
    } else if (n >= (int)sizeof(page)) {
        n = (int)sizeof(page) - 1;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, n);
}

esp_err_t http_wifi_api_handler(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    static char nets[2048];
    size_t o = 0;
    nets[0] = '\0';

    if (!s_scan_running) {
        for (uint16_t i = 0; i < s_scan_n; ++i) {
            if (s_scan_buf[i][0] == '\0') {
                continue;
            }
            char e[200];
            json_escape(e, sizeof(e), s_scan_buf[i]);
            size_t need = strlen(e) + 64;
            if (sizeof(nets) - o <= need) {
                break;
            }
            int w = snprintf(nets + o, sizeof(nets) - o,
                             "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
                             o ? "," : "", e, s_scan_rssi[i],
                             s_scan_secure[i] ? "true" : "false");
            if (w < 0 || (size_t)w >= sizeof(nets) - o) {
                break;
            }
            o += (size_t)w;
            nets[o] = '\0';
        }
    }

    char ssid_esc[80];
    json_escape(ssid_esc, sizeof(ssid_esc), s_wifi_ssid);

    int n = snprintf(page, sizeof(page),
        "{\"state\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,"
        "\"scanning\":%s,\"networks\":[%s]}",
        wifi_state_str(), wifi_ip_str(), ssid_esc,
        s_ui.wifi_rssi ? s_ui.wifi_rssi : 0,
        s_scan_running ? "true" : "false", nets);
    if (n < 0) {
        n = 0;
    } else if (n >= (int)sizeof(page)) {
        n = (int)sizeof(page) - 1;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, n);
}

bool web_cmd_push(const web_cmd_t *c)
{
    if (!s_web_q || c->type == WEB_CMD_NONE) {
        return false;
    }
    return xQueueSend(s_web_q, c, 0) == pdTRUE;
}

bool web_back_target(const char *back, char *out, size_t n)
{
    if (strcmp(back, "/") == 0 || strcmp(back, "/presets") == 0 ||
        strcmp(back, "/wifi") == 0 || strcmp(back, "/info") == 0) {
        size_t l = strlen(back);
        if (l < n) {
            memcpy(out, back, l + 1);
            return true;
        }
    }
    return false;
}

static bool parse_int(const char *s, int32_t *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < -2147483647L || v > 2147483647L) {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

static bool request_is_same_origin(httpd_req_t *req)
{
    char site[32];
    if (httpd_req_get_hdr_value_str(req, "Sec-Fetch-Site", site, sizeof(site)) == ESP_OK) {
        if (strcmp(site, "same-origin") != 0 && strcmp(site, "none") != 0) {
            return false;
        }
    }
    char origin[128];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK) {
        char host[64];
        const char *h = strstr(origin, "://");
        if (!h || httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
            strcmp(h + 3, host) != 0) {
            return false;
        }
    }
    return true;
}

esp_err_t http_cmd_handler(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    char q[512];
    char val[96];
    char arg[96];
    char back[16] = "";
    char back_url[16] = "";
    web_cmd_t c;
    memset(&c, 0, sizeof(c));

    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad query");
        return ESP_FAIL;
    }
    if (!request_is_same_origin(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "forbidden");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(q, "back", val, sizeof(val)) == ESP_OK) {
        url_decode(val);
        if (web_back_target(val, back_url, sizeof(back_url))) {
            snprintf(back, sizeof(back), "%s", back_url);
        }
    }

    if (httpd_query_key_value(q, "tune", val, sizeof(val)) == ESP_OK) {
        c.type = WEB_CMD_TUNE_STEP;
        c.a = (strcmp(val, "up") == 0) ? 1 : -1;
    } else if (httpd_query_key_value(q, "freq", val, sizeof(val)) == ESP_OK) {
        char *end = NULL;
        double f = strtod(val, &end);
        if (end == val || *end != '\0' || f <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad freq");
            return ESP_FAIL;
        }
        bool fm_band = (band_table[s_ui.band_idx].default_mode == RADIO_MODE_FM);
        bool mhz = fm_band || (strchr(val, '.') != NULL);
        double hz = mhz ? f * 1000000.0 : f * 1000.0;
        if (hz > 300000000.0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad freq");
            return ESP_FAIL;
        }
        c.type = WEB_CMD_TUNE_FREQ;
        c.a = (int32_t)hz;
    } else if (httpd_query_key_value(q, "volume", val, sizeof(val)) == ESP_OK) {
        c.type = WEB_CMD_VOLUME;
        if (!parse_int(val, &c.a)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad volume");
            return ESP_FAIL;
        }
    } else if (httpd_query_key_value(q, "band", val, sizeof(val)) == ESP_OK) {
        c.type = WEB_CMD_BAND;
        if (!parse_int(val, &c.a)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad band");
            return ESP_FAIL;
        }
    } else if (httpd_query_key_value(q, "mode", val, sizeof(val)) == ESP_OK) {
        if (strcmp(val, "FM") == 0) {
            c.a = RADIO_MODE_FM;
        } else if (strcmp(val, "LSB") == 0) {
            c.a = RADIO_MODE_LSB;
        } else if (strcmp(val, "USB") == 0) {
            c.a = RADIO_MODE_USB;
        } else {
            c.a = RADIO_MODE_AM;
        }
        c.type = WEB_CMD_MODE;
    } else if (httpd_query_key_value(q, "preset", val, sizeof(val)) == ESP_OK) {
        c.type = WEB_CMD_PRESET_TUNE;
        if (!parse_int(val, &c.a)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad preset");
            return ESP_FAIL;
        }
    } else if (httpd_query_key_value(q, "save", val, sizeof(val)) == ESP_OK) {
        c.type = WEB_CMD_PRESET_SAVE;
    } else if (httpd_query_key_value(q, "del", val, sizeof(val)) == ESP_OK) {
        c.type = WEB_CMD_PRESET_DEL;
        if (!parse_int(val, &c.a)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad preset");
            return ESP_FAIL;
        }
    } else if (httpd_query_key_value(q, "radio", val, sizeof(val)) == ESP_OK) {
        int item = -1;
        if (strcmp(val, "step") == 0) {
            item = RM_STEP;
        } else if (strcmp(val, "bw") == 0) {
            item = RM_BW;
        } else if (strcmp(val, "bfo") == 0) {
            item = RM_BFO;
        } else if (strcmp(val, "rds") == 0) {
            item = RM_RDS;
        } else if (strcmp(val, "stereo") == 0) {
            item = RM_STEREO;
        } else if (strcmp(val, "deemph") == 0) {
            item = RM_DEEMPH;
        } else if (strcmp(val, "agc") == 0) {
            item = RM_AGC;
        } else if (strcmp(val, "avc") == 0) {
            item = RM_AVC;
        } else if (strcmp(val, "softmute") == 0) {
            item = RM_SOFTMUTE;
        } else if (strcmp(val, "squelch") == 0) {
            item = RM_SQUELCH;
        } else if (strcmp(val, "ats") == 0) {
            item = RM_ATS;
        }
        if (item >= 0) {
            c.type = WEB_CMD_RADIO;
            c.a = item;
            if (httpd_query_key_value(q, "v", arg, sizeof(arg)) != ESP_OK ||
                !parse_int(arg, &c.b)) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad value");
                return ESP_FAIL;
            }
            if (item == RM_BFO) {
                if (c.b > BFO_MAX_HZ) { c.b = BFO_MAX_HZ; }
                if (c.b < -BFO_MAX_HZ) { c.b = -BFO_MAX_HZ; }
            } else if (item == RM_BW) {
                if (c.b < 0) { c.b = 0; }
                if (c.b > bw_max()) { c.b = bw_max(); }
            } else if (item == RM_STEP) {
                int max = step_count(mode_slot(s_ui.mode));
                if (c.b < 0) { c.b = 0; }
                if (c.b > max) { c.b = max; }
            }
        }
    } else if (httpd_query_key_value(q, "set", val, sizeof(val)) == ESP_OK) {
        if (strcmp(val, "utc") == 0) {
            c.type = WEB_CMD_UTC;
            if (httpd_query_key_value(q, "v", arg, sizeof(arg)) != ESP_OK ||
                !parse_int(arg, &c.a)) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad value");
                return ESP_FAIL;
            }
        } else if (strcmp(val, "time") == 0) {
            if (httpd_query_key_value(q, "v", arg, sizeof(arg)) == ESP_OK) {
                snprintf(c.s1, sizeof(c.s1), "%.*s", (int)sizeof(c.s1) - 1, arg);
                c.type = WEB_CMD_TIME;
            }
        } else if (strcmp(val, "follow") == 0) {
            c.type = WEB_CMD_FOLLOW;
            if (httpd_query_key_value(q, "v", arg, sizeof(arg)) != ESP_OK ||
                !parse_int(arg, &c.a)) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad value");
                return ESP_FAIL;
            }
        }
    } else if (httpd_query_key_value(q, "wifi", val, sizeof(val)) == ESP_OK) {
        if (strcmp(val, "off") == 0) {
            c.type = WEB_CMD_WIFI_OFF;
        } else if (strcmp(val, "ap") == 0) {
            c.type = WEB_CMD_WIFI_AP;
        } else if (strcmp(val, "scan") == 0) {
            c.type = WEB_CMD_WIFI_SCAN;
        } else {
            char ssid[200] = "";
            char pass[256] = "";
            if (httpd_query_key_value(q, "ssid", ssid, sizeof(ssid)) == ESP_OK) {
                url_decode(ssid);
            }
            if (httpd_query_key_value(q, "pass", pass, sizeof(pass)) == ESP_OK) {
                url_decode(pass);
            }
            snprintf(c.s1, sizeof(c.s1), "%s", ssid);
            snprintf(c.s2, sizeof(c.s2), "%s", pass);
            c.type = WEB_CMD_WIFI_STA;
        }
    }

    if (c.type == WEB_CMD_NONE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown cmd");
        return ESP_FAIL;
    }
    if (!web_cmd_push(&c)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "busy");
        return ESP_FAIL;
    }
    esp_err_t err;
    if (back[0]) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", back);
        httpd_resp_set_type(req, "text/plain");
        err = httpd_resp_send(req, "OK", 2);
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        err = httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    return err;
}

esp_err_t http_info_handler(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    char nav[2048];
    web_nav(nav, sizeof(nav), 4);

    int n = snprintf(page, sizeof(page),
        "<!doctype html><html><head><meta charset='utf-8'>" WEB_THEME_HEAD
        "<meta name='viewport' content='width=device-width,initial-scale=1,"
        "viewport-fit=cover'><meta name='theme-color' content='#eef1f7'>"
        "<title>Info - MINI-radio</title><style>%s</style></head><body>"
        "<div class='top'><div class='brand'>Info</div>"
        "<span class='pill gray'>GNBD firmware</span></div>"
        "<main class='wrap'><section class='card'>"
        "<div class='rowline'><span class='k'>Project</span>"
        "<span class='v'>%s</span></div>"
        "<div class='rowline'><span class='k'>Version</span>"
        "<span class='v'>%s</span></div>"
        "<div class='rowline'><span class='k'>ESP-IDF</span>"
        "<span class='v'>%s</span></div>"
        "<div class='rowline'><span class='k'>Build</span>"
        "<span class='v'>%s</span></div>"
        "<div class='rowline'><span class='k'>Uptime</span>"
        "<span class='v'>%lu s</span></div></section>"
        "<section class='card'><div class='sec'>MEMORY</div>"
        "<div class='rowline'><span class='k'>Free heap</span>"
        "<span class='v'>%lu B</span></div>"
        "<div class='rowline'><span class='k'>Minimum free heap</span>"
        "<span class='v'>%lu B</span></div></section>"
        "<section class='card'><a class='btn soft' style='width:100%%'"
        " href='/licenses'>Licenses &amp; notices</a></section>"
        "</main>%s</body></html>",
        WEB_STYLE, app_project_name(), app_version(), app_idf_version(),
        app_build_no(), (unsigned long)(now_ms() / 1000),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size(), nav);
    if (n < 0) {
        n = 0;
    } else if (n >= (int)sizeof(page)) {
        n = (int)sizeof(page) - 1;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, n);
}

esp_err_t http_presets_handler(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    char q[128];
    char val[16];
    web_cmd_t c;
    memset(&c, 0, sizeof(c));

    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "del", val, sizeof(val)) == ESP_OK) {
            c.type = WEB_CMD_PRESET_DEL;
            if (!parse_int(val, &c.a)) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad preset");
                return ESP_FAIL;
            }
        } else if (httpd_query_key_value(q, "save", val, sizeof(val)) == ESP_OK) {
            c.type = WEB_CMD_PRESET_SAVE;
        }
    }
    if (c.type != WEB_CMD_NONE) {
        if (!web_cmd_push(&c)) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "busy");
            return ESP_FAIL;
        }
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/presets");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "OK", 2);
    }

    char nav[2048];
    web_nav(nav, sizeof(nav), 1);

    int n = snprintf(page, sizeof(page),
        "<!doctype html><html><head><meta charset='utf-8'>" WEB_THEME_HEAD
        "<meta name='viewport' content='width=device-width,initial-scale=1,"
        "viewport-fit=cover'><meta name='theme-color' content='#eef1f7'>"
        "<title>Presets - MINI-radio</title><style>%s</style></head><body>"
        "<div class='top'><div class='brand'>Presets</div>"
        "<span class='pill'>%d / %d</span></div>"
        "<main class='wrap'><section class='card'>",
        WEB_STYLE, s_presets.count, STORAGE_PRESET_SLOTS);

    if (s_presets.count == 0) {
        n += snprintf(page + n, sizeof(page) - (size_t)n,
                      "<div class='empty'>No presets saved yet.</div>");
    }

    for (int i = 0; i < s_presets.count && n > 0 && n < (int)sizeof(page);
         ++i) {
        const preset_entry_t *p = &s_presets.slot[i];
        const char *bn = (p->band < band_count) ? band_table[p->band].name : "?";
        char f[28];
        fmt_freq_web(p->freq_hz, (radio_mode_t)p->mode, f, sizeof(f));
        char bn_esc[48];
        html_escape(bn_esc, sizeof(bn_esc), bn);
        int w = snprintf(page + n, sizeof(page) - (size_t)n,
                         "<div class='item'><a class='pick' href='/api/cmd?"
                         "preset=%d&back=/presets'><span class='num'>P%02d</span>"
                         "<span class='txt'><b>%s</b><small>%s &middot; %s</small>"
                         "</span></a><a class='del' href='/api/cmd?del=%d"
                         "&back=/presets'>Delete</a></div>",
                         i, i + 1, f, bn_esc,
                         mode_str((radio_mode_t)p->mode), i);
        if (w < 0) {
            break;
        }
        n += w;
    }
    if (n < 0) {
        n = 0;
    }
    if (n > (int)sizeof(page) - 1) {
        n = (int)sizeof(page) - 1;
    }
    int w = snprintf(page + n, sizeof(page) - (size_t)n,
        "</section><div class='btns'>"
        "<a class='btn primary' style='width:100%%'"
        " href='/api/cmd?save=1&back=/presets'>Save current station</a>"
        "</div></main>%s</body></html>", nav);
    if (w > 0) {
        n += w;
        if (n > (int)sizeof(page) - 1) {
            n = (int)sizeof(page) - 1;
        }
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, n);
}

esp_err_t http_wifi_handler(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    char q[256];
    char ssid[200] = "";
    char pass[256] = "";
    char mode[8] = "";
    char ssid_esc[200];
    web_cmd_t c;
    memset(&c, 0, sizeof(c));

    if (!request_is_same_origin(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "forbidden");
        return ESP_FAIL;
    }

    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "mode", mode, sizeof(mode));
        if (httpd_query_key_value(q, "ssid", ssid, sizeof(ssid)) == ESP_OK) {
            url_decode(ssid);
        }
        if (httpd_query_key_value(q, "pass", pass, sizeof(pass)) == ESP_OK) {
            url_decode(pass);
        }
    }

    if (strcmp(mode, "off") == 0) {
        c.type = WEB_CMD_WIFI_OFF;
    } else if (strcmp(mode, "ap") == 0) {
        c.type = WEB_CMD_WIFI_AP;
    } else if (ssid[0]) {
        snprintf(c.s1, sizeof(c.s1), "%s", ssid);
        snprintf(c.s2, sizeof(c.s2), "%s", pass);
        c.type = WEB_CMD_WIFI_STA;
    }
    if (c.type != WEB_CMD_NONE) {
        if (!web_cmd_push(&c)) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "busy");
            return ESP_FAIL;
        }
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/wifi");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "OK", 2);
    }

    html_escape(ssid_esc, sizeof(ssid_esc), s_wifi_ssid);
    const char *refresh = s_scan_running
        ? "<meta http-equiv='refresh' content='3'>" : "";

    static char scan_html[4096];
    size_t so = 0;
    scan_html[0] = '\0';
    if (s_scan_running) {
        snprintf(scan_html, sizeof(scan_html),
                 "<div class='sub'>Scanning...</div>");
    } else if (s_scan_n == 0) {
        snprintf(scan_html, sizeof(scan_html),
                 "<div class='sub'>No networks yet. Tap Scan.</div>");
    } else {
        for (uint16_t i = 0; i < s_scan_n && so < sizeof(scan_html); ++i) {
            if (s_scan_buf[i][0] == '\0') {
                continue;
            }
            char e[200];
            html_escape(e, sizeof(e), s_scan_buf[i]);
            int w = snprintf(scan_html + so, sizeof(scan_html) - so,
                             "<label class='ap'><input type='radio' name='ssid'"
                             " value='%s'><span>%s</span><span class='rssi'>"
                             "%d dBm%s</span></label>",
                             e, e, s_scan_rssi[i],
                             s_scan_secure[i] ? " &#128274;" : "");
            if (w < 0) {
                break;
            }
            so += (size_t)w;
            if (so >= sizeof(scan_html)) {
                so = sizeof(scan_html) - 1;
            }
            scan_html[so] = '\0';
        }
    }

    char nav[2048];
    web_nav(nav, sizeof(nav), 2);

    int n = snprintf(page, sizeof(page),
        "<!doctype html><html><head><meta charset='utf-8'>%s" WEB_THEME_HEAD
        "<meta name='viewport' content='width=device-width,initial-scale=1,"
        "viewport-fit=cover'><meta name='theme-color' content='#eef1f7'>"
        "<title>Wi-Fi - MINI-radio</title><style>%s</style></head><body>"
        "<div class='top'><div class='brand'>Wi-Fi</div>"
        "<span class='pill'>%s</span></div><main class='wrap'>"
        "<section class='card'>"
        "<div class='rowline'><span class='k'>State</span>"
        "<span class='v'>%s</span></div>"
        "<div class='rowline'><span class='k'>IP address</span>"
        "<span class='v'>%s</span></div>"
        "<div class='rowline'><span class='k'>SSID</span>"
        "<span class='v'>%s</span></div>"
        "<div class='rowline'><span class='k'>Signal</span>"
        "<span class='v'>%d dBm</span></div></section>"
        "<section class='card'><div class='sec'>MANUAL CONNECTION</div>"
        "<form action='/api/cmd' method='get'>"
        "<input type='hidden' name='wifi' value='sta'>"
        "<input name='ssid' placeholder='SSID' value='%s'>"
        "<input name='pass' type='password' placeholder='Password'>"
        "<button class='btn primary' style='width:100%%;margin-top:8px'>"
        "Connect</button></form></section>"
        "<section class='card'><div class='sec'>AVAILABLE NETWORKS</div>"
        "<form action='/api/cmd' method='get'>"
        "<input type='hidden' name='wifi' value='sta'>%s"
        "<input name='pass' type='password' placeholder='Password'>"
        "<button class='btn primary' style='width:100%%;margin-top:8px'>"
        "Connect to selected</button></form>"
        "<div class='sub'>Network scan is available in STA mode.</div></section>"
        "<section class='card'><div class='sec'>NETWORK SCAN</div>"
        "<div class='btns'>"
        "<a class='btn soft' href='/api/cmd?wifi=scan&back=/wifi'>Scan</a>"
        "</div></section></main>%s</body></html>",
        refresh, WEB_STYLE, wifi_state_str(), wifi_state_str(), wifi_ip_str(),
        s_wifi_ssid[0] ? ssid_esc : "-",
        s_ui.wifi_rssi ? s_ui.wifi_rssi : 0,
        ssid_esc, scan_html, nav);
    if (n < 0) {
        n = 0;
    } else if (n >= (int)sizeof(page)) {
        n = (int)sizeof(page) - 1;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, n);
}

esp_err_t http_settings_handler(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return web_require_auth(req);
    }
    char nav[2048];
    web_nav(nav, sizeof(nav), 3);
    char uesc[64];
    html_escape(uesc, sizeof(uesc), s_web_user);
    const char *wa_checked = s_web_auth_on ? "checked" : "";

    int n = snprintf(page, sizeof(page),
        "<!doctype html><html><head><meta charset='utf-8'>" WEB_THEME_HEAD
        "<meta name='viewport' content='width=device-width,initial-scale=1,"
        "viewport-fit=cover'><meta name='theme-color' content='#eef1f7'>"
        "<title>Settings - MINI-radio</title><style>%s</style></head><body>"
        "<div class='top'><div class='brand'>Settings</div>"
        "<span class='pill gray'>Clock</span></div>"
        "<main class='wrap'><section class='card'>"
        "<div class='rowline'><span class='k'>Device time</span>"
        "<span class='v' id='clock'>--</span></div>"
        "<div class='setrow'><span class='k'>UTC offset</span>"
        "<select id='utc' onchange='setUtc()'></select></div>"
        "<div class='sec' style='margin-top:18px'>SET TIME</div>"
        "<input id='time' type='datetime-local'"
        " oninput='timeTouched=true'>"
        "<button class='btn primary' style='width:100%%;margin-top:8px'"
        " onclick='setTime()'>Set time</button>"
        "<div class='setrow' style='margin-top:18px'>"
        "<span class='k'>Follow browser time</span>"
        "<select id='follow' onchange='setFollow()'>"
        "<option value='0'>OFF</option><option value='1'>ON</option>"
        "</select></div>"
        "<div class='btns' style='margin-top:10px'>"
        "<button class='btn soft' onclick='syncNow()'>Sync now</button></div>"
        "<div class='hint'>When ON, the clock syncs with this browser"
        " while this page is open.</div></section>"
        "<section class='card'><div class='sec'>APPEARANCE</div>"
        "<div class='setrow'><span class='k'>Theme</span>"
        "<select id='theme' onchange='setTheme(this.value)'>"
        "<option value='auto'>Auto</option><option value='light'>Light</option>"
        "<option value='dark'>Dark</option></select></div>"
        "<div class='hint'>Auto follows your device setting.</div>"
        "</section><section class='card'><div class='sec'>WEB LOGIN</div>"
        "<div class='setrow'><span class='k'>Require login</span>"
        "<label class='switch'><input type='checkbox' id='wa'"
        " onchange='setWebAuth(this.checked)' %s>"
        "<span class='slider'></span></label></div>"
        "<form method='post' action='/api/webauth'>"
        "<label class='k'>ID</label><input name='u' maxlength='15'"
        " value='%s' required>"
        "<label class='k'>PW</label><input name='p' type='password'"
        " maxlength='31' placeholder='blank = keep current'>"
        "<button class='btn primary' style='width:100%%;margin-top:8px'"
        " type='submit'>Save login</button></form>"
        "<div class='hint'>Browser login (HTTP Basic). "
        "Password blank keeps current. Initial: admin / 12345678</div>"
        "</section></main>%s"
        "<script>%s</script></body></html>",
        WEB_STYLE, wa_checked, uesc, nav,
        "const $=id=>document.getElementById(id);"
        "function setWebAuth(v){fetch('/api/webauth?on='+(v?1:0),"
        "{method:'POST'}).then(()=>location.reload());}"
        "let followOn=false,timeTouched=false;"
        "function setTheme(v){try{if(v==='auto'){"
        "delete document.documentElement.dataset.theme;"
        "localStorage.removeItem('mrTheme');}else{"
        "document.documentElement.dataset.theme=v;"
        "localStorage.setItem('mrTheme',v);}}catch(e){}}"
        "(function(){try{$('theme').value=localStorage.getItem('mrTheme')"
        "||'auto';}catch(e){}})();"
        "(function(){const u=$('utc');for(let h=-12;h<=14;h++){"
        "const o=document.createElement('option');o.value=h*60;"
        "o.textContent='UTC'+(h>=0?'+':'-')+"
        "String(Math.abs(h)).padStart(2,'0')+':00';u.appendChild(o);}})();"
        "function setUtc(){fetch('/api/cmd?set=utc&v='+$('utc').value,"
        "{cache:'no-store'}).then(()=>setTimeout(loadStatus,300));}"
        "function syncNow(){fetch('/api/cmd?set=time&v='+"
        "Math.floor(Date.now()/1000),{cache:'no-store'}).then(()=>setTimeout(loadStatus,300));}"
        "function setFollow(){const v=$('follow').value==='1';followOn=v;"
        "fetch('/api/cmd?set=follow&v='+(v?1:0),{cache:'no-store'});"
        "if(v)syncNow();}"
        "function setTime(){const v=$('time').value;if(!v)return;"
        "const [d,t]=v.split('T');const [y,mo,da]=d.split('-').map(Number);"
        "const [h,mi]=t.split(':').map(Number);const off=parseInt($('utc').value)||0;"
        "const e=Math.floor(Date.UTC(y,mo-1,da,h,mi)/1000)-off*60;"
        "followOn=false;$('follow').value='0';"
        "fetch('/api/cmd?set=follow&v=0',{cache:'no-store'}).then(()=>"
        "fetch('/api/cmd?set=time&v='+e,{cache:'no-store'}))"
        ".then(()=>{timeTouched=false;setTimeout(loadStatus,400);});}"
        "async function loadStatus(){try{const r=await fetch('/api/status',"
        "{cache:'no-store'});const s=await r.json();"
        "$('clock').textContent=s.date+' '+s.clock;"
        "if(document.activeElement!==$('utc'))$('utc').value=s.utc_min;"
        "$('follow').value=s.follow?'1':'0';"
        "if(!timeTouched&&document.activeElement!==$('time'))"
        "$('time').value=s.date+'T'+s.clock;"
        "if(s.follow&&!followOn){followOn=true;syncNow();}"
        "else if(!s.follow)followOn=false;"
        "}catch(e){$('clock').textContent='Offline';}}"
        "loadStatus();setInterval(loadStatus,1000);"
        "setInterval(()=>{if(followOn)syncNow();},60000);");
    if (n < 0) {
        n = 0;
    } else if (n >= (int)sizeof(page)) {
        n = (int)sizeof(page) - 1;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, n);
}

void http_start(void)
{
    if (s_http) {
        return;
    }
    if (!s_web_q) {
        s_web_q = xQueueCreate(8, sizeof(web_cmd_t));
    }
    web_auth_init();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.stack_size = 12288;
    cfg.max_uri_handlers = 16;
    if (httpd_start(&s_http, &cfg) != ESP_OK) {
        s_http = NULL;
        return;
    }
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = http_root_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = http_status_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t cmd = {
        .uri = "/api/cmd",
        .method = HTTP_GET,
        .handler = http_cmd_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t wifi_api = {
        .uri = "/api/wifi",
        .method = HTTP_GET,
        .handler = http_wifi_api_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t info = {
        .uri = "/info",
        .method = HTTP_GET,
        .handler = http_info_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t presets = {
        .uri = "/presets",
        .method = HTTP_GET,
        .handler = http_presets_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t wifi = {
        .uri = "/wifi",
        .method = HTTP_GET,
        .handler = http_wifi_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t settings = {
        .uri = "/settings",
        .method = HTTP_GET,
        .handler = http_settings_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t webauth = {
        .uri = "/api/webauth",
        .method = HTTP_POST,
        .handler = http_webauth_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t shot = {
        .uri = "/api/screenshot",
        .method = HTTP_GET,
        .handler = http_screenshot_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t setup = {
        .uri = "/setup",
        .method = HTTP_GET,
        .handler = http_setup_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t setup_api = {
        .uri = "/api/setup",
        .method = HTTP_POST,
        .handler = http_setup_api_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t licenses = {
        .uri = "/licenses",
        .method = HTTP_GET,
        .handler = http_licenses_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_http, &webauth);
    httpd_register_uri_handler(s_http, &shot);
    httpd_register_uri_handler(s_http, &setup);
    httpd_register_uri_handler(s_http, &setup_api);
    httpd_register_uri_handler(s_http, &licenses);
    httpd_register_uri_handler(s_http, &root);
    httpd_register_uri_handler(s_http, &status);
    httpd_register_uri_handler(s_http, &cmd);
    httpd_register_uri_handler(s_http, &wifi_api);
    httpd_register_uri_handler(s_http, &info);
    httpd_register_uri_handler(s_http, &presets);
    httpd_register_uri_handler(s_http, &wifi);
    httpd_register_uri_handler(s_http, &settings);
}

void http_stop(void)
{
    if (s_http) {
        httpd_stop(s_http);
        s_http = NULL;
    }
}

