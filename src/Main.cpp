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

#define DEBUG_SERIAL_P1

#define MAXBOOTFAILURE 3 // reset settings if boot fails more than this
#define WATCHDOGINTERVAL 30000

#include <Arduino.h>
#include <EEPROM.h>
#include "GlobalVar.h"

char clientName[CLIENTNAMESIZE];
unsigned long lastWatchDogsCheck = 0;

settings config_data;

#include "WifiMgr.h"
WifiMgr *WifiClient = nullptr;

#include "TelnetMgr.h"
TelnetMgr *TelnetServer = nullptr;

#include "P1Reader.h"
P1Reader *DataReaderP1 = nullptr;

#include "HTTPMgr.h"
HTTPMgr *HTTPClient = nullptr;

ADC_MODE(ADC_VCC); // allows monitoring internal VCC level

#define DEBUG_LOG_LINES 40
#define DEBUG_LOG_LINE_LEN 120
static char debugLogRing[DEBUG_LOG_LINES][DEBUG_LOG_LINE_LEN];
static int debugLogHead = 0;
static int debugLogCount = 0;

static void AddDebugLog(const char *payload)
{
  if (!payload || payload[0] == '\0') return;
  unsigned long s = millis() / 1000;
  unsigned int h = s / 3600;
  unsigned int m = (s % 3600) / 60;
  unsigned int sec = s % 60;
  snprintf(debugLogRing[debugLogHead], DEBUG_LOG_LINE_LEN, "[%02u:%02u:%02u] %s", h, m, sec, payload);
  debugLogHead = (debugLogHead + 1) % DEBUG_LOG_LINES;
  if (debugLogCount < DEBUG_LOG_LINES) debugLogCount++;
}

String GetDebugLogs()
{
  String out;
  out.reserve(debugLogCount * 80);
  int start = (debugLogCount < DEBUG_LOG_LINES) ? 0 : debugLogHead;
  for (int i = 0; i < debugLogCount; i++)
  {
    int idx = (start + i) % DEBUG_LOG_LINES;
    out += debugLogRing[idx];
    out += "\n";
  }
  return out;
}

void MainSendDebug(const char *payload)
{
  AddDebugLog(payload);

  #ifdef DEBUG_SERIAL_P1
  Serial.println(payload);
  #endif

  if (TelnetServer != nullptr)
  {
    TelnetServer->SendDebug(payload);
  }
}

void MainSendDebugPrintf(const char *format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    MainSendDebug(buffer);
}

void Yield_Delay(unsigned long ms)
{
  unsigned long start = millis();
  while (millis() - start < ms)
  {
    yield();
  }
}

void blink(int t, unsigned long speed)
{
  t = t * 2;
  for (int i = 0; i <= t; i++)
  {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(speed);
  }
}

#ifdef DEBUG_SERIAL_P1
void PrintConfigData()
{
  MainSendDebug("[Core] Instellingen :");
  MainSendDebugPrintf(" - ConfigVersion : %d", config_data.ConfigVersion);
  MainSendDebugPrintf(" - Boot poging : %d", config_data.BootFailed);
  MainSendDebugPrintf(" - Admin login : %s", config_data.adminUser);
  MainSendDebugPrintf(" - SSID : %s", config_data.ssid);
  MainSendDebugPrintf(" - Meetinterval : %u sec", config_data.interval);
  MainSendDebugPrintf(" - Invert high/low tarief: %s", (config_data.InverseHigh_1_2_Tarif) ? "Ja" : "Nee");
  MainSendDebugPrintf(" - Poort 23 (Telnet) Actief : %s", (config_data.telnet) ? "Ja" : "Nee");
  MainSendDebugPrintf("   # Poort : %u", config_data.telnetPort);
  MainSendDebugPrintf("   # Stream Datagram : %s", (config_data.reportToTelnet) ? "Ja" : "Nee");
  MainSendDebugPrintf("   # Send debug : %s", (config_data.debugToTelnet) ? "Ja" : "Nee");
  Yield_Delay(20);
}
#endif

void SetName()
{
    const u_int8_t macLen = 17;
    char macAddr[18];

    strncpy(macAddr, WiFi.macAddress().c_str(), sizeof(macAddr) - 1);
    strcpy(clientName, HOSTNAME);
    strcat(clientName, "-");
    
    const char* macStr = macAddr;
    
    char lastBytes[5];
    lastBytes[0] = macStr[macLen-5];
    lastBytes[1] = macStr[macLen-4];
    lastBytes[2] = macStr[macLen-2];
    lastBytes[3] = macStr[macLen-1];
    lastBytes[4] = '\0';
    
    strcat(clientName, lastBytes);
}

char* GetClientName()
{
  return clientName;
}

