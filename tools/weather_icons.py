#!/usr/bin/env python3
"""Regenerate resources/images/weather_*.png from the official Pebble timeline
weather icons: download the SVGs, render them large and make every visible
pixel solid white, except the dark outlines that separate two filled shapes
(e.g. cloud in front of the sun), which become gaps. Then scale down to
SIZE px. Needs rsvg-convert + Pillow.

Run from the project folder: python3 tools/weather_icons.py
"""
import io
import subprocess
import urllib.request

from PIL import Image, ImageChops, ImageFilter

SIZE = 16
BIG = SIZE * 4
URL = "https://developer.repebble.com/assets/images/guides/timeline/{}.svg"
ICONS = ["TIMELINE_SUN", "PARTLY_CLOUDY", "CLOUDY_DAY", "LIGHT_RAIN", "HEAVY_RAIN",
         "LIGHT_SNOW", "HEAVY_SNOW", "RAINING_AND_SNOWING", "TIMELINE_WEATHER"]

for name in ICONS:
    req = urllib.request.Request(URL.format(name), headers={"User-Agent": "curl/8"})
    svg = urllib.request.urlopen(req).read()
    png = subprocess.run(["rsvg-convert", "-w", str(BIG), "-h", str(BIG)],
                         input=svg, capture_output=True, check=True).stdout
    src = Image.open(io.BytesIO(png)).convert("RGBA")
    shape = src.getchannel("A").point(lambda a: 255 if a > 100 else 0)
    light = Image.eval(src.convert("L"), lambda v: 255 if v > 128 else 0)
    fill = ImageChops.multiply(shape, light)
    # Dark pixels squeezed between filled areas = inner separators
    closed = fill.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.MinFilter(9))
    gaps = ImageChops.subtract(closed, fill)
    mask = ImageChops.subtract(shape, gaps)
    small = mask.resize((SIZE, SIZE), Image.BOX).point(lambda a: 255 if a > 110 else 0)
    out = Image.new("RGBA", (SIZE, SIZE), (255, 255, 255, 0))
    out.putalpha(small)
    out = Image.composite(Image.new("RGBA", out.size, (255, 255, 255, 255)), out, small)
    out.save("resources/images/weather_{}.png".format(name.lower()))
    print("wrote", name)
