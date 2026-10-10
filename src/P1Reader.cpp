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

#include "P1Reader.h"

P1Reader::P1Reader(settings &currentConf) : conf(currentConf)
{
  Serial.begin(SERIALSPEED);
  datagram.reserve(3000);
  unsigned long intervalMs = (unsigned long)conf.interval * 1000UL;
  if (intervalMs > 5000)
  {
    lastReadTime = millis() - (intervalMs - 5000);
  }
  else
  {
    lastReadTime = millis();
  }
}

void P1Reader::RTS_on() // switch on Data Request
{
  MainSendDebug("[P1] Data requested");
  Serial.flush();
  while (Serial.available() > 0)
  {
    Serial.read();
  }
  
  state = State::WAITING;
  digitalWrite(DR, HIGH); // turn on Data Request
  digitalWrite(OE, LOW);  // enable buffer (active low on 74AHCT1G125)
  readStartTime = millis();
}

void P1Reader::RTS_off() // switch off Data Request
{
  state = State::DISABLED;
  lastReadTime = millis();
  digitalWrite(DR, LOW);  // turn off Data Request
  digitalWrite(OE, HIGH); // put buffer in Tristate mode
}

void P1Reader::ResetnextUpdateTime()
{
  RTS_off();
  lastReadTime = millis() - ((unsigned long)conf.interval * 1000UL);
}

int P1Reader::FindCharInArray(const char array[], char c, int len)
{
  for (int i = 0; i < len; i++)
  {
    if (array[i] == c)
    {
      return i;
    }
  }
  return -1;
}

String P1Reader::identifyMeter(String Name)
{
  if (Name.indexOf("FLU5\\") != -1)
  {
    return "Siconia (België)";
  }
  if (Name.indexOf("ISK5\\2M550E-1011") != -1 || Name.indexOf("ISK5\\2M550E-1012") != -1)
  {
    return "Iskra AM550";
  }
  if (Name.indexOf("KFM5KAIFA-METER") != -1 || Name.indexOf("KFM5") != -1)
  {
    return "Kaifa (MA105/MA304)";
  }
  if (Name.indexOf("XMX5LGBBFG10") != -1)
  {
    return "Landis + Gyr E350";
  }
  if (Name.indexOf("XMX5LG") != -1)
  {
    return "Landis + Gyr";
  }
  if (Name.indexOf("Ene5\\T210-D") != -1 || Name.indexOf("T210-D") != -1)
  {
    return "Sagemcom T210-D";
  }
  return "Slimme Meter";
}

void P1Reader::decodeTelegram(int len)
{
  int startChar = FindCharInArray(telegram, '/', len);
  int endChar = FindCharInArray(telegram, '!', len);

  if (state == State::WAITING)
  {
    if (startChar >= 0)
    {
      MainSendDebug("[P1] Start of datagram found");
      datagram = "";
      dataEnd = false;
      state = State::READING;

      for (int cnt = startChar; cnt < len; cnt++)
      {
        datagram += telegram[cnt];
      }

      if (meterName == "")
      {
        meterName = identifyMeter(telegram);
      }
      return;
    }
    else
    {
      return;
    }
  }

  if (state == State::READING)
  {
    if (endChar >= 0)
    {
      MainSendDebug("[P1] End of datagram found");
      dataEnd = true;
     
      if (datagram.length() < 4096)
      {
        for (int cnt = 0; cnt < len; cnt++)
        {
          datagram += telegram[cnt];
        }
        datagram += "\r\n";
      }
      else
      {
        MainSendDebug("[P1] Datagram buffer overflow!");
        state = State::FAULT;
        return;
      }

      state = State::DONE;
      LastSample = millis();
      return;
    }
    else
    {
      for (int cnt = 0; cnt < len; cnt++)
      {
        datagram += telegram[cnt];
      }
      OBISparser(len);
    }
  }
}

String P1Reader::readFirstParenthesisVal(int start, int end)
{
  String value = "";
  int i = start + 1;
  bool trailingZero = true;
  while ((i < end) && (telegram[i] != ')'))
  {
    if (trailingZero && telegram[i] == '0')
    {
      i++;
    }
    else
    {
      value += (char)telegram[i];
      trailingZero = false;
      i++;
    }
  }
  return (value.length() > 0) ? value : "0";
}

String P1Reader::readUntilStar(int start, int end)
{
  String value = "";
  int i = start + 1;
  bool trailingZero = true;
  while ((i < end) && (telegram[i] != '*'))
  {
    if (trailingZero && telegram[i] != '0')
    {
      trailingZero = false;
    }
    if (telegram[i] == '0' && (i + 1 < end) && telegram[i + 1] == '.')
    {
      trailingZero = false;
    }
    if (!trailingZero)
    {
      value += (char)telegram[i];
    }
    i++;
  }
  return (value.length() > 0) ? value : "0";
}

String P1Reader::readBetweenDoubleParenthesis(int start, int end)
{
  String value = "";
  int i = start + 1;
  while ((i < end - 1) && (telegram[i] != ')') && (telegram[i + 1] != '('))
  {
    i++;
  }
  i++;
  value = readUntilStar(i, end);
  return (value.length() > 0) ? value : "0";
}

