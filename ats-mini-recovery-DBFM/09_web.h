// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// Part of ats-mini-recovery-theme. Included by the main .ino in this order.

// ---------------------------------------------------------------------------
// Background web server (always running unless disabled)
// ---------------------------------------------------------------------------

static String htmlEsc(const String &s)
{
  String o = s;
  o.replace("&", "&amp;");
  o.replace("<", "&lt;");
  o.replace(">", "&gt;");
  o.replace("\"", "&quot;");
  o.replace("'", "&#39;");
  return o;
}

static String fmtBytes(uint32_t b)
{
  char buf[24];
  if(b >= 1024UL * 1024)    snprintf(buf, sizeof(buf), "%.1f MB", b / 1048576.0);
  else if(b >= 1024UL * 10) snprintf(buf, sizeof(buf), "%.0f KB", b / 1024.0);
  else if(b >= 1024UL)      snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
  else                      snprintf(buf, sizeof(buf), "%u B", (unsigned)b);
  return String(buf);
}

static uint32_t appImageSize(const esp_partition_t *p)
{
  if(p->type != ESP_PARTITION_TYPE_APP) return 0;

  uint8_t hdr[24];
  if(esp_partition_read(p, 0, hdr, sizeof(hdr)) != ESP_OK) return 0;
  if(hdr[0] != 0xE9) return 0;

  uint8_t segs = hdr[1];
  if(segs == 0 || segs > 16) return 0;
  bool hashAppended = (hdr[23] != 0);

  uint32_t off = 24;
  uint32_t total = 24;
  for(uint8_t i = 0; i < segs; i++)
  {
    uint8_t sh[8];
    if(esp_partition_read(p, off, sh, sizeof(sh)) != ESP_OK) return 0;
    uint32_t len = (uint32_t)sh[4] | ((uint32_t)sh[5] << 8) |
                   ((uint32_t)sh[6] << 16) | ((uint32_t)sh[7] << 24);
    if(len == 0 || off + 8 + len > p->size) return 0;
    off += 8 + len;
    total += 8 + len;
  }
  if(hashAppended) total += 32;
  return (total <= p->size) ? total : 0;
}

static String buildPartitionRows()
{
  String rows;
  esp_partition_iterator_t it =
    esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);

  while(it)
  {
    const esp_partition_t *p = esp_partition_get(it);

    uint32_t size = p->size;
    uint32_t used = 0;
    bool known = false;

    if(p->type == ESP_PARTITION_TYPE_APP)
    {
      used = appImageSize(p);
      known = true;
    }
    else if(strcmp(p->label, "littlefs") == 0)
    {
      size = (uint32_t)LittleFS.totalBytes();
      used = (uint32_t)LittleFS.usedBytes();
      known = true;
    }

    rows += "<div class='row'><span><span class='mono'>";
    rows += p->label;
    rows += "</span><span class='muted sm' style='display:block'>0x";
    {
      char a[16];
      snprintf(a, sizeof(a), "%06X", (unsigned)p->address);
      rows += a;
    }
    rows += " &middot; ";
    rows += fmtBytes(p->size);
    rows += "</span></span>";

    if(known)
    {
      uint32_t freeb = (size > used) ? (size - used) : 0;
      int pct = size ? (int)((uint64_t)used * 100 / size) : 0;
      rows += "<span class='mono sm' style='text-align:right'>" + fmtBytes(used) +
              " used<span class='muted' style='display:block'>" + fmtBytes(freeb) +
              " free &middot; " + String(pct) + "%</span></span>";
    }
    else
    {
      rows += "<span class='mono sm muted' style='text-align:right'>used &mdash;</span>";
    }

    rows += "</div>";
    it = esp_partition_next(it);
  }
  return rows;
}

static String buildFileRows()
{
  String names[32];
  int n = 0;
  File root = LittleFS.open("/", "r");
  if(root && root.isDirectory())
  {
    File f = root.openNextFile();
    while(f && n < 32)
    {
      if(!f.isDirectory()) names[n++] = String(f.name());
      f = root.openNextFile();
    }
    root.close();
  }

  for(int i = 1; i < n; i++)
  {
    String k = names[i];
    int j = i - 1;
    while(j >= 0 && names[j] > k) { names[j + 1] = names[j]; j--; }
    names[j + 1] = k;
  }

  if(n == 0) return "<div class='row'><span class='muted'>(no files)</span></div>";

  String rows;
  for(int i = 0; i < n; i++)
  {
    File f = LittleFS.open("/" + names[i], "r");
    uint32_t sz = f ? f.size() : 0;
    if(f) f.close();

    String en = htmlEsc(names[i]);
    rows += "<div class='row'><div class='fname'>" + en +
            "<div class='muted sm'>" + fmtBytes(sz) + "</div></div><div class='acts'>";
    rows += "<button class='btn sm get' data-act='get' data-name='" + en + "'>Get</button>";
    if(names[i].endsWith(".bin"))
    {
      rows += "<button class='btn sm' data-act='flash' data-slot='0' data-name='" + en + "'>App0</button>";
      rows += "<button class='btn sm' data-act='flash' data-slot='1' data-name='" + en + "'>App1</button>";
    }
    rows += "<button class='btn sm del' data-act='del' data-name='" + en + "'>Del</button>";
    rows += "</div></div>";
  }
  return rows;
}

