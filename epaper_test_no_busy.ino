/*
 * E-Paper Test OHNE BUSY Pin
 * Verwendet nur Software-Delays statt BUSY zu prüfen
 */

#include <SPI.h>
#include <GxEPD2_3C.h>

// Pin Definitionen
#define EPD_CS      D7
#define EPD_DC      D6
#define EPD_RST     D5
#define EPD_BUSY    -1   // BUSY deaktiviert!
#define EPD_MOSI    D10
#define EPD_SCK     D8

// Display Objekt
GxEPD2_3C<GxEPD2_420c, GxEPD2_420c::HEIGHT> display(GxEPD2_420c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n=== E-PAPER TEST (ohne BUSY) ===\n");

  // SPI Init
  Serial.println("1. Initialisiere SPI...");
  SPI.begin(EPD_SCK, -1, EPD_MOSI, -1);
  SPI.setFrequency(4000000);
  Serial.println("   ✓ SPI initialisiert");

  // Display Init
  Serial.println("\n2. Initialisiere Display (GxEPD2_420c)...");
  display.init(115200, true, 10, false);
  Serial.println("   ✓ Display initialisiert");

  // Test: Großes schwarzes Rechteck
  Serial.println("\n3. Zeichne GROSSES schwarzes Rechteck...");
  Serial.println("   Warte 30 Sekunden für vollständigen Update...");

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillRect(50, 50, 300, 200, GxEPD_BLACK);
  } while (display.nextPage());

  Serial.println("   Warte zusätzliche 30 Sekunden...");
  delay(30000);  // Extra lange warten

  Serial.println("   ✓ Fertig!");
  Serial.println("\n>>> SIEHST DU JETZT EIN SCHWARZES RECHTECK? <<<\n");

  // Test 2: Rotes Rechteck
  Serial.println("\n4. Zeichne rotes Rechteck...");
  Serial.println("   Warte 30 Sekunden...");

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillRect(100, 100, 200, 100, GxEPD_RED);
  } while (display.nextPage());

  Serial.println("   Warte zusätzliche 30 Sekunden...");
  delay(30000);

  Serial.println("   ✓ Fertig!");
  Serial.println("\n>>> SIEHST DU JETZT EIN ROTES RECHTECK? <<<\n");

  Serial.println("\n=== TEST ABGESCHLOSSEN ===");
  Serial.println("\nWenn du IMMER NOCH nichts siehst:");
  Serial.println("1. Das Display ist möglicherweise defekt");
  Serial.println("2. Oder es ist ein anderer Controller (nicht UC8176)");
  Serial.println("3. Schau auf die Rückseite des Displays - steht da ein Typ?");
  Serial.println("   (z.B. GDEW042Z15, SSD1608, IL0398, etc.)");
}

void loop() {
  // Nichts
}
