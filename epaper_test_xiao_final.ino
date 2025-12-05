/*
 * E-Paper Test für GDEY042Z98 (SSD1683, 3-Farben)
 * FÜR SEEED XIAO ESP32-C3 mit dem richtigen Pinout!
 */

#include <SPI.h>
#include <GxEPD2_3C.h>  // 3-Farben Library!
#include <Fonts/FreeMonoBold9pt7b.h>

// Pin Konfiguration für SEEED XIAO ESP32-C3
// Basierend auf Hersteller-Code, aber mit XIAO Pinout angepasst
//
// Hersteller sagt: CS=GPIO7, DC=GPIO1, RST=GPIO2, BUSY=GPIO3, SCL=GPIO4, SDA=GPIO6, POWER=GPIO8
// XIAO Pinout:     D0=GPIO2, D1=GPIO3, D2=GPIO4, D4=GPIO6, D5=GPIO7, D6=GPIO21, D8=GPIO8
//
// Problem: GPIO1 existiert nicht auf XIAO! Wir verwenden stattdessen einen freien Pin.
// Lösung: Nutze D6 (GPIO21/TX) für DC - sollte funktionieren wenn nicht für Serial gebraucht

#define EPD_CS      D5   // GPIO 7 (Hersteller: CS=7)
#define EPD_DC      D6   // GPIO 21 (TX) - ersetzt GPIO 1 vom Hersteller
#define EPD_RST     D0   // GPIO 2 (Hersteller: RST=2)
#define EPD_BUSY    D1   // GPIO 3 (Hersteller: BUSY=3)
#define EPD_POWER   D8   // GPIO 8 (Hersteller: POWER=8)
// SPI Pins:
// SCL = D2 (GPIO 4)
// SDA = D4 (GPIO 6)

// Display: GDEY042Z98 mit SSD1683 Controller (400x300, 3-Farben)
GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT> display(GxEPD2_420c_GDEY042Z98(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n=== E-PAPER TEST: XIAO ESP32-C3 ===\n");
  Serial.println("Display: GDEY042Z98 (SSD1683, 3-Farben)");
  Serial.println("\nPin-Mapping für XIAO:");
  Serial.println("  CS    = D5  (GPIO 7)");
  Serial.println("  DC    = D6  (GPIO 21/TX)");
  Serial.println("  RST   = D0  (GPIO 2)");
  Serial.println("  BUSY  = D1  (GPIO 3)");
  Serial.println("  SDA   = D4  (GPIO 6)");
  Serial.println("  SCL   = D2  (GPIO 4)");
  Serial.println("  POWER = D8  (GPIO 8)\n");

  // WICHTIG: Power Enable Pin auf HIGH!
  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, HIGH);
  Serial.println("✓ Display Power aktiviert (D8 = HIGH)");
  delay(100);

  // SPI Init mit richtigen Pins für XIAO
  Serial.println("\n→ Initialisiere SPI...");
  // SPI.begin(SCK, MISO, MOSI, SS)
  // SCL=D2 (GPIO4), SDA=D4 (GPIO6)
  SPI.begin(D2, -1, D4, -1);  // SCK=D2, MISO=unused, MOSI=D4, SS=unused
  SPI.setFrequency(4000000);
  Serial.println("✓ SPI initialisiert");

  // Display Init
  Serial.println("\n→ Initialisiere Display...");
  display.init(115200, true, 50, false);  // 50ms reset wie beim Hersteller
  Serial.println("✓ Display initialisiert\n");

  // Test: "Hello World" in schwarz und rot
  Serial.println("→ Zeige 'Hello World'...");
  const char HelloWorld[] = "Hello World!";
  const char HelloWeACtStudio[] = "WeAct Studio";

  display.setRotation(1);  // Landscape
  display.setFont(&FreeMonoBold9pt7b);

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

  Serial.println("\nVerkabelung:");
  Serial.println("  Display SDA  → XIAO D4");
  Serial.println("  Display SCL  → XIAO D2");
  Serial.println("  Display CS   → XIAO D5");
  Serial.println("  Display DC   → XIAO D6");
  Serial.println("  Display RES  → XIAO D0");
  Serial.println("  Display BUSY → XIAO D1");
  Serial.println("  Display VCC  → XIAO 3.3V");
  Serial.println("  Display GND  → XIAO GND");
  Serial.println("\nWICHTIG: Pin D8 MUSS mit 3.3V verbunden sein (Power Enable)!");

  display.hibernate();
}

void loop() {
  // LED blinken
  digitalWrite(EPD_POWER, HIGH);
  delay(1000);
  digitalWrite(EPD_POWER, LOW);
  delay(1000);
}
