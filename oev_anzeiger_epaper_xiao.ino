/*
 * Schweizer ÖV Abfahrtsanzeiger für ESP32-C3 mit E-Paper Display
 * Hardware: Seeed Studio XIAO ESP32-C3 + WeAct Studio 4.2" E-Paper (400x300, 3-color)
 *
 * Schritt 1: WiFi-Setup → Verbindung herstellen
 * Schritt 2: Haltestelle auswählen (mit funktionierender Suche!)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <GxEPD2_3C.h>  // 3-Farben E-Paper Library
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <time.h>

// NTP Server für Schweiz
const char* ntpServer = "ch.pool.ntp.org";
const long gmtOffset_sec = 3600;  // UTC+1
const int daylightOffset_sec = 3600;  // Sommerzeit +1h

// ===== PIN KONFIGURATION XIAO ESP32-C3 + E-Paper =====
// WeAct Studio E-Paper Pin-Beschriftung → XIAO ESP32-C3
// SDA (MOSI)  → D10  (SPI Data)
// SCL (SCK)   → D8   (SPI Clock)
// CS          → D7   (Chip Select)
// D/C         → D6   (Data/Command)
// RES         → D5   (Reset)
// BUSY        → D4   (Busy Signal)

#define EPD_CS      D7   // E-Paper Pin: CS
#define EPD_DC      D6   // E-Paper Pin: D/C
#define EPD_RST     D5   // E-Paper Pin: RES
#define EPD_BUSY    D4   // E-Paper Pin: BUSY
// SPI Pins (automatisch):
// D10 = MOSI (E-Paper Pin: SDA)
// D8  = SCK  (E-Paper Pin: SCL)

// E-Paper Display - WeAct Studio 4.2" 400x300 3-Color
// Der Controller ist typischerweise UC8176 oder kompatibel
GxEPD2_3C<GxEPD2_420c, GxEPD2_420c::HEIGHT> display(GxEPD2_420c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Config Button (XIAO ESP32-C3 hat Boot-Button auf D9)
#define CONFIG_BUTTON_PIN D9

// Webserver und DNS
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Preferences
Preferences preferences;

// Globale Variablen
String ssid = "";
String password = "";
String stationName = "";
bool filterBus = true;
bool filterTram = true;
bool filterZug = true;

// Modi
bool configMode = false;
bool apMode = false;  // True wenn Access Point läuft
bool normalMode = false;
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 300000;  // 5 Minuten für E-Paper (statt 1 Minute)

// AP Timeout Management
unsigned long apStartTime = 0;
unsigned long lastApActivity = 0;
const unsigned long AP_TIMEOUT = 120000;  // 2 Minuten
bool apTimeoutEnabled = false;

// Display Update Management
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 60000;  // 1 Minute für E-Paper

// Struktur für Abfahrten
struct Departure {
  String line;
  String destination;
  String category;
  String departureTime;
  int delay;
};

std::vector<Departure> currentDepartures;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // E-Paper Display initialisieren
  Serial.println("\n→ Initialisiere E-Paper Display...");
  display.init(115200);  // oder 0 für Standard Serial Debug
  display.setRotation(0);  // 0 = Portrait, 1 = Landscape

  // Boot-Anzeige
  displayBootScreen();
  delay(3000);

  Serial.println("\n\n=================================");
  Serial.println("ÖV Abfahrtsanzeiger gestartet");
  Serial.println("ESP32-C3 + E-Paper 4.2\"");
  Serial.println("=================================\n");

  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);

  loadSettings();

  // Wenn WiFi konfiguriert ist: Normalbetrieb starten + Webserver über Home-Netzwerk
  if (ssid.length() > 0) {
    Serial.println("→ WiFi konfiguriert - starte Normalbetrieb");
    displayStatus("WiFi Connect...", ssid.c_str());

    // Verbinde mit WiFi für Normalbetrieb
    connectToWiFi();

    if (WiFi.status() == WL_CONNECTED) {
      normalMode = true;

      // Zeige IP im Display für 5 Sekunden
      String ipStr = WiFi.localIP().toString();
      displayStatus("Webserver aktiv", ipStr.c_str());
      Serial.println("→ Webserver läuft über Home-Netzwerk");
      Serial.println("→ URL: http://" + ipStr);
      Serial.println("→ Webserver läuft 2 Min. nach letzter Aktivität\n");
      delay(5000);  // 5 Sekunden IP anzeigen

      // Starte nur Webserver (ohne AP und DNS)
      startWebserverOnly();
      apTimeoutEnabled = true;
      apStartTime = millis();
      lastApActivity = millis();
    } else {
      // WiFi fehlgeschlagen - starte Config-Modus
      Serial.println("→ WiFi-Verbindung fehlgeschlagen - starte Config-Modus");
      displayStatus("WiFi Fehler!", "Config-Modus");
      delay(2000);
      startConfigMode();
      apTimeoutEnabled = false;
    }
  } else {
    // Keine WiFi-Daten: Nur Config-Modus ohne Timeout
    Serial.println("→ Keine WiFi-Daten - starte Config-Modus");
    displayStatus("Config-Modus", "OEV-Anzeiger-Config");
    startConfigMode();
    apTimeoutEnabled = false;
  }
}

void loop() {
  // ======= CONFIG-MODUS TEIL =======
  if (configMode) {
    // DNS nur verarbeiten wenn AP läuft
    if (apMode) {
      dnsServer.processNextRequest();
    }
    server.handleClient();

    // Update Display im Config-Modus (seltener für E-Paper)
    if (millis() - lastDisplayUpdate > 30000) {  // Alle 30 Sekunden
      if (apMode) {
        displayConfigMode();
      }
      lastDisplayUpdate = millis();
    }

    // Prüfe Timeout (nur wenn aktiviert)
    if (apTimeoutEnabled) {
      unsigned long timeSinceLastActivity = millis() - lastApActivity;

      // Zeige Status alle 30 Sekunden
      static unsigned long lastStatusLog = 0;
      if (millis() - lastStatusLog > 30000) {
        unsigned long remaining = (AP_TIMEOUT - timeSinceLastActivity) / 1000;
        if (apMode) {
          Serial.printf("AP aktiv - Timeout in %lu Sek. (Clients: %d)\n",
                        remaining, WiFi.softAPgetStationNum());
        } else {
          Serial.printf("Webserver aktiv - Timeout in %lu Sek.\n", remaining);
        }
        lastStatusLog = millis();
      }

      // Prüfe ob Timeout erreicht wurde
      if (timeSinceLastActivity > AP_TIMEOUT) {
        if (apMode) {
          Serial.println("\n=== AP TIMEOUT ERREICHT ===");
          Serial.println("Keine Aktivität - schließe Access Point\n");
        } else {
          Serial.println("\n=== WEBSERVER TIMEOUT ERREICHT ===");
          Serial.println("Keine Aktivität - schließe Webserver\n");
        }
        stopConfigMode();
      }
    }
  }

  // ======= NORMALBETRIEB TEIL =======
  if (normalMode) {
    // Prüfe WiFi-Verbindung
    if (WiFi.status() == WL_CONNECTED) {
      // Hole regelmäßig Abfahrten
      if (millis() - lastUpdate > UPDATE_INTERVAL || lastUpdate == 0) {
        fetchAndDisplayDepartures();
        lastUpdate = millis();
      }
    } else {
      // WiFi-Verbindung verloren
      displayStatus("WiFi verloren!", "Reconnect...");

      static unsigned long lastReconnect = 0;
      if (millis() - lastReconnect > 30000) {
        Serial.println("WiFi verloren, versuche Reconnect...");
        connectToWiFi();
        lastReconnect = millis();
      }
    }
  }

  delay(10);
}

// ============= DISPLAY FUNKTIONEN FÜR E-PAPER =============

void displayBootScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Titel in Schwarz
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold18pt7b);
    display.setCursor(50, 120);
    display.print("OeV Anzeige");

    // Untertitel
    display.setFont(&FreeSans9pt7b);
    display.setCursor(80, 160);
    display.print("XIAO ESP32-C3");

    // E-Paper Info in Rot
    display.setTextColor(GxEPD_RED);
    display.setCursor(60, 190);
    display.print("4.2\" E-Paper");

  } while (display.nextPage());
}

void displayStatus(const char* line1, const char* line2) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold12pt7b);

    display.setCursor(20, 120);
    display.print(line1);

    display.setFont(&FreeSans9pt7b);
    display.setCursor(20, 160);
    display.print(line2);

  } while (display.nextPage());
}

void displayConfigMode() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Titel in Rot
    display.setTextColor(GxEPD_RED);
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(80, 50);
    display.print("CONFIG-MODUS");

    // Details in Schwarz
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSans9pt7b);

    display.setCursor(20, 100);
    display.print("SSID:");
    display.setCursor(20, 125);
    display.print("OEV-Anzeiger-Config");

    display.setCursor(20, 160);
    display.print("Passwort: config123");

    display.setCursor(20, 195);
    display.print("IP: 192.168.4.1");

    display.setCursor(20, 250);
    display.print("Mit dem Netzwerk verbinden");
    display.setCursor(20, 275);
    display.print("und 192.168.4.1 im Browser");
    display.setCursor(20, 290);
    display.print("oeffnen.");

  } while (display.nextPage());
}

void displayDepartures() {
  if (currentDepartures.size() == 0) {
    displayStatus("Keine Abfahrten", stationName.c_str());
    return;
  }

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // === HEADER ===
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold12pt7b);

    // Aktuelle Uhrzeit holen
    struct tm timeinfo;
    String timeStr = "--:--";
    if (getLocalTime(&timeinfo)) {
      char timeBuffer[6];
      sprintf(timeBuffer, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      timeStr = String(timeBuffer);
    }

    // Uhrzeit oben links
    display.setCursor(10, 25);
    display.print(timeStr);

    // Station rechts davon
    display.setFont(&FreeSans9pt7b);
    display.setCursor(120, 25);
    display.print(stationName);

    // Trennlinie
    display.drawLine(0, 35, 400, 35, GxEPD_BLACK);

    // === TABELLEN-HEADER ===
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(10, 60);
    display.print("Linie");
    display.setCursor(90, 60);
    display.print("Ziel");
    display.setCursor(300, 60);
    display.print("Abfahrt");

    display.drawLine(0, 68, 400, 68, GxEPD_BLACK);

    // === ABFAHRTEN ===
    display.setFont(&FreeMonoBold9pt7b);
    int y = 95;
    int lineHeight = 45;  // Mehr Platz zwischen Zeilen

    for (size_t i = 0; i < min((size_t)6, currentDepartures.size()); i++) {  // Bis zu 6 Abfahrten
      Departure& dep = currentDepartures[i];

      // Linie (max 6 Zeichen)
      String line = dep.line;
      if (line.length() > 6) line = line.substring(0, 6);
      display.setCursor(10, y);
      display.print(line);

      // Ziel (gekürzt auf 18 Zeichen)
      String dest = dep.destination;
      if (dest.length() > 18) {
        dest = dest.substring(0, 18);
        dest += "..";
      }
      display.setCursor(90, y);
      display.print(dest);

      // Abfahrtszeit
      display.setCursor(300, y);
      display.print(dep.departureTime);

      // Verspätung in Rot wenn > 0
      if (dep.delay > 0) {
        display.setTextColor(GxEPD_RED);
        display.setCursor(360, y);
        display.print("+" + String(dep.delay));
        display.setTextColor(GxEPD_BLACK);
      } else if (dep.delay < 0) {
        display.setTextColor(GxEPD_RED);
        display.setCursor(360, y);
        display.print(String(dep.delay));
        display.setTextColor(GxEPD_BLACK);
      }

      y += lineHeight;
    }

    // === FOOTER ===
    display.drawLine(0, 280, 400, 280, GxEPD_BLACK);

    display.setFont(&FreeSans9pt7b);

    // Nächstes Update
    int minToUpdate = (UPDATE_INTERVAL - (millis() - lastUpdate)) / 60000;
    if (minToUpdate > 0) {
      display.setCursor(10, 298);
      display.print("Update in " + String(minToUpdate) + " Min.");
    }

    // WiFi Signal rechts
    int rssi = WiFi.RSSI();
    display.setCursor(280, 298);
    display.print("WiFi: ");
    if (rssi > -60) display.print("****");
    else if (rssi > -70) display.print("*** ");
    else if (rssi > -80) display.print("**  ");
    else if (rssi > -90) display.print("*   ");
    else display.print("-   ");

  } while (display.nextPage());
}

// ============= URSPRÜNGLICHE FUNKTIONEN (unverändert) =============

void loadSettings() {
  preferences.begin("oev-config", false);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  stationName = preferences.getString("station", "");
  stationName.trim();
  filterBus = preferences.getBool("filterBus", true);
  filterTram = preferences.getBool("filterTram", true);
  filterZug = preferences.getBool("filterZug", true);
  preferences.end();

  Serial.println("Gespeicherte Einstellungen:");
  Serial.println("SSID: " + String(ssid.length() > 0 ? ssid : "(leer)"));
  Serial.println("Station: " + String(stationName.length() > 0 ? stationName : "(leer)"));
}

void saveSettings() {
  stationName.trim();

  preferences.begin("oev-config", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.putString("station", stationName);
  preferences.putBool("filterBus", filterBus);
  preferences.putBool("filterTram", filterTram);
  preferences.putBool("filterZug", filterZug);
  preferences.end();
  Serial.println("✓ Einstellungen gespeichert!");
}

void startWebserverOnly() {
  configMode = true;
  apMode = false;  // Kein Access Point

  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║   WEBSERVER GESTARTET         ║");
  Serial.println("╚════════════════════════════════╝\n");

  // Webserver Routen
  Serial.println("→ Konfiguriere Webserver...");
  server.on("/", handleRoot);
  server.on("/savewifi", handleSaveWiFi);
  server.on("/step2", handleStep2);
  server.on("/save", handleSaveFinal);
  server.on("/search", handleSearch);
  server.on("/scanwifi", handleWiFiScan);
  server.on("/reset", handleReset);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("✓ Webserver gestartet auf Port 80!\n");
  Serial.println("════════════════════════════════");
  Serial.println("   Zugriff über Home-Netzwerk:");
  Serial.println("   http://" + WiFi.localIP().toString());
  Serial.println("════════════════════════════════\n");
}

void startConfigMode() {
  configMode = true;
  apMode = true;  // Access Point wird gestartet

  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║   CONFIG-MODUS GESTARTET      ║");
  Serial.println("╚════════════════════════════════╝\n");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500);

  Serial.println("→ Starte AP+STA Modus...");
  WiFi.mode(WIFI_AP_STA);
  delay(500);

  Serial.println("→ Starte Access Point...");

  WiFi.softAPConfig(
    IPAddress(192, 168, 4, 1),
    IPAddress(192, 168, 4, 1),
    IPAddress(255, 255, 255, 0)
  );

  bool apStarted = WiFi.softAP("OEV-Anzeiger-Config", "config123");

  if (!apStarted) {
    Serial.println("✗ AP Start fehlgeschlagen, versuche erneut...");
    delay(1000);
    WiFi.softAP("OEV-Anzeiger-Config", "config123");
  }

  delay(500);

  IPAddress IP = WiFi.softAPIP();
  Serial.println("\n✓ Access Point aktiv!");
  Serial.println("SSID: OEV-Anzeiger-Config");
  Serial.println("Passwort: config123");
  Serial.println("AP IP: " + IP.toString());
  Serial.println("URL: http://192.168.4.1\n");

  Serial.println("→ Starte DNS Server...");

  bool dnsStarted = dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  if (dnsStarted) {
    Serial.println("✓ DNS Server aktiv auf Port 53");
    Serial.println("   Alle DNS-Anfragen → " + WiFi.softAPIP().toString());
  } else {
    Serial.println("✗ DNS Server Start fehlgeschlagen!");
  }

  Serial.println();

  Serial.println("→ Konfiguriere Webserver...");
  server.on("/", handleRoot);
  server.on("/savewifi", handleSaveWiFi);
  server.on("/step2", handleStep2);
  server.on("/save", handleSaveFinal);
  server.on("/search", handleSearch);
  server.on("/scanwifi", handleWiFiScan);
  server.on("/reset", handleReset);

  // Captive Portal Detection Endpoints
  server.on("/generate_204", handleCaptivePortal);
  server.on("/gen_204", handleCaptivePortal);
  server.on("/fwlink", handleCaptivePortal);
  server.on("/redirect", handleCaptivePortal);
  server.on("/hotspot-detect.html", handleCaptivePortal);
  server.on("/library/test/success.html", handleCaptivePortal);
  server.on("/connecttest.txt", handleCaptivePortal);
  server.on("/ncsi.txt", handleCaptivePortal);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("✓ Webserver gestartet!\n");

  Serial.println("════════════════════════════════");
  Serial.println("   VERBINDE MIT DEM NETZWERK:");
  Serial.println("   SSID: OEV-Anzeiger-Config");
  Serial.println("   Passwort: config123");
  Serial.println("════════════════════════════════\n");
}

void stopConfigMode() {
  configMode = false;
  apTimeoutEnabled = false;

  if (apMode) {
    Serial.println("→ Stoppe Access Point und Webserver");
    displayStatus("AP gestoppt", "Normal-Betrieb");
  } else {
    Serial.println("→ Stoppe Webserver");
    displayStatus("Webserver off", "Normal-Betrieb");
  }

  server.stop();

  if (apMode) {
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
  }

  apMode = false;

  delay(500);

  if (!normalMode) {
    WiFi.mode(WIFI_STA);
  }

  Serial.println("✓ Config-Modus beendet");
  Serial.println("→ Nur noch Station-Modus aktiv\n");
}

void handleRoot() {
  lastApActivity = millis();

  // Wenn im Normalbetrieb (WiFi bereits verbunden), direkt zu Schritt 2
  if (normalMode && WiFi.status() == WL_CONNECTED) {
    Serial.println("→ Normalbetrieb aktiv - Weiterleitung zu Schritt 2");
    server.sendHeader("Location", "/step2", true);
    server.send(302, "text/html", "");
    return;
  }

  // Ansonsten: Schritt 1 (WiFi-Setup) anzeigen
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>ÖV-Anzeiger Setup</title>";
  html += "<style>";
  html += "body{font-family:Arial;max-width:400px;margin:50px auto;padding:20px;background:#f0f0f0}";
  html += "h1{color:#333;text-align:center;margin-bottom:30px}";
  html += ".card{background:white;padding:20px;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:20px}";
  html += "label{display:block;margin:15px 0 5px;color:#555;font-weight:bold}";
  html += "input,select{width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;font-size:16px}";
  html += "button{width:100%;padding:12px;background:#4CAF50;color:white;border:none;border-radius:5px;font-size:16px;cursor:pointer;margin-top:15px}";
  html += "button:hover{background:#45a049}";
  html += ".scan-btn{background:#2196F3;margin-top:5px;padding:8px}";
  html += ".scan-btn:hover{background:#0b7dda}";
  html += ".status{text-align:center;padding:10px;background:#e3f2fd;border-radius:5px;margin-top:10px;display:none}";
  html += "#networks{margin-top:10px}";
  html += ".network{padding:10px;background:#f5f5f5;margin:5px 0;border-radius:5px;cursor:pointer}";
  html += ".network:hover{background:#e0e0e0}";
  html += "</style></head><body>";

  html += "<h1>🚂 ÖV-Anzeiger Setup</h1>";
  html += "<div class='card'>";
  html += "<h2>Schritt 1: WiFi</h2>";
  html += "<form action='/savewifi' method='POST'>";

  html += "<label>SSID:</label>";
  html += "<input type='text' name='ssid' id='ssid' value='" + ssid + "' required>";
  html += "<button type='button' class='scan-btn' onclick='scanNetworks()'>📡 Netzwerke scannen</button>";
  html += "<div id='networks'></div>";

  html += "<label>Passwort:</label>";
  html += "<input type='password' name='password' value='" + password + "'>";

  html += "<button type='submit'>Weiter zu Schritt 2 →</button>";
  html += "</form>";
  html += "<div class='status' id='status'></div>";
  html += "</div>";

  html += "<script>";
  html += "function scanNetworks(){";
  html += "document.getElementById('status').style.display='block';";
  html += "document.getElementById('status').innerHTML='Scanne Netzwerke...';";
  html += "fetch('/scanwifi').then(r=>r.json()).then(data=>{";
  html += "let html='';";
  html += "data.networks.forEach(net=>{";
  html += "html+='<div class=\"network\" onclick=\"selectNetwork(\\''+net.ssid+'\\')\">';";
  html += "html+=net.ssid+' ('+net.rssi+' dBm)';";
  html += "html+='</div>';";
  html += "});";
  html += "document.getElementById('networks').innerHTML=html;";
  html += "document.getElementById('status').style.display='none';";
  html += "}).catch(e=>{";
  html += "document.getElementById('status').innerHTML='Scan fehlgeschlagen';";
  html += "});";
  html += "}";
  html += "function selectNetwork(ssid){";
  html += "document.getElementById('ssid').value=ssid;";
  html += "}";
  html += "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSaveWiFi() {
  lastApActivity = millis();

  if (server.hasArg("ssid")) {
    ssid = server.arg("ssid");
    password = server.hasArg("password") ? server.arg("password") : "";

    ssid.trim();
    password.trim();

    Serial.println("\n=== WiFi-Daten empfangen ===");
    Serial.println("SSID: " + ssid);
    Serial.println("Passwort: " + String(password.length() > 0 ? "***" : "(leer)"));

    displayStatus("WiFi Test...", ssid.c_str());

    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    Serial.print("Teste Verbindung");
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✓ WiFi-Verbindung erfolgreich!");
      Serial.println("IP: " + WiFi.localIP().toString());

      displayStatus("WiFi OK!", "Weiter zu Schritt 2");

      String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
      html += "<meta http-equiv='refresh' content='2;url=/step2'>";
      html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
      html += "<title>WiFi OK</title>";
      html += "<style>body{font-family:Arial;text-align:center;padding:50px;background:#f0f0f0}";
      html += ".success{background:#4CAF50;color:white;padding:30px;border-radius:10px;display:inline-block}";
      html += "</style></head><body>";
      html += "<div class='success'>";
      html += "<h1>✓ WiFi verbunden!</h1>";
      html += "<p>Weiterleitung zu Schritt 2...</p>";
      html += "</div></body></html>";

      server.send(200, "text/html", html);
    } else {
      Serial.println("✗ WiFi-Verbindung fehlgeschlagen!");

      displayStatus("WiFi Fehler!", "Pruefe Daten");

      String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
      html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
      html += "<title>WiFi Fehler</title>";
      html += "<style>body{font-family:Arial;text-align:center;padding:50px;background:#f0f0f0}";
      html += ".error{background:#f44336;color:white;padding:30px;border-radius:10px;display:inline-block}";
      html += "button{padding:15px 30px;font-size:16px;background:white;border:none;border-radius:5px;cursor:pointer;margin-top:20px}";
      html += "</style></head><body>";
      html += "<div class='error'>";
      html += "<h1>✗ Verbindung fehlgeschlagen</h1>";
      html += "<p>SSID oder Passwort falsch?</p>";
      html += "<button onclick='history.back()'>← Zurück</button>";
      html += "</div></body></html>";

      server.send(200, "text/html", html);
    }
  } else {
    server.send(400, "text/plain", "SSID fehlt!");
  }
}

void handleWiFiScan() {
  lastApActivity = millis();

  Serial.println("\n→ Starte WiFi Scan...");
  displayStatus("WiFi Scan...", "Bitte warten");

  int n = WiFi.scanNetworks();

  String json = "{\"networks\":[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i));
    json += "}";
  }
  json += "]}";

  Serial.println("✓ " + String(n) + " Netzwerke gefunden");

  server.send(200, "application/json", json);
}

void handleStep2() {
  lastApActivity = millis();

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Haltestellenauswahl</title>";
  html += "<style>";
  html += "body{font-family:Arial;max-width:400px;margin:50px auto;padding:20px;background:#f0f0f0}";
  html += "h1{color:#333;text-align:center;margin-bottom:30px}";
  html += ".card{background:white;padding:20px;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:20px}";
  html += "label{display:block;margin:15px 0 5px;color:#555;font-weight:bold}";
  html += "input{width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;font-size:16px}";
  html += "button{width:100%;padding:12px;background:#4CAF50;color:white;border:none;border-radius:5px;font-size:16px;cursor:pointer;margin-top:15px}";
  html += "button:hover{background:#45a049}";
  html += ".search-btn{background:#2196F3;margin-top:5px}";
  html += ".search-btn:hover{background:#0b7dda}";
  html += ".reset-btn{background:#f44336;margin-top:20px}";
  html += ".reset-btn:hover{background:#da190b}";
  html += ".station{padding:10px;background:#f5f5f5;margin:5px 0;border-radius:5px;cursor:pointer}";
  html += ".station:hover{background:#e0e0e0}";
  html += ".checkbox-group{margin:15px 0}";
  html += ".checkbox-group label{display:inline-block;margin-right:15px;font-weight:normal}";
  html += "#results{max-height:200px;overflow-y:auto;margin-top:10px}";
  html += ".status{text-align:center;padding:10px;background:#e3f2fd;border-radius:5px;margin-top:10px;display:none}";
  html += "</style></head><body>";

  html += "<h1>🚉 Haltestelle wählen</h1>";
  html += "<div class='card'>";
  html += "<form action='/save' method='POST' id='configForm'>";

  html += "<label>Haltestelle:</label>";
  html += "<input type='text' name='station' id='station' value='" + stationName + "' required>";
  html += "<input type='hidden' name='stationExact' id='stationExact' value=''>";
  html += "<button type='button' class='search-btn' onclick='searchStations()'>🔍 Suchen</button>";
  html += "<div id='results'></div>";

  html += "<div class='checkbox-group'>";
  html += "<label>Anzeigen:</label><br>";
  html += "<label><input type='checkbox' name='filterBus' " + String(filterBus ? "checked" : "") + "> 🚌 Bus</label>";
  html += "<label><input type='checkbox' name='filterTram' " + String(filterTram ? "checked" : "") + "> 🚊 Tram</label>";
  html += "<label><input type='checkbox' name='filterZug' " + String(filterZug ? "checked" : "") + "> 🚂 Zug</label>";
  html += "</div>";

  html += "<button type='submit'>✓ Speichern & Starten</button>";
  html += "</form>";

  html += "<button type='button' class='reset-btn' onclick='resetDevice()'>🔄 Gerät zurücksetzen</button>";

  html += "<div class='status' id='status'></div>";
  html += "</div>";

  html += "<script>";
  html += "function searchStations(){";
  html += "let query=document.getElementById('station').value;";
  html += "if(query.length<2){alert('Mind. 2 Zeichen eingeben');return;}";
  html += "document.getElementById('status').style.display='block';";
  html += "document.getElementById('status').innerHTML='Suche...';";
  html += "fetch('/search?q='+encodeURIComponent(query))";
  html += ".then(r=>r.json())";
  html += ".then(data=>{";
  html += "let html='';";
  html += "data.stations.forEach(st=>{";
  html += "html+='<div class=\"station\" onclick=\"selectStation(\\''+st.name+'\\')\">';";
  html += "html+=st.name;";
  html += "html+='</div>';";
  html += "});";
  html += "document.getElementById('results').innerHTML=html;";
  html += "document.getElementById('status').style.display='none';";
  html += "}).catch(e=>{";
  html += "document.getElementById('status').innerHTML='Fehler: '+e;";
  html += "});";
  html += "}";
  html += "function selectStation(name){";
  html += "document.getElementById('station').value=name;";
  html += "document.getElementById('stationExact').value=name;";
  html += "document.getElementById('results').innerHTML='';";
  html += "}";
  html += "function resetDevice(){";
  html += "if(confirm('Alle Einstellungen löschen und Gerät zurücksetzen?')){";
  html += "document.getElementById('status').style.display='block';";
  html += "document.getElementById('status').innerHTML='Setze zurück...';";
  html += "fetch('/reset').then(()=>{";
  html += "document.getElementById('status').innerHTML='Neustart...';";
  html += "}).catch(()=>{";
  html += "document.getElementById('status').innerHTML='Neustart...';";
  html += "});";
  html += "}";
  html += "}";
  html += "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSearch() {
  lastApActivity = millis();

  if (!server.hasArg("q")) {
    server.send(400, "text/plain", "Query fehlt");
    return;
  }

  String query = server.arg("q");
  query.trim();

  Serial.println("\n→ Suche Haltestellen: " + query);
  displayStatus("Suche...", query.c_str());

  HTTPClient http;
  String url = "http://transport.opendata.ch/v1/locations?query=" + urlEncode(query) + "&type=station";

  http.begin(url);
  http.setTimeout(10000);

  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      JsonArray stations = doc["stations"].as<JsonArray>();

      String json = "{\"stations\":[";
      int count = 0;
      for (JsonObject station : stations) {
        if (count > 0) json += ",";
        json += "{\"name\":\"" + station["name"].as<String>() + "\"}";
        count++;
        if (count >= 10) break;
      }
      json += "]}";

      Serial.println("✓ " + String(count) + " Stationen gefunden");
      server.send(200, "application/json", json);
    } else {
      Serial.println("✗ JSON Parse Error");
      server.send(500, "text/plain", "JSON Error");
    }
  } else {
    Serial.println("✗ HTTP Error: " + String(httpCode));
    server.send(500, "text/plain", "API Error");
  }

  http.end();
}

void handleSaveFinal() {
  lastApActivity = millis();

  if (server.hasArg("station")) {
    // Verwende stationExact falls vorhanden, sonst station
    if (server.hasArg("stationExact") && server.arg("stationExact").length() > 0) {
      stationName = server.arg("stationExact");
    } else {
      stationName = server.arg("station");
    }

    stationName.trim();

    filterBus = server.hasArg("filterBus");
    filterTram = server.hasArg("filterTram");
    filterZug = server.hasArg("filterZug");

    Serial.println("\n=== Finale Konfiguration ===");
    Serial.println("SSID: " + ssid);
    Serial.println("Station: " + stationName);
    Serial.println("Filter - Bus: " + String(filterBus) + ", Tram: " + String(filterTram) + ", Zug: " + String(filterZug));

    saveSettings();

    displayStatus("Gespeichert!", "Starte...");

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='3;url=/'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Fertig!</title>";
    html += "<style>body{font-family:Arial;text-align:center;padding:50px;background:#f0f0f0}";
    html += ".success{background:#4CAF50;color:white;padding:40px;border-radius:10px;display:inline-block}";
    html += "h1{margin:0 0 20px 0}";
    html += "</style></head><body>";
    html += "<div class='success'>";
    html += "<h1>✓ Konfiguration gespeichert!</h1>";
    html += "<p>Gerät startet in 3 Sekunden neu...</p>";
    html += "</div></body></html>";

    server.send(200, "text/html", html);

    delay(3000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Station fehlt!");
  }
}

void handleReset() {
  lastApActivity = millis();

  Serial.println("\n╔════════════════════════════════╗");
  Serial.println("║   GERÄT WIRD ZURÜCKGESETZT    ║");
  Serial.println("╚════════════════════════════════╝\n");

  displayStatus("Reset...", "Loesche Daten");

  // Alle gespeicherten Daten löschen
  preferences.begin("oev-config", false);
  preferences.clear();
  preferences.end();

  Serial.println("✓ Alle Daten gelöscht!");
  Serial.println("→ Neustart in 2 Sekunden...\n");

  // Bestätigungsseite senden
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Zurückgesetzt</title>";
  html += "<style>body{font-family:Arial;text-align:center;padding:50px;background:#f0f0f0}";
  html += ".success{background:#4CAF50;color:white;padding:40px;border-radius:10px;display:inline-block}";
  html += "h1{margin:0 0 20px 0}";
  html += "</style></head><body>";
  html += "<div class='success'>";
  html += "<h1>✓ Gerät zurückgesetzt!</h1>";
  html += "<p>Alle Daten gelöscht.</p>";
  html += "<p>Neustart erfolgt...</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);

  delay(2000);

  displayStatus("Neustart...", "Bitte warten");

  delay(1000);

  // Gerät neu starten
  ESP.restart();
}

void handleCaptivePortal() {
  lastApActivity = millis();

  String uri = server.uri();
  Serial.println("→ Captive Portal Request: " + uri);

  if (uri == "/generate_204" || uri == "/gen_204") {
    Serial.println("→ Android Captive Portal erkannt!");
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.send(302, "text/html", "");
    return;
  }

  if (uri == "/hotspot-detect.html" || uri == "/library/test/success.html") {
    Serial.println("→ iOS Captive Portal erkannt!");
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.send(302, "text/html", "");
    return;
  }

  if (uri == "/fwlink" || uri == "/redirect") {
    Serial.println("→ Microsoft Captive Portal erkannt!");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    return;
  }

  if (uri == "/connecttest.txt" || uri == "/ncsi.txt") {
    Serial.println("→ Windows Captive Portal erkannt!");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.send(200, "text/plain", "Microsoft Connect Test");
    return;
  }

  Serial.println("→ Redirect zur Config-Seite");
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(302, "text/html", "");
}

void handleNotFound() {
  lastApActivity = millis();

  String uri = server.uri();
  Serial.println("→ 404 NotFound: " + uri);

  handleCaptivePortal();
}

void connectToWiFi() {
  if (ssid.length() == 0) {
    Serial.println("Keine SSID konfiguriert!");
    displayStatus("Keine SSID!", "Config Mode");
    return;
  }

  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
  }

  Serial.println("\n=== WiFi-Verbindung ===");
  Serial.println("SSID: " + ssid);

  displayStatus("WiFi Connect...", ssid.c_str());

  WiFi.begin(ssid.c_str(), password.c_str());

  int attempts = 0;
  Serial.print("Verbinde");
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi verbunden!");
    Serial.println("IP: " + WiFi.localIP().toString());
    Serial.println("Signal: " + String(WiFi.RSSI()) + " dBm");

    // NTP Zeit synchronisieren
    Serial.println("→ Synchronisiere Zeit mit NTP...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // Warte bis Zeit synchronisiert ist (max 5 Sekunden)
    int timeoutCounter = 0;
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo) && timeoutCounter < 10) {
      delay(500);
      Serial.print(".");
      timeoutCounter++;
    }

    if (timeoutCounter < 10) {
      Serial.println("\n✓ Zeit synchronisiert: " + String(timeinfo.tm_hour) + ":" +
                     String(timeinfo.tm_min < 10 ? "0" : "") + String(timeinfo.tm_min));
    } else {
      Serial.println("\n✗ Zeit-Synchronisation fehlgeschlagen");
    }
    Serial.println();

    displayStatus("WiFi OK!", WiFi.localIP().toString().c_str());
    delay(2000);
  } else {
    Serial.println("\n✗ WiFi-Verbindung fehlgeschlagen!");
    displayStatus("WiFi Fehler!", "Check Config");
  }
}

void fetchAndDisplayDepartures() {
  if (stationName.length() == 0) {
    Serial.println("Keine Haltestelle!");
    displayStatus("Keine Station!", "Config needed");
    return;
  }

  stationName.trim();

  Serial.println("\n=== Abfahrten: " + stationName + " ===");
  displayStatus("Lade Daten...", stationName.c_str());

  HTTPClient http;
  String url = "http://transport.opendata.ch/v1/stationboard?station=" + urlEncode(stationName) + "&limit=10";

  Serial.println("URL: " + url);

  http.begin(url);
  http.setTimeout(10000);

  int httpCode = http.GET();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode == 200) {
    String payload = http.getString();

    Serial.print("Empfangene Daten: ");
    Serial.print(payload.length());
    Serial.println(" Bytes");

    if (payload.length() == 0) {
      Serial.println("\n✗ Keine Daten empfangen!");
      displayStatus("Keine Daten!", "API Error");
      http.end();
      return;
    }

    // Buffer für JSON (32KB)
    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON Error: ");
      Serial.println(error.c_str());
      displayStatus("JSON Fehler!", "Parse Error");
      http.end();
      return;
    }

    Serial.println("✓ JSON erfolgreich geparst");

    if (!doc["station"].isNull()) {
      String foundStation = doc["station"]["name"].as<String>();
      Serial.println("Station: " + foundStation);
    }

    if (!doc.containsKey("stationboard") || doc["stationboard"].isNull()) {
      Serial.println("✗ Keine Stationboard-Daten!");
      displayStatus("Keine Daten!", stationName.c_str());
      http.end();
      return;
    }

    currentDepartures.clear();
    JsonArray stationboard = doc["stationboard"].as<JsonArray>();

    Serial.print("Anzahl Verbindungen: ");
    Serial.println(stationboard.size());

    for (JsonObject connection : stationboard) {
      String category = connection["category"].as<String>();

      bool showThis = false;
      if (filterBus && (category == "B" || category == "BUS")) showThis = true;
      if (filterTram && (category == "T" || category == "TRAM")) showThis = true;
      if (filterZug && (category == "S" || category == "IC" || category == "IR" ||
                        category == "RE" || category == "R" || category == "EC" ||
                        category == "ICE" || category == "RB")) showThis = true;

      if (showThis && currentDepartures.size() < 6) {  // 6 Abfahrten für größeres Display
        Departure dep;
        dep.line = connection["number"].as<String>();
        if (dep.line == "null" || dep.line.length() == 0) {
          dep.line = category;
        }
        dep.destination = connection["to"].as<String>();
        dep.category = category;

        String departure = connection["stop"]["departure"].as<String>();
        if (departure.length() >= 16) {
          dep.departureTime = departure.substring(11, 16);
        } else {
          dep.departureTime = "??:??";
        }

        if (connection["stop"]["delay"].isNull()) {
          dep.delay = 0;
        } else {
          dep.delay = connection["stop"]["delay"].as<int>();
        }

        currentDepartures.push_back(dep);
      }

      if (currentDepartures.size() >= 6) break;
    }

    Serial.println();
    if (currentDepartures.size() == 0) {
      Serial.println("Keine Abfahrten (Filter zu restriktiv?)");
      displayStatus("Keine Abfahrten", "Check Filter");
    } else {
      for (size_t i = 0; i < currentDepartures.size(); i++) {
        String line = currentDepartures[i].line;
        while (line.length() < 5) line += " ";

        String dest = currentDepartures[i].destination;
        if (dest.length() > 30) dest = dest.substring(0, 30);
        while (dest.length() < 30) dest += " ";

        String delayStr = "";
        if (currentDepartures[i].delay >= 0) delayStr = "+";
        delayStr += String(currentDepartures[i].delay);
        while (delayStr.length() < 3) delayStr = " " + delayStr;

        Serial.print(line);
        Serial.print("  ");
        Serial.print(dest);
        Serial.print("  ");
        Serial.print(currentDepartures[i].departureTime);
        Serial.print(" ");
        Serial.println(delayStr);
      }

      // Display aktualisieren
      displayDepartures();
    }

    Serial.println("\n=== Update in 5 Min ===\n");

  } else if (httpCode > 0) {
    Serial.print("✗ HTTP Error: ");
    Serial.println(httpCode);
    displayStatus("HTTP Error", String(httpCode).c_str());
  } else {
    Serial.print("✗ HTTP Request fehlgeschlagen: ");
    Serial.println(http.errorToString(httpCode));
    displayStatus("Request Error", "Check WiFi");
  }

  http.end();
}

String urlEncode(String str) {
  String encoded = "";
  char c;
  for (size_t i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      encoded += '%';
      char hex[3];
      sprintf(hex, "%02X", c);
      encoded += hex;
    }
  }
  return encoded;
}
