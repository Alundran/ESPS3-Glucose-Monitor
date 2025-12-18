#!/usr/bin/env python3
"""
Convert PNG image to C array for LVGL
"""
from PIL import Image
import sys

def png_to_c_array(png_path, output_path, var_name):
    # Open image
    img = Image.open(png_path)
    
    # Convert to RGB565 format
    img = img.convert('RGB')
    width, height = img.size
    
    # Create C file
    with open(output_path, 'w') as f:
        f.write('#include "lvgl.h"\n\n')
        f.write(f'// Image: {png_path}\n')
        f.write(f'// Size: {width}x{height}\n\n')
        
        # Write pixel data as RGB565
        f.write(f'const lv_img_dsc_t {var_name} = {{\n')
        f.write('  .header = {\n')
        f.write(f'    .w = {width},\n')
        f.write(f'    .h = {height},\n')
        f.write('    .cf = LV_COLOR_FORMAT_RGB565,\n')
        f.write('  },\n')
        f.write(f'  .data_size = {width * height * 2},\n')
        f.write('  .data = (const uint8_t[]){\n')
        
        pixels = img.load()
        pixel_count = 0
        
        for y in range(height):
            f.write('    ')
            for x in range(width):
                r, g, b = pixels[x, y]
                # Convert to RGB565
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                # Write as little-endian bytes
                f.write(f'0x{rgb565 & 0xFF:02X}, 0x{(rgb565 >> 8) & 0xFF:02X}, ')
                pixel_count += 1
                if pixel_count % 8 == 0:
                    f.write('\n    ')
            f.write('\n')
        
        f.write('  },\n')
        f.write('};\n')
    
    print(f'Converted {png_path} to {output_path}')
    print(f'Image size: {width}x{height}')

if __name__ == '__main__':
    png_to_c_array(
        'c:/CodeProjects/glucose-s3-idf/supreme_glucose_splash.png',
        'c:/CodeProjects/glucose-s3-idf/main/splash_image.c',
        'splash_image'
    )