void P1Reader::OBISparser(int len)
{
  int i;
  String inString = "";
  int idx;

  for (i = 0; i < len; i++)
  {
    if (telegram[i] == '(')
    {
      break;
    }
    if (isDigit(telegram[i]))
    {
      inString += (char)telegram[i];
    }
  }

  idx = inString.toInt();
  
  switch (idx)
  {
  case 0:
    break;
  case 9614: // device type
    readFirstParenthesisVal(i, len).toCharArray(DataReaded.P1version, sizeof(DataReaded.P1version));
    break;
  case 100:
    readFirstParenthesisVal(i, len).toCharArray(DataReaded.P1timestamp, sizeof(DataReaded.P1timestamp));
    break;
  case 96140:
    DataReaded.tariffIndicatorElectricity = readFirstParenthesisVal(i, len).toInt();
    if (conf.InverseHigh_1_2_Tarif)
    {
      if (DataReaded.tariffIndicatorElectricity == 1)
      {
        DataReaded.tariffIndicatorElectricity = 2;
      }
      else
      {
        DataReaded.tariffIndicatorElectricity = 1;
      }
    }
    break;
  case 9611:
    readFirstParenthesisVal(i, len).toCharArray(DataReaded.equipmentId, sizeof(DataReaded.equipmentId));
    break;
  case 19610:
    readFirstParenthesisVal(i, len).toCharArray(DataReaded.equipmentId2, sizeof(DataReaded.equipmentId2));
    break;
  case 9810:
    break;
  case 10170: // 1-0:1.7.0 – actualElectricityPowerDelivered
    DataReaded.actualElectricityPowerDeli = FixedValue(readUntilStar(i, len));
    break;
  case 10270: // 1-0:2.7.0 – actualElectricityPowerReturned
    DataReaded.actualElectricityPowerRet = FixedValue(readUntilStar(i, len));
    break;
  case 10181: // 1-0:1.8.1 Elektra verbruik laag tarief
    if (!conf.InverseHigh_1_2_Tarif)
    {
      DataReaded.electricityUsedTariff1 = FixedValue(readUntilStar(i, len));
    }
    else
    {
      DataReaded.electricityUsedTariff2 = FixedValue(readUntilStar(i, len));
    }
    break;
  case 10182: // 1-0:1.8.2 Elektra verbruik hoog tarief
    if (!conf.InverseHigh_1_2_Tarif)
    {
      DataReaded.electricityUsedTariff2 = FixedValue(readUntilStar(i, len));
    }
    else
    {
      DataReaded.electricityUsedTariff1 = FixedValue(readUntilStar(i, len));
    }
    break;
  case 10281: // 1-0:2.8.1 Elektra opbrengst laag tarief
    if (!conf.InverseHigh_1_2_Tarif)
    {
      DataReaded.electricityReturnedTariff1 = FixedValue(readUntilStar(i, len));
    }
    else
    {
      DataReaded.electricityReturnedTariff2 = FixedValue(readUntilStar(i, len));
    }
    break;
  case 10282: // 1-0:2.8.2 Elektra opbrengst hoog tarief
    if (!conf.InverseHigh_1_2_Tarif)
    {
      DataReaded.electricityReturnedTariff2 = FixedValue(readUntilStar(i, len));
    }
    else
    {
      DataReaded.electricityReturnedTariff1 = FixedValue(readUntilStar(i, len));
    }
    break;
  case 103170: // 1-0:31.7.0 Stroom L1
    DataReaded.instantaneousCurrentL1 = FixedValue(readUntilStar(i, len));
    break;
  case 105170: // 1-0:51.7.0 Stroom L2
    DataReaded.instantaneousCurrentL2 = FixedValue(readUntilStar(i, len));
    break;
  case 107170: // 1-0:71.7.0 Stroom L3
    DataReaded.instantaneousCurrentL3 = FixedValue(readUntilStar(i, len));
    break;
  case 103270: // 1-0:32.7.0 Spanning L1
    DataReaded.instantaneousVoltageL1 = FixedValue(readUntilStar(i, len));
    break;
  case 105270: // 1-0:52.7.0 Spanning L2
    DataReaded.instantaneousVoltageL2 = FixedValue(readUntilStar(i, len));
    break;
  case 107270: // 1-0:72.7.0 Spanning L3
    DataReaded.instantaneousVoltageL3 = FixedValue(readUntilStar(i, len));
    break;
  case 102170: // 1-0:21.7.0 Actief vermogen L1 geleverd
    DataReaded.activePowerL1P = FixedValue(readUntilStar(i, len));
    break;
  case 104170: // 1-0:41.7.0 Actief vermogen L2 geleverd
    DataReaded.activePowerL2P = FixedValue(readUntilStar(i, len));
    break;
  case 106170: // 1-0:61.7.0 Actief vermogen L3 geleverd
    DataReaded.activePowerL3P = FixedValue(readUntilStar(i, len));
    break;
  case 102270: // 1-0:22.7.0 Actief vermogen L1 teruggeleverd
    DataReaded.activePowerL1NP = FixedValue(readUntilStar(i, len));
    break;
  case 104270: // 1-0:42.7.0 Actief vermogen L2 teruggeleverd
    DataReaded.activePowerL2NP = FixedValue(readUntilStar(i, len));
    break;
  case 106270: // 1-0:62.7.0 Actief vermogen L3 teruggeleverd
    DataReaded.activePowerL3NP = FixedValue(readUntilStar(i, len));
    break;
  case 1032320: // Korte spanningsdalingen L1
    DataReaded.numberVoltageSagsL1 = readFirstParenthesisVal(i, len).toInt();
    break;
  case 1052320: // Korte spanningsdalingen L2
    DataReaded.numberVoltageSagsL2 = readFirstParenthesisVal(i, len).toInt();
    break;
  case 1072320: // Korte spanningsdalingen L3
    DataReaded.numberVoltageSagsL3 = readFirstParenthesisVal(i, len).toInt();
    break;
  case 1032360: // Korte spanningsstijgingen L1
    DataReaded.numberVoltageSwellsL1 = readFirstParenthesisVal(i, len).toInt();
    break;
  case 1052360: // Korte spanningsstijgingen L2
    DataReaded.numberVoltageSwellsL2 = readFirstParenthesisVal(i, len).toInt();
    break;
  case 1072360: // Korte spanningsstijgingen L3
    DataReaded.numberVoltageSwellsL3 = readFirstParenthesisVal(i, len).toInt();
    break;
  case 12421: // Gas channel 1 (NL DSMR4/5)
  case 22421: // Gas channel 2 (NL DSMR4/5)
  case 32421: // Gas channel 3
  case 42421: // Gas channel 4
  case 12423: // Gas channel 1 (België Fluvius)
  case 22423: // Gas channel 2 (België Fluvius)
    readBetweenDoubleParenthesis(i, len).toCharArray(DataReaded.gasReceived5min, sizeof(DataReaded.gasReceived5min));
    readBetweenDoubleParenthesis(i, len).toCharArray(DataReaded.gasDomoticz, sizeof(DataReaded.gasDomoticz));
    break;
  case 96721: // Aantal stroomonderbrekingen
    DataReaded.numberPowerFailuresAny = readFirstParenthesisVal(i, len).toInt();
    break;
  case 9679: // Aantal lange stroomonderbrekingen
    DataReaded.numberLongPowerFailuresAny = readFirstParenthesisVal(i, len).toInt();
    break;
  case 1099970: // Long power failure log
    DataReaded.longPowerFailuresLog = "";
    while (i < len)
    {
      DataReaded.longPowerFailuresLog += (char)telegram[i];
      i++;
    }
    break;
  case 10140:
  case 10160:
  case 96310:
  case 1700:
  case 103140:
  case 96130:
    break;
  default:
    MainSendDebugPrintf("[P1] Unknown OBIS line: %s", inString.c_str());
    break;
  }
  inString = "";
}

