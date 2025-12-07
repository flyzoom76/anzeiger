/*
 * E-Paper Test für GDEY042Z98 (SSD1683, 3-Farben)
 * Basiert auf Hersteller-Code für ESP32-C3
 */

#include <SPI.h>
#include <GxEPD2_3C.h>  // 3-Farben Library!
#include <Fonts/FreeMonoBold9pt7b.h>

// Pin Konfiguration laut Hersteller
// CS(SS)=7, SCL(SCK)=4, SDA(MOSI)=6, BUSY=3, RES(RST)=2, DC=1
#define EPD_CS      SS   // Pin 7
#define EPD_DC      1    // Pin 1
#define EPD_RST     2    // Pin 2
#define EPD_BUSY    3    // Pin 3
#define EPD_POWER   8    // Pin 8 (Power Enable!)

// Display: GDEY042Z98 mit SSD1683 Controller (400x300, 3-Farben: schwarz/weiß/rot)
GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT> display(GxEPD2_420c_GDEY042Z98(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n=== E-PAPER TEST: GDEY042Z98 (3-Farben) ===\n");
  Serial.println("Controller: SSD1683");
  Serial.println("Farben: Schwarz/Weiß/Rot");
  Serial.println("Pins laut Hersteller:");
  Serial.println("  CS=7, DC=1, RST=2, BUSY=3");
  Serial.println("  SCL=4, SDA=6, POWER=8\n");

  // WICHTIG: Power Enable Pin auf HIGH!
  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, HIGH);
  Serial.println("✓ Display Power aktiviert (Pin 8 = HIGH)");
  delay(100);

  // Display Init mit Hersteller-Parametern
  Serial.println("\n→ Initialisiere Display...");
  display.init(115200, true, 50, false);  // 50ms reset wie beim Hersteller
  Serial.println("✓ Display initialisiert\n");

  // Test: "Hello World" wie im Hersteller-Beispiel
  Serial.println("→ Zeige 'Hello World'...");
  const char HelloWorld[] = "Hello World!";
  const char HelloWeACtStudio[] = "WeAct Studio";

  display.setRotation(1);  // Landscape
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);

  int16_t tbx, tby;
  uint16_t tbw, tbh;

  // Zentriere "Hello World"
  display.getTextBounds(HelloWorld, 0, 0, &tbx, &tby, &tbw, &tbh);
  uint16_t x = ((display.width() - tbw) / 2) - tbx;
  uint16_t y = ((display.height() - tbh) / 2) - tby;

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // "Hello World" in Schwarz
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(x, y - tbh);
    display.print(HelloWorld);

    // "WeAct Studio" in Rot (3-Farben Test!)
    display.setTextColor(GxEPD_RED);
    display.getTextBounds(HelloWeACtStudio, 0, 0, &tbx, &tby, &tbw, &tbh);
    x = ((display.width() - tbw) / 2) - tbx;
    display.setCursor(x, y + tbh);
    display.print(HelloWeACtStudio);
  }
  while (display.nextPage());

  Serial.println("✓ Fertig!\n");
  Serial.println(">>> SIEHST DU 'Hello World' (SCHWARZ) UND 'WeAct Studio' (ROT)? <<<\n");

  Serial.println("\nWenn JA: Die Verkabelung ist jetzt korrekt!");
  Serial.println("Wenn NEIN: Prüfe die Verkabelung:");
  Serial.println("  Display SDA → ESP32-C3 Pin 6");
  Serial.println("  Display SCL → ESP32-C3 Pin 4");
  Serial.println("  Display CS  → ESP32-C3 Pin 7");
  Serial.println("  Display DC  → ESP32-C3 Pin 1");
  Serial.println("  Display RES → ESP32-C3 Pin 2");
  Serial.println("  Display BUSY→ ESP32-C3 Pin 3");
  Serial.println("  Display VCC → 3.3V");
  Serial.println("  Display GND → GND");

  display.hibernate();
}

void loop() {
  // LED blinken wie im Original
  digitalWrite(EPD_POWER, HIGH);
  delay(1000);
  digitalWrite(EPD_POWER, LOW);
  delay(1000);
}
