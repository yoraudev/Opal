import glob
import math
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "res")
S = 1024


def find_browser():
    candidates = glob.glob(r"C:\Program Files (x86)\Microsoft\EdgeCore\*\msedge.exe")
    candidates += [
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def render_svg():
    browser = find_browser()
    if not browser:
        sys.exit("No Edge/Chrome found to render logo.svg")
    svg = os.path.join(RES, "logo.svg")
    head = open(svg, "rb").read(2048).decode("utf-8", "ignore")
    import re
    w = int(float(re.search(r'width="([\d.]+)"', head).group(1)))
    h = int(float(re.search(r'height="([\d.]+)"', head).group(1)))
    tmp = tempfile.mkdtemp(prefix="opal-icon-")
    out = os.path.join(tmp, "logo.png")
    subprocess.run([browser, "--headless", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=2",
                    "--default-background-color=00000000", f"--window-size={w},{h}",
                    f"--user-data-dir={os.path.join(tmp, 'profile')}", f"--screenshot={out}",
                    "file:///" + svg.replace("\\", "/")], check=True, capture_output=True)
    im = Image.open(out).convert("RGBA")
    im = im.crop(im.getbbox())
    im.save(os.path.join(RES, "logo.png"))
    print("rendered", im.size)


def squircle_mask(size, inset, n=5.0):
    m = Image.new("L", (size, size), 0)
    px = m.load()
    c = size / 2
    r = size / 2 - inset
    for y in range(size):
        dy = abs((y + 0.5 - c) / r)
        if dy > 1:
            continue
        dx = (1 - dy ** n) ** (1 / n)
        for x in range(max(0, int(round(c - dx * r))), min(size, int(round(c + dx * r)))):
            px[x, y] = 255
    return m


def radial(size, center, radius, inner, outer):
    img = Image.new("RGB", (size, size))
    px = img.load()
    cx, cy = center
    for y in range(size):
        for x in range(size):
            t = min(1.0, math.hypot(x - cx, y - cy) / radius)
            t = t * t * (3 - 2 * t)
            px[x, y] = tuple(int(inner[i] + (outer[i] - inner[i]) * t) for i in range(3))
    return img


def build_tile(logo, box_frac, small):
    inset = int(S * 0.06)
    mask = squircle_mask(S, inset)

    tile = radial(S, (S * 0.5, S * 0.38), S * 0.78, (34, 38, 82), (6, 7, 16)).convert("RGBA")

    glow = Image.new("L", (S, S), 0)
    ImageDraw.Draw(glow).ellipse((int(S * 0.22), int(S * 0.2), int(S * 0.78), int(S * 0.8)), fill=120)
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.1))
    tile = Image.composite(Image.new("RGBA", (S, S), (70, 80, 255, 255)), tile, glow.point(lambda v: v // 2))

    box = S * box_frac
    k = min(box / logo.width, box / logo.height)
    lw, lh = int(logo.width * k), int(logo.height * k)
    mark = logo.resize((lw, lh), Image.LANCZOS)
    if small:
        alpha = mark.getchannel("A")
        rgb = ImageEnhance.Contrast(ImageEnhance.Brightness(mark.convert("RGB")).enhance(1.45)).enhance(1.25)
        mark = Image.merge("RGBA", (*rgb.split(), alpha.point(lambda v: min(255, int(v * 1.3)))))
    tile.alpha_composite(mark, ((S - lw) // 2, (S - lh) // 2 + int(S * 0.01)))

    icon = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    icon.paste(tile, (0, 0), mask)

    rim = ImageChops.subtract(mask, squircle_mask(S, inset + int(S * 0.006)))
    shade = Image.linear_gradient("L").resize((S, S)).point(lambda v: int(150 * (1 - v / 255) + 25))
    rim = ImageChops.multiply(rim, shade)
    icon.paste(Image.new("RGBA", (S, S), (190, 200, 255, 255)), (0, 0), rim)
    return icon


def build_icon():
    logo = Image.open(os.path.join(RES, "logo.png")).convert("RGBA")
    icon = build_tile(logo, 0.68, False)
    small = build_tile(logo, 0.84, True)

    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    def frame(sz):
        if sz <= 32:
            return small.resize((sz, sz), Image.LANCZOS).filter(ImageFilter.UnsharpMask(radius=0.6, percent=60))
        return icon.resize((sz, sz), Image.LANCZOS)

    frames = [frame(sz) for sz in sizes]
    frames[-1].save(os.path.join(RES, "opal.ico"), format="ICO", sizes=[(s, s) for s in sizes],
                    append_images=frames[:-1])
    icon.resize((512, 512), Image.LANCZOS).save(os.path.join(RES, "opal.png"))
    print("wrote res/opal.ico and res/opal.png")


if __name__ == "__main__":
    if "--render" in sys.argv or not os.path.exists(os.path.join(RES, "logo.png")):
        render_svg()
    build_icon()
