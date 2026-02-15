#!/usr/bin/env python3
"""Convert large icon PNGs to smaller 200x200 optimized PNGs for M5Stack Tab 5."""

from PIL import Image
import os

# Input and output directories
input_dir = "assets"
output_dir = "icons"

# Create output directory if it doesn't exist
os.makedirs(output_dir, exist_ok=True)

# Process each icon
for i in range(1, 9):
    input_path = os.path.join(input_dir, f"icon-{i}.png")
    output_path = os.path.join(output_dir, f"icon-{i}.png")
    
    if not os.path.exists(input_path):
        print(f"Warning: {input_path} not found, skipping...")
        continue
    
    print(f"Processing icon-{i}.png...")
    
    # Open and resize image
    img = Image.open(input_path)
    img = img.convert("RGB")  # Ensure RGB mode
    img = img.resize((200, 200), Image.Resampling.LANCZOS)
    
    # Save as optimized PNG
    img.save(output_path, "PNG", optimize=True)
    
    # Show file size
    size = os.path.getsize(output_path)
    print(f"  Saved {output_path} ({size} bytes)")

print("\nDone! Copy the icons folder to your SD card as:")
print("  /M5Stack-Tab-5-Adventure/icons/")
