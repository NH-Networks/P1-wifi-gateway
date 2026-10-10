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

#include "TelnetMgr.h"

extern void RequestRestart(unsigned long delay);

TelnetMgr::TelnetMgr(settings &currentConf, P1Reader &currentP1)
    : conf(currentConf), P1Captor(currentP1), telnet(currentConf.telnetPort ? currentConf.telnetPort : DEFAULT_TELNETPORT)
{
    unsigned int p = conf.telnetPort ? conf.telnetPort : DEFAULT_TELNETPORT;
    MainSendDebugPrintf("[TELNET] Starting server on port %u", p);
    Yield_Delay(50);

    for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    {
        lastActivityTime[i] = 0;
    }

    telnet.setNoDelay(true);
    telnet.begin();

    if (conf.reportToTelnet)
    {
        P1Captor.OnNewDatagram([this]()
        {
            SendDataGram();
        });
    }
}

TelnetMgr::~TelnetMgr()
{
    stop();
}

void TelnetMgr::DoMe()
{
    handleClientActivity();
    handleNewConnections();
}

void TelnetMgr::stop()
{
    for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    {
        if (telnetClients[i])
        {
            telnetClients[i].flush();
            telnetClients[i].stop();
        }
        lastActivityTime[i] = 0;
    }
    telnet.stop();
}

int TelnetMgr::findFreeClientSlot()
{
    for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    {
        if (!telnetClients[i] || !telnetClients[i].connected())
        {
            return i;
        }
    }
    return MAX_SRV_CLIENTS;
}

void TelnetMgr::closeConnection(int clientId)
{
    if (clientId >= 0 && clientId < MAX_SRV_CLIENTS)
    {
        if (telnetClients[clientId])
        {
            telnetClients[clientId].flush();
            telnetClients[clientId].stop();
        }
        lastActivityTime[clientId] = 0;
    }
}

void TelnetMgr::handleNewConnections()
{
    if (telnet.hasClient())
    {
        int i = findFreeClientSlot();
        if (i < MAX_SRV_CLIENTS)
        {
            telnetClients[i] = telnet.accept();
            telnetClients[i].setNoDelay(true);
            telnetClients[i].setTimeout(200);
            lastActivityTime[i] = millis();
            MainSendDebugPrintf("[TELNET] Client %d connected (%s)", i, telnetClients[i].remoteIP().toString().c_str());
            // Intentionally no banner/prompt is sent here so DSMR consumers (Home Assistant, DSMR-reader)
            // receive only pristine telegram data with correct CRC.
        }
        else
        {
            WiFiClient busyClient = telnet.accept();
            busyClient.println("Server busy (max clients reached)");
            busyClient.flush();
            busyClient.stop();
            MainSendDebugPrintf("[TELNET] No slot free for new connection");
        }
    }
}

void TelnetMgr::handleClientActivity()
{
    for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    {
        if (telnetClients[i] && telnetClients[i].connected())
        {
            while (telnetClients[i].available())
            {
                String command = telnetClients[i].readStringUntil('\n');
                command.trim();
                lastActivityTime[i] = millis();

                if (command.length() > 0)
                {
                    processCommand(i, command);
                }
            }
        }
        else if (telnetClients[i] && !telnetClients[i].connected())
        {
            closeConnection(i);
        }
    }
}

void TelnetMgr::processCommand(int clientId, const String &command)
{
    if (command.equalsIgnoreCase("exit") || command.equalsIgnoreCase("quit"))
    {
        telnetClients[clientId].println("Goodbye!");
        closeConnection(clientId);
    }
    else if (command.equalsIgnoreCase("reboot"))
    {
        MainSendDebugPrintf("[TELNET] Reboot requested by client %d", clientId);
        telnetClients[clientId].println("Rebooting ESP...");
        telnetClients[clientId].flush();
        RequestRestart(1000);
    }
    else if (command.equalsIgnoreCase("help") || command == "?")
    {
        commandeHelp(clientId);
    }
    else if (command.equalsIgnoreCase("raw"))
    {
        telnetClients[clientId].print(P1Captor.datagram);
    }
    else if (command.equalsIgnoreCase("read"))
    {
        P1Captor.ResetnextUpdateTime();
        telnetClients[clientId].println("Read triggered");
    }
    else
    {
        telnetClients[clientId].printf("Unknown command: %s. Type 'help' for commands.\r\n", command.c_str());
    }
}

void TelnetMgr::commandeHelp(int clientId)
{
    telnetClients[clientId].println("Available commands: raw, read, reboot, help, exit");
}

void TelnetMgr::SendDataGram()
{
    if (!conf.reportToTelnet || P1Captor.datagram.length() == 0)
    {
        return;
    }

    size_t len = P1Captor.datagram.length();
    const uint8_t *data = reinterpret_cast<const uint8_t *>(P1Captor.datagram.c_str());

    for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    {
        if (telnetClients[i] && telnetClients[i].connected())
        {
            if (telnetClients[i].availableForWrite() >= 1)
            {
                telnetClients[i].write(data, len);
                telnetClients[i].flush();
                lastActivityTime[i] = millis();
            }
            else
            {
                MainSendDebugPrintf("[TELNET] Client %d congested, disconnecting.", i);
                closeConnection(i);
            }
        }
    }
    yield();
}

void TelnetMgr::SendDebug(const char *payload)
{
    if (!conf.debugToTelnet || payload == nullptr)
    {
        return;
    }

    for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    {
        if (telnetClients[i] && telnetClients[i].connected())
        {
            if (telnetClients[i].availableForWrite() > 0)
            {
                telnetClients[i].print("[DEBUG] ");
                telnetClients[i].println(payload);
            }
        }
    }
}

uint8_t TelnetMgr::GetConnectedClientCount()
{
    uint8_t count = 0;
    for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    {
        if (telnetClients[i] && telnetClients[i].connected())
        {
            count++;
        }
    }
    return count;
}

bool TelnetMgr::IsRunning()
{
    return true;
}
