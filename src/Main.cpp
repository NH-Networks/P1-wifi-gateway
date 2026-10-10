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
unsigned long WatchDogsTimer = millis() + WATCHDOGINTERVAL;

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

void MainSendDebug(const char *payload)
{
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
    const int initialBufferSize = 128;
    const int maxBufferSize = 1024;
    char* buffer = nullptr;
    int bufferSize = initialBufferSize;
    int length = 0;
    va_list args;

    do
    {
        delete[] buffer;
        buffer = new char[bufferSize];
        
        va_start(args, format);
        length = vsnprintf(buffer, bufferSize, format, args);
        va_end(args);

        if (length < 0)
        {
            delete[] buffer;
            return;
        }

        if (length >= bufferSize)
        {
            bufferSize *= 2;
            if (bufferSize > maxBufferSize)
            {
                delete[] buffer;
                return;
            }
        }
    } while (length >= bufferSize);

    MainSendDebug(buffer);
    delete[] buffer;
}

void Yield_Delay(unsigned long ms)
{
  unsigned long WaitUntil = millis() + ms;
  while (millis() <= WaitUntil)
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
    if (config_data.ConfigVersion != SETTINGVERSION)
    {
      MainSendDebugPrintf("[Core] Reset settings (wanted:%d actual:%d)", SETTINGVERSION, config_data.ConfigVersion);
    }
    else
    {
      MainSendDebugPrintf("[Core] Too many boot failures (%d), Reset config!", config_data.BootFailed);
    }

    blink(20, 50UL);

    config_data = (settings){
      SETTINGVERSION, // ConfigVersion
      0,              // BootFailed
      true,           // NeedConfig
      "",             // ssid
      "",             // password
      10,             // interval
      false,          // InverseHigh_1_2_Tarif
      true,           // telnet
      true,           // reportToTelnet
      false,          // debugToTelnet
      23,             // telnetPort
      "",             // adminPassword
      ""              // adminUser
    };
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
    ESP.reset();
  }

  if (config_data.BootFailed != 0)
  {
    config_data.BootFailed = 0;
    EEPROM.put(0, config_data);
    EEPROM.commit();
  }

  WatchDogsTimer = millis() + WATCHDOGINTERVAL;
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

  if (millis() > WatchDogsTimer)
  {
    doWatchDogs();
  }
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