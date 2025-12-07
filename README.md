# ÖV Abfahrtsanzeiger für die Schweiz

Schweizer ÖV (Öffentlicher Verkehr) Abfahrtsanzeiger mit E-Paper Display für ESP32-C3.

## Hardware

### Version 1: ESP32-C3 + E-Paper (NEU)
- **MCU**: Seeed Studio XIAO ESP32-C3
- **Display**: WeAct Studio 4.2" E-Paper (400x300 Pixel, 3 Farben: schwarz/weiß/rot)
- **Datei**: `oev_anzeiger_epaper_xiao.ino`

### Version 2: LilyGO T3 + OLED (ORIGINAL)
- **MCU**: LilyGO T3 1.6.1 (ESP32)
- **Display**: 0.96" OLED (128x64 Pixel, SSD1306)
- **Datei**: `oev_wizard_oled_proto.ino`

## Features

- ✅ **WiFi-Setup** via Captive Portal
- ✅ **Haltestellensuche** mit opendata.ch API
- ✅ **Live-Abfahrten** für Bus, Tram und Zug
- ✅ **Filter** für Verkehrsmittel
- ✅ **Verspätungsanzeige** in Rot (E-Paper Version)
- ✅ **Automatische Updates** alle 5 Minuten (E-Paper) bzw. 1 Minute (OLED)
- ✅ **NTP-Zeitsynchronisation**
- ✅ **Web-Interface** für Konfiguration

## Pin-Konfiguration

### ESP32-C3 + E-Paper (XIAO)

```
E-Paper Display Pin-Mapping:
(Display → XIAO ESP32-C3)

├── SDA   → D4   (GPIO 6 - MOSI/SPI Data)
├── SCL   → D2   (GPIO 4 - SCK/SPI Clock)
├── CS    → D5   (GPIO 7 - Chip Select)
├── D/C   → D3   (GPIO 5 - Data/Command) ⚠️ NICHT D6 verwenden!
├── RES   → D0   (GPIO 2 - Reset)
├── BUSY  → D1   (GPIO 3 - Busy Signal)
├── VCC   → 3.3V
└── GND   → GND

WICHTIG: Pin D8 (GPIO 8) wird im Code aktiviert (Power Enable)

Config Button: D9 (Boot-Button)

Hinweise:
- SDA/SCL beim E-Paper sind SPI-Pins (nicht I2C!)
  SDA = MOSI, SCL = SCK
- Display-Controller: GDEY042Z98 mit SSD1683
- DC-Pin verwendet D3 (GPIO5) - D6 ist TX und verursacht Upload-Konflikte!
- Basiert auf Hersteller-Code, angepasst für XIAO Pinout
```

### LilyGO T3 + OLED

```
OLED Display (I2C):
├── SDA → GPIO 21
└── SCL → GPIO 22

Config Button: GPIO 0
```

## Benötigte Arduino Libraries

### Für E-Paper Version (ESP32-C3):
```
- WiFi (ESP32 Core)
- WebServer (ESP32 Core)
- Preferences (ESP32 Core)
- HTTPClient (ESP32 Core)
- ArduinoJson (by Benoit Blanchon) - v6.x
- DNSServer (ESP32 Core)
- GxEPD2 (by Jean-Marc Zingg)
- Adafruit GFX Library
```

### Für OLED Version (LilyGO T3):
```
- WiFi (ESP32 Core)
- WebServer (ESP32 Core)
- Preferences (ESP32 Core)
- HTTPClient (ESP32 Core)
- ArduinoJson (by Benoit Blanchon) - v6.x
- DNSServer (ESP32 Core)
- U8g2 (by oliver)
- Wire (Arduino Core)
```

## Installation

### 1. Arduino IDE vorbereiten

1. **ESP32 Board Support installieren**:
   - Datei → Einstellungen
   - Zusätzliche Boardverwalter-URLs:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Werkzeuge → Board → Boardverwalter → "ESP32" suchen und installieren

2. **Libraries installieren**:
   - Sketch → Bibliothek einbinden → Bibliotheken verwalten
   - Alle oben genannten Libraries suchen und installieren

### 2. Board auswählen

