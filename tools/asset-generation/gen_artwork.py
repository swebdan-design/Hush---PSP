#!/usr/bin/env python3
"""HUSH release artwork, generated programmatically (PIL + NumPy).

A small industrial flight craft hovering in a vast rain-soaked canyon of
towers that dissolve into fog. Everything is drawn from code: no image
generation models, no photographs, no third-party artwork. Typefaces: Inter
(SIL OFL 1.1).

usage: python3 tools/asset-generation/gen_artwork.py
outputs: artwork/HUSH_Cover.png (3000x3000), artwork/HUSH_Cover_No_Text.png,
         artwork/HUSH_Logo.png, artwork/HUSH_Wallpaper.png (1920x1080),
         artwork/promotional/HUSH_Banner.png (1500x500),
         artwork/psp/ICON0.PNG (144x80), artwork/psp/PIC1.PNG (480x272)
"""
import os, math, random
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
ART = os.path.join(ROOT, 'artwork')
THIN = '/usr/share/fonts/opentype/inter/InterDisplay-ExtraLight.otf'
LIGHT = '/usr/share/fonts/opentype/inter/Inter-Light.otf'

CHARCOAL = np.array([18, 20, 24], float)
STEEL = np.array([34, 52, 72], float)
VIOLET = np.array([44, 34, 66], float)
CYAN = np.array([110, 190, 205], float)
AMBER = np.array([255, 176, 84], float)


def sky(w, h):
    y = np.linspace(0, 1, h)[:, None, None]
    top = np.array([8, 9, 14], float)
    mid = VIOLET * 0.7 + STEEL * 0.3
    hor = STEEL * 1.2 + np.array([10, 14, 18], float)
    img = np.where(y < 0.55, top + (mid - top) * (y / 0.55), mid + (hor - mid) * ((y - 0.55) / 0.45))
    return np.repeat(img, w, axis=1)


def scene(W, H, seed=2146, craft=True):
    rnd = random.Random(seed)
    img = Image.fromarray(np.clip(sky(W, H), 0, 255).astype(np.uint8), 'RGB').convert('RGBA')
    vx, vy = W * 0.5, H * 0.46            # vanishing point
    fog = tuple(int(v) for v in (STEEL * 1.25 + np.array([8, 12, 16])))
    # towers in depth layers: far (faint, many) -> near (dark, few, huge)
    layers = 9
    for L in range(layers):
        depth = 1.0 - L / (layers - 1)    # 1 = far
        layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        n = int(12 + 30 * depth)
        scale = 0.12 + 0.88 * (1 - depth) ** 1.6
        for i in range(n):
            side = -1 if i % 2 == 0 else 1
            off = (0.08 + rnd.random() * 0.95) * side
            cx = vx + off * W * (0.25 + 1.2 * scale)
            tw = W * (0.02 + 0.07 * rnd.random()) * (0.4 + 2.6 * scale)
            top = vy - H * (0.08 + 0.42 * rnd.random()) * (0.45 + 1.15 * scale)
            bot = H * 1.05
            base = CHARCOAL * (0.6 + 0.6 * depth) + STEEL * 0.25 * depth
            col = base * (1 - 0.75 * depth) + np.array(fog) * 0.75 * depth
            c = tuple(int(v) for v in col) + (255,)
            d.rectangle([cx - tw / 2, top, cx + tw / 2, bot], fill=c)
            # setback crown
            if rnd.random() < 0.5:
                d.rectangle([cx - tw * 0.3, top - tw * 0.6, cx + tw * 0.3, top], fill=c)
            # windows: small warm/cool cells, sparse with depth
            cell = max(1.0, W / 900.0 * (0.25 + 1.6 * scale))
            rows = int((bot - top) / (cell * 2.6))
            cols = max(1, int((tw - cell) / (cell * 2.0)))
            for r in range(rows):
                y = top + cell * 1.4 + r * cell * 2.6
                floor = rnd.random()
                if floor < 0.45:
                    continue
                p_lit = (0.5 if floor > 0.85 else 0.12) * (1 - depth * 0.5)
                for k in range(cols):
                    if rnd.random() > p_lit:
                        continue
                    x = cx - tw / 2 + cell * 0.8 + k * cell * 2.0
                    warm = rnd.random() < 0.6
                    wc = AMBER * 0.75 if warm else CYAN * 0.8
                    wc = wc * (1 - 0.7 * depth) + np.array(fog) * 0.7 * depth
                    d.rectangle([x, y, x + cell * 1.2, y + cell * 1.3], fill=tuple(int(v) for v in wc) + (255,))
            # aviation light
            if rnd.random() < 0.6:
                r0 = max(1.5, 5 * scale * W / 3000)
                d.ellipse([cx - r0, top - r0 * 3, cx + r0, top - r0], fill=(255, 60, 50, int(255 * (1 - 0.5 * depth))))
        img = Image.alpha_composite(img, layer)
        # fog veil between layers
        veil = Image.new('RGBA', (W, H), fog + (int(26 * depth),))
        img = Image.alpha_composite(img, veil)
    # glow for lights
    glow = img.filter(ImageFilter.GaussianBlur(W / 300))
    img = Image.blend(img, glow, 0.35)
    # low ground haze
    haze = np.zeros((H, W, 4), np.uint8)
    yy = np.linspace(0, 1, H)[:, None]
    haze[..., :3] = fog
    haze[..., 3] = np.clip((yy - 0.55) / 0.45, 0, 1) ** 1.5 * 150
    img = Image.alpha_composite(img, Image.fromarray(haze, 'RGBA'))
    if craft:
        img = Image.alpha_composite(img, moth_layer(W, H, vx, vy + H * 0.08, W * 0.038))
    img = Image.alpha_composite(img, rain_layer(W, H, rnd))
    # vignette
    vg = np.zeros((H, W, 4), np.uint8)
    xx, yy2 = np.meshgrid(np.linspace(-1, 1, W), np.linspace(-1, 1, H))
    vg[..., 3] = np.clip((xx ** 2 + yy2 ** 2 - 0.5) * 120, 0, 170)
    img = Image.alpha_composite(img, Image.fromarray(vg, 'RGBA'))
    return img