static void handleRoot()
{
  if(!webAuthed()) return;
  uint32_t fsUsed  = LittleFS.usedBytes();
  uint32_t fsTotal = LittleFS.totalBytes();
  int fsPct = fsTotal ? (int)((uint64_t)fsUsed * 100 / fsTotal) : 0;

  String html = F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>DBFM</title><style>"
    ":root{--bg:#070b14;--panel:#0f1726;--panel2:#16223a;--line:#243450;"
    "--text:#eaf2ff;--muted:#8fa6c7;--acc:#5b8cff;--gold:#ffc266;"
    "--ok:#5ee6a8;--warn:#f0a36b;--red:#ef6b6b}"
    "*{box-sizing:border-box}"
    "body{margin:0;min-height:100vh;color:var(--text);"
    "background:radial-gradient(900px 500px at 80% -10%,#142b52 0,var(--bg) 60%);"
    "font-family:Inter,-apple-system,'Segoe UI',Roboto,sans-serif;font-size:14px}"
    ".shell{max-width:1120px;margin:0 auto;padding:18px;display:grid;"
    "grid-template-columns:224px minmax(0,1fr);gap:18px;min-height:100vh}"
    ".side{display:flex;flex-direction:column;gap:16px}"
    ".brand{display:flex;align-items:center;gap:10px;padding:4px 2px 12px}"
    ".logo{width:38px;height:38px;border-radius:12px;display:grid;place-items:center;"
    "background:linear-gradient(145deg,#244b8a,#12223d);border:1px solid #355e9e;"
    "color:var(--acc);font-weight:800;font-size:17px;letter-spacing:-.5px}"
    ".brand b{display:block;font-size:18px;letter-spacing:-.3px}"
    ".brand span{display:block;color:var(--muted);font-size:11px;margin-top:2px}"
    ".tabs{display:flex;flex-direction:column;gap:5px}"
    ".tab{display:flex;align-items:center;gap:9px;width:100%;text-align:left;"
    "padding:10px 12px;border:1px solid transparent;border-radius:10px;"
    "background:transparent;color:var(--muted);font:inherit;cursor:pointer;"
    "transition:.15s}"
    ".tab:hover{background:var(--panel);color:var(--text)}"
    ".tab.on{background:var(--panel2);border-color:var(--line);color:var(--text)}"
    ".tab.on:before{background:var(--acc)}"
    ".tab:before{content:'';width:3px;height:14px;border-radius:2px;background:transparent}"
    ".sidefoot{margin-top:auto;color:var(--muted);font-size:11px;padding:8px 2px}"
    ".main{min-width:0}"
    ".top{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;"
    "margin:4px 0 16px}"
    ".top h1{font-size:24px;margin:0;letter-spacing:-.5px}"
    ".top .sub{margin:5px 0 0;color:var(--muted);font-size:12px}"
    ".pill{display:inline-flex;align-items:center;gap:6px;padding:6px 10px;"
    "border:1px solid var(--line);border-radius:999px;background:var(--panel);"
    "color:var(--muted);font-size:11px;white-space:nowrap}"
    ".pill i{width:7px;height:7px;border-radius:50%;background:var(--ok);"
    "box-shadow:0 0 8px var(--ok)}"
    ".pane{display:none}.pane.on{display:block}"
    ".card{background:linear-gradient(180deg,var(--panel),#0d1613);"
    "border:1px solid var(--line);border-radius:14px;padding:15px;margin:0 0 12px}"
    ".card h2{font-size:11px;margin:0 0 12px;color:var(--gold);"
    "text-transform:uppercase;letter-spacing:.9px;display:flex;align-items:center;gap:7px}"
    ".card h2:before{content:'';width:3px;height:11px;border-radius:2px;background:var(--gold)}"
    ".grid{display:grid;grid-template-columns:auto minmax(0,1fr);gap:8px 16px;font-size:13px;"
    "align-items:center}"
    ".grid .k{color:var(--muted)}"
    ".mono{font-family:ui-monospace,Consolas,monospace;font-size:12px}"
    ".muted{color:var(--muted)}.sm{font-size:12px}"
    ".row{display:flex;justify-content:space-between;align-items:center;gap:10px;"
    "padding:10px 0;border-bottom:1px solid var(--line)}"
    ".row:last-child{border-bottom:0}"
    ".acts{display:flex;gap:6px;flex-wrap:wrap;align-items:center}"
    ".fname{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
    ".btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;"
    "padding:9px 13px;border:1px solid transparent;border-radius:9px;"
    "background:var(--acc);color:#061326;font:inherit;font-weight:700;"
    "font-size:13px;cursor:pointer;text-decoration:none}"
    ".btn:hover{filter:brightness(1.08)}"
    ".btn.ghost{background:var(--panel2);border-color:var(--line);color:var(--text)}"
    ".btn.get{background:var(--ok);color:#062015}"
    ".btn.del{background:var(--red);color:#2a0808}"
    ".btn.sm{padding:5px 9px;font-size:11px;border-radius:7px}"
    "input,select{background:#0a120f;color:var(--text);border:1px solid var(--line);"
    "border-radius:9px;padding:10px;width:100%;margin:5px 0;font:inherit;font-size:13px}"
    "input:focus,select:focus{outline:none;border-color:var(--acc)}"
    "input[type=file]{padding:8px}"
    ".net{display:flex;justify-content:space-between;gap:10px;padding:9px 11px;"
    "border:1px solid var(--line);border-radius:9px;margin:6px 0;cursor:pointer}"
    ".net:hover{border-color:var(--acc);background:var(--panel2)}"
    "#wlist{max-height:210px;overflow:auto;margin-top:6px}"
    ".modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.72);z-index:20;"
    "align-items:center;justify-content:center;padding:20px}"
    ".modalBox{background:var(--panel);border:1px solid var(--line);border-radius:14px;"
    "padding:18px;max-width:380px;width:100%;box-shadow:0 24px 60px #000b}"
    "#mtitle{font-size:16px;font-weight:700;margin-bottom:6px}"
    "#mtext{font-size:13px;margin-bottom:16px;word-break:break-word}"
    ".pbar{width:100%;height:8px;background:#0a120f;border-radius:6px;overflow:hidden;"
    "display:none;margin-top:8px;border:1px solid var(--line)}"
    ".pfill{width:0%;height:100%;background:linear-gradient(90deg,var(--acc),var(--gold));"
    "transition:width .2s}"
    "@media(max-width:760px){.shell{grid-template-columns:1fr;padding:12px;gap:12px}"
    ".side{gap:10px}.brand{padding-bottom:4px}.tabs{flex-direction:row;overflow:auto;"
    "padding-bottom:2px}.tab{width:auto;white-space:nowrap;padding:8px 10px}"
    ".sidefoot{display:none}.top h1{font-size:20px}}"
    "</style></head><body><div class='shell'>"
    "<aside class='side'>"
    "<div class='brand'><div class='logo'>D</div><div><b>DBFM</b>"
    "<span>DualBoot Firmware Manager</span></div></div>"
    "<nav class='tabs'>"
    "<button class='tab on' data-tab='status'>Status</button>"
    "<button class='tab' data-tab='firmware'>Firmware</button>"
    "<button class='tab' data-tab='files'>Files</button>"
    "<button class='tab' data-tab='network'>Network</button>"
    "<button class='tab' data-tab='system'>System</button>"
    "</nav>"
    "<div class='sidefoot'>v" RECOVERY_VERSION "</div>"
    "</aside><main class='main'>"
    "<header class='top'><div><h1 id='ptitle'>Status</h1>"
    "<p class='sub' id='psub'>Device overview and quick actions</p></div>"
    "<span class='pill'><i></i>ONLINE</span></header>"
    "<div class='panes'>");

  // ---- Status ----
  html += F("<div class='pane on' id='p-status'>");
  html += F("<div class='card'><h2>Callsign</h2>");
  html += "<input type='text' id='callin' maxlength='16' "
          "placeholder='HL2ABC (blank = default)' value='" + htmlEsc(gCallsign) + "'>";
  html += F("<label style='display:flex;gap:8px;align-items:center;font-size:14px;margin:6px 0'>"
    "<input type='checkbox' id='greet' style='width:auto;margin:0'");
  if(gGreeting) html += F(" checked");
  html += F("><span>Show greeting (WELCOME)</span></label>"
    "<button class='btn' data-act='callsign'>Save</button></div>");
  html += F("<div class='card'><h2>App slots</h2>");
  for(int s = 0; s < 2; s++)
  {
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(
        s ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0), NULL);
    esp_app_desc_t d;
    bool has = appImageOk(p) && esp_ota_get_partition_description(p, &d) == ESP_OK;
    bool active = (gBootIsApp1 == (s == 1));

    html += "<div class='row'><div class='fname'>";
    html += (s ? "App1" : "App0");
    if(active) html += " <span class='muted sm'>(active)</span>";
    html += "<div class='muted sm'>";
    html += has ? htmlEsc(String(d.project_name) + " " + d.version) : String("empty");
    html += "</div></div><div class='acts'>";
    html += "<button class='btn sm' data-act='boot' data-href='/boot?slot=" +
            String(s) + "'>Boot</button>";
    html += "</div></div>";
  }
  html += F("</div>");

  html += F("<div class='card'><h2>Storage</h2><div class='grid'>");
  uint8_t env = desGetCurrentEnv();
  html += "<div class='k'>Boot slot</div><div>" + String(gBootSlot) + "</div>";
  html += "<div class='k'>Environment</div><div>" +
          (env == 0xFF ? String("?") : "ENV" + String(env)) + "</div>";
  html += "<div class='k'>Used</div><div>" + fmtBytes(fsUsed) + " / " + fmtBytes(fsTotal) +
          " (" + String(fsPct) + "%)</div>";
  html += "<div class='k'>Uptime</div><div>" + String(millis() / 1000) + " s</div>";
  html += F("</div>");
  html += "<div class='pbar' style='display:block;margin-top:12px'><div class='pfill' style='width:" +
          String(fsPct) + "%'></div></div>";
  html += F("</div>");
  html += F("</div>");

  // ---- Firmware ----
  html += F("<div class='pane' id='p-firmware'>");
  html += F("<div class='card'><h2>Upload</h2>"
    "<input type='file' id='fi' accept='.bin,.txt,.nvs'>"
    "<select id='tg'>"
    "<option value='store'>Save to storage</option>"
    "<option value='0'>Upload &amp; flash to App0</option>"
    "<option value='1'>Upload &amp; flash to App1</option>"
    "</select>"
    "<button class='btn' id='ubtn' data-act='upload'>Upload</button></div>");
  html += F("</div>");

  // ---- Files ----
  html += F("<div class='pane' id='p-files'><div class='card'><h2>Files</h2>");
  html += buildFileRows();
  html += F("</div></div>");

  // ---- Network ----
  html += F("<div class='pane' id='p-network'>");
  html += F("<div class='card'><h2>WiFi</h2>"
    "<input type='text' id='wssid' placeholder='SSID'>"
    "<input type='password' id='wpass' placeholder='Password'>"
    "<div class='acts'>"
    "<button class='btn' data-act='scan'>Scan</button>"
    "<button class='btn' data-act='connect'>Connect</button>"
    "</div>"
    "<div id='wlist'></div>"
    "<div id='wmsg' class='muted sm'></div></div>");
  bool sta = (WiFi.status() == WL_CONNECTED);
  bool ap  = apModeActive;
  String ssid = sta ? WiFi.SSID() : (ap ? "ats-recovery" : "");
  html += F("<div class='card'><h2>Network</h2><div class='grid'>");
  html += "<div class='k'>SSID</div><div>" + htmlEsc(ssid.length() ? ssid : "-") + "</div>";
  html += "<div class='k'>IP</div><div class='mono'>" + htmlEsc(currentIp()) + "</div>";
  html += "<div class='k'>Mode</div><div>" +
          String(sta ? (ap ? "AP+STA" : "Station") : (ap ? "AP" : "Offline")) + "</div>";
  html += "<div class='k'>AP IP</div><div class='mono'>" +
          htmlEsc(ap ? WiFi.softAPIP().toString() : String("-")) + "</div>";
  html += F("</div></div>");
  html += F("<div class='card'><h2>Web account</h2><div class='grid'>");
  html += "<div class='k'>Login</div><div>" + String(gWebAuthOn ? "ON" : "OFF") + "</div>";
  html += "<div class='k'>ID</div><div>" + htmlEsc(gWebUser) + "</div>";
  html += F("</div>"
    "<input type='text' id='auser' placeholder='ID'>"
    "<input type='password' id='apass' placeholder='Password'>"
    "<div class='acts'>"
    "<button class='btn get' data-act='authset'>Save &amp; enable</button>");
  if(gWebAuthOn)
    html += F("<button class='btn del' data-act='authoff'>Disable</button>");
  html += F("</div></div>");

  html += F("</div>");

  // ---- System ----
  html += F("<div class='pane' id='p-system'>");
  html += F("<div class='card'><h2>Device</h2><div class='grid'>");
  html += "<div class='k'>Recovery</div><div>v" RECOVERY_VERSION "</div>";
  html += "<div class='k'>Chip</div><div>" + String(ESP.getChipModel()) +
          " rev " + String((int)ESP.getChipRevision()) + "</div>";
  html += "<div class='k'>Flash</div><div>" + fmtBytes(ESP.getFlashChipSize()) + "</div>";
  html += "<div class='k'>MAC</div><div class='mono'>" + WiFi.macAddress() + "</div>";
  html += "<div class='k'>Free heap</div><div>" + fmtBytes(ESP.getFreeHeap()) + "</div>";
  html += "<div class='k'>Reset reason</div><div>" + String((int)esp_reset_reason()) + "</div>";
  html += F("</div></div>");
  html += F("<div class='card'><h2>Partitions</h2>");
  html += buildPartitionRows();
  html += F("</div>");
  html += F("</div>");

  html += F("</div></main></div>");

  html += F("<div class='modal' id='mbox'><div class='modalBox'>"
    "<div id='mtitle'></div><div id='mtext' class='muted'></div>"
    "<div class='acts'><button class='btn' id='myes'>Yes</button>"
    "<button class='btn ghost' id='mno'>No</button></div>"
    "</div></div>");

  html += F("<div class='modal' id='pbox'><div class='modalBox'>"
    "<div id='ptitle'></div>"
    "<div class='pbar' id='pbar2' style='display:block;margin-top:10px'>"
    "<div class='pfill' id='pfill2'></div></div>"
    "<div id='pmsg' class='muted sm' style='margin-top:10px'></div>"
    "</div></div>");

  html += F("<script>"
    "var busy=false;"
    "function $(id){return document.getElementById(id);}"
    "function show(id){var ps=document.querySelectorAll('.pane');"
    "for(var i=0;i<ps.length;i++)ps[i].classList.remove('on');"
    "var t=document.getElementById('p-'+id);if(t)t.classList.add('on');"
    "var bs=document.querySelectorAll('.tab');"
    "for(var i=0;i<bs.length;i++)bs[i].classList.toggle('on',bs[i].getAttribute('data-tab')===id);"
    "var T={status:['Status','Device overview and quick actions'],"
    "firmware:['Firmware','Upload or flash a firmware image'],"
    "files:['Files','LittleFS storage and file actions'],"
    "network:['Network','WiFi, web server and account'],"
    "system:['System','Device, memory and partitions']};"
    "if(T[id]){$('ptitle').textContent=T[id][0];$('psub').textContent=T[id][1];}}"
    "var tabs=document.querySelectorAll('.tab');"
    "for(var i=0;i<tabs.length;i++)tabs[i].addEventListener('click',function(e){"
    "e.preventDefault();show(this.getAttribute('data-tab'));});"

    // ---- progress modal ----
    "function pOpen(t){$('ptitle').textContent=t;$('pfill2').style.width='0%';"
    "$('pmsg').textContent='';$('pbox').style.display='flex';}"
    "function pSet(p,m){$('pfill2').style.width=p+'%';$('pmsg').textContent=m;}"
    "function pClose(){$('pbox').style.display='none';}"

    // ---- upload ----
    "function go(){if(busy)return;var f=$('fi').files[0];if(!f)return;"
    "var tg=$('tg').value;busy=true;$('ubtn').disabled=true;"
    "pOpen(tg==='store'?'Uploading file':'Uploading firmware');"
    "var fd=new FormData();fd.append('file',f);var x=new XMLHttpRequest();"
    "x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);"
    "pSet(p,'Uploading '+p+'%');}};"
    "x.onload=function(){busy=false;$('ubtn').disabled=false;"
    "if(x.status>=400){pSet(100,'Rejected - device busy');return;}"
    "if(tg!=='store'){startFlash(tg,f.name);}"
    "else{pSet(100,'Saved');setTimeout(function(){pClose();location.reload();},800);}};"
    "x.onerror=function(){pSet(100,'Upload failed');"
    "busy=false;$('ubtn').disabled=false;};"
    "x.open('POST','/upload');x.send(fd);}"

    // ---- flash progress ----
    "function startFlash(slot,name){"
    "pOpen('Flashing App'+slot);pSet(0,'Starting...');"
    "fetch('/flash?slot='+slot+'&name='+encodeURIComponent(name))"
    ".then(function(r){if(!r.ok)throw new Error('busy');return r.json();}).then(function(){"
    "var last=Date.now();"
    "var iv=setInterval(function(){"
    "if(Date.now()-last>30000){clearInterval(iv);pSet(100,'Lost contact with the device');return;}"
    "fetch('/flashstatus').then(function(r){return r.json();}).then(function(s){"
    "last=Date.now();"
    "if(s.pct>=0&&s.pct<=100){pSet(s.pct,s.msg);}"
    "else if(s.pct===101){clearInterval(iv);pSet(100,'Done! Booting App'+slot+'...');}"
    "else if(s.pct===102){clearInterval(iv);pSet(100,'Failed: '+s.msg);}"
    "else if(!s.busy){clearInterval(iv);pSet(100,'Device restarted');}"
    "}).catch(function(){});},400);})"
    ".catch(function(){pSet(100,'Flash request failed - device busy?');});"
    "return false;}"

    // ---- wifi ----
    "function scan(){$('wmsg').textContent='Scanning...';"
    "fetch('/scan').then(function(r){return r.json();}).then(function(a){"
    "var l=$('wlist');l.innerHTML='';"
    "if(!a.length){$('wmsg').textContent='No networks found';return;}"
    "a.forEach(function(n){var d=document.createElement('div');d.className='net';"
    "var s=document.createElement('span');s.textContent=n.ssid;"
    "var m=document.createElement('span');m.className='muted sm';"
    "m.textContent=n.rssi+' dBm'+(n.secure?' - sec':' - open');"
    "d.appendChild(s);d.appendChild(m);"
    "d.onclick=function(){$('wssid').value=n.ssid;$('wpass').focus();};"
    "l.appendChild(d);});"
    "$('wmsg').textContent=a.length+' networks - tap one to fill SSID';"
    "}).catch(function(){$('wmsg').textContent='Scan failed';});}"
    "function connect(){var s=$('wssid').value;"
    "if(!s){$('wmsg').textContent='Enter an SSID first';return;}"
    "var p=$('wpass').value;"
    "$('wmsg').textContent='Connecting to '+s+' ...';"
    "fetch('/connect?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p))"
    ".then(function(r){return r.json();}).then(function(){"
    "var t=Date.now();"
    "var iv=setInterval(function(){"
    "fetch('/status').then(function(r){return r.json();}).then(function(st){"
    "if(st.connected){clearInterval(iv);"
    "$('wmsg').textContent='Connected: '+st.ssid+' @ '+st.ip;"
    "setTimeout(function(){location.reload();},2500);}"
    "else if(Date.now()-t>20000){clearInterval(iv);"
    "$('wmsg').textContent='Failed or timeout - check the password';}"
    "}).catch(function(){});},1000);})"
    ".catch(function(){$('wmsg').textContent='Connect request failed';});}"

    // ---- confirmation modal: every action asks Yes/No first ----
    "var CONFIRM={"
    "scan:['Scan','Search for nearby WiFi networks?'],"
    "connect:['Connect','Switch network? The page will reload.'],"
    "upload:['Upload','Send this file to the device?'],"
    "flash:['Flash','Overwrite the slot, then reboot into it?'],"
    "boot:['Boot','Reboot into this app?'],"
    "web:['Web server','Change the web server state?'],"
    "del:['Delete','Delete this file permanently?'],"
    "authset:['Web account','Save and enable login?'],"
    "authoff:['Web account','Disable login?'],"
    "callsign:['Callsign','Save the boot callsign?'],"
    "get:['Download','Download this file?']};"
    "var mtarget=null;"
    "function askText(el,def){var s=el.getAttribute('data-name');"
    "var slot=el.getAttribute('data-slot');var p=[];"
    "if(slot!==null)p.push('App'+slot);if(s)p.push(s);"
    "return p.length?p.join(' / ')+': '+def:def;}"
    "function askClose(){$('mbox').style.display='none';mtarget=null;}"
    "function runAction(el){var a=el.getAttribute('data-act');"
    "if(a==='scan')scan();"
    "else if(a==='connect')connect();"
    "else if(a==='upload')go();"
    "else if(a==='flash')startFlash(el.getAttribute('data-slot'),el.getAttribute('data-name'));"
    "else if(a==='get')location.href='/download?name='+encodeURIComponent(el.getAttribute('data-name'));"
    "else if(a==='del')location.href='/delete?name='+encodeURIComponent(el.getAttribute('data-name'));"
    "else if(a==='authset')location.href='/webauth?on=1&user='+encodeURIComponent($('auser').value)+'&pass='+encodeURIComponent($('apass').value);"
    "else if(a==='authoff')location.href='/webauth?on=0';"
    "else if(a==='callsign')location.href='/callsign?text='+encodeURIComponent($('callin').value)+'&greet='+($('greet').checked?'1':'0');"
    "else location.href=el.getAttribute('data-href');}"
    "document.addEventListener('click',function(e){"
    "var el=e.target.closest?e.target.closest('[data-act]'):null;"
    "if(!el)return;"
    "e.preventDefault();"
    "var c=CONFIRM[el.getAttribute('data-act')];"
    "if(!c)return;"
    "mtarget=el;$('mtitle').textContent=c[0];"
    "$('mtext').textContent=askText(el,c[1]);"
    "$('mbox').style.display='flex';});"
    "$('myes').onclick=function(){var el=mtarget;askClose();if(el)runAction(el);};"
    "$('mno').onclick=askClose;"
    "$('mbox').onclick=function(e){if(e.target===this)askClose();};"
    "document.addEventListener('keydown',"
    "function(e){if(e.key==='Escape')askClose();});"
    "</script>");

  html += F("</div></body></html>");
  server.send(200, "text/html", html);
}