**Für ESP32-C3 (XIAO):**
- Werkzeuge → Board → ESP32 Arduino → "XIAO_ESP32C3"
- USB CDC On Boot: "Enabled"
- Upload Speed: 921600

**Für LilyGO T3:**
- Werkzeuge → Board → ESP32 Arduino → "ESP32 Dev Module"
- Upload Speed: 921600

### 3. Code hochladen

1. Die entsprechende .ino Datei öffnen
2. Board und Port auswählen
3. Upload-Button klicken

## Verwendung

### Erste Inbetriebnahme

1. **ESP32 startet im Config-Modus**:
   - Access Point wird erstellt: `OEV-Anzeiger-Config`
   - Passwort: `config123`
   - Display zeigt die Zugangsdaten an

2. **Mit dem Gerät verbinden**:
   - WLAN-Netzwerk "OEV-Anzeiger-Config" wählen
   - Passwort eingeben: `config123`
   - Browser öffnet automatisch (Captive Portal)
   - Oder manuell zu `192.168.4.1` navigieren

3. **WiFi konfigurieren (Schritt 1)**:
   - Netzwerk scannen oder SSID manuell eingeben
   - WLAN-Passwort eingeben
   - "Weiter zu Schritt 2" klicken

4. **Haltestelle auswählen (Schritt 2)**:
   - Haltestellenname eingeben (z.B. "Zürich HB")
   - "Suchen" klicken für genaue Treffer
   - Haltestelle aus Liste auswählen
   - Gewünschte Verkehrsmittel aktivieren (Bus/Tram/Zug)
   - "Speichern & Starten" klicken

5. **Gerät startet neu** und zeigt die Abfahrten an

### Normale Nutzung

Nach erfolgreicher Konfiguration:
- Zeigt automatisch die nächsten Abfahrten an
- **E-Paper**: 6 Abfahrten, Update alle 5 Minuten
- **OLED**: 4 Abfahrten, Update alle 60 Sekunden
- Verspätungen werden in **Rot** angezeigt (E-Paper)
- WiFi-Signal und Uhrzeit werden angezeigt

### Einstellungen ändern

**Methode 1: Über Home-Netzwerk**
- ESP32 ist mit deinem WiFi verbunden
- Die IP-Adresse wird kurz nach dem Start im Display angezeigt
- Im Browser zur IP-Adresse navigieren (z.B. `http://192.168.1.123`)
- Webinterface ist 2 Minuten nach letzter Aktivität verfügbar

**Methode 2: Neustart im Config-Modus**
- Gerät über den Web-Interface zurücksetzen
- Oder alle Preferences manuell über seriellen Monitor löschen

## Datenquelle