def moth_layer(W, H, cx, cy, s):
    """The Moth seen from slightly behind: body, four ducted nacelles, amber and cyan running lights."""
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    body = (26, 28, 34, 255)
    rim = (70, 80, 92, 255)
    # thrust downwash glow
    gl = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(gl)
    for sx in (-1, 1):
        for sz in (-0.55, 0.55):
            x = cx + sx * s * 1.05
            y = cy + sz * s * 0.35 + s * 0.25
            gd.ellipse([x - s * 0.5, y - s * 0.12, x + s * 0.5, y + s * 0.4], fill=(255, 170, 90, 90))
    gl = gl.filter(ImageFilter.GaussianBlur(s * 0.25))
    layer = Image.alpha_composite(layer, gl)
    d = ImageDraw.Draw(layer)
    # arms
    for sx in (-1, 1):
        d.polygon([(cx, cy - s * 0.05), (cx + sx * s * 1.05, cy - s * 0.22), (cx + sx * s * 1.05, cy - s * 0.12), (cx, cy + s * 0.06)], fill=body)
        d.polygon([(cx, cy + s * 0.05), (cx + sx * s * 1.05, cy + s * 0.18), (cx + sx * s * 1.05, cy + s * 0.28), (cx, cy + s * 0.16)], fill=body)
    # fuselage
    d.ellipse([cx - s * 0.42, cy - s * 0.28, cx + s * 0.42, cy + s * 0.3], fill=body, outline=rim, width=max(1, int(s * 0.03)))
    d.rectangle([cx - s * 0.05, cy - s * 0.75, cx + s * 0.05, cy - s * 0.2], fill=body)     # tail fin
    d.polygon([(cx - s * 0.22, cy - s * 0.15), (cx + s * 0.22, cy - s * 0.15), (cx + s * 0.14, cy + s * 0.05), (cx - s * 0.14, cy + s * 0.05)], fill=(30, 70, 82, 255))
    # nacelles
    for sx in (-1, 1):
        for sz, yo in ((-1, -0.17), (1, 0.23)):
            x = cx + sx * s * 1.08
            y = cy + yo * s
            r = s * (0.33 if sz > 0 else 0.28)
            d.ellipse([x - r, y - r * 0.42, x + r, y + r * 0.42], fill=body, outline=rim, width=max(1, int(s * 0.025)))
            d.ellipse([x - r * 0.7, y - r * 0.25, x + r * 0.7, y + r * 0.25], fill=(255, 168, 80, 150))
    # running lights
    for (x, y, c) in ((cx - s * 1.42, cy, (255, 60, 50)), (cx + s * 1.42, cy, (90, 255, 160)), (cx, cy - s * 0.8, (240, 240, 255)), (cx, cy + s * 0.32, AMBER)):
        rr = s * 0.05
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=tuple(int(v) for v in c) + (255,))
    halo = layer.filter(ImageFilter.GaussianBlur(s * 0.08))
    return Image.alpha_composite(halo, layer)


