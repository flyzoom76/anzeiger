#!/usr/bin/env python3
from PIL import Image
import sys

def image_to_bitmap(image_path, output_name, width, height, threshold=128):
    """Konvertiere Bild zu Arduino Bitmap Array"""

    # Lade und skaliere Bild
    img = Image.open(image_path)
    img = img.convert('L')  # Graustufen
    img = img.resize((width, height), Image.Resampling.LANCZOS)

    # Konvertiere zu Schwarz/Weiß mit Threshold
    pixels = img.load()

    # Erstelle Bitmap-Array
    bitmap_data = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for bit in range(8):
                if x + bit < width:
                    pixel = pixels[x + bit, y]
                    # Schwarz wenn Pixel dunkel ist (unter Threshold)
                    if pixel < threshold:
                        byte |= (0x80 >> bit)
            bitmap_data.append(byte)

    # Generiere C-Code
    print(f"// Logo: {width}x{height} Pixel")
    print(f"const unsigned char {output_name}[] PROGMEM = {{")

    for i in range(0, len(bitmap_data), 16):
        line = bitmap_data[i:i+16]
        hex_values = ", ".join(f"0x{b:02x}" for b in line)
        if i + 16 < len(bitmap_data):
            print(f"  {hex_values},")
        else:
            print(f"  {hex_values}")

    print("};")
    print()
    print(f"// Verwendung:")
    print(f"// display.drawBitmap(x, y, {output_name}, {width}, {height}, GxEPD_BLACK);")

if __name__ == "__main__":
    # Konvertiere das hochgeladene Bild
    image_to_bitmap("/tmp/logo.png", "logo_oevgo", 200, 120, threshold=128)
