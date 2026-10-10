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

#include "WifiMgr.h"

extern char* GetClientName();
extern void Yield_Delay(unsigned long ms);

void WifiMgr::DoMe()
{
  if (WiFi.status() != LastStatusEvent)
  {
    wl_status_t tmp = LastStatusEvent;
    LastStatusEvent = WiFi.status();
    
    MainSendDebugPrintf("[WIFI][Connected:%s] Event %s -> %s", (WiFi.isConnected()) ? "Y" : "N", StatusIdToString(tmp).c_str(), StatusIdToString(LastStatusEvent).c_str());

    if (LastStatusEvent == WL_NO_SSID_AVAIL && conf.ssid[0] != '\0')
    {
      Connect();
      return;
    }

    if (DelegateWifiChange != nullptr)
    {
      DelegateWifiChange(WiFi.isConnected(), tmp, LastStatusEvent);
    }
  }

  // If in AP mode with a configured SSID: periodic scan to recover connection
  if (AsAP() && conf.ssid[0] != '\0')
  {
    // Auto-reconnect if AP has been running with no client connected for over 5 minutes
    if (WiFi.softAPgetStationNum() == 0 && APtimer > 0 && (millis() - APtimer > 300000))
    {
      MainSendDebug("[WIFI] In AP-modus zonder verbonden clients voor >5 min; opnieuw verbinden met WiFi...");
      APtimer = millis();
      Reconnect();
      return;
    }

    if ((millis() - LastScanSSID) > INTERVAL_SCAN_SSID_MS)
    {
      LastScanSSID = millis();
      if (FindThesSSID())
      {
        Reconnect();
      }
    }
  }
}

String WifiMgr::CurrentIP()
{
  return WiFi.localIP().toString();
}

bool WifiMgr::FindThesSSID()
{
  MainSendDebugPrintf("[WIFI] Zoeken naar SSID: %s", conf.ssid);
  
  int numNetworks = WiFi.scanNetworks();
  if (numNetworks <= 0)
  {
    return false;
  }

  for (int i = 0; i < numNetworks; ++i)
  {
    if (strcmp(WiFi.SSID(i).c_str(), conf.ssid) == 0)
    {
      return true;
    }
  }

  return false;
}

String WifiMgr::StatusIdToString(wl_status_t status)
{
  switch (status)
  {
  case WL_NO_SHIELD:
    return "NO_SHIELD";
  case WL_IDLE_STATUS:
    return "IDLE_STATUS";
  case WL_NO_SSID_AVAIL:
    return "NO_SSID_AVAIL";
  case WL_SCAN_COMPLETED:
    return "SCAN_COMPLETED";
  case WL_CONNECTED:
    return "CONNECTED";
  case WL_CONNECT_FAILED:
    return "CONNECT_FAILED";
  case WL_CONNECTION_LOST:
    return "CONNECTION_LOST";
  case WL_WRONG_PASSWORD:
    return "WRONG_PASSWORD";
  case WL_DISCONNECTED:
    return "DISCONNECTED";
  default:
    return "UNKNOWN";
  }
}

WifiMgr::WifiMgr(settings &currentConf) : conf(currentConf)
{
  LastStatusEvent = WL_IDLE_STATUS;
  WiFi.persistent(true);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
}

void WifiMgr::OnWifiEvent(std::function<void(bool, wl_status_t, wl_status_t)> CallBack)
{
  DelegateWifiChange = CallBack;
}

void WifiMgr::Connect()
{
  if (conf.ssid[0] != '\0')
  {
    MainSendDebugPrintf("[WIFI] Verbinden met '%s'", conf.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(conf.ssid, conf.password);

    byte tries = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      Yield_Delay(300);

      if (tries++ >= 30)
      {
        MainSendDebugPrintf("[WIFI] Kan geen verbinding maken met '%s'. AP-modus geactiveerd.", conf.ssid);
        SetAPMod();
        break;
      }
    }

    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED)
    {
      WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event)
      {
        MainSendDebugPrintf("[WIFI] Verbinding verbroken (reden: %u)", (unsigned int)event.reason);
      });
      WiFi.setAutoReconnect(true);
      MainSendDebugPrintf("[WIFI] Verbonden, IP: %s", WiFi.localIP().toString().c_str());
      setRFPower();
      digitalWrite(LED_BUILTIN, LED_OFF);
    }
  }
  else
  {
    SetAPMod();
  }
}

void WifiMgr::Reconnect()
{
  MainSendDebugPrintf("[WIFI] Opnieuw verbinden met '%s'...", conf.ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(conf.ssid, conf.password);
  byte tries = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    Yield_Delay(300);
    if (tries++ >= 30)
    {
      MainSendDebugPrintf("[WIFI] Kan niet verbinden met '%s', terug naar AP-modus", conf.ssid);
      SetAPMod();
      return;
    }
  }

  MainSendDebugPrintf("[WIFI] Opnieuw verbonden, IP: %s", WiFi.localIP().toString().c_str());
  setRFPower();
  digitalWrite(LED_BUILTIN, LED_OFF);
}

bool WifiMgr::IsConnected()
{
  return WiFi.isConnected();
}

void WifiMgr::SetAPMod()
{
  digitalWrite(LED_BUILTIN, LED_ON);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(GetClientName(), "");
  MainSendDebugPrintf("[WIFI] Setup AP '%s' gestart (IP: %s)", GetClientName(), WiFi.softAPIP().toString().c_str());
  APtimer = millis();
}

bool WifiMgr::AsAP()
{
  return (WiFi.getMode() != WIFI_STA);
}

void WifiMgr::setRFPower()
{
  // Full transmit power (20.5 dBm) for maximum penetration through metal meter cabinets
  WiFi.setOutputPower(20.5);
  currentPowerWifi = 20.5;
}

float WifiMgr::CalcuAdjustWiFiPower()
{
  return 20.5;
}
