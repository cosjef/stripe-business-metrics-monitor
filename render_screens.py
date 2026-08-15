from PIL import Image, ImageDraw, ImageFont
import os

S = 240
SCALE = 4  # supersample for crisp output
PAD = 16

BG = "#121211"
FG = "#F4F2EC"
MUT = "#8E8C84"
DIM = "#6B6A64"
OFF = "#3A3A37"
GRN = "#5DCAA5"
AMB = "#EF9F27"

MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
MONOB = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

_cache = {}
def f(size, bold=False):
    k = (size, bold)
    if k not in _cache:
        _cache[k] = ImageFont.truetype(MONOB if bold else MONO, int(size * SCALE))
    return _cache[k]

OUT = "/home/claude/screens"
os.makedirs(OUT, exist_ok=True)


def new_screen():
    img = Image.new("RGB", (S * SCALE, S * SCALE), BG)
    return img, ImageDraw.Draw(img)


MAXW = S - 2 * PAD  # 208px usable text column

def text(d, xy, s, size, color=FG, bold=False, anchor="la"):
    w = d.textlength(s, font=f(size, bold)) / SCALE
    if w > MAXW:
        raise ValueError(f"OVERFLOW {w:.0f}px > {MAXW}px : {s!r} @ {size}px")
    d.text((xy[0] * SCALE, xy[1] * SCALE), s, font=f(size, bold), fill=color, anchor=anchor)


def dots(d, active, total=6, y=214):
    gap, r = 17, 4
    w = (total - 1) * gap
    x0 = (S - w) / 2
    for i in range(total):
        cx = x0 + i * gap
        col = FG if i == active else OFF
        d.ellipse([(cx - r) * SCALE, (y - r) * SCALE, (cx + r) * SCALE, (y + r) * SCALE], fill=col)


def save(img, name):
    img = img.resize((S * 2, S * 2), Image.LANCZOS)
    p = os.path.join(OUT, name)
    img.save(p)
    return p


def rotation(label, hero, hero_size, sub, sub_color, idx, hero_color=FG):
    img, d = new_screen()
    text(d, (PAD, PAD), label, 20, MUT)
    text(d, (PAD, 150), hero, hero_size, hero_color, bold=True, anchor="ls")
    text(d, (PAD, 178), sub, 22, sub_color, anchor="ls")
    dots(d, idx)
    return img


save(rotation("MRR", "$6.5k", 60, "+$118 today", GRN, 0), "01-mrr.png")
save(rotation("NEW PAID", "2", 88, "today, $58 MRR", MUT, 1, GRN), "02-new-paid.png")
save(rotation("PAID SUBS", "94", 88, "+7 this month", GRN, 2), "03-paid-subs.png")
save(rotation("TRIALS", "11", 88, "3 end this week", MUT, 3), "04-trials.png")
save(rotation("CONVERSION", "34%", 76, "trial to paid", MUT, 4), "05-conversion.png")
save(rotation("LAST EVENT", "+$29", 52, "new paid, 2m", MUT, 5, GRN), "06-last-event.png")

# State A: stale
img, d = new_screen()
text(d, (PAD, PAD), "MRR", 20, DIM)
text(d, (PAD, 150), "$6.5k", 60, MUT, bold=True, anchor="ls")
text(d, (PAD, 178), "stale | 22 min", 22, AMB, anchor="ls")
text(d, (PAD, 210), "retrying", 18, DIM)
save(img, "07-state-stale.png")

# State B: auth error
img, d = new_screen()
text(d, (PAD, PAD), "NO ACCESS", 20, AMB)
text(d, (PAD, 118), "Stripe key", 30, FG, bold=True, anchor="ls")
text(d, (PAD, 152), "rejected", 30, FG, bold=True, anchor="ls")
text(d, (PAD, 182), "check permissions", 20, MUT, anchor="ls")
text(d, (PAD, 210), "err 401", 18, DIM)
save(img, "08-state-auth-error.png")

# State C: setup
img, d = new_screen()
text(d, (PAD, PAD), "SETUP", 20, MUT)
text(d, (PAD, 118), "Join wifi", 28, FG, bold=True, anchor="ls")
text(d, (PAD, 152), "Setup-4C21", 28, GRN, bold=True, anchor="ls")
text(d, (PAD, 182), "then open browser", 20, MUT, anchor="ls")
text(d, (PAD, 210), "v1.0.3", 18, DIM)
save(img, "09-state-setup.png")

print("\n".join(sorted(os.listdir(OUT))))
