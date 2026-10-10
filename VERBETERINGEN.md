# Verbeterde P1-WiFi Gateway Firmware (Port 23 Versie)

Deze build is een geoptimaliseerde en verbeterde versie van `narfight_builds/P1meter-Dutch-1.03.bin.gz`, specifiek aangepast naar aanleiding van uw wensen:
1. **Interne grafieken volledig verwijderd**: Geen Google Charts, geen LittleFS-logging (`/Last24H.json`), geen onnodige flash-slijtage en aanzienlijk snellere pagina-laadtijden.
2. **Uitsluitend Poort 23 optie**: Domoticz HTTP client en MQTT client zijn verwijderd. De focus ligt 100% op het betrouwbaar en direct streamen van ruwe P1 DSMR-telegrammen over TCP poort 23.
3. **Volledig compatibel met Home Assistant / DSMR-Reader**:
   - Geen verstorende banners (`Welcome!`) of shell-prompts (`P1meter>`) op de TCP stream die CRC-fouten veroorzaakten in DSMR-parsers.
   - Geen login-prompt op poort 23 wanneer er een admin-wachtwoord is ingesteld op de webinterface. Home Assistant en andere DSMR-integraties kunnen nu zonder problemen 24/7 verbinden.
   - Verbindingen worden niet meer na 5 minuten verbroken door foutieve inactiviteitstimeouts.
4. **Geoptimaliseerde voetafdruk**:
   - RAM-geheugengebruik verlaagd van **42.1%** naar **39.0%** (+2.5 KB vrije RAM).
   - Flash-geheugengebruik verlaagd van **54.2%** (417 KB) naar **45.5%** (350 KB) (meer dan 66 KB kleiner!).
   - Gecomprimeerde OTA-grootte verlaagd van **291 KB** naar **248 KB**.

---

## Beschikbare Firmware Bestanden

In de map `narfight_builds/` en `.pio/build/Prod_NL/`:

- [P1meter-Dutch-1.04.bin.gz](file:///d:/Development/P1-WiFi/narfight_builds/P1meter-Dutch-1.04.bin.gz) (Aanbevolen voor OTA-update)
- [P1meter-Dutch-1.04.bin](file:///d:/Development/P1-WiFi/narfight_builds/P1meter-Dutch-1.04.bin) (Voor serieel flashen of ongecomprimeerde upload)
- [P1meter-Dutch-1.03.bin.gz](file:///d:/Development/P1-WiFi/narfight_builds/P1meter-Dutch-1.03.bin.gz) (Vervangende kopie met 1.03 bestandsnaam voor backwards compatibility)

---

## Wijzigingen in Detail

### 1. Verwijdering van Interne Grafieken
- **Verwijderd**: `src/LogP1Mgr.h`, route `/Log24H`, script `/Log24H.js`, bestandsendpoint `/file`.
- **Geen externe afhankelijkheden meer**: Google Charts CDN (`https://www.gstatic.com/charts/loader.js`) is weg; de webinterface werkt nu 100% lokaal en offline.
- **Geen flash-slijtage**: Er wordt niet meer elk uur data naar LittleFS geschreven.

### 2. Focus op Poort 23 (Telnet / Raw DSMR TCP)
- **Verwijderd**:
  - `src/MQTT.cpp` en `src/MQTT.h`
  - `src/DomoticzMgr.cpp` en `src/DomoticzMgr.h`
  - Bibliotheek `AsyncMqttClient` uit `platformio.ini`
  - Alle Domoticz- en MQTT-velden uit het configuratiescherm (`/Setup`)
- **Behouden en verbeterd**:
  - **Meteropties**: Meetinterval (vanaf 1 seconde voor DSMR 5 meters), Peak/off-peak wissel.
  - **WiFi-instellingen**: SSID en wachtwoord.
  - **Poort 23 instellingen**: Poort activeren (standaard aan), poortnummer (standaard 23), DSMR-datagram streamen (standaard aan), optioneel debuggen via poort 23.
  - **Webinterface**: Gemeten waarden (`/P1`), Live JSON (`/P1.json`), Ruw datagram (`/raw`), Firmware update (`/update`), Inloggegevens (`/setPassword`), Herstart en Reset.
  - **Statusbalk**: Toont statusindicator voor Poort 23 en P1 meter.

### 3. Hoe te Flashen via OTA
1. Surf naar het IP-adres van uw P1 meter (bijv. `http://192.168.x.x`).
2. Klik in het menu op **Firmware bijwerken** (`/update`).
3. Kies het bestand `P1meter-Dutch-1.04.bin.gz` (of `P1meter-Dutch-1.03.bin.gz`).
4. Klik op **Bijwerken**. Na ca. 30 seconden herstart de module en is de nieuwe versie actief.
