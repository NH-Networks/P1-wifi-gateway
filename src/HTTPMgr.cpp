/*
 * Copyright (c) 2025 Jean-Pierre Sneyers, Ronald Leenes, & Contributors
 * Source : https://github.com/narfight/P1-wifi-gateway
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "HTTPMgr.h"

extern char* GetClientName();
extern void RequestRestart(unsigned long delay);

HTTPMgr::HTTPMgr(settings &currentConf, TelnetMgr *currentTelnet, P1Reader &currentP1)
    : conf(currentConf), TelnetSrv(currentTelnet), P1Captor(currentP1), server(80)
{
}

void HTTPMgr::start_webservices()
{
  // Header and asset files
  server.on("/style.css", std::bind(&HTTPMgr::handleStyleCSS, this));
  server.on("/favicon.svg", std::bind(&HTTPMgr::handleFavicon, this));
  server.on("/main.js", std::bind(&HTTPMgr::handleMainJS, this));

  // Data endpoints
  server.on("/P1.json", std::bind(&HTTPMgr::handleJSON, this));
  server.on("/P1.js", std::bind(&HTTPMgr::handleP1Js, this));
  server.on("/status.json", std::bind(&HTTPMgr::handleJSONStatus, this));

  // Pages
  server.on("/", std::bind(&HTTPMgr::handleRoot, this));
  server.on("/setPassword", std::bind(&HTTPMgr::handlePassword, this));
  server.on("/Setup", std::bind(&HTTPMgr::handleSetup, this));
  server.on("/SetupSave", std::bind(&HTTPMgr::handleSetupSave, this));
  server.on("/reset", std::bind(&HTTPMgr::handleFactoryReset, this));
  server.on("/reboot", std::bind(&HTTPMgr::handleReboot, this));
  server.on("/P1", std::bind(&HTTPMgr::handleP1, this));
  server.on("/raw", std::bind(&HTTPMgr::handleRAW, this));
  server.on("/Data", std::bind(&HTTPMgr::handleRAW, this));
  server.on("/data", std::bind(&HTTPMgr::handleRAW, this));
  server.on("/debug", std::bind(&HTTPMgr::handleDebug, this));
  server.on("/debug.log", std::bind(&HTTPMgr::handleDebugLog, this));

  // OTA firmware update
  server.on("/update", HTTP_GET, std::bind(&HTTPMgr::handleUploadForm, this));
  server.on("/update", HTTP_POST, [this]()
  {
    if (!ChekifAsAdmin())
    {
      return;
    }

    if (UpdateResultFailed)
    {
      ReplyOTA(false, UpdateMsg.c_str(), UpdateErrorCode);
      UpdateResultFailed = false;
      UpdateMsg = "";
    }
    else
    {
      ReplyOTA(true, LANG_OTASTATUSOK, 0);
    }
  }, std::bind(&HTTPMgr::handleUploadFlash, this));

  server.begin();
}

bool HTTPMgr::ActifCache(bool enabled)
{
  if (enabled)
  {
    char etag[15];
    snprintf(etag, sizeof(etag), "W/\"%d\"", BUILD_DATE);
    if (server.header("If-None-Match") == etag)
    {
      server.send(304);
      return true;
    }
    server.sendHeader("ETag", etag);
    server.sendHeader("Cache-Control", "max-age=86400");
    return false;
  }
  else
  {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    return false;
  }
}

void HTTPMgr::DoMe()
{
  server.handleClient();
}

void HTTPMgr::handleRoot()
{
  if (conf.NeedConfig)
  {
    server.sendHeader("Location", "/setPassword");
    server.send(302, "text/plain", "Redirecting");
    return;
  }

  static const char template_html[] PROGMEM = R"(
    <fieldset><legend>)" LANG_H1DATA R"(</legend>
    <a href="/P1" class="bt">)" LANG_MENUP1 R"(</a>
    <a href="/raw" class="bt">)" LANG_SHOWRAW R"(</a>
    <a href="/P1.json" class="bt">)" LANG_SHOWJSON R"(</a>
    <a href="/debug" class="bt">)" LANG_MENUDEBUG R"(</a>
    </fieldset>
    <fieldset><legend>)" LANG_ConfH1 R"(</legend>
    <a href="/Setup" class="bt">)" LANG_MENUConf R"(</a>
    <a href="/setPassword" class="bt">)" LANG_MENUPASSWORD R"(</a>
    <a href="/update" class="bt">)" LANG_MENUOTA R"(</a>
    <a href="/reboot" class="bt bwarning">)" LANG_MENUREBOOT R"(</a>
    <a href="/reset" class="bt bwarning">)" LANG_MENURESET R"(</a>
    </fieldset>)";

  SendWithHeaderFooter("text/html", template_html, "", false);
}

void HTTPMgr::handleUploadForm()
{
  if (!ChekifAsAdmin())
  {
    return;
  }

  static const char html[] PROGMEM = R"(
<form method='post' action='' enctype='multipart/form-data'>
  <fieldset><legend>)" LANG_OTAH1 R"(</legend>
  <p><label for="firmware">)" LANG_OTAFIRMWARE R"( :</label><input type='file' accept='.bin,.bin.gz' id='firmware' name='firmware' required></p>
  </fieldset><button class="bt bwarning" type='submit'>)" LANG_OTABTUPDATE R"(</button></form>
  <a href="/" class="bt">)" LANG_MENU R"(</a>)";

  SendWithHeaderFooter("text/html", html, "", false);
}

void HTTPMgr::handleUploadFlash()
{
  if (ChekifAsAdmin())
  {
    HTTPUpload &upload = server.upload();

    if (UpdateResultFailed)
    {
      return;
    }

    if (upload.status == UPLOAD_FILE_START)
    {
      Update.clearError();
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      MainSendDebugPrintf("[FLASH] Upload of '%s' (%lu bytes - free %lu)", upload.filename.c_str(), upload.contentLength, maxSketchSpace);

      if (upload.contentLength > maxSketchSpace)
      {
        UpdateResultFailed = true;
        UpdateMsg = "Not enough space for update";
        UpdateErrorCode = 4;
        return;
      }

      if (!Update.begin(maxSketchSpace, U_FLASH))
      {
        UpdateResultFailed = true;
        UpdateMsg = Update.getErrorString();
        UpdateErrorCode = 0;
        return;
      }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
      {
        UpdateResultFailed = true;
        UpdateMsg = Update.getErrorString();
        UpdateErrorCode = 1;
        return;
      }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
      if (Update.end(true))
      {
        return;
      }
      else
      {
        UpdateResultFailed = true;
        UpdateMsg = Update.getErrorString();
        UpdateErrorCode = 1;
        return;
      }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
      UpdateResultFailed = true;
      UpdateMsg = "Update was aborted";
      UpdateErrorCode = 3;
      return;
    }
    yield();
  }
}

void HTTPMgr::ReplyOTA(bool success, const char *error, u_int ref)
{
  if (success)
  {
    MainSendDebug("[FLASH] Update successful, rebooting");
  }
  else
  {
    MainSendDebugPrintf("[FLASH] Error : %s (%u)", error, ref);
  }

  static const char template_html[] PROGMEM = R"(
<fieldset><p>%s : <strong>%s (%u)</strong></p>
<p>)" LANG_OTASUCCESS1 R"(</p>
<p>)" LANG_OTASUCCESS2 R"(</p>
<p>)" LANG_OTASUCCESS3 R"(</p>
<p>)" LANG_OTASUCCESS4 R"(</p>
%s</fieldset>)";

  const char *animation = GetAnimWait();
  snprintf_P(HTMLBufferContent, sizeof(HTMLBufferContent), template_html, (success) ? LANG_OTANSUCCESSOK : LANG_OTANSUCCESSNOK, error, ref, GetClientName(), animation);

  SendWithHeaderFooter("text/html", HTMLBufferContent, "", true);
  RequestRestart(1000);
}

void HTTPMgr::handleRAW()
{
  server.send(200, "text/plain", P1Captor.datagram);
}

void HTTPMgr::handleP1Js()
{
  if (ActifCache(true))
    return;
  static const char template_html[] PROGMEM = R"(async function updateValues(){try{let e=await fetch("P1.json"),a=await e.json();document.getElementById("LastSample").value=parseDateTime(a.LastSample).toLocaleString(),document.getElementById("T1").value=a.P1.T1+" kWh",document.getElementById("T2").value=a.P1.T2+" kWh",document.getElementById("RT1").value=a.P1.RT1+" kWh",document.getElementById("RT2").value=a.P1.RT2+" kWh",document.getElementById("TA").value=a.P1.TA+" kWh",document.getElementById("RTA").value=a.P1.RTA+" kWh",document.getElementById("VL1").value=a.P1.V.L1+" V",document.getElementById("VL2").value=a.P1.V.L2+" V",document.getElementById("VL3").value=a.P1.V.L3+" V",document.getElementById("AL1").value=a.P1.A.L1+" A",document.getElementById("AL2").value=a.P1.A.L2+" A",document.getElementById("AL3").value=a.P1.A.L3+" A",document.getElementById("gasReceived5min").value=a.P1.gasReceived5min+" m3"}catch(t){console.error("Error on update :",t)}}setInterval(updateValues,1e4),window.onload=updateValues;)";
  server.send(200, "application/javascript", template_html);
}

void HTTPMgr::handleStyleCSS()
{
  if (ActifCache(true))
    return;
  static const char css[] PROGMEM = R"(
:root{--bg:#0b0f19;--card:#151c2c;--border:#26324a;--text:#f1f5f9;--muted:#94a3b8;--cyan:#38bdf8;--green:#22c55e;--red:#ef4444}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--text);margin:0;padding:20px;text-align:center}
.container{max-width:620px;margin:0 auto;background:var(--card);border:1px solid var(--border);border-radius:12px;box-shadow:0 10px 25px -5px rgba(0,0,0,.6);padding:22px 20px 10px}
h2{text-align:center;color:var(--cyan);margin:0 0 16px;font-weight:600;letter-spacing:-.5px}
fieldset{border:1px solid var(--border);border-radius:10px;padding:16px;margin-bottom:18px;background:rgba(11,15,25,.5);text-align:left}
legend{font-weight:600;padding:0 8px;font-size:1.1em;color:var(--cyan)}
fieldset input[type=text],fieldset input[type=password],fieldset input[type=number],fieldset select{width:40%;padding:7px 10px;background:#080c14;color:var(--text);border:1px solid var(--border);border-radius:6px;font-size:1em;box-sizing:border-box;outline:none}
fieldset input:focus{border-color:var(--cyan);box-shadow:0 0 0 2px rgba(56,189,248,.25)}
label{display:inline-block;width:55%;text-align:right;margin-right:12px;margin-bottom:12px;color:var(--muted);font-size:.95em}
.help,.footer{text-align:right;font-size:12px;color:var(--muted);margin-top:2px}
p{margin:.5em 0}
button,.bt{display:inline-block;box-sizing:border-box;text-align:center;text-decoration:none;border:0;border-radius:8px;background:#0284c7;color:#fff;line-height:2.5rem;font-size:1.05rem;font-weight:500;width:100%;transition:all .2s ease;cursor:pointer;margin-top:7px}
button:hover,.bt:hover{background:#0369a1;transform:translateY(-1px)}
.bt[href="/"]{background:#334155}
.bt[href="/"]:hover{background:#475569}
.bt[href="/P1"]{background:#0284c7}
.bt[href="/P1"]:hover{background:#0369a1}
.bwarning{background:#dc2626!important}
.bwarning:hover{background:#b91c1c!important}
a{color:var(--cyan);text-decoration:none}
a:hover{text-decoration:underline}
.row{display:flex;align-items:center;justify-content:space-between;margin-bottom:6px}
.row label{text-align:left;width:60%;margin-bottom:0}
.row input{width:38%!important;text-align:right}
svg{display:block;margin:auto}
.status-bar{display:flex;justify-content:flex-end;margin-top:14px;padding:12px 4px 4px;border-top:1px solid var(--border)}
.status-bar .indicator{width:10px;height:10px;border-radius:50%;background:var(--green);box-shadow:0 0 8px var(--green);margin-right:6px;display:inline-block}
.error{background:var(--red)!important;box-shadow:0 0 8px var(--red)!important}
.status-bar .text{margin-left:2px;color:var(--muted);font-size:.9em}
.status-bar .item{display:flex;align-items:center;margin-left:14px}
.terminal{background:#06080e;border:1px solid var(--border);border-radius:8px;color:#4ade80;font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;font-size:.85em;padding:12px;text-align:left;white-space:pre-wrap;word-break:break-all;max-height:300px;overflow-y:auto;line-height:1.45}
.info-table{width:100%;border-collapse:collapse;font-size:.95em;margin-bottom:6px}
.info-table td{padding:5px 4px}
.info-table td:first-child{color:var(--muted);width:50%}
.info-table td:last-child{text-align:right;font-weight:500;color:var(--text)}
)";
  server.send(200, "text/css", css);
}

void HTTPMgr::handleMainJS()
{
  if (ActifCache(true))
    return;

  static const char js[] PROGMEM = R"(function parseDateTime(t){return new Date("20"+t.substring(0,2),t.substring(2,4)-1,t.substring(4,6),t.substring(6,8),t.substring(8,10),t.substring(10,12))}async function updateStatus(){try{let e=await fetch("status.json"),s=await e.json();const p=document.getElementById("Port23-indicator");if(null!=p){(s.Port23&&s.Port23.Active)?p.classList.remove("error"):p.classList.add("error")}const n=document.getElementById("P1-indicator");if(""!=s.P1.LastSample){var t=parseDateTime(s.P1.LastSample);t.setSeconds(t.getSeconds()+3*s.P1.Interval);t<Date.now()?n.classList.add("error"):n.classList.remove("error")}else{n.classList.add("error")}}catch(t){console.error("Error on update status:",t)}}window.onload=function(){updateStatus();document.querySelectorAll(".bwarning").forEach((t=>{t.addEventListener("click",(function(t){confirm(")" LANG_ASKCONFIRM R"(")||t.preventDefault()}))})),setInterval(updateStatus,1e4)};)";

  server.send(200, "application/javascript", js);
}

void HTTPMgr::handleFavicon()
{
  if (ActifCache(true))
    return;

  static const char fav[] PROGMEM = R"(<?xml version="1.0" encoding="UTF-8"?><svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" version="1.1" viewBox="0 0 24 24"><path d="M4,4H20A2,2 0 0,1 22,6V18A2,2 0 0,1 20,20H4A2,2 0 0,1 2,18V6A2,2 0 0,1 4,4M4,6V18H11V6H4M20,18V6H18.76C19,6.54 18.95,7.07 18.95,7.13C18.88,7.8 18.41,8.5 18.24,8.75L15.91,11.3L19.23,11.28L19.24,12.5L14.04,12.47L14,11.47C14,11.47 17.05,8.24 17.2,7.95C17.34,7.67 17.91,6 16.5,6C15.27,6.05 15.41,7.3 15.41,7.3L13.87,7.31C13.87,7.31 13.88,6.65 14.25,6H13V18H15.58L15.57,17.14L16.54,17.13C16.54,17.13 17.45,16.97 17.46,16.08C17.5,15.08 16.65,15.08 16.5,15.08C16.37,15.08 15.43,15.13 15.43,15.95H13.91C13.91,15.95 13.95,13.89 16.5,13.89C19.1,13.89 18.96,15.91 18.96,15.91C18.96,15.91 19,17.16 17.85,17.63L18.37,18H20M8.92,16H7.42V10.2L5.62,10.76V9.53L8.76,8.41H8.92V16Z"/></svg>)";

  server.send(200, "image/svg+xml", fav);
}

void HTTPMgr::handleReboot()
{
  if (!ChekifAsAdmin())
  {
    return;
  }
  RebootPage(LANG_TXTREBOOTPAGE);
  RequestRestart(1000);
}

void HTTPMgr::handleFactoryReset()
{
  if (!ChekifAsAdmin())
  {
    return;
  }

  RebootPage(LANG_RF_RESTTXT);

  conf.ConfigVersion = SETTINGVERSIONNULL;

  EEPROM.begin(sizeof(struct settings));
  EEPROM.put(0, conf);
  EEPROM.commit();

  RequestRestart(1000);
}

void HTTPMgr::handlePassword()
{
  if (!conf.NeedConfig)
  {
    if (!ChekifAsAdmin())
    {
      return;
    }
  }

  if (server.method() == HTTP_POST && server.hasArg("psd1") && server.hasArg("psd2"))
  {
    if (server.arg("psd1") == server.arg("psd2"))
    {
      conf.NeedConfig = false;
      server.arg("psd1").toCharArray(conf.adminPassword, sizeof(conf.adminPassword));
      if (server.hasArg("adminUser") && server.arg("adminUser").length() > 0)
      {
        server.arg("adminUser").toCharArray(conf.adminUser, sizeof(conf.adminUser));
      }
      else if (conf.adminUser[0] == '\0')
      {
        strncpy(conf.adminUser, "admin", sizeof(conf.adminUser));
      }

      conf.BootFailed = 0;
      MainSendDebug("[HTTP] New password saved");
      EEPROM.begin(sizeof(struct settings));
      EEPROM.put(0, conf);
      EEPROM.commit();

      server.sendHeader("Location", "/");
      server.send(302, "text/plain", "Redirecting");
      return;
    }
  }

  static const char template_html[] PROGMEM = R"(
<form action="/setPassword" method="post" onsubmit='return Check()'>
<fieldset><legend>)" LANG_H1Welcome R"(</legend>
<label for="adminUser">)" LANG_PSWDLOGIN R"( :</label><input type="text" name="adminUser" id="adminUser" maxlength="32" value="%s" /><br />
<label for="psd1">)" LANG_PSWD1 R"( :</label><input type="password" name="psd1" id="psd1" maxlength="32"><br />
<label for="psd2">)" LANG_PSWD2 R"( :</label><input type="password" name="psd2" id="psd2" maxlength="32"><br />
<span id="passwordError" class="error"></span>
</fieldset><button type="submit">)" LANG_ConfSave R"(</button></form>
<a href="/" class="bt">)" LANG_MENU R"(</a>
)";

  String cleanUser = escapeHtml(conf.adminUser);
  snprintf_P(HTMLBufferContent, sizeof(HTMLBufferContent), template_html, cleanUser.c_str());

  SendWithHeaderFooter("text/html", HTMLBufferContent, "", false);
}

void HTTPMgr::handleSetup()
{
  if (!ChekifAsAdmin())
  {
    return;
  }

  static const char template_html[] PROGMEM = R"(
<form action="/SetupSave" method="post">
<fieldset><legend>)" LANG_ConfP1H2 R"(</legend>
<label for="interval">)" LANG_ConfReadP1Intr R"( :</label><input type="number" min="1" id="interval" name="interval" value="%u"><br />
<label for="InvTarif">)" LANG_ConfPERMUTTARIF R"( :</label><input type="checkbox" name="InvTarif" id="InvTarif" %s><br />
</fieldset>
<fieldset><legend>)" LANG_ConfWIFIH2 R"(</legend>
<label for="ssid">)" LANG_ConfSSID R"( :</label><input type="text" name="ssid" id="ssid" maxlength="32" value="%s"><br />
<label for="password">)" LANG_ConfWIFIPWD R"( :</label><input type="password" maxlength="64" name="password" id="password" value="%s"><br />
</fieldset>
<fieldset><legend>)" LANG_ConfTLNETH2 R"(</legend>
<label for="telnet">)" LANG_ConfTLNETBool R"( :</label><input type="checkbox" name="telnet" id="telnet" %s><br />
<label for="telnetPort">)" LANG_ConfTLNETPort R"( :</label><input type="number" min="1" max="65535" id="telnetPort" name="telnetPort" value="%u"><br />
<label for="reportToTelnet">)" LANG_ConfTLNETREPPORT R"( :</label><input type="checkbox" name="reportToTelnet" id="reportToTelnet" %s><br />
<label for="debugToTelnet">)" LANG_ConfTLNETDBG R"( :</label><input type="checkbox" name="debugToTelnet" id="debugToTelnet" %s><br />
</fieldset>
<span id="passwordError" class="error"></span>
<button type="submit">)" LANG_ACTIONSAVE R"(</button></form>
<a href="/" class="bt">)" LANG_MENU R"(</a>
)";

  String cleanSsid = escapeHtml(conf.ssid);
  String cleanPass = escapeHtml(conf.password);

  snprintf_P(HTMLBufferContent, sizeof(HTMLBufferContent), template_html,
             conf.interval,
             (conf.InverseHigh_1_2_Tarif) ? "checked" : "",
             cleanSsid.c_str(),
             cleanPass.c_str(),
             (conf.telnet) ? "checked" : "",
             conf.telnetPort ? conf.telnetPort : 23,
             (conf.reportToTelnet) ? "checked" : "",
             (conf.debugToTelnet) ? "checked" : "");

  SendWithHeaderFooter("text/html", HTMLBufferContent, "", false);
}

void HTTPMgr::handleSetupSave()
{
  if (!ChekifAsAdmin())
  {
    return;
  }

  if (server.method() == HTTP_POST)
  {
    settings NewConf;
    NewConf.NeedConfig = false;
    strcpy(NewConf.adminPassword, conf.adminPassword);
    strcpy(NewConf.adminUser, conf.adminUser);

    server.arg("ssid").toCharArray(NewConf.ssid, sizeof(NewConf.ssid));
    server.arg("password").toCharArray(NewConf.password, sizeof(NewConf.password));

    NewConf.interval = server.arg("interval").toInt();
    if (NewConf.interval < 1) NewConf.interval = 10;
    NewConf.InverseHigh_1_2_Tarif = (server.arg("InvTarif") == "on");

    NewConf.telnet = (server.arg("telnet") == "on");
    NewConf.telnetPort = server.arg("telnetPort").toInt();
    if (NewConf.telnetPort == 0) NewConf.telnetPort = 23;
    NewConf.reportToTelnet = (server.arg("reportToTelnet") == "on");
    NewConf.debugToTelnet = (server.arg("debugToTelnet") == "on");

    NewConf.ConfigVersion = SETTINGVERSION;

    RebootPage(LANG_Conf_Saved);

    EEPROM.begin(sizeof(struct settings));
    EEPROM.put(0, NewConf);
    EEPROM.commit();

    RequestRestart(1000);
  }
}

void HTTPMgr::RebootPage(const char *Message)
{
  static const char template_html[] PROGMEM = R"(
<fieldset><legend>)" LANG_ConfH1 R"(</legend>
<p>%s</p>
<p>)" LANG_ConfReboot R"(</p>
<p></p>
<p>)" LANG_ConfLedStart R"(</p>
<p>)" LANG_ConfLedError R"(</p>
%s
</fieldset>
)";

  snprintf_P(HTMLBufferContent, sizeof(HTMLBufferContent), template_html, Message, GetClientName(), GetAnimWait());
  SendWithHeaderFooter("text/html", HTMLBufferContent, "", true);
}

void HTTPMgr::handleP1()
{
  static const char template_html[] PROGMEM = R"(
<fieldset><legend>)" LANG_DATAH1 R"(</legend>
<div class="row"><label for="LastSample">)" LANG_DATALastGet R"(</label><input type="text" class="c6" id="LastSample"/></div>
<div class="row"><label for="T1">)" LANG_DATAFullL R"(</label><input type="text" class="c6" id="T1"/></div>
<div class="row"><label for="T2">)" LANG_DATAFullH R"(</label><input type="text" class="c6" id="T2"/></div>
<div class="row"><label for="RT1">)" LANG_DATAFullProdL R"(</label><input type="text" class="c6" id="RT1"/></div>
<div class="row"><label for="RT2">)" LANG_DATAFullProdH R"(</label><input type="text" class="c6" id="RT2"/></div>
<div class="row"><label for="TA">)" LANG_DATACurAmp R"(</label><input type="text" class="c6" id="TA"/></div>
<div class="row"><label for="RTA">)" LANG_DATACurProdAmp R"(</label><input type="text" class="c6" id="RTA"/></div>
<div class="row"><label for="VL1">)" LANG_DATAUL1 R"(</label><input type="text" class="c6" id="VL1"/></div>
<div class="row"><label for="VL2">)" LANG_DATAUL2 R"(</label><input type="text" class="c6" id="VL2"/></div>
<div class="row"><label for="VL3">)" LANG_DATAUL3 R"(</label><input type="text" class="c6" id="VL3"/></div>
<div class="row"><label for="AL1">)" LANG_DATAAL1 R"(</label><input type="text" class="c6" id="AL1"/></div>
<div class="row"><label for="AL2">)" LANG_DATAAL2 R"(</label><input type="text" class="c6" id="AL2"/></div>
<div class="row"><label for="AL3">)" LANG_DATAAL3 R"(</label><input type="text" class="c6" id="AL3"/></div>
<div class="row"><label for="gasReceived5min">)" LANG_DATAGFull R"(</label><input type="text" class="c6" id="gasReceived5min"/></div>
</fieldset>
<a href="/P1.json" class="bt">)" LANG_SHOWJSON R"(</a>
<a href="/raw" class="bt">)" LANG_SHOWRAW R"(</a>
<a href="/" class="bt">)" LANG_MENU R"(</a>
)";
  SendWithHeaderFooter("text/html", template_html, "<script type=\"text/javascript\" src=\"P1.js\"></script>", false);
}

void HTTPMgr::handleJSONStatus()
{
  JsonDocument doc;

  doc["P1"]["LastSample"] = P1Captor.DataReaded.P1timestamp;
  doc["P1"]["Interval"] = conf.interval;
  doc["P1"]["NextUpdateIn"] = P1Captor.GetNextUpdateInMs();
  doc["Port23"]["Active"] = (TelnetSrv != nullptr);
  doc["Port23"]["Clients"] = (TelnetSrv != nullptr) ? TelnetSrv->GetConnectedClientCount() : 0;
  doc["Port23"]["Port"] = conf.telnetPort ? conf.telnetPort : 23;

  String out;
  serializeJson(doc, out);

  ActifCache(false);
  server.send(200, "application/json", out);
}

void HTTPMgr::handleJSON()
{
  JsonDocument doc;
  doc["LastSample"] = P1Captor.DataReaded.P1timestamp;
  doc["NextUpdateIn"] = P1Captor.GetNextUpdateInMs();
  doc["P1"]["T1"] = P1Captor.DataReaded.electricityUsedTariff1.val();
  doc["P1"]["T2"] = P1Captor.DataReaded.electricityUsedTariff2.val();
  doc["P1"]["RT1"] = P1Captor.DataReaded.electricityReturnedTariff1.val();
  doc["P1"]["RT2"] = P1Captor.DataReaded.electricityReturnedTariff2.val();
  doc["P1"]["TA"] = P1Captor.DataReaded.actualElectricityPowerDeli.val();
  doc["P1"]["RTA"] = P1Captor.DataReaded.actualElectricityPowerRet.val();
  doc["P1"]["V"]["L1"] = P1Captor.DataReaded.instantaneousVoltageL1.val();
  doc["P1"]["V"]["L2"] = P1Captor.DataReaded.instantaneousVoltageL2.val();
  doc["P1"]["V"]["L3"] = P1Captor.DataReaded.instantaneousVoltageL3.val();
  doc["P1"]["A"]["L1"] = P1Captor.DataReaded.instantaneousCurrentL1.val();
  doc["P1"]["A"]["L2"] = P1Captor.DataReaded.instantaneousCurrentL2.val();
  doc["P1"]["A"]["L3"] = P1Captor.DataReaded.instantaneousCurrentL3.val();
  doc["P1"]["gasReceived5min"] = P1Captor.DataReaded.gasReceived5min;

  String str;
  serializeJson(doc, str);

  ActifCache(false);
  server.send(200, "application/json", str);
}

bool HTTPMgr::ChekifAsAdmin()
{
  if (strlen(conf.adminPassword) != 0)
  {
    if (!server.authenticate(conf.adminUser, conf.adminPassword))
    {
      server.requestAuthentication();
      return false;
    }
  }
  return true;
}

String HTTPMgr::escapeHtml(const char *input)
{
  if (!input) return String("");
  String out;
  out.reserve(strlen(input) + 16);
  for (size_t i = 0; input[i] != '\0'; i++)
  {
    switch (input[i])
    {
      case '&':  out += "&amp;"; break;
      case '<':  out += "&lt;"; break;
      case '>':  out += "&gt;"; break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default:   out += input[i]; break;
    }
  }
  return out;
}

const char *HTTPMgr::GetAnimWait()
{
  static const char anim_wait[] PROGMEM = R"(
<svg width="100" height="100" viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
    <circle cx="50" cy="50" r="40" stroke="#ccc" stroke-width="4" fill="none" />
    <circle cx="50" cy="10" r="6" fill="#007bff"><animateTransform attributeName="transform" type="rotate" from="0 50 50" to="360 50 50" dur="1s" repeatCount="indefinite" /></circle>
    <circle cx="90" cy="50" r="6" fill="#007bff"><animateTransform attributeName="transform" type="rotate" from="0 50 50" to="360 50 50" dur="2s" repeatCount="indefinite" /></circle>
    <circle cx="50" cy="90" r="6" fill="#007bff"><animateTransform attributeName="transform" type="rotate" from="0 50 50" to="360 50 50" dur="3s" repeatCount="indefinite" /></circle>
    <circle cx="10" cy="50" r="6" fill="#007bff"><animateTransform attributeName="transform" type="rotate" from="0 50 50" to="360 50 50" dur="4s" repeatCount="indefinite" /></circle>
</svg>)";

  return anim_wait;
}

void HTTPMgr::SendWithHeaderFooter(const char *content_type, const char *content, const char *header, bool refresh)
{
  char buffer[1500];
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  static const char template_html_header[] PROGMEM = R"(
<!DOCTYPE html>
<html lang=")" LANG_HEADERLG R"(">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<link rel="icon" href="favicon.svg">
<link rel="stylesheet" type="text/css" href="style.css">
<script type="text/javascript" src="main.js"></script>
<title>%s</title>
%s
%s
</head>
<body><div class="container"><h2>P1 wifi-gateway</h2>
<p class="help"><a href="https://github.com/romix123/P1-wifi-gateway" target="_blank">)" LANG_HLPH1 R"(</a></p>)";

  static const char template_html_footer[] PROGMEM = R"(
<div class="status-bar">
<div class="item"><span class="indicator" id="Port23-indicator"></span><span class="text">)" LANG_STATUS_PORT23 R"(</span></div>
<div class="item"><span class="indicator" id="P1-indicator"></span><span class="text">P1</span></div>
</div></div>
)" LANG_OTAFIRMWARE R"( : v%s.%d  | <a href="https://github.com/romix123/P1-wifi-gateway" target="_blank">Github</a></body></html>
)";

  snprintf_P(buffer, sizeof(buffer), template_html_header,
             GetClientName(),
             header,
             (refresh) ? "<script>function chk() {fetch('http://' + window.location.hostname).then(response => {if (response.ok) {setTimeout(function () {window.location.href = '/';}, 1000);}}).catch(ex =>{});};setTimeout(setInterval(chk, 1000), 3000);</script>" : "");
  server.sendContent(buffer);

  server.sendContent(content);

  snprintf_P(buffer, sizeof(buffer), template_html_footer,
             VERSION,
             BUILD_DATE);

  server.sendContent(buffer);
  server.sendContent("");
}

void HTTPMgr::handleDebugLog()
{
  extern String GetDebugLogs();
  server.send(200, "text/plain", GetDebugLogs());
}

void HTTPMgr::handleDebug()
{
  static const char template_html[] PROGMEM = R"raw(
<fieldset><legend>%s</legend>
<table class="info-table">
<tr><td>)raw" LANG_DEBUG_METERID R"raw(</td><td>%s</td></tr>
<tr><td>)raw" LANG_DEBUG_LASTREAD R"raw(</td><td>%s</td></tr>
<tr><td>)raw" LANG_DEBUG_DATALEN R"raw(</td><td>%u bytes</td></tr>
<tr><td>)raw" LANG_DEBUG_WIFIRSSI R"raw(</td><td>%d dBm</td></tr>
<tr><td>)raw" LANG_DEBUG_WIFIIP R"raw(</td><td>%s</td></tr>
<tr><td>)raw" LANG_DEBUG_UPTIME R"raw(</td><td>%s</td></tr>
<tr><td>)raw" LANG_DEBUG_FREEHEAP R"raw(</td><td>%u bytes</td></tr>
<tr><td>)raw" LANG_DEBUG_PORT23STATUS R"raw(</td><td>%s (%u clients)</td></tr>
</table>
</fieldset>
<fieldset><legend>)raw" LANG_DEBUG_LIVETITLE R"raw(</legend>
<div class="terminal" id="termLogs">)raw" LANG_DEBUG_LOADING R"raw(</div>
<button type="button" class="bt" id="btnRefreshLog" onclick="fetchLogs()" style="margin-top:8px">)raw" LANG_DEBUG_REFRESH R"raw(</button>
</fieldset>
<a href="/raw" class="bt">%s</a>
<a href="/P1.json" class="bt">%s</a>
<a href="/" class="bt">%s</a>
<script>
async function fetchLogs(){
  try{
    let r = await fetch("/debug.log");
    if(r.ok){
      let t = await r.text();
      let el = document.getElementById("termLogs");
      el.innerText = t;
      el.scrollTop = el.scrollHeight;
    }
  }catch(e){}
}
fetchLogs();
setInterval(fetchLogs, 4000);
</script>
)raw";

  char uptimeStr[40];
  unsigned long s = millis() / 1000;
  unsigned int d = s / 86400;
  unsigned int h = (s % 86400) / 3600;
  unsigned int m = (s % 3600) / 60;
  unsigned int sec = s % 60;
  snprintf(uptimeStr, sizeof(uptimeStr), "%ud %uh %um %us", d, h, m, sec);

  const char *mName = P1Captor.meterName.length() > 0 ? P1Captor.meterName.c_str() : "Sagemcom / DSMR 5.0";
  const char *lastTs = P1Captor.DataReaded.P1timestamp[0] != '\0' ? P1Captor.DataReaded.P1timestamp : LANG_DEBUG_WAITDATA;
  size_t dLen = P1Captor.datagram.length();
  int rssi = WiFi.RSSI();
  uint32_t freeHeap = ESP.getFreeHeap();
  uint8_t clients = (TelnetSrv != nullptr) ? TelnetSrv->GetConnectedClientCount() : 0;
  const char *telnetState = (TelnetSrv != nullptr) ? LANG_DEBUG_ACTIVE : LANG_DEBUG_DISABLED;

  snprintf_P(HTMLBufferContent, sizeof(HTMLBufferContent), template_html,
             LANG_DEBUGH1,
             mName,
             lastTs,
             (unsigned int)dLen,
             rssi,
             WiFi.localIP().toString().c_str(),
             uptimeStr,
             freeHeap,
             telnetState,
             clients,
             LANG_SHOWRAW,
             LANG_SHOWJSON,
             LANG_MENU);

  SendWithHeaderFooter("text/html", HTMLBufferContent, "", false);
}