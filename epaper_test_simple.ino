/*
 * Einfacher E-Paper Hardware Test
 * Zeichnet nur ein schwarzes Rechteck
 */

#include <SPI.h>
#include <GxEPD2_3C.h>

// Pin Definitionen
#define EPD_CS      D7
#define EPD_DC      D6
#define EPD_RST     D5
#define EPD_BUSY    D4
#define EPD_MOSI    D10
#define EPD_SCK     D8

// Display Objekt - probiere verschiedene Varianten:
//
// OPTION 1: Z21 Variante (Standard, versuche diese zuerst)
GxEPD2_3C<GxEPD2_420c_Z21, GxEPD2_420c_Z21::HEIGHT> display(GxEPD2_420c_Z21(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
//
// OPTION 2: Standard UC8176 (wenn Option 1 nicht funktioniert)
// GxEPD2_3C<GxEPD2_420c, GxEPD2_420c::HEIGHT> display(GxEPD2_420c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n=== E-PAPER HARDWARE TEST ===\n");

  // SPI Init
  Serial.println("1. Initialisiere SPI...");
  SPI.begin(EPD_SCK, -1, EPD_MOSI, -1);
  SPI.setFrequency(4000000);
  Serial.println("   ✓ SPI initialisiert (4MHz)");

  // Display Init
  Serial.println("\n2. Initialisiere Display...");
  display.init(115200, true, 10, false);
  Serial.println("   ✓ Display initialisiert");

  // Test 1: Display komplett weiß
  Serial.println("\n3. Test 1: Display weiß machen...");
  Serial.println("   Das Display sollte jetzt komplett weiß werden.");
  Serial.println("   Wenn du nichts siehst, ist die Verkabelung falsch!");
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());
  Serial.println("   ✓ Fertig");
  Serial.println("   >>> SIEHST DU ETWAS AUF DEM DISPLAY? <<<");
  delay(5000);

  // Test 2: Schwarzes Rechteck
  Serial.println("\n4. Test 2: Zeichne schwarzes Rechteck...");
  Serial.println("   Das Display sollte jetzt ein schwarzes Rechteck zeigen.");
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillRect(50, 50, 300, 200, GxEPD_BLACK);
  } while (display.nextPage());
  Serial.println("   ✓ Fertig");
  Serial.println("   >>> SIEHST DU EIN SCHWARZES RECHTECK? <<<");
  delay(5000);

  // Test 3: Rotes Rechteck
  Serial.println("\n5. Test 3: Zeichne rotes Rechteck...");
  Serial.println("   Das Display sollte jetzt ein rotes Rechteck zeigen.");
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillRect(100, 100, 200, 100, GxEPD_RED);
  } while (display.nextPage());
  Serial.println("   ✓ Fertig");
  Serial.println("   >>> SIEHST DU EIN ROTES RECHTECK? <<<");
  delay(5000);

  // Fertig
  Serial.println("\n\n=== TEST ABGESCHLOSSEN ===");
  Serial.println("\nWenn du NICHTS gesehen hast:");
  Serial.println("1. Prüfe VCC-Verbindung (Display braucht 3.3V!)");
  Serial.println("2. Prüfe GND-Verbindung");
  Serial.println("3. Prüfe alle anderen Pins (CS, DC, RST, BUSY, SDA, SCL)");
  Serial.println("4. USB-Stromversorgung könnte zu schwach sein - probiere externes Netzteil");
  Serial.println("5. Probiere Option 2 (GxEPD2_420c) in Zeile 23");
  Serial.println("\nDrücke RESET um den Test zu wiederholen.");
}

void loop() {
  // Nichts
}
