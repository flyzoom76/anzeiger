/*
 * Schweizer ÖV Abfahrtsanzeiger für ESP32-S3 mit E-Paper Display
 * Hardware: Seeed Studio XIAO ESP32-S3 + WeAct Studio 4.2" E-Paper (400x300, 3-color)
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
#include <SPI.h>
#include <vector>
// #include <GxEPD2_BW.h>  // 2-Farben E-Paper Library (für schwarz/weiß)
#include <GxEPD2_3C.h>  // 3-Farben E-Paper Library (für schwarz/weiß/rot)
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <time.h>

// NTP Server für Schweiz
const char* ntpServer = "ch.pool.ntp.org";
const long gmtOffset_sec = 3600;  // UTC+1
const int daylightOffset_sec = 3600;  // Sommerzeit +1h

// ===== PIN KONFIGURATION XIAO ESP32-C6 + E-Paper =====
// Pin-Konfiguration für SEEED XIAO ESP32-C6 mit WeAct Studio 4.2" E-Paper
// Basierend auf Hersteller-Code, angepasst für XIAO Pinout
//
// Hersteller-Pins:  CS=GPIO7, DC=GPIO1, RST=GPIO2, BUSY=GPIO3, SCL=GPIO4, SDA=GPIO6, POWER=GPIO8
// XIAO C6 Pinout:   D0=GPIO2, D1=GPIO3, D2=GPIO4, D3=GPIO5, D4=GPIO6, D5=GPIO7, D6=GPIO21/TX, D8=GPIO8
//
// ESP32-S3 Pin-Definitionen für XIAO ESP32-S3
// WICHTIG: Kabel stecken an D-Pins (gleich wie beim C6), aber GPIO-Mapping ist anders!
// ESP32-C6 D-Aliases: D0=GPIO2, D1=GPIO3, D2=GPIO4, D3=GPIO5, D4=GPIO6, D5=GPIO7, D8=GPIO8, D9=GPIO9
// ESP32-S3 D-Aliases: D0=GPIO1, D1=GPIO2, D2=GPIO3, D3=GPIO4, D4=GPIO5, D5=GPIO6, D8=GPIO7, D9=GPIO8

#define EPD_CS      6   // D5 = GPIO 6 beim S3 (war GPIO7 beim C6)
#define EPD_DC      4   // D3 = GPIO 4 beim S3 (war GPIO5 beim C6)
#define EPD_RST     1   // D0 = GPIO 1 beim S3 (war GPIO2 beim C6)
#define EPD_BUSY    2   // D1 = GPIO 2 beim S3 (war GPIO3 beim C6)
#define EPD_POWER   7   // D8 = GPIO 7 beim S3 (war GPIO8 beim C6)
// SPI Pins (an D-Pins angeschlossen):
// SCK  = D2 = GPIO 3 beim S3 (war GPIO4 beim C6)
// MOSI = D4 = GPIO 5 beim S3 (war GPIO6 beim C6)

// Display: GDEY042Z98 mit SSD1683 Controller (400x300, 3-Farben: schwarz/weiß/rot)
GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT> display(GxEPD2_420c_GDEY042Z98(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Config Button (D9 = GPIO8 beim S3)
#define CONFIG_BUTTON_PIN 8

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
String stationName2 = "";  // 2. Haltestelle (optional)
String allowedDestinations = "";  // Pipe-separierte Liste erlaubter Ziele für Station 1
String allowedDestinations2 = "";  // Pipe-separierte Liste erlaubter Ziele für Station 2
int walkingTimeMinutes = 0;  // Fußweg zur Haltestelle in Minuten
int displayLines = 4;  // Anzahl der anzuzeigenden Abfahrten (1-8, Standard: 4)
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
  String stationName;  // Zu welcher Haltestelle gehört diese Abfahrt?
};

std::vector<Departure> currentDepartures;

// Struktur für Wetter
struct Weather {
  float temp_c;
  int condition_code;
  String condition_text;
  float wind_kph;
  String wind_dir;
  bool valid;
};

Weather currentWeather = {0.0, 0, "", 0.0, "", false};

// Wetter API Konfiguration
const char* WEATHER_API_KEY = "015c830239c34d4f8f2140512250612";
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 3600000;  // 60 Minuten
float stationLat = 0.0;
float stationLon = 0.0;
bool stationCoordsValid = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=================================");
  Serial.println("ÖV Abfahrtsanzeiger - ESP32-S3");
  Serial.println("=================================");

  Serial.println("\nPin-Mapping für XIAO ESP32-S3 (D-Pins):");
  Serial.println("  CS    = D5 = GPIO 6");
  Serial.println("  DC    = D3 = GPIO 4");
  Serial.println("  RST   = D0 = GPIO 1");
  Serial.println("  BUSY  = D1 = GPIO 2");
  Serial.println("  POWER = D8 = GPIO 7");
  Serial.println("  SCK   = D2 = GPIO 3");
  Serial.println("  MOSI  = D4 = GPIO 5\n");

  // PSRAM Check
  Serial.println("Speicher-Info:");
  Serial.print("  PSRAM gefunden: ");
  Serial.println(psramFound() ? "Ja" : "Nein");
  if (psramFound()) {
    Serial.print("  PSRAM Größe: ");
    Serial.print(ESP.getPsramSize() / 1024);
    Serial.println(" KB");
    Serial.print("  PSRAM frei: ");
    Serial.print(ESP.getFreePsram() / 1024);
    Serial.println(" KB");
  }
  Serial.print("  Heap Größe: ");
  Serial.print(ESP.getHeapSize() / 1024);
  Serial.println(" KB");
  Serial.print("  Heap frei: ");
  Serial.print(ESP.getFreeHeap() / 1024);
  Serial.println(" KB\n");

  // WICHTIG: Power Enable Pin auf HIGH!
  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, HIGH);
  Serial.println("✓ Display Power aktiviert (D8 = GPIO 7 = HIGH)");
  delay(100);

  // SPI explizit initialisieren für E-Paper
  Serial.println("\n→ Initialisiere SPI...");
  // SPI.begin(SCK, MISO, MOSI, SS) - richtige Reihenfolge!
  SPI.begin(3, -1, 5, -1);  // SCK=D2=GPIO3, MISO=unused, MOSI=D4=GPIO5, SS=unused
  SPI.setFrequency(4000000);  // 4MHz - sicherer für längere Kabel
  Serial.println("✓ SPI initialisiert (4MHz)");

  // E-Paper Display initialisieren
  Serial.println("\n→ Initialisiere E-Paper Display...");
  Serial.println("   Display-Typ: GDEY042Z98 (SSD1683)");
  Serial.println("   Auflösung: 400x300 Pixel, 3 Farben (schwarz/weiß/rot)");

  display.init(115200, true, 50, false);  // serial debug, reset, reset_duration (50ms wie Hersteller), pulldown_rst
  display.setRotation(0);  // 0 = Portrait, 1 = Landscape
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();

  Serial.println("✓ E-Paper initialisiert");
  Serial.println("   Versuche Display zu löschen...");

  // Einfacher Test: Display komplett weiß machen
  display.clearScreen();
  Serial.println("✓ Display gelöscht");
  delay(2000);

  // Boot-Anzeige
  Serial.println("\n→ Zeige Boot-Screen...");
  displayBootScreen();
  Serial.println("✓ Boot-Screen angezeigt");
  delay(3000);

  // WICHTIG: Delay nach E-Paper Operationen, bevor WiFi startet
  // E-Paper Timer müssen freigegeben werden, sonst hat WiFi PHY keine Timer mehr
  Serial.println("→ Warte auf Timer-Freigabe...");
  display.hibernate();  // E-Paper in Schlafmodus versetzen
  delay(1000);  // 1 Sekunde warten, damit Timer sauber freigegeben werden

  Serial.println("\n\n=================================");
  Serial.println("ÖV Abfahrtsanzeiger gestartet");
  Serial.println("ESP32-C6 + E-Paper 4.2\"");
  Serial.println("=================================\n");

  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);

  loadSettings();

  // Wenn WiFi konfiguriert ist: Normalbetrieb starten + Webserver über Home-Netzwerk
  if (ssid.length() > 0) {
    Serial.println("→ WiFi konfiguriert - starte Normalbetrieb");

    // Verbinde mit WiFi für Normalbetrieb
    connectToWiFi();

    if (WiFi.status() == WL_CONNECTED) {
      normalMode = true;

      Serial.println("→ Webserver läuft über Home-Netzwerk");
      Serial.println("→ URL: http://" + WiFi.localIP().toString());
      Serial.println("→ Webserver läuft 2 Min. nach letzter Aktivität\n");

      // Zeige WiFi Info-Screen mit IP
      displayWiFiInfo();
      delay(5000);  // 5 Sekunden anzeigen

      // Starte nur Webserver (ohne AP und DNS)
      startWebserverOnly();
      apTimeoutEnabled = true;
      apStartTime = millis();
      lastApActivity = millis();
    } else {
      // WiFi fehlgeschlagen - starte Config-Modus
      Serial.println("→ WiFi-Verbindung fehlgeschlagen - starte Config-Modus");
      displayStatus("WiFi Fehler!", "Starte Config...");
      delay(2000);
      startConfigMode();
      displayConfigMode();  // Zeige neuen Config-Screen
      apTimeoutEnabled = false;
    }
  } else {
    // Keine WiFi-Daten: Nur Config-Modus ohne Timeout
    Serial.println("→ Keine WiFi-Daten - starte Config-Modus");
    startConfigMode();
    displayConfigMode();  // Zeige neuen Config-Screen mit Anleitung
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

    // E-Paper: Display-Updates während Config deaktiviert (zu langsam, blockiert Webserver)
    // Display wird nur einmal beim Start des Config-Modus aktualisiert

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
        lastUpdate = millis();  // Setze VOR dem Aufruf, um Doppelaufrufe zu vermeiden

        // Beim ersten Mal: Koordinaten abrufen
        if (!stationCoordsValid) {
          fetchStationCoordinates();
        }

        // Wetter alle 60 Minuten aktualisieren
        if (millis() - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL || lastWeatherUpdate == 0) {
          lastWeatherUpdate = millis();
          fetchWeatherData();
        }

        fetchAndDisplayDepartures();
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



// OeV-Go Logo: Hochgeschwindigkeitszug im Kreis (128x128 Pixel)
// Konvertiert mit image2cpp vom Original-Logo
const unsigned char logo_oevgo[] PROGMEM = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x07, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x07, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x0f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x9f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xbe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x03, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x03, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x07, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x07, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x07, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x0f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x0f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfe, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfe, 0x7f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 0x1f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 0x0f, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfc, 0x07, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfc, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfc, 0x01, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfc, 0x01, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00,
	0x01, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfc, 0x00, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00,
	0x01, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfc, 0x00, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00,
	0x01, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfc, 0x00, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00,
	0x03, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xfc, 0x00, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00,
	0x03, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xfc, 0x00, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00,
	0x03, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xfc, 0x00, 0xfb, 0xff, 0x00, 0x00, 0x00, 0x00,
	0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x00, 0xf9, 0xff, 0x80, 0x00, 0x00, 0x00,
	0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x00, 0xf8, 0xff, 0xc0, 0x00, 0x00, 0x00,
	0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x00, 0xf8, 0x7f, 0xe0, 0x00, 0x00, 0x00,
	0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x00, 0xf8, 0x3f, 0xf8, 0x00, 0x00, 0x00,
	0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x00, 0xf8, 0x0f, 0xfc, 0x00, 0x00, 0x00,
	0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x00, 0xf8, 0x07, 0xfe, 0x00, 0x00, 0x00,
	0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x00, 0xf8, 0x07, 0xff, 0x80, 0x00, 0x00,
	0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x00, 0xf8, 0x07, 0xff, 0xc0, 0x00, 0x00,
	0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xf8, 0x07, 0xff, 0xf0, 0x00, 0x00,
	0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0xf8, 0x07, 0x9f, 0xfc, 0x00, 0x00,
	0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0xf8, 0x07, 0x8f, 0xff, 0x00, 0x00,
	0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xf8, 0x07, 0x83, 0xff, 0xc0, 0x00,
	0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0xf8, 0x07, 0x81, 0xff, 0xf0, 0x00,
	0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0xf8, 0x07, 0x81, 0xff, 0xfc, 0x00,
	0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0xf8, 0x07, 0x81, 0xef, 0xff, 0x00,
	0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x07, 0x81, 0xe3, 0xff, 0xc0,
	0x3f, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x87, 0xff, 0xff, 0xff, 0xf8, 0x07, 0x81, 0xe0, 0xff, 0xf8,
	0x3f, 0xc0, 0x7f, 0xff, 0xff, 0xfe, 0x03, 0xff, 0xff, 0xff, 0xf8, 0x07, 0x81, 0xe0, 0xff, 0xfc,
	0x3f, 0x80, 0x3f, 0xff, 0xff, 0xfe, 0x01, 0xff, 0xff, 0xff, 0xfc, 0x07, 0x81, 0xe0, 0xff, 0xfe,
	0x7f, 0x80, 0x3f, 0xff, 0xff, 0xfc, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x81, 0xe0, 0xe3, 0xfe,
	0x7f, 0x80, 0x1f, 0xff, 0xff, 0xfc, 0x00, 0xff, 0xff, 0xff, 0xff, 0xc7, 0x81, 0xe0, 0xe1, 0xfe,
	0x7f, 0x80, 0x1f, 0xff, 0xff, 0xfc, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x81, 0xe0, 0xe1, 0xfe,
	0x7f, 0x80, 0x1f, 0xff, 0xff, 0xfc, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x81, 0xe0, 0xe1, 0xce,
	0x7f, 0x80, 0x3f, 0xff, 0xff, 0xfc, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe1, 0xe0, 0xe1, 0xce,
	0x7f, 0xc0, 0x3f, 0xff, 0xff, 0xfe, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xe0, 0xe1, 0xce,
	0x7f, 0xc0, 0x7f, 0xff, 0xff, 0xff, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0xe1, 0xce,
	0x7f, 0xf0, 0xff, 0xff, 0xff, 0xff, 0x87, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0xe1, 0xce,
	0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe1, 0xce,
	0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf9, 0xce,
	0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xce,
	0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x00, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xfc,
	0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
	0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc,
	0x00, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x00,
	0x00, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
	0x00, 0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00,
	0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00,
	0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x03, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x07, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0f, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};





// WiFi-Signal Icons (16x16 Pixel) - 4 Stärken
const unsigned char wifi_icon_4[] PROGMEM = {  // Stark (4 Balken)
  0x00, 0x00, 0x07, 0xe0, 0x1f, 0xf8, 0x3e, 0x7c, 0x78, 0x1e, 0x61, 0x86, 0x47, 0xe2,
  0x0e, 0x70, 0x1c, 0x38, 0x01, 0x80, 0x03, 0xc0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

const unsigned char wifi_icon_3[] PROGMEM = {  // Mittel (3 Balken)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x7c, 0x78, 0x1e, 0x61, 0x86, 0x47, 0xe2,
  0x0e, 0x70, 0x1c, 0x38, 0x01, 0x80, 0x03, 0xc0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

const unsigned char wifi_icon_2[] PROGMEM = {  // Schwach (2 Balken)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0xe2,
  0x0e, 0x70, 0x1c, 0x38, 0x01, 0x80, 0x03, 0xc0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

const unsigned char wifi_icon_1[] PROGMEM = {  // Sehr schwach (1 Balken)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x03, 0xc0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Wetter-Icons (16x16 Pixel)
const unsigned char weather_sun[] PROGMEM = {  // Sonne (Code 1000)
  0x01, 0x80, 0x01, 0x80, 0x00, 0x00, 0x10, 0x08, 0x18, 0x18, 0x07, 0xe0,
  0x0f, 0xf0, 0x1f, 0xf8, 0x1f, 0xf8, 0x0f, 0xf0, 0x07, 0xe0, 0x18, 0x18,
  0x10, 0x08, 0x00, 0x00, 0x01, 0x80, 0x01, 0x80
};

const unsigned char weather_cloud[] PROGMEM = {  // Bewölkt (Codes 1006, 1009)
  0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x10, 0x80, 0x20, 0x40, 0x20, 0x40,
  0x47, 0xe0, 0x88, 0x10, 0x90, 0x08, 0x90, 0x08, 0x90, 0x08, 0x88, 0x10,
  0x47, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char weather_partly_cloudy[] PROGMEM = {  // Teilweise bewölkt (Code 1003)
  0x01, 0x80, 0x01, 0x80, 0x10, 0x00, 0x18, 0x18, 0x27, 0xe0, 0x4f, 0xf0,
  0x9f, 0xf8, 0x8f, 0xf0, 0x87, 0xe0, 0x98, 0x10, 0x90, 0x08, 0x88, 0x10,
  0x47, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char weather_rain[] PROGMEM = {  // Regen (Codes 1063, 1180-1201)
  0x00, 0x00, 0x0f, 0x00, 0x10, 0x80, 0x20, 0x40, 0x47, 0xe0, 0x88, 0x10,
  0x90, 0x08, 0x88, 0x10, 0x47, 0xe0, 0x00, 0x00, 0x04, 0x20, 0x08, 0x10,
  0x04, 0x20, 0x08, 0x10, 0x04, 0x20, 0x00, 0x00
};

const unsigned char weather_snow[] PROGMEM = {  // Schnee (Codes 1210-1225)
  0x00, 0x00, 0x0f, 0x00, 0x10, 0x80, 0x20, 0x40, 0x47, 0xe0, 0x88, 0x10,
  0x90, 0x08, 0x88, 0x10, 0x47, 0xe0, 0x00, 0x00, 0x01, 0x80, 0x05, 0xa0,
  0x03, 0xc0, 0x05, 0xa0, 0x01, 0x80, 0x00, 0x00
};

const unsigned char weather_thunder[] PROGMEM = {  // Gewitter (Codes 1273-1282)
  0x00, 0x00, 0x0f, 0x00, 0x10, 0x80, 0x20, 0x40, 0x47, 0xe0, 0x88, 0x10,
  0x90, 0x08, 0x88, 0x10, 0x47, 0xe0, 0x00, 0x00, 0x03, 0x00, 0x06, 0x00,
  0x0f, 0x80, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00
};

void displayBootScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Logo oben zentriert (128x128 Pixel - Hochgeschwindigkeitszug im Kreis)
    int16_t logo_x = (400 - 128) / 2;  // Zentriert bei 400px Breite
    int16_t logo_y = 20;
    display.drawBitmap(logo_x, logo_y, logo_oevgo, 128, 128, GxEPD_BLACK);

    // "OeV-Go" - Groß und fett in Schwarz (24pt ohne Skalierung = beste Qualität)
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold24pt7b);

    // Text zentrieren
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds("OeV-Go", 0, 0, &x1, &y1, &w, &h);
    int16_t text_x = (400 - w) / 2;
    int16_t text_y = 195;  // Guter Abstand zum Logo (128px Logo + 47px Abstand)

    display.setCursor(text_x, text_y);
    display.print("OeV-Go");

    // Untertitel in Rot
    display.setTextColor(GxEPD_RED);
    display.setFont(&FreeSans9pt7b);
    display.getTextBounds("Schweizer OeV Abfahrten", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 230);  // Abstand unter Titel
    display.print("Schweizer OeV Abfahrten");

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

    int16_t x1, y1;
    uint16_t w, h;
    int16_t text_x;

    // Titel in Rot - zentriert
    display.setTextColor(GxEPD_RED);
    display.setFont(&FreeSansBold12pt7b);
    display.getTextBounds("CONFIG-MODUS", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 50);
    display.print("CONFIG-MODUS");

    // Anweisung 1 - zentriert
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold9pt7b);
    display.getTextBounds("1. Mit WLAN verbinden:", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 90);
    display.print("1. Mit WLAN verbinden:");

    // SSID - zentriert
    display.setFont(&FreeSans9pt7b);
    display.getTextBounds("SSID: OEV-Anzeiger-Config", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 120);
    display.print("SSID: OEV-Anzeiger-Config");

    // Passwort - zentriert
    display.getTextBounds("Passwort: config123", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 145);
    display.print("Passwort: config123");

    // Trennlinie
    display.drawLine(50, 165, 350, 165, GxEPD_BLACK);

    // Anweisung 2 - zentriert
    display.setFont(&FreeSansBold9pt7b);
    display.getTextBounds("2. Browser oeffnen:", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 195);
    display.print("2. Browser oeffnen:");

    // IP - zentriert und größer
    display.setFont(&FreeSansBold12pt7b);
    display.getTextBounds("192.168.4.1", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 230);
    display.print("192.168.4.1");

    // Hinweis unten - zentriert
    display.setFont(&FreeSans9pt7b);
    display.getTextBounds("Setup startet automatisch", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 275);
    display.print("Setup startet automatisch");

  } while (display.nextPage());
}

void displayWiFiInfo() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    int16_t x1, y1;
    uint16_t w, h;
    int16_t text_x;

    // Titel "WiFi verbunden!" - zentriert und fett
    display.setFont(&FreeSansBold12pt7b);
    display.getTextBounds("WiFi verbunden!", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 50);
    display.print("WiFi verbunden!");

    // "IP-Adresse:" - zentriert
    display.setFont(&FreeSans9pt7b);
    display.getTextBounds("IP-Adresse:", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 95);
    display.print("IP-Adresse:");

    // IP groß und zentriert
    display.setFont(&FreeSansBold18pt7b);
    String ipStr = WiFi.localIP().toString();
    display.getTextBounds(ipStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 135);
    display.print(ipStr);

    // "Config-Seite:" - zentriert
    display.setFont(&FreeSans9pt7b);
    display.getTextBounds("Config-Seite:", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 180);
    display.print("Config-Seite:");

    // "Browser oeffnen und eingeben" - zentriert
    display.getTextBounds("Browser oeffnen und eingeben", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 210);
    display.print("Browser oeffnen und eingeben");

    // URL - zentriert
    display.setFont(&FreeSansBold12pt7b);
    String urlStr = "http://" + ipStr;
    display.getTextBounds(urlStr.c_str(), 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 245);
    display.print(urlStr);

    // Hinweis - zentriert, klein
    display.setFont(&FreeSans9pt7b);
    display.getTextBounds("(2 Min. nach Neustart)", 0, 0, &x1, &y1, &w, &h);
    text_x = (400 - w) / 2;
    display.setCursor(text_x, 275);
    display.print("(2 Min. nach Neustart)");

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
    display.setFont(&FreeSans12pt7b);

    // Prüfe ob 2 Haltestellen konfiguriert sind
    bool has2Stations = (stationName2.length() > 0);

    // Station(en) oben links
    display.setCursor(10, 28);
    if (has2Stations) {
      display.print("2 Haltestellen");
    } else {
      display.print(replaceUmlauts(stationName));
    }

    // WiFi-Signal Icon oben rechts
    int rssi = WiFi.RSSI();
    const unsigned char* wifi_icon;
    if (rssi > -60) wifi_icon = wifi_icon_4;       // Stark
    else if (rssi > -70) wifi_icon = wifi_icon_3;  // Mittel
    else if (rssi > -80) wifi_icon = wifi_icon_2;  // Schwach
    else wifi_icon = wifi_icon_1;                  // Sehr schwach

    display.drawBitmap(375, 5, wifi_icon, 16, 16, GxEPD_BLACK);

    // Trennlinie
    display.drawLine(0, 35, 400, 35, GxEPD_BLACK);

    // === TABELLEN-HEADER ===
    display.setFont(&FreeSansBold9pt7b);
    if (has2Stations) {
      // Mit 2 Haltestellen: Station | Linie | Ziel | Abfahrt
      display.setCursor(10, 55);
      display.print("Halt");
      display.setCursor(80, 55);
      display.print("Linie");
      display.setCursor(150, 55);
      display.print("Ziel");
      display.setCursor(300, 55);
      display.print("Abfahrt");
    } else {
      // Mit 1 Haltestelle: Linie | Ziel | Abfahrt
      display.setCursor(10, 55);
      display.print("Linie");
      display.setCursor(90, 55);
      display.print("Ziel");
      display.setCursor(300, 55);
      display.print("Abfahrt");
    }

    display.drawLine(0, 63, 400, 63, GxEPD_BLACK);

    // === ABFAHRTEN ===
    display.setFont(&FreeMonoBold9pt7b);
    int y = 80;  // Näher an Header-Zeile (war 105)

    // Dynamische Berechnung der Zeilenhöhe
    // Verfügbarer Platz: 293 (Footer) - 80 (Start) = 213 Pixel
    int availableSpace = 213;
    int totalDepartures = currentDepartures.size();
    int lineHeight = availableSpace / totalDepartures;
    if (lineHeight < 24) lineHeight = 24;  // Minimum 24 Pixel pro Zeile
    if (lineHeight > 45) lineHeight = 45;  // Maximum 45 Pixel pro Zeile

    for (size_t i = 0; i < currentDepartures.size(); i++) {
      Departure& dep = currentDepartures[i];

      if (has2Stations) {
        // === MIT 2 HALTESTELLEN ===
        // Station (verkürzt auf 5-6 Zeichen)
        String station = replaceUmlauts(dep.stationName);
        if (station.length() > 6) station = station.substring(0, 6);
        display.setCursor(10, y);
        display.print(station);

        // Linie mit Category (z.B. "S5", "IC1")
        String lineCat = dep.category + dep.line;
        if (lineCat.length() > 6) lineCat = lineCat.substring(0, 6);
        display.setCursor(80, y);
        display.print(lineCat);

        // Ziel (gekürzt auf 12 Zeichen)
        String dest = replaceUmlauts(dep.destination);
        if (dest.length() > 12) {
          dest = dest.substring(0, 12);
          dest += "..";
        }
        display.setCursor(150, y);
        display.print(dest);

        // Abfahrtszeit
        display.setCursor(300, y);
        display.print(dep.departureTime);

        // Verspätung in Rot
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
      } else {
        // === MIT 1 HALTESTELLE ===
        // Linie mit Category (z.B. "S5", "IC1")
        String lineCat = dep.category + dep.line;
        if (lineCat.length() > 8) lineCat = lineCat.substring(0, 8);
        display.setCursor(10, y);
        display.print(lineCat);

        // Ziel (gekürzt auf 18 Zeichen)
        String dest = replaceUmlauts(dep.destination);
        if (dest.length() > 18) {
          dest = dest.substring(0, 18);
          dest += "..";
        }
        display.setCursor(90, y);
        display.print(dest);

        // Abfahrtszeit
        display.setCursor(300, y);
        display.print(dep.departureTime);

        // Verspätung in Rot
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
      }

      y += lineHeight;
    }

    // === WETTER FOOTER ===
    if (currentWeather.valid) {
      int footer_y = 293;  // Position unten im Display (300px Höhe)

      display.setFont(&FreeSans9pt7b);
      display.setTextColor(GxEPD_BLACK);

      // Temperatur
      display.setCursor(10, footer_y);
      char tempStr[10];
      sprintf(tempStr, "%.0f", currentWeather.temp_c);  // Ganze Zahl
      display.print(tempStr);
      display.print(" Grad");

      // Wetter-Beschreibung basierend auf Condition Code
      String weatherText = "";
      int code = currentWeather.condition_code;

      if (code == 1000) {
        weatherText = "Sonnig";
      } else if (code == 1003) {
        weatherText = "Teilweise bewoelkt";
      } else if (code == 1006) {
        weatherText = "Bewoelkt";
      } else if (code == 1009) {
        weatherText = "Bedeckt";
      } else if (code == 1030 || code == 1135 || code == 1147) {
        weatherText = "Nebel";
      } else if (code >= 1063 && code <= 1072) {
        weatherText = "Moeglicher Regen";
      } else if (code >= 1150 && code <= 1171) {
        weatherText = "Leichter Nieselregen";
      } else if (code >= 1180 && code <= 1186) {
        weatherText = "Leichter Regen";
      } else if (code >= 1189 && code <= 1201) {
        weatherText = "Regen";
      } else if (code >= 1210 && code <= 1216) {
        weatherText = "Leichter Schneefall";
      } else if (code >= 1219 && code <= 1225) {
        weatherText = "Schneefall";
      } else if (code >= 1237 && code <= 1264) {
        weatherText = "Graupel/Schneeregen";
      } else if (code >= 1273 && code <= 1282) {
        weatherText = "Gewitter moeglich";
      } else {
        weatherText = "Siehe draussen";
      }

      // Wetter-Text anzeigen
      display.setCursor(100, footer_y);
      display.print(weatherText);

      // Wind rechts bündig
      char windStr[20];
      sprintf(windStr, "%s %d km/h", currentWeather.wind_dir.c_str(), (int)currentWeather.wind_kph);

      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(windStr, 0, 0, &x1, &y1, &w, &h);
      display.setCursor(390 - w, footer_y);  // Rechts bündig (10px Rand)
      display.print(windStr);
    }

  } while (display.nextPage());
}

// ============= URSPRÜNGLICHE FUNKTIONEN (unverändert) =============

void loadSettings() {
  preferences.begin("oev-config", false);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  stationName = preferences.getString("station", "");
  stationName.trim();
  stationName2 = preferences.getString("station2", "");
  stationName2.trim();
  allowedDestinations = preferences.getString("destinations", "");
  allowedDestinations2 = preferences.getString("destinations2", "");
  walkingTimeMinutes = preferences.getInt("walkingTime", 0);
  displayLines = preferences.getInt("displayLines", 4);  // Standard: 4 Linien
  filterBus = preferences.getBool("filterBus", true);
  filterTram = preferences.getBool("filterTram", true);
  filterZug = preferences.getBool("filterZug", true);
  preferences.end();

  Serial.println("Gespeicherte Einstellungen:");
  Serial.println("SSID: " + String(ssid.length() > 0 ? ssid : "(leer)"));
  Serial.println("Station 1: " + String(stationName.length() > 0 ? stationName : "(leer)"));
  Serial.println("Erlaubte Ziele 1: " + String(allowedDestinations.length() > 0 ? allowedDestinations : "(alle)"));
  Serial.println("Station 2: " + String(stationName2.length() > 0 ? stationName2 : "(leer)"));
  Serial.println("Erlaubte Ziele 2: " + String(allowedDestinations2.length() > 0 ? allowedDestinations2 : "(alle)"));
  Serial.println("Fußweg: " + String(walkingTimeMinutes) + " Minuten");
  Serial.println("Anzeigelinien: " + String(displayLines));
}

void saveSettings() {
  stationName.trim();
  stationName2.trim();
  allowedDestinations.trim();
  allowedDestinations2.trim();

  preferences.begin("oev-config", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.putString("station", stationName);
  preferences.putString("station2", stationName2);
  preferences.putString("destinations", allowedDestinations);
  preferences.putString("destinations2", allowedDestinations2);
  preferences.putInt("walkingTime", walkingTimeMinutes);
  preferences.putInt("displayLines", displayLines);
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
  server.on("/destinations", handleDestinations);
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

  // Kompletter WiFi-Reset (wichtig nach fehlgeschlagenen Verbindungsversuchen!)
  Serial.println("→ Setze WiFi zurück...");
  WiFi.disconnect(true, true);  // true, true = Disconnect + erase WiFi config
  WiFi.mode(WIFI_OFF);
  delay(1000);  // Längere Wartezeit für sauberen Reset

  Serial.println("→ Starte AP+STA Modus...");
  WiFi.mode(WIFI_AP_STA);
  delay(1000);  // Längere Wartezeit für Mode-Wechsel

  // Setze WiFi TX Power auf Maximum für bessere Reichweite
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Maximum Power (78 = 19.5dBm)
  Serial.println("→ WiFi TX Power auf Maximum gesetzt (19.5dBm)");

  Serial.println("→ Starte Access Point...");

  WiFi.softAPConfig(
    IPAddress(192, 168, 4, 1),
    IPAddress(192, 168, 4, 1),
    IPAddress(255, 255, 255, 0)
  );

  // Versuche AP mehrfach zu starten bei Fehler
  // Channel 1, hidden=false, max_connections=4
  bool apStarted = WiFi.softAP("OEV-Anzeiger-Config", "config123", 1, 0, 4);

  if (!apStarted) {
    Serial.println("✗ AP Start fehlgeschlagen, versuche erneut...");
    delay(1000);
    WiFi.mode(WIFI_OFF);
    delay(500);
    WiFi.mode(WIFI_AP_STA);
    delay(1000);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Maximum Power auch beim Retry
    apStarted = WiFi.softAP("OEV-Anzeiger-Config", "config123", 1, 0, 4);
  }

  if (!apStarted) {
    Serial.println("✗ AP Start fehlgeschlagen - ESP32 neu starten!");
    Serial.println("   Führe Neustart in 3 Sekunden aus...");
    delay(3000);
    ESP.restart();
  }

  delay(1000);  // Zeit für AP zum Hochfahren

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
  server.on("/destinations", handleDestinations);
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
    // displayStatus("AP gestoppt", "Normal-Betrieb");
  } else {
    Serial.println("→ Stoppe Webserver");
    // displayStatus("Webserver off", "Normal-Betrieb");
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

    // Display-Update während Config deaktiviert (blockiert Webserver für 15 Sek)
    // displayStatus("WiFi Test...", ssid.c_str());

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

      // Display-Update während Config deaktiviert
      // displayStatus("WiFi OK!", "Weiter zu Schritt 2");

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

      // WICHTIG: Lösche WiFi-Credentials aus NVS, sonst versucht ESP32 beim Neustart automatisch zu verbinden!
      WiFi.disconnect(true, true);  // true, true = Disconnect + erase WiFi config aus NVS

      // Display-Update während Config deaktiviert
      // displayStatus("WiFi Fehler!", "Pruefe Daten");

      String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
      html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
      html += "<title>WiFi Fehler</title>";
      html += "<style>body{font-family:Arial;text-align:center;padding:50px;background:#f0f0f0}";
      html += ".error{background:#f44336;color:white;padding:30px;border-radius:10px;display:inline-block}";
      html += "button{padding:15px 30px;font-size:16px;background:white;border:none;border-radius:5px;cursor:pointer;margin-top:20px}";
      html += "</style></head><body>";
      html += "<div class='error'>";
      html += "<h1>✗ Verbindung fehlgeschlagen</h1>";
      html += "<p>Prüfe SSID und Passwort</p>";
      html += "<p style='font-size:14px;color:#ffcdd2'>Stelle sicher, dass eine WiFi-Antenne angeschlossen ist!</p>";
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
  // Display-Update während Config deaktiviert
  // displayStatus("WiFi Scan...", "Bitte warten");

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
  html += ".reset-btn{background:#f44336;margin-top:20px}";
  html += ".reset-btn:hover{background:#da190b}";
  html += ".station{padding:10px;background:#f5f5f5;margin:5px 0;border-radius:5px;cursor:pointer}";
  html += ".station:hover{background:#e0e0e0}";
  html += ".checkbox-group{margin:15px 0}";
  html += ".checkbox-group label{display:inline-block;margin-right:15px;font-weight:normal}";
  html += "#results{max-height:200px;overflow-y:auto;margin-top:10px}";
  html += "#destinationsContainer{margin-top:20px;display:none}";
  html += "#destinationsContainer h3{margin-bottom:10px;color:#555}";
  html += ".dest-checkbox{margin:8px 0}";
  html += ".dest-checkbox label{display:flex;align-items:center;cursor:pointer;padding:8px;background:#f5f5f5;border-radius:5px}";
  html += ".dest-checkbox label:hover{background:#e0e0e0}";
  html += ".dest-checkbox input{margin-right:10px;width:auto;cursor:pointer}";
  html += "#destinationsList{max-height:300px;overflow-y:auto}";
  html += ".select-all-btn{background:#9E9E9E;padding:8px;font-size:14px;margin-bottom:10px}";
  html += ".select-all-btn:hover{background:#757575}";
  html += ".status{text-align:center;padding:10px;background:#e3f2fd;border-radius:5px;margin-top:10px;display:none}";
  html += "</style></head><body>";

  html += "<h1>🚉 Haltestelle wählen</h1>";
  html += "<div class='card'>";
  html += "<form action='/save' method='POST' id='configForm'>";

  html += "<label>Haltestelle 1:</label>";
  html += "<input type='text' name='station' id='station' value='" + stationName + "' required oninput='onStationInput()' placeholder='Tippen um zu suchen...'>";
  html += "<input type='hidden' name='stationExact' id='stationExact' value=''>";
  html += "<div id='results'></div>";

  html += "<div id='destinationsContainer'>";
  html += "<h3>Ziele auswählen (Haltestelle 1):</h3>";
  html += "<button type='button' class='select-all-btn' onclick='toggleAllDestinations()'>Alle auswählen / abwählen</button>";
  html += "<div id='destinationsList'></div>";
  html += "<input type='hidden' name='destinations' id='destinations' value=''>";
  html += "</div>";

  html += "<hr style='margin:30px 0;border:none;border-top:1px solid #ddd'>";

  html += "<label>Haltestelle 2 (optional):</label>";
  html += "<input type='text' name='station2' id='station2' value='" + stationName2 + "' oninput='onStation2Input()' placeholder='Tippen um zu suchen (optional)...'>";
  html += "<input type='hidden' name='station2Exact' id='station2Exact' value=''>";
  html += "<div id='results2'></div>";

  html += "<div id='destinationsContainer2' style='display:none'>";
  html += "<h3>Ziele auswählen (Haltestelle 2):</h3>";
  html += "<button type='button' class='select-all-btn' onclick='toggleAllDestinations2()'>Alle auswählen / abwählen</button>";
  html += "<div id='destinationsList2'></div>";
  html += "<input type='hidden' name='destinations2' id='destinations2' value=''>";
  html += "</div>";

  html += "<hr style='margin:30px 0;border:none;border-top:1px solid #ddd'>";

  html += "<label>Fußweg zur Haltestelle (Minuten):</label>";
  html += "<input type='number' name='walkingTime' id='walkingTime' value='" + String(walkingTimeMinutes) + "' min='0' max='60' placeholder='z.B. 10'>";
  html += "<small style='display:block;color:#666;margin-top:5px'>Verbindungen, die früher abfahren, werden nicht angezeigt</small>";

  html += "<label>Anzahl anzuzeigende Abfahrten:</label>";
  html += "<input type='number' name='displayLines' id='displayLines' value='" + String(displayLines) + "' min='1' max='8' placeholder='z.B. 4'>";
  html += "<small style='display:block;color:#666;margin-top:5px'>Wie viele Abfahrten auf dem Display angezeigt werden (1-8)</small>";
  html += "<small style='display:block;color:#666;margin-top:5px'>Hinweis: Bei 2 Haltestellen werden pro Haltestelle nur 3 Abfahrten angezeigt</small>";

  html += "<button type='submit'>✓ Speichern & Starten</button>";
  html += "</form>";

  html += "<button type='button' class='reset-btn' onclick='resetDevice()'>🔄 Gerät zurücksetzen</button>";

  html += "<div class='status' id='status'></div>";
  html += "</div>";

  html += "<script>";
  html += "let searchTimeout;";
  html += "function onStationInput(){";
  html += "let query=document.getElementById('station').value;";
  html += "clearTimeout(searchTimeout);";
  html += "if(query.length<2){";
  html += "document.getElementById('results').innerHTML='';";
  html += "return;";
  html += "}";
  html += "searchTimeout=setTimeout(()=>searchStations(query),300);";
  html += "}";
  html += "function searchStations(query){";
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
  html += "loadDestinations(name);";
  html += "}";
  html += "function loadDestinations(station){";
  html += "document.getElementById('status').style.display='block';";
  html += "document.getElementById('status').innerHTML='Lade Ziele...';";
  html += "fetch('/destinations?station='+encodeURIComponent(station))";
  html += ".then(r=>r.json())";
  html += ".then(data=>{";
  html += "let html='';";
  html += "data.destinations.forEach((dest,idx)=>{";
  html += "html+='<div class=\"dest-checkbox\">';";
  html += "html+='<label>';";
  html += "html+='<input type=\"checkbox\" id=\"dest'+idx+'\" value=\"'+dest.name+'\" checked onchange=\"updateDestinations()\">';";
  html += "html+=dest.name;";
  html += "html+='</label>';";
  html += "html+='</div>';";
  html += "});";
  html += "document.getElementById('destinationsList').innerHTML=html;";
  html += "document.getElementById('destinationsContainer').style.display='block';";
  html += "document.getElementById('status').style.display='none';";
  html += "updateDestinations();";
  html += "}).catch(e=>{";
  html += "document.getElementById('status').innerHTML='Fehler beim Laden der Ziele';";
  html += "});";
  html += "}";
  html += "function toggleAllDestinations(){";
  html += "let checkboxes=document.querySelectorAll('#destinationsList input[type=\"checkbox\"]');";
  html += "let allChecked=Array.from(checkboxes).every(cb=>cb.checked);";
  html += "checkboxes.forEach(cb=>cb.checked=!allChecked);";
  html += "updateDestinations();";
  html += "}";
  html += "function updateDestinations(){";
  html += "let selected=[];";
  html += "document.querySelectorAll('#destinationsList input[type=\"checkbox\"]:checked').forEach(cb=>{";
  html += "selected.push(cb.value);";
  html += "});";
  html += "document.getElementById('destinations').value=selected.join('|');";
  html += "}";
  html += "let searchTimeout2;";
  html += "function onStation2Input(){";
  html += "let query=document.getElementById('station2').value;";
  html += "clearTimeout(searchTimeout2);";
  html += "if(query.length<2){";
  html += "document.getElementById('results2').innerHTML='';";
  html += "document.getElementById('destinationsContainer2').style.display='none';";
  html += "return;";
  html += "}";
  html += "searchTimeout2=setTimeout(()=>searchStations2(query),300);";
  html += "}";
  html += "function searchStations2(query){";
  html += "document.getElementById('status').style.display='block';";
  html += "document.getElementById('status').innerHTML='Suche...';";
  html += "fetch('/search?q='+encodeURIComponent(query))";
  html += ".then(r=>r.json())";
  html += ".then(data=>{";
  html += "let html='';";
  html += "data.stations.forEach(st=>{";
  html += "html+='<div class=\"station\" onclick=\"selectStation2(\\''+st.name+'\\')\">';";
  html += "html+=st.name;";
  html += "html+='</div>';";
  html += "});";
  html += "document.getElementById('results2').innerHTML=html;";
  html += "document.getElementById('status').style.display='none';";
  html += "}).catch(e=>{";
  html += "document.getElementById('status').innerHTML='Fehler: '+e;";
  html += "});";
  html += "}";
  html += "function selectStation2(name){";
  html += "document.getElementById('station2').value=name;";
  html += "document.getElementById('station2Exact').value=name;";
  html += "document.getElementById('results2').innerHTML='';";
  html += "loadDestinations2(name);";
  html += "}";
  html += "function loadDestinations2(station){";
  html += "document.getElementById('status').style.display='block';";
  html += "document.getElementById('status').innerHTML='Lade Ziele...';";
  html += "fetch('/destinations?station='+encodeURIComponent(station))";
  html += ".then(r=>r.json())";
  html += ".then(data=>{";
  html += "let html='';";
  html += "data.destinations.forEach((dest,idx)=>{";
  html += "html+='<div class=\"dest-checkbox\">';";
  html += "html+='<label>';";
  html += "html+='<input type=\"checkbox\" id=\"dest2'+idx+'\" value=\"'+dest.name+'\" checked onchange=\"updateDestinations2()\">';";
  html += "html+=dest.name;";
  html += "html+='</label>';";
  html += "html+='</div>';";
  html += "});";
  html += "document.getElementById('destinationsList2').innerHTML=html;";
  html += "document.getElementById('destinationsContainer2').style.display='block';";
  html += "document.getElementById('status').style.display='none';";
  html += "updateDestinations2();";
  html += "}).catch(e=>{";
  html += "document.getElementById('status').innerHTML='Fehler beim Laden der Ziele';";
  html += "});";
  html += "}";
  html += "function toggleAllDestinations2(){";
  html += "let checkboxes=document.querySelectorAll('#destinationsList2 input[type=\"checkbox\"]');";
  html += "let allChecked=Array.from(checkboxes).every(cb=>cb.checked);";
  html += "checkboxes.forEach(cb=>cb.checked=!allChecked);";
  html += "updateDestinations2();";
  html += "}";
  html += "function updateDestinations2(){";
  html += "let selected=[];";
  html += "document.querySelectorAll('#destinationsList2 input[type=\"checkbox\"]:checked').forEach(cb=>{";
  html += "selected.push(cb.value);";
  html += "});";
  html += "document.getElementById('destinations2').value=selected.join('|');";
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
  // Display-Update während Config deaktiviert
  // displayStatus("Suche...", query.c_str());

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

void handleDestinations() {
  lastApActivity = millis();

  if (!server.hasArg("station")) {
    Serial.println("✗ Station Parameter fehlt");
    server.send(400, "text/plain", "Station fehlt");
    return;
  }

  String station = server.arg("station");
  station.trim();

  Serial.println("\n→ Lade Ziele für Station: " + station);

  HTTPClient http;
  // ESP32-S3 hat mehr Ressourcen und PSRAM - limit=40 ist kein Problem
  String url = "http://transport.opendata.ch/v1/stationboard?station=" + urlEncode(station) + "&limit=40";

  Serial.println("URL: " + url);

  http.begin(url);
  http.setTimeout(10000);

  int httpCode = http.GET();
  Serial.println("HTTP Code: " + String(httpCode));

  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("Payload Länge: " + String(payload.length()) + " Bytes");

    if (payload.length() == 0) {
      Serial.println("✗ Keine Daten empfangen!");
      server.send(500, "text/plain", "Keine Daten");
      http.end();
      return;
    }

    // Info: Payload-Größe überwachen
    if (payload.length() >= 61440) {
      Serial.println("ℹ Info: Großer Payload (>60KB) - ESP32-S3 sollte dies verarbeiten können");
    }

    // Entferne Whitespace
    payload.trim();

    // Debug: Erste und letzte Zeichen
    Serial.print("Erste 50 Zeichen: ");
    Serial.println(payload.substring(0, min(50, (int)payload.length())));
    Serial.print("Letzte 50 Zeichen: ");
    int debugLen = min(50, (int)payload.length());
    Serial.println(payload.substring(payload.length() - debugLen));

    // ANFANG: Entferne alle Zeichen vor dem ersten { (Chunked-Encoding Header)
    int charsRemovedStart = 0;
    while (payload.length() > 0) {
      char firstChar = payload.charAt(0);
      if (firstChar == '{' || firstChar == '[') {
        break;
      }
      payload.remove(0, 1);
      charsRemovedStart++;
      if (charsRemovedStart > 100) break;
    }
    if (charsRemovedStart > 0) {
      Serial.println("Entfernt am Anfang: " + String(charsRemovedStart) + " Zeichen");
    }

    // ENDE: Entferne alle Zeichen nach dem letzten } oder ]
    int charsRemovedEnd = 0;
    while (payload.length() > 0) {
      char lastChar = payload.charAt(payload.length() - 1);
      if (lastChar == '}' || lastChar == ']') {
        break;
      }
      payload.remove(payload.length() - 1);
      charsRemovedEnd++;
      if (charsRemovedEnd > 100) break;
    }
    if (charsRemovedEnd > 0) {
      Serial.println("Entfernt am Ende: " + String(charsRemovedEnd) + " Zeichen");
    }

    Serial.println("Bereinigte Länge: " + String(payload.length()) + " Bytes");

    // Größerer Buffer für große Payloads (ESP32-C6 hat genug RAM)
    DynamicJsonDocument doc(98304);  // 96KB
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      Serial.println("✓ JSON erfolgreich geparst");
      Serial.println("Speichernutzung: " + String(doc.memoryUsage()) + " Bytes");
      if (!doc.containsKey("stationboard")) {
        Serial.println("✗ Keine Stationboard-Daten in Response");
        server.send(500, "text/plain", "Keine Stationboard-Daten");
        http.end();
        return;
      }

      JsonArray stationboard = doc["stationboard"].as<JsonArray>();
      Serial.println("Anzahl Verbindungen: " + String(stationboard.size()));

      // Verwende Vector statt Array (heap statt stack)
      std::vector<String> destinations;

      for (JsonObject connection : stationboard) {
        if (!connection.containsKey("to")) continue;

        String to = connection["to"].as<String>();
        if (to.length() == 0) continue;

        // Prüfe, ob Ziel bereits in der Liste ist
        bool exists = false;
        for (size_t i = 0; i < destinations.size(); i++) {
          if (destinations[i] == to) {
            exists = true;
            break;
          }
        }

        // Füge neues Ziel hinzu
        if (!exists && destinations.size() < 40) {
          destinations.push_back(to);
        }
      }

      // JSON-Response erstellen
      String json = "{\"destinations\":[";
      for (size_t i = 0; i < destinations.size(); i++) {
        if (i > 0) json += ",";
        // Escape Anführungszeichen in Zielnamen
        String escapedName = destinations[i];
        escapedName.replace("\"", "\\\"");
        json += "{\"name\":\"" + escapedName + "\"}";
      }
      json += "]}";

      Serial.println("✓ " + String(destinations.size()) + " einzigartige Ziele gefunden");
      server.send(200, "application/json", json);
    } else {
      Serial.println("✗ JSON Parse Error: " + String(error.c_str()));
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

    // Ziele übernehmen
    if (server.hasArg("destinations")) {
      allowedDestinations = server.arg("destinations");
      allowedDestinations.trim();
      Serial.println("Destinations Parameter empfangen: '" + allowedDestinations + "'");
    } else {
      allowedDestinations = "";  // Wenn keine Ziele ausgewählt, alle erlauben
      Serial.println("Destinations Parameter NICHT empfangen - alle Ziele erlaubt");
    }

    // Fußweg-Zeit übernehmen
    if (server.hasArg("walkingTime")) {
      walkingTimeMinutes = server.arg("walkingTime").toInt();
      if (walkingTimeMinutes < 0) walkingTimeMinutes = 0;
      if (walkingTimeMinutes > 60) walkingTimeMinutes = 60;
    } else {
      walkingTimeMinutes = 0;
    }

    // Anzahl Anzeigelinien übernehmen
    if (server.hasArg("displayLines")) {
      displayLines = server.arg("displayLines").toInt();
      if (displayLines < 1) displayLines = 1;
      if (displayLines > 8) displayLines = 8;
    } else {
      displayLines = 4;  // Standard: 4 Linien
    }

    // 2. Haltestelle übernehmen (optional)
    if (server.hasArg("station2Exact") && server.arg("station2Exact").length() > 0) {
      stationName2 = server.arg("station2Exact");
    } else if (server.hasArg("station2") && server.arg("station2").length() > 0) {
      stationName2 = server.arg("station2");
    } else {
      stationName2 = "";
    }
    stationName2.trim();

    // Ziele für 2. Haltestelle übernehmen
    if (server.hasArg("destinations2")) {
      allowedDestinations2 = server.arg("destinations2");
      allowedDestinations2.trim();
      Serial.println("Destinations2 Parameter empfangen: '" + allowedDestinations2 + "'");
    } else {
      allowedDestinations2 = "";
      Serial.println("Destinations2 Parameter NICHT empfangen - alle Ziele erlaubt");
    }

    Serial.println("\n=== Finale Konfiguration ===");
    Serial.println("SSID: " + ssid);
    Serial.println("Station 1: " + stationName);
    Serial.println("Erlaubte Ziele 1: " + String(allowedDestinations.length() > 0 ? allowedDestinations : "(alle)"));
    Serial.println("Station 2: " + String(stationName2.length() > 0 ? stationName2 : "(keine)"));
    Serial.println("Erlaubte Ziele 2: " + String(allowedDestinations2.length() > 0 ? allowedDestinations2 : "(alle)"));
    Serial.println("Fußweg: " + String(walkingTimeMinutes) + " Minuten");
    Serial.println("Anzeigelinien: " + String(displayLines));

    saveSettings();

    // Display-Update während Config deaktiviert
    // displayStatus("Gespeichert!", "Starte...");

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

  // Display-Update während Config deaktiviert
  // displayStatus("Reset...", "Loesche Daten");

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

  // Display-Update während Config deaktiviert
  // displayStatus("Neustart...", "Bitte warten");

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
    delay(100);  // Kurzer Delay nach Mode-Wechsel
  }

  Serial.println("\n=== WiFi-Verbindung ===");
  Serial.println("SSID: " + ssid);

  // Keine Display-Meldung - im Normalbetrieb später "Lade Daten..." anzeigen

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

    // Keine Display-Meldungen mehr - wird direkt mit Daten-Laden fortfahren
  } else {
    Serial.println("\n✗ WiFi-Verbindung fehlgeschlagen!");
    // Keine Display-Meldung - Config-Modus wird gleich starten
  }
}

void fetchStationCoordinates() {
  if (stationName.length() == 0) {
    Serial.println("Keine Station für Koordinaten-Abruf!");
    stationCoordsValid = false;
    return;
  }

  Serial.println("\n=== Koordinaten abrufen: " + stationName + " ===");

  HTTPClient http;
  String url = "http://transport.opendata.ch/v1/locations?query=" + urlEncode(stationName) + "&type=station";

  Serial.println("URL: " + url);

  http.begin(url);
  http.setTimeout(10000);

  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();

    DynamicJsonDocument doc(16384);  // 16KB für Koordinaten-Antwort
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON Error: ");
      Serial.println(error.c_str());
      stationCoordsValid = false;
      http.end();
      return;
    }

    JsonArray stations = doc["stations"];
    if (stations.size() > 0) {
      JsonObject station = stations[0];
      if (!station["coordinate"]["x"].isNull() && !station["coordinate"]["y"].isNull()) {
        // transport.opendata.ch gibt WGS84 zurück: x=Longitude, y=Latitude
        stationLat = station["coordinate"]["x"].as<float>();
        stationLon = station["coordinate"]["y"].as<float>();
        stationCoordsValid = true;

        Serial.print("✓ Koordinaten (Lat, Lon): ");
        Serial.print(stationLat, 4);
        Serial.print(", ");
        Serial.println(stationLon, 4);
      } else {
        Serial.println("✗ Keine Koordinaten in Response");
        stationCoordsValid = false;
      }
    } else {
      Serial.println("✗ Keine Station gefunden");
      stationCoordsValid = false;
    }
  } else {
    Serial.print("✗ HTTP Error: ");
    Serial.println(httpCode);
    stationCoordsValid = false;
  }

  http.end();
}

void fetchWeatherData() {
  if (!stationCoordsValid) {
    Serial.println("Keine gültigen Koordinaten für Wetter-Abruf!");
    currentWeather.valid = false;
    return;
  }

  Serial.println("\n=== Wetter abrufen ===");

  HTTPClient http;
  String coords = String(stationLat, 4) + "," + String(stationLon, 4);
  String url = "http://api.weatherapi.com/v1/current.json?key=" + String(WEATHER_API_KEY) + "&q=" + coords + "&aqi=no";

  Serial.println("URL: " + url);

  http.begin(url);
  http.setTimeout(10000);

  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();

    DynamicJsonDocument doc(8192);  // 8KB für Wetter-Antwort
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON Error: ");
      Serial.println(error.c_str());
      currentWeather.valid = false;
      http.end();
      return;
    }

    if (!doc["current"].isNull()) {
      currentWeather.temp_c = doc["current"]["temp_c"].as<float>();
      currentWeather.condition_code = doc["current"]["condition"]["code"].as<int>();
      currentWeather.condition_text = doc["current"]["condition"]["text"].as<String>();
      currentWeather.wind_kph = doc["current"]["wind_kph"].as<float>();
      currentWeather.wind_dir = doc["current"]["wind_dir"].as<String>();
      currentWeather.valid = true;

      Serial.print("✓ Temperatur: ");
      Serial.print(currentWeather.temp_c, 1);
      Serial.println("°C");
      Serial.print("✓ Wetter: ");
      Serial.print(currentWeather.condition_text);
      Serial.print(" (Code: ");
      Serial.print(currentWeather.condition_code);
      Serial.println(")");
      Serial.print("✓ Wind: ");
      Serial.print(currentWeather.wind_dir);
      Serial.print(" ");
      Serial.print(currentWeather.wind_kph, 0);
      Serial.println(" km/h");
    } else {
      Serial.println("✗ Keine Wetterdaten in Response");
      currentWeather.valid = false;
    }
  } else {
    Serial.print("✗ HTTP Error: ");
    Serial.println(httpCode);
    currentWeather.valid = false;
  }

  http.end();
}

// Hilfsfunktion: Lädt Abfahrten für eine Haltestelle und fügt sie currentDepartures hinzu
void fetchDeparturesForStation(String station, String allowedDests, int maxDepartures) {
  if (station.length() == 0) return;

  station.trim();

  Serial.println("\n=== Abfahrten: " + station + " ===");

  HTTPClient http;
  String url = "http://transport.opendata.ch/v1/stationboard?station=" + urlEncode(station) + "&limit=40";  // ESP32-S3 mit PSRAM kann mehr Verbindungen verarbeiten

  Serial.println("URL: " + url);

  http.begin(url);
  http.setTimeout(15000);

  int httpCode = http.GET();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode == 200) {
    // EINFACHE METHODE: getString() - funktioniert mit PSRAM problemlos
    // Payload ist ca. 400KB mit limit=40, aber ESP32-S3 hat 8MB PSRAM
    String payload = http.getString();

    Serial.print("Empfangene Daten: ");
    Serial.print(payload.length());
    Serial.println(" Bytes");

    if (payload.length() == 0) {
      Serial.println("✗ Keine Daten empfangen!");
      displayStatus("Keine Daten!", "API Error");
      http.end();
      return;
    }

    // Entferne Whitespace am Ende
    payload.trim();

    Serial.print("Nach Trim: ");
    Serial.print(payload.length());
    Serial.println(" Bytes");

    // Debug: Zeige letzte Zeichen
    int debugLen = min(50, (int)payload.length());
    Serial.print("Letzte ");
    Serial.print(debugLen);
    Serial.print(" Zeichen: ");
    Serial.println(payload.substring(payload.length() - debugLen));

    // ANFANG: Entferne alle Zeichen vor dem ersten { (HTTP Chunked-Encoding Header)
    int charsRemovedStart = 0;
    while (payload.length() > 0) {
      char firstChar = payload.charAt(0);
      if (firstChar == '{' || firstChar == '[') {
        break;  // Stoppe bei gültigem JSON-Start
      }
      payload.remove(0, 1);  // Entferne erstes Zeichen
      charsRemovedStart++;
      if (charsRemovedStart > 100) break;  // Sicherheits-Limit
    }

    if (charsRemovedStart > 0) {
      Serial.print("Entfernt am Anfang: ");
      Serial.print(charsRemovedStart);
      Serial.println(" Zeichen (Chunked-Encoding Header)");
    }

    // ENDE: Entferne alle Zeichen nach dem letzten } oder ] (HTTP Chunked-Encoding-Marker)
    // Verwende remove() statt substring() - sicherer bei großen Strings
    int charsRemoved = 0;
    while (payload.length() > 0) {
      char lastChar = payload.charAt(payload.length() - 1);
      if (lastChar == '}' || lastChar == ']') {
        break;  // Stoppe bei gültigem JSON-Ende
      }
      payload.remove(payload.length() - 1);  // Entferne letztes Zeichen
      charsRemoved++;
      if (charsRemoved > 100) break;  // Sicherheits-Limit
    }

    if (charsRemoved > 0) {
      Serial.print("Entfernt: ");
      Serial.print(charsRemoved);
      Serial.print(" Zeichen (Chunked-Encoding)");
      Serial.print(" - Neue Länge: ");
      Serial.print(payload.length());
      Serial.println(" Bytes");
    }

    // Prüfe ob JSON vollständig ist
    char lastChar = payload.charAt(payload.length() - 1);
    if (lastChar != '}' && lastChar != ']') {
      Serial.print("✗ Ungültiges Ende: '");
      Serial.print(lastChar);
      Serial.print("' (ASCII ");
      Serial.print((int)lastChar);
      Serial.println(")");
      displayStatus("Daten unvollst.", "Retry...");
      http.end();
      return;
    }

    Serial.println("✓ Vollständige Daten empfangen");

    // Debug: Zeige Anfang des JSON
    Serial.print("Erste 100 Zeichen: ");
    Serial.println(payload.substring(0, min(100, (int)payload.length())));

    // Buffer für JSON - muss kleiner sein wegen ESP32-C3 RAM-Limit
    // 64KB Payload + 96KB JSON-Buffer = 160KB (sicher innerhalb 400KB RAM)
    DynamicJsonDocument doc(98304);  // 96KB
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON Error: ");
      Serial.println(error.c_str());
      Serial.print("Benötigter Speicher: ");
      Serial.println(doc.memoryUsage());
      Serial.print("Payload-Länge: ");
      Serial.println(payload.length());

      // Debug: Zeige problematische Stelle
      Serial.println("Erste 200 Zeichen zur Analyse:");
      Serial.println(payload.substring(0, min(200, (int)payload.length())));

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
      http.end();
      return;
    }

    JsonArray stationboard = doc["stationboard"].as<JsonArray>();

    Serial.print("Anzahl Verbindungen: ");
    Serial.println(stationboard.size());
    Serial.print("Ziel-Filter aktiv: ");
    Serial.println(allowedDests.length() > 0 ? "Ja" : "Nein (alle erlaubt)");
    if (allowedDests.length() > 0) {
      Serial.println("Erlaubte Ziele: " + allowedDests);
    }

    int addedCount = 0;

    for (JsonObject connection : stationboard) {
      String category = connection["category"].as<String>();
      String destination = connection["to"].as<String>();

      Serial.print("  Verbindung: ");
      Serial.print(category);
      Serial.print(" → ");
      Serial.print(destination);

      // Prüfe ob Ziel erlaubt ist (wenn Filter aktiv)
      bool destinationAllowed = true;
      if (allowedDests.length() > 0) {
        destinationAllowed = false;
        // Durchsuche pipe-separierte Liste (| statt , wegen "Zürich, Bahnhof")
        int startPos = 0;
        int pipePos;
        while ((pipePos = allowedDests.indexOf('|', startPos)) != -1) {
          String allowedDest = allowedDests.substring(startPos, pipePos);
          allowedDest.trim();
          if (allowedDest == destination) {
            destinationAllowed = true;
            break;
          }
          startPos = pipePos + 1;
        }
        // Letztes Ziel (oder einziges, wenn keine Pipes)
        if (!destinationAllowed) {
          String allowedDest = allowedDests.substring(startPos);
          allowedDest.trim();
          if (allowedDest == destination) {
            destinationAllowed = true;
          }
        }
        Serial.print(" → ");
        Serial.println(destinationAllowed ? "✓ Erlaubt" : "✗ Gefiltert");
      } else {
        Serial.println(" → ✓ Alle erlaubt");
      }

      // Prüfe Fußweg-Zeit: Ist Abfahrt noch erreichbar?
      bool reachable = true;
      String departure = connection["stop"]["departure"].as<String>();
      String departureTime = "??:??";

      if (departure.length() >= 16) {
        departureTime = departure.substring(11, 16);  // "HH:MM"

        if (walkingTimeMinutes > 0 && departureTime != "??:??") {
          // Parse Abfahrtszeit
          int depHour = departureTime.substring(0, 2).toInt();
          int depMin = departureTime.substring(3, 5).toInt();
          int depTotalMin = depHour * 60 + depMin;

          // Aktuelle Zeit
          time_t now;
          struct tm timeinfo;
          time(&now);
          localtime_r(&now, &timeinfo);
          int nowTotalMin = timeinfo.tm_hour * 60 + timeinfo.tm_min;

          // Mindest-Abfahrtszeit = Jetzt + Fußweg
          int minDepartureMin = nowTotalMin + walkingTimeMinutes;

          // Prüfe ob erreichbar
          if (depTotalMin < minDepartureMin) {
            reachable = false;
          }
        }
      }

      // Nur erlaubte Ziele anzeigen, die noch erreichbar sind
      if (destinationAllowed && reachable && addedCount < maxDepartures) {
        Departure dep;
        dep.line = connection["number"].as<String>();
        if (dep.line == "null" || dep.line.length() == 0) {
          dep.line = category;
        }
        // Entferne führende Nullen (z.B. "000902" -> "902")
        dep.line = removeLeadingZeros(dep.line);
        dep.destination = destination;
        dep.category = category;
        dep.departureTime = departureTime;
        dep.stationName = station;  // Speichere Haltestellenname

        if (connection["stop"]["delay"].isNull()) {
          dep.delay = 0;
        } else {
          dep.delay = connection["stop"]["delay"].as<int>();
        }

        currentDepartures.push_back(dep);
        addedCount++;
      }

      if (addedCount >= maxDepartures) break;
    }

    Serial.println("✓ " + String(addedCount) + " Abfahrten hinzugefügt");

  } else if (httpCode > 0) {
    Serial.print("✗ HTTP Error: ");
    Serial.println(httpCode);
  } else {
    Serial.print("✗ HTTP Request fehlgeschlagen: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// Hauptfunktion: Lädt Abfahrten für 1 oder 2 Haltestellen und zeigt sie an
void fetchAndDisplayDepartures() {
  if (stationName.length() == 0) {
    Serial.println("Keine Haltestelle!");
    displayStatus("Keine Station!", "Config needed");
    return;
  }

  currentDepartures.clear();

  // Prüfe ob 2 Haltestellen konfiguriert sind
  bool has2Stations = (stationName2.length() > 0);

  if (has2Stations) {
    // 2 Haltestellen: Je 3 Abfahrten
    Serial.println("\n=== 2 Haltestellen Modus ===");
    fetchDeparturesForStation(stationName, allowedDestinations, 3);

    // WICHTIG: Delay zwischen API-Calls, damit Timer freigegeben werden
    Serial.println("→ Warte auf Timer-Freigabe zwischen API-Calls...");
    delay(2000);  // 2 Sekunden warten - HTTPClient braucht Zeit für Timer-Freigabe

    fetchDeparturesForStation(stationName2, allowedDestinations2, 3);
  } else {
    // 1 Haltestelle: Nutze displayLines
    fetchDeparturesForStation(stationName, allowedDestinations, displayLines);
  }

  // Ausgabe und Display-Update
  Serial.println("\n=== Geladene Abfahrten ===");
  if (currentDepartures.size() == 0) {
    Serial.println("Keine Abfahrten (Filter zu restriktiv?)");
    displayStatus("Keine Abfahrten", "Check Filter");
  } else {
    for (size_t i = 0; i < currentDepartures.size(); i++) {
      String line = currentDepartures[i].line;
      while (line.length() < 5) line += " ";

      String station = currentDepartures[i].stationName;
      if (station.length() > 15) station = station.substring(0, 15);
      while (station.length() < 15) station += " ";

      String dest = currentDepartures[i].destination;
      if (dest.length() > 30) dest = dest.substring(0, 30);
      while (dest.length() < 30) dest += " ";

      String delayStr = "";
      if (currentDepartures[i].delay >= 0) delayStr = "+";
      delayStr += String(currentDepartures[i].delay);
      while (delayStr.length() < 3) delayStr = " " + delayStr;

      Serial.print(station);
      Serial.print("  ");
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
}

String replaceUmlauts(String str) {
  str.replace("ü", "ue");
  str.replace("Ü", "Ue");
  str.replace("ä", "ae");
  str.replace("Ä", "Ae");
  str.replace("ö", "oe");
  str.replace("Ö", "Oe");
  return str;
}

String removeLeadingZeros(String str) {
  // Entferne führende Nullen (z.B. "000902" -> "902")
  while (str.length() > 1 && str.charAt(0) == '0') {
    str = str.substring(1);
  }
  return str;
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
