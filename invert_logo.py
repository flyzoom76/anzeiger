#!/usr/bin/env python3
import re

# Lese die logo_original.h Datei
with open('logo_original.h', 'r') as f:
    content = f.read()

# Finde alle Hex-Werte und invertiere sie
def invert_hex(match):
    hex_val = match.group(1)
    inverted = format(0xFF ^ int(hex_val, 16), '02x')
    return f'0x{inverted}'

# Invertiere alle Hex-Bytes
inverted_content = re.sub(r'0x([0-9a-fA-F]{2})', invert_hex, content)

# Schreibe in neue Datei
with open('logo_inverted.h', 'w') as f:
    f.write(inverted_content)

print("Logo invertiert und in logo_inverted.h gespeichert")