static File uploadFile;
static bool uploadRejected = false;
static bool uploadDenied = false;

static void handleUploadDone()
{
  gUploadLastMs = 0;
  if(uploadDenied)
  {
    uploadDenied = false;
    server.requestAuthentication(BASIC_AUTH, "DBFM");
    return;
  }
  if(uploadRejected)
  {
    uploadRejected = false;
    server.send(409, "text/plain", "device is busy flashing");
    return;
  }
  server.send(200, "text/plain", "OK");
}

static void handleUpload()
{
  HTTPUpload& up = server.upload();
  if(up.status == UPLOAD_FILE_START)
  {
    uploadDenied = !webAuthOk();
    uploadRejected = gFlashBusy;
    gUploadLastMs = millis();
    if(uploadRejected || uploadDenied) uploadFile = File();
    else uploadFile = LittleFS.open("/" + String(up.filename.c_str()), FILE_WRITE);
  }
  else if(up.status == UPLOAD_FILE_WRITE)
  {
    gUploadLastMs = millis();
    if(uploadFile) uploadFile.write(up.buf, up.currentSize);
  }
  else if(up.status == UPLOAD_FILE_END)
  {
    if(uploadFile) uploadFile.close();
    gUiRefresh = true;
  }
}

// Shared answer when a request arrives while an upload/flash owns the device.
static void busyPage()
{
  server.send(409, "text/html",
    "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<body style='font-family:sans-serif;background:#0e1116;color:#e6edf3;text-align:center;padding:48px'>"
    "<h2>Device is busy</h2>"
    "<p style='color:#8b949e'>Wait for the upload or flash to finish, then try again.</p>"
    "<a href='/' style='color:#58a6ff'>Back</a></body>");
}

