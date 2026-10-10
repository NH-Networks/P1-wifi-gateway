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

#ifndef TELNETMGR_H
#define TELNETMGR_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "Debug.h"
#include "GlobalVar.h"
#include "P1Reader.h"

#define MAX_SRV_CLIENTS 5
#define DEFAULT_TELNETPORT 23

class TelnetMgr
{
private:
  settings &conf;
  P1Reader &P1Captor;
  WiFiServer telnet;
  WiFiClient telnetClients[MAX_SRV_CLIENTS];
  unsigned long lastActivityTime[MAX_SRV_CLIENTS];

  void handleNewConnections();
  int findFreeClientSlot();
  void handleClientActivity();
  void processCommand(int clientId, const String &command);
  void commandeHelp(int clientId);
  void closeConnection(int clientId);

public:
  explicit TelnetMgr(settings &currentConf, P1Reader &currentP1);
  ~TelnetMgr();
  void DoMe();
  void stop();
  void SendDataGram();
  void SendDebug(const char *payload);
  uint8_t GetConnectedClientCount();
  bool IsRunning();
};

#endif