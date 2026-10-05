"""Generate AgeSkeleton's embedded splash from its original application icon."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
factor = 3
image = Image.new("RGBA", (600 * factor, 390 * factor))
icon = Image.open(root / "misc/ageskeleton/icon.png").convert("RGBA")
icon = icon.resize((190 * factor, 190 * factor), Image.Resampling.LANCZOS)
image.alpha_composite(icon, (205 * factor, 12 * factor))
draw = ImageDraw.Draw(image)
font_path = Path("C:/Windows/Fonts/segoeuib.ttf")
if not font_path.exists():
    raise SystemExit("Supply Segoe UI Bold to regenerate the Windows branding asset.")
font = ImageFont.truetype(str(font_path), 44 * factor)
draw.text((300 * factor, 248 * factor), "AgeSkeleton", font=font, fill="#ECEEEE", anchor="mm")
font = ImageFont.truetype(str(font_path), 16 * factor)
draw.text((300 * factor, 295 * factor), "2D SKELETAL ANIMATION", font=font, fill="#A8B9B9", anchor="mm")
image = image.resize((600, 390), Image.Resampling.LANCZOS)
for name in ("splash.png", "splash_editor.png"):
    image.save(root / "main" / name)
print("Updated embedded editor and fallback splash images")