def rain_layer(W, H, rnd):
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    n = int(W * H / 2600)
    for _ in range(n):
        x = rnd.random() * W
        y = rnd.random() * H
        l = H * (0.01 + 0.03 * rnd.random())
        a = int(20 + 50 * rnd.random())
        d.line([(x, y), (x - l * 0.12, y + l)], fill=(170, 190, 215, a), width=max(1, W // 1500))
    return layer


def title(img, size, y, credit=True):
    W, H = img.size
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(THIN, size)
    letters = 'HUSH'
    track = size * 0.42
    widths = [f.getlength(c) for c in letters]
    total = sum(widths) + track * 3
    x = (W - total) / 2
    for c, w in zip(letters, widths):
        d.text((x, y), c, font=f, fill=(232, 236, 244, 255))
        x += w + track
    d.line([(W * 0.3, y + size * 1.18), (W * 0.7, y + size * 1.18)], fill=(232, 236, 244, 90), width=max(1, size // 80))
    if credit:
        fc = ImageFont.truetype(LIGHT, int(size * 0.2))
        t = 'b y   S y n t h   C e n t r i c'
        d.text(((W - fc.getlength(t)) / 2, y + size * 1.32), t, font=fc, fill=(196, 204, 218, 255))


def main():
    os.makedirs(os.path.join(ART, 'promotional'), exist_ok=True)
    os.makedirs(os.path.join(ART, 'psp'), exist_ok=True)
    cover = scene(3000, 3000)
    cover.convert('RGB').save(os.path.join(ART, 'HUSH_Cover_No_Text.png'), optimize=True)
    c2 = cover.copy()
    title(c2, 300, 360)
    c2.convert('RGB').save(os.path.join(ART, 'HUSH_Cover.png'), optimize=True)
    logo = Image.new('RGBA', (2000, 700), (0, 0, 0, 0))
    title(logo, 300, 120)
    logo.save(os.path.join(ART, 'HUSH_Logo.png'))
    wall = scene(1920, 1080, seed=77)
    title(wall, 120, 110)
    wall.convert('RGB').save(os.path.join(ART, 'HUSH_Wallpaper.png'), optimize=True)
    ban = scene(1500, 500, seed=31)
    d = ImageDraw.Draw(ban)
    title(ban, 90, 70)
    fs = ImageFont.truetype(LIGHT, 22)
    t = 'A GENERATIVE CYBERPUNK FLIGHT EXPERIENCE  -  PLAYSTATION PORTABLE'
    d.text(((1500 - fs.getlength(t)) / 2, 420), t, font=fs, fill=(170, 182, 196, 255))
    ban.convert('RGB').save(os.path.join(ART, 'promotional', 'HUSH_Banner.png'), optimize=True)
    pic1 = scene(480, 272, seed=12)
    pic1.convert('RGB').save(os.path.join(ART, 'psp', 'PIC1.PNG'))
    icon = scene(144, 80, seed=5, craft=False)
    title(icon, 30, 14, credit=False)
    icon.convert('RGB').save(os.path.join(ART, 'psp', 'ICON0.PNG'))
    print('artwork written to', ART)


if __name__ == '__main__':
    main()
