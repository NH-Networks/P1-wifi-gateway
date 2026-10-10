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

#ifndef VARMGR_H
#define VARMGR_H

#include <Arduino.h>

#define VERSION "2.0.0"
#define HOSTNAME "P1meter"

#define CLIENTNAMESIZE 32

#define OE 16 // IO16 OE on the 74AHCT1G125
#define DR 4  // IO4 is Data Request

#define LED_ON 0x0
#define LED_OFF 0x1

#define SETTINGVERSIONNULL 0 //= no config
#define SETTINGVERSION 4

struct settings
{
  byte ConfigVersion;
  byte BootFailed = 0;
  bool NeedConfig = true;
  char ssid[33];
  char password[65];
  unsigned int interval = 10;
  bool InverseHigh_1_2_Tarif = false;
  bool telnet = true;
  bool reportToTelnet = true;
  bool debugToTelnet = false;
  unsigned int telnetPort = 23;
  char adminPassword[33];
  char adminUser[33];
};

#ifndef LANGUAGE
  #define LANGUAGE 2
#endif

#include "Langues/2-English.h"

#ifndef LANG_LOADED
  #error "LANGUAGE has no valid value"
#endif
#endif