void setup()
{
  #ifdef DEBUG_SERIAL_P1
  Serial.begin(SERIALSPEED);
  Serial.println("Booting...");
  #endif
  SetName();
  MainSendDebugPrintf("[Core] Firmware: v%s.%u", VERSION, BUILD_DATE);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(OE, OUTPUT);    // IO16 OE on the 74AHCT1G125
  digitalWrite(OE, HIGH); // Put/Keep OE in Tristate mode
  pinMode(DR, OUTPUT);    // IO4 Data Request
  digitalWrite(DR, LOW);  // DR low (only goes high when we want to receive data)

  MainSendDebug("[Core] Load configuration from EEPROM");

  EEPROM.begin(sizeof(struct settings));
  EEPROM.get(0, config_data);

  if (config_data.ConfigVersion != SETTINGVERSION || config_data.BootFailed >= MAXBOOTFAILURE)
  {    
    char savedSsid[33] = "";
    char savedPass[65] = "";
    bool canMigrateWifi = false;

    if (config_data.BootFailed < MAXBOOTFAILURE &&
        (config_data.ConfigVersion == 2 || config_data.ConfigVersion == 3) &&
        config_data.ssid[0] != '\0' && (uint8_t)config_data.ssid[0] != 0xFF)
    {
      strncpy(savedSsid, config_data.ssid, sizeof(savedSsid) - 1);
      strncpy(savedPass, config_data.password, sizeof(savedPass) - 1);
      canMigrateWifi = true;
    }
    else if (config_data.BootFailed < MAXBOOTFAILURE &&
             config_data.ConfigVersion == 'j') // Romix 1.1bcd (dataSet[0] == 'j')
    {
      // Romix 1.1 layout: dataSet[4], ssid[32] at offset 4, password[64] at offset 36
      for (int i = 0; i < 32; i++) { savedSsid[i] = (char)EEPROM.read(4 + i); }
      savedSsid[32] = '\0';
      for (int i = 0; i < 64; i++) { savedPass[i] = (char)EEPROM.read(36 + i); }
      savedPass[64] = '\0';

      if (savedSsid[0] != '\0' && (uint8_t)savedSsid[0] != 0xFF)
      {
        canMigrateWifi = true;
      }
      else
      {
        savedSsid[0] = '\0';
        savedPass[0] = '\0';
      }
    }

    if (config_data.ConfigVersion != SETTINGVERSION)
    {
      MainSendDebugPrintf("[Core] Instellingen migreren/bijwerken (was:%d nieuw:%d)", config_data.ConfigVersion, SETTINGVERSION);
    }
    else
    {
      MainSendDebugPrintf("[Core] Te veel boot failures (%d), reset!", config_data.BootFailed);
    }

    blink(20, 50UL);

    config_data = (settings){
      SETTINGVERSION, // ConfigVersion
      0,              // BootFailed
      (savedSsid[0] == '\0'), // NeedConfig: false als we WiFi migreren, true bij schone reset
      "",             // ssid
      "",             // password
      10,             // interval
      false,          // InverseHigh_1_2_Tarif
      true,           // telnet
      true,           // reportToTelnet
      false,          // debugToTelnet
      23,             // telnetPort
      "",             // adminPassword
      "admin"         // adminUser
    };

    if (canMigrateWifi)
    {
      strncpy(config_data.ssid, savedSsid, sizeof(config_data.ssid) - 1);
      strncpy(config_data.password, savedPass, sizeof(config_data.password) - 1);
      MainSendDebugPrintf("[Core] WiFi instellingen succesvol gemigreerd voor SSID '%s'", config_data.ssid);
    }
  }
  else
  {
    config_data.BootFailed++;
  }

  EEPROM.put(0, config_data);
  EEPROM.commit();

  #ifdef DEBUG_SERIAL_P1
  PrintConfigData();
  #endif

  WifiClient = new WifiMgr(config_data);
  DataReaderP1 = new P1Reader(config_data);

  if (config_data.telnet)
  {
    TelnetServer = new TelnetMgr(config_data, *DataReaderP1);
  }

  HTTPClient = new HTTPMgr(config_data, TelnetServer, *DataReaderP1);

  blink(2, 500UL);

  WifiClient->Connect();
  HTTPClient->start_webservices();
}

void doWatchDogs()
{
  if (ESP.getFreeHeap() < 2000)
  {
    MainSendDebug("[Core] FATAL: Memory leak detected!");
    ESP.restart();
  }

  if (config_data.BootFailed != 0)
  {
    config_data.BootFailed = 0;
    EEPROM.put(0, config_data);
    EEPROM.commit();
  }

  lastWatchDogsCheck = millis();
}

void loop()
{
  WifiClient->DoMe();
  DataReaderP1->DoMe();
  HTTPClient->DoMe();

  if (TelnetServer != nullptr)
  {
    TelnetServer->DoMe();
  }

  if (millis() - lastWatchDogsCheck >= WATCHDOGINTERVAL)
  {
    doWatchDogs();
  }
  yield();
}

void RequestRestart(unsigned long delay)
{
  MainSendDebug("[Core] Reboot requested!");

  if (TelnetServer != nullptr)
  {
    TelnetServer->stop();
  }

  Yield_Delay(delay);
  ESP.restart();
}