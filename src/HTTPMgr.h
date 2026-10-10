/*
 * Copyright (c) 2025 Jean-Pierre Sneyers, Ronald Leenes, & Contributors
 * Source : https://github.com/NH-Networks/P1-wifi-gateway
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

#ifndef WEBSERVERMGR_H
#define WEBSERVERMGR_H

#define WWW_PORT_HTTP 80
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "GlobalVar.h"
#include "TelnetMgr.h"
#include "P1Reader.h"

class HTTPMgr
{
public:
  explicit HTTPMgr(settings &currentConf, TelnetMgr *currentTelnet, P1Reader &currentP1);
  void DoMe();
  void start_webservices();
  static String escapeHtml(const char *input);

private:
  settings &conf;
  TelnetMgr *TelnetSrv;
  P1Reader &P1Captor;
  ESP8266WebServer server;
  char HTMLBufferContent[3000];
  bool ChekifAsAdmin();
  void SendWithHeaderFooter(const char *content_type, const char *content, const char *header, bool refresh);
  const char* GetAnimWait();
  void handleRoot();
  void handlePassword();
  void handleSetup();
  void handleRAW();
  void handleFactoryReset();
  void handleSetupSave();
  void handleUploadForm();
  void handleUploadFlash();
  void handleFavicon();
  void handleStyleCSS();
  void handleJSON();
  void handleJSONStatus();
  void handleDebug();
  void handleDebugLog();
  void handleP1Js();
  void handleMainJS();
  void handleReboot();
  void handleHelp();
  void handleApiInfo();
  void handleApiData();
  void handleApiTelegram();

  void RebootPage(const char *Message);
  bool ActifCache(bool);
  void ReplyOTA(bool success, const char* error, u_int ref);

  bool UpdateResultFailed = false;
  String UpdateMsg;
  uint UpdateErrorCode = 0;

  void handleP1();
};

#endif