static void handleDelete()
{
  if(!webAuthed()) return;
  if(gFlashBusy || uploadBusy()) { busyPage(); return; }

  if(server.hasArg("name"))
  {
    String name = server.arg("name");
    if(!name.startsWith("/")) name = "/" + name;
    LittleFS.remove(name);
  }
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

static void handleBoot()
{
  if(!webAuthed()) return;
  if(gFlashBusy || uploadBusy()) { busyPage(); return; }

  int slot = server.hasArg("slot") ? server.arg("slot").toInt() : 0;
  server.send(200, "text/html",
    "<meta charset='utf-8'><body style='font-family:sans-serif;background:#0e1116;color:#e6edf3;"
    "text-align:center;padding:48px'><h2>Booting...</h2></body>");
  delay(600);
  bootToApp(static_cast<esp_partition_subtype_t>(
    slot == 1 ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0));
}

// Runs the flash off the web task so / keeps answering while it progresses.
struct FlashJob
{
  String name;
  int slot;
};

static void flashTask(void *arg)
{
  FlashJob *job = (FlashJob *)arg;
  String name = job->name;
  int slot = job->slot;
  delete job;

  flashSetProgress(0, "Starting...");
  gUiQuiet = true;
  bool ok = flashFirmware(name, slot);
  gUiQuiet = false;

  int pct;
  char msg[sizeof(gFlashMsg)];
  flashGetProgress(&pct, msg, sizeof(msg));

  if(ok && !setBootSlot(slot))
  {
    flashSetProgress(102, "Could not select boot slot");
    ok = false;
  }
  else if(ok) flashSetProgress(101, "Booting...");
  else if(pct != 102) flashSetProgress(102, "Flash failed");

  gUiRefresh = true;
  gWebFlash = false;

  if(ok)
  {
     gSprite.pushSprite(0, 0);
    delay(2500);   // let the browser read "Booting..." before the socket drops
    ESP.restart();
  }

  gFlashBusy = false;
  vTaskDelete(NULL);
}

static void handleFlash()
{
  if(!webAuthed()) return;
  if(!server.hasArg("name") || !server.hasArg("slot"))
  {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"missing name/slot\"}");
    return;
  }
  if(gFlashBusy || uploadBusy())
  {
    server.send(409, "application/json", "{\"ok\":false,\"err\":\"busy\"}");
    return;
  }

  FlashJob *job = new FlashJob;
  if(!job)
  {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"oom\"}");
    return;
  }
  job->name = server.arg("name");
  job->slot = server.arg("slot").toInt();

  gFlashBusy = true;
  gWebFlash = true;
  flashSetProgress(0, "Starting...");

  if(xTaskCreate(flashTask, "flashui", 8192, job, 1, nullptr) != pdPASS)
  {
    delete job;
    gFlashBusy = false;
    gWebFlash = false;
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"task\"}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

static void setWebEnabled(bool on)
{
  if(on == gWebEnabled) return;
  gWebEnabled = on;
  saveWebEnabled(gWebEnabled);
  gUiRefresh = true;
}

static void drawWifiAbout()
{
  gSprite.fillScreen(COL_BG);
  uiDotGrid();
  drawHeader("NETWORK ABOUT");
  uiPanel(8, 34, 304, 110, COL_ACC);

  gSprite.setTextDatum(TL_DATUM);
  gSprite.setTextColor(COL_MUTED, COL_PANEL);

  bool sta = (WiFi.status() == WL_CONNECTED);
  bool ap  = apModeActive;
  int y = 42;

  if(sta)
  {
    gSprite.drawString("SSID: " + WiFi.SSID(), 18, y, FONT_SMALL); y += 17;
  }
  else if(ap)
  {
    gSprite.drawString("AP:   ats-recovery", 18, y, FONT_SMALL); y += 17;
  }

  if(ap)
  {
    gSprite.drawString("PASS: 12345678", 18, y, FONT_SMALL); y += 17;
  }

  const char *mode = sta ? (ap ? "AP+STA" : "Station") : (ap ? "AP" : "Offline");
  gSprite.drawString("Mode: " + String(mode), 18, y, FONT_SMALL); y += 17;

  String ip = currentIp();
  gSprite.drawString(ip == "-" ? String("IP:   -") : String("IP:   http://" + ip),
                 18, y, FONT_SMALL);
  y += 17;

  uiHintBar("", "HOLD=BACK");
  gSprite.pushSprite(0, 0);
}

static void runWifiAbout()
{
  gUiRefresh = false;
  drawWifiAbout();

  while(true)
  {
    if(gUiRefresh) { gUiRefresh = false; drawWifiAbout(); }

    BtnEvent ev = readButtonEvent();
    if(ev == BTN_HOLD) return;
    delay(10);
  }
}

static void handleDownload()
{
  if(!webAuthed()) return;
  if(!server.hasArg("name"))
  {
    server.send(400, "text/plain", "missing name");
    return;
  }

  String name = server.arg("name");
  if(!name.startsWith("/")) name = "/" + name;
  if(!LittleFS.exists(name))
  {
    server.send(404, "text/plain", "not found");
    return;
  }

  File f = LittleFS.open(name, "r");
  if(!f)
  {
    server.send(500, "text/plain", "open failed");
    return;
  }

  String fn = name.substring(1);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + fn + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

// Web UI account: stores the ID/PW and enables or disables Basic Auth.
static void handleWebAuth()
{
  if(!webAuthed()) return;

  bool on = server.hasArg("on") && server.arg("on") == "1";
  if(on)
  {
    if(server.hasArg("user") && server.arg("user").length())
      gWebUser = server.arg("user");
    if(server.hasArg("pass") && server.arg("pass").length())
      gWebPass = server.arg("pass");
  }
  gWebAuthOn = on && gWebPass.length() > 0;
  saveWebAuth();

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

static void handleCallsign()
{
  if(!webAuthed()) return;
  if(gFlashBusy || uploadBusy()) { busyPage(); return; }

  String text = server.hasArg("text") ? server.arg("text") : "";
  bool greet = !server.hasArg("greet") || server.arg("greet") == "1";
  text.trim();

  if(text.length() > CALLSIGN_MAX || !asciiPrintable(text))
  {
    server.send(400, "text/html",
      "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<body style='font-family:sans-serif;background:#0e1116;color:#e6edf3;text-align:center;padding:48px'>"
      "<h2>Invalid callsign</h2>"
      "<p style='color:#8b949e'>Use printable ASCII only (A-Z, 0-9, symbols), up to 16 characters.</p>"
      "<a href='/' style='color:#58a6ff'>Back</a></body>");
    return;
  }

  gCallsign = text;
  gGreeting = greet;
  saveCallsign(text);
  saveGreeting(greet);
  gUiRefresh = true;

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

static String jsonEsc(const String &s)
{
  String o;
  o.reserve(s.length() + 8);
  for(size_t i = 0; i < s.length(); i++)
  {
    char c = s.charAt(i);
    if(c == '"' || c == '\\') { o += '\\'; o += c; }
    else if((uint8_t)c < 0x20)
    {
      char b[8];
      snprintf(b, sizeof(b), "\\u%04x", (unsigned)(uint8_t)c);
      o += b;
    }
    else o += c;
  }
  return o;
}

// Enables the STA interface for a scan/connect while keeping the access point
// alive, so the browser session (and the LCD) do not lose the device.
static void ensureApStaMode()
{
  WiFi.mode(apModeActive ? WIFI_AP_STA : WIFI_STA);
}

static void handleScan()
{
  if(!webAuthed()) return;
  ensureApStaMode();

  int n = WiFi.scanNetworks();
  String json = "[";
  int added = 0;
  for(int i = 0; i < n; i++)
  {
    String s = WiFi.SSID(i);
    if(!s.length()) continue;
    if(added++) json += ",";
    json += "{\"ssid\":\"" + jsonEsc(s) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
            ",\"secure\":" +
            String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += "]";
  WiFi.scanDelete();

  server.send(200, "application/json", json);
}

static void handleConnect()
{
  if(!webAuthed()) return;
  if(!server.hasArg("ssid") || !server.arg("ssid").length())
  {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }

  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";

  saveWifiCredentials(ssid, pass);

  // answer first: WiFi.begin() may drop the link we are answering on
  server.send(200, "application/json",
              "{\"ok\":true,\"ssid\":\"" + jsonEsc(ssid) + "\"}");

  ensureApStaMode();
  applyTxPower();
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  gUiRefresh = true;
}

static void handleStatus()
{
  if(!webAuthed()) return;
  bool sta = (WiFi.status() == WL_CONNECTED);
  String j = "{\"connected\":";
  j += sta ? "true" : "false";
  j += ",\"ssid\":\"" + jsonEsc(sta ? WiFi.SSID() : String()) + "\"";
  j += ",\"ip\":\"" + (sta ? WiFi.localIP().toString() : String()) + "\"";
  j += ",\"ap\":";
  j += apModeActive ? "true" : "false";
  j += ",\"apip\":\"" + (apModeActive ? WiFi.softAPIP().toString() : String()) + "\"";
  j += ",\"web\":";
  j += gWebEnabled ? "true" : "false";
  j += "}";
  server.send(200, "application/json", j);
}

static void handleFlashStatus()
{
  if(!webAuthed()) return;
  int pct = -1;
  char msg[sizeof(gFlashMsg)] = {0};
  flashGetProgress(&pct, msg, sizeof(msg));

  String j = "{\"busy\":";
  j += gFlashBusy ? "true" : "false";
  j += ",\"pct\":" + String(pct);
  j += ",\"msg\":\"" + jsonEsc(String(msg)) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

static void webServerTask(void *)
{
  bool applied = false;

  for(;;)
  {
    if(gWebEnabled != applied)
    {
      if(!gWebEnabled)
      {
        vTaskDelay(pdMS_TO_TICKS(400));
        if(!gWebEnabled)
        {
          if(serverRunning) { server.stop(); serverRunning = false; }
          if(apModeActive)
          {
            WiFi.softAPdisconnect(true);
            apModeActive = false;
          }
        }
      }
      else
      {
        if(WiFi.status() != WL_CONNECTED && !apModeActive) wifiStartAp();
        if(!serverRunning) { server.begin(); serverRunning = true; }
      }
      applied = gWebEnabled;
      gUiRefresh = true;
      continue;
    }

    if(gWebEnabled && serverRunning) server.handleClient();
    vTaskDelay(1);
  }
}

static void startWebBackground()
{
  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUpload);
  server.on("/delete", HTTP_GET, handleDelete);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/boot", HTTP_GET, handleBoot);
  server.on("/flash", HTTP_GET, handleFlash);
  server.on("/flashstatus", HTTP_GET, handleFlashStatus);
  server.on("/webauth", HTTP_GET, handleWebAuth);
  server.on("/callsign", HTTP_GET, handleCallsign);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_GET, handleConnect);
  server.on("/status", HTTP_GET, handleStatus);

  if(xTaskCreate(webServerTask, "webui", 6144, nullptr, 1, &webTaskHandle) != pdPASS)
    Serial.println("step: web task create failed");
}