Verwendet die offizielle Schweizer ÖV-API:
- **API**: [transport.opendata.ch](https://transport.opendata.ch)
- **Dokumentation**: [transport.opendata.ch/docs](https://transport.opendata.ch/docs.html)
- **Kostenlos** und ohne API-Key

## Unterschiede E-Paper vs OLED

| Feature | E-Paper (400x300) | OLED (128x64) |
|---------|------------------|---------------|
| **Abfahrten** | 6 gleichzeitig | 4 gleichzeitig |
| **Update-Intervall** | 5 Minuten | 1 Minute |
| **Farben** | 3 (schwarz/weiß/rot) | 1 (monochrom) |
| **Stromverbrauch** | Sehr niedrig | Mittel |
| **Lesbarkeit** | Ausgezeichnet | Gut |
| **Blickwinkel** | 180° | Eingeschränkt |
| **Display-Refresh** | ~15 Sekunden | Instant |

## E-Paper Display Hinweise

### Display Controller
Das WeAct Studio 4.2" E-Paper (GDEY042Z98) verwendet den **SSD1683** Controller. Der Code ist basierend auf dem Hersteller-Beispielcode für ESP32-C3 und angepasst für das XIAO Pinout:

```cpp
// Display-Typ in oev_anzeiger_epaper_xiao.ino:
GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT> display(...);
```

**Wichtige Hinweise:**
- Pin D8 (GPIO 8) muss auf HIGH gesetzt werden für Power Enable
- Reset-Dauer: 50ms (wie vom Hersteller empfohlen)
- SPI-Frequenz: 4MHz

### Refresh-Zeit
- **Full Refresh**: ~15 Sekunden (alle Updates verwenden Full Refresh)
- Das Display "blinkt" bei jedem Update - das ist normal für E-Paper
- **Vorteil**: Display bleibt auch ohne Strom sichtbar

### Lebensdauer
- E-Paper Displays haben eine begrenzte Anzahl von Refresh-Zyklen
- **Update-Intervall von 5 Minuten** ist ein guter Kompromiss
- Bei 5 Min Intervall: ~105.000 Updates/Jahr (Display hält typisch >1 Million)

## Troubleshooting

### WiFi verbindet nicht
- SSID und Passwort überprüfen
- 2.4 GHz Netzwerk verwenden (ESP32 unterstützt kein 5 GHz)
- Router-Einstellungen prüfen (WPA2 bevorzugt)

### Upload funktioniert nicht / Upload-Fehler
- **Ursache**: D6 (GPIO21/TX) ist der serielle TX-Pin und wird für den Upload verwendet
- **Lösung im Code**: DC-Pin verwendet jetzt D3 (GPIO5) statt D6
- **Falls du noch alte Verkabelung hast**:
  - Trenne Display DC-Kabel während des Uploads
  - Oder verändere Verkabelung: Display DC → XIAO D3 (statt D6)

### Display zeigt nichts
- **E-Paper**:
  - Pin-Konfiguration prüfen (siehe Pin-Mapping oben)
  - **WICHTIG**: Pin D8 muss mit 3.3V verbunden sein (Power Enable)!
  - Verkabelung überprüfen:
    * Display SDA → XIAO D4
    * Display SCL → XIAO D2
    * Display CS → XIAO D5
    * Display DC → XIAO D3 (NICHT D6!)
    * Display RES → XIAO D0
    * Display BUSY → XIAO D1
  - Erstes Update dauert bis zu 15 Sekunden
  - USB-Stromversorgung könnte zu schwach sein - probiere externes Netzteil
- **OLED**:
  - I2C-Adresse prüfen (0x3C oder 0x3D)
  - Wire.begin() Pins überprüfen

### Keine Abfahrten sichtbar
- Haltestellenname exakt wie in API schreiben
- Suchfunktion verwenden für korrekte Schreibweise
- Mindestens einen Filter aktivieren (Bus/Tram/Zug)
- Serielle Konsole (115200 baud) für Debug-Ausgaben prüfen

### API gibt keine Daten zurück
- Internetverbindung prüfen
- transport.opendata.ch im Browser testen
- Eventuell gibt es aktuell keine Abfahrten an der Station

## Technische Details

### Speicher
- Einstellungen werden im **NVS (Non-Volatile Storage)** gespeichert
- Überleben Neustarts und Firmware-Updates
- Namespace: `oev-config`

### Zeitzone
- Standard: **Schweiz (UTC+1 / UTC+2 bei Sommerzeit)**
- NTP-Server: `ch.pool.ntp.org`
- Automatische Sommerzeit-Anpassung

### Sicherheit
- **Access Point Passwort**: `config123` (hart-codiert)
- Webserver nur lokal erreichbar
- Keine Verschlüsselung der WiFi-Credentials im Flash (ESP32 Standard)

## Weiterentwicklung

Mögliche Erweiterungen:
- [ ] OTA (Over-The-Air) Updates
- [ ] Deep Sleep Modus für Batteriebetrieb
- [ ] Mehrere Haltestellen
- [ ] Custom Icons für Verkehrsmittel
- [ ] MQTT Integration
- [ ] Touch-Buttons (für E-Paper Version)

## Lizenz

Dieses Projekt verwendet öffentliche APIs und ist für den privaten Gebrauch gedacht.

## Credits

- **ÖV-Daten**: [transport.opendata.ch](https://transport.opendata.ch)
- **E-Paper Library**: [GxEPD2](https://github.com/ZinggJM/GxEPD2)
- **OLED Library**: [U8g2](https://github.com/olikraus/u8g2)

---

**Version**: 1.0
**Datum**: Dezember 2025
**Hardware**: ESP32-C3 + WeAct E-Paper 4.2" oder LilyGO T3 + OLED