unsigned long P1Reader::GetNextUpdateInMs()
{
  if (state != State::DISABLED)
  {
    return 0;
  }
  unsigned long intervalMs = (unsigned long)conf.interval * 1000UL;
  unsigned long elapsed = millis() - lastReadTime;
  if (elapsed >= intervalMs)
  {
    return 0;
  }
  return intervalMs - elapsed;
}

unsigned long P1Reader::GetnextUpdateTime()
{
  return millis() + GetNextUpdateInMs();
}

void P1Reader::DoMe()
{
  unsigned long intervalMs = (unsigned long)conf.interval * 1000UL;
  if ((millis() - lastReadTime >= intervalMs) && state == State::DISABLED)
  {
    RTS_on();
  }

  if (state == State::WAITING || state == State::READING)
  {
    readTelegram();
  }
}

bool P1Reader::CheckTimeout()
{
  if (millis() - readStartTime >= P1TIMEOUTREAD)
  {
    MainSendDebug("[P1] Read timeout reached");
    RTS_off();
    return true;
  }
  return false;
}

void P1Reader::readTelegram()
{
  if (state != State::WAITING && state != State::READING)
  {
    return;
  }

  if (CheckTimeout())
  {
    return;
  }

  if (Serial.available())
  {
    memset(telegram, 0, sizeof(telegram));
    while (Serial.available())
    {
      if (CheckTimeout())
      {
        return;
      }

      int len = Serial.readBytesUntil('\n', telegram, sizeof(telegram) - 2);
      if (len <= 0)
      {
        break;
      }
      telegram[len] = '\n';
      telegram[len + 1] = 0;
      
      decodeTelegram(len + 1);

      if (state == State::DONE)
      {
        // Non-blocking quick LED pulse
        digitalWrite(LED_BUILTIN, LED_ON);
        RTS_off();
        TriggerCallbacks();
        digitalWrite(LED_BUILTIN, LED_OFF);
        break;
      }
    }
  }
}
