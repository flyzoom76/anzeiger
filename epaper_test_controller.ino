/*
 * E-Paper Test für verschiedene Controller
 * Probiert verschiedene 4.2" E-Paper Controller
 */

#include <SPI.h>
#include <GxEPD2_3C.h>

// Pin Definitionen (XIAO ESP32-C3)
#define EPD_CS      D7
#define EPD_DC      D6
#define EPD_RST     D5
#define EPD_BUSY    D4
#define EPD_MOSI    D10
#define EPD_SCK     D8

// WÄHLE EINEN CONTROLLER (kommentiere die anderen aus):
//
// OPTION 1: UC8176 Standard (häufigster 4.2" Controller)
// GxEPD2_3C<GxEPD2_420c, GxEPD2_420c::HEIGHT> display(GxEPD2_420c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
//
// OPTION 2: UC8176 Z21 Variante
GxEPD2_3C<GxEPD2_420c_Z21, GxEPD2_420c_Z21::HEIGHT> display(GxEPD2_420c_Z21(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
//
// HINWEIS: SSD1680 gibt es in GxEPD2 nur für kleinere Displays (1.54", 2.13")
// Falls dein Display wirklich SSD1680 hat, ist es wahrscheinlich KEIN 4.2" Display
// oder es braucht einen speziellen Treiber der nicht in GxEPD2 ist.

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n=== E-PAPER CONTROLLER TEST ===\n");
  Serial.println("Hinweis: SSD1680 wird normalerweise NICHT für 4.2\" verwendet!");
  Serial.println("Versuche trotzdem mit UC8176 Treiber...\n");

  // SPI Init
  Serial.println("1. Initialisiere SPI...");
  SPI.begin(EPD_SCK, -1, EPD_MOSI, -1);
  SPI.setFrequency(4000000);
  Serial.println("   ✓ SPI initialisiert");

  // Display Init
  Serial.println("\n2. Initialisiere Display...");
  display.init(115200, true, 20, false);  // Längerer Reset (20ms)
  Serial.println("   ✓ Display initialisiert");

  // Test: Großes schwarzes Rechteck
  Serial.println("\n3. Zeichne GROSSES schwarzes Rechteck...");
  Serial.println("   Wenn du NICHTS siehst:");
  Serial.println("   - Display braucht evtl. speziellen SSD1680 Treiber");
  Serial.println("   - Oder es ist ein anderer Controller");
  Serial.println("   - Schau auf die Rückseite des Displays nach der Typnummer!");

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    // Großes schwarzes Rechteck
    display.fillRect(50, 50, 300, 200, GxEPD_BLACK);
  } while (display.nextPage());

  Serial.println("   ✓ Update gesendet");
  Serial.println("   Warte 15 Sekunden...");
  delay(15000);

  Serial.println("\n>>> SIEHST DU EIN SCHWARZES RECHTECK? <<<");
  Serial.println("\nFalls NEIN:");
  Serial.println("1. Mach ein Foto von der RÜCKSEITE des Displays");
  Serial.println("2. Suche nach Aufschriften wie:");
  Serial.println("   - GDEW042Z15");
  Serial.println("   - UC8176");
  Serial.println("   - SSD1608");
  Serial.println("   - IL0398");
  Serial.println("   - Oder andere Typ-Nummern");
  Serial.println("\n3. Teile mir die GENAUE Typnummer mit!");
}

void loop() {
  // Nichts
}
