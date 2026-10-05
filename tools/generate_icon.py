"""Generate the AgeSkeleton vector and multi-resolution app icons (Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
asset = root / "misc/ageskeleton"
asset.mkdir(parents=True, exist_ok=True)
# Two articulated bones and a diamond keyframe. No lettering at small sizes.
svg = '''<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
<rect x="16" y="16" width="480" height="480" rx="108" fill="#171A2B"/>
<path d="M116 362L201 186L238 247Z" fill="#B9A7FF"/>
<path d="M222 224L380 139L329 221Z" fill="#ECE8FF"/>
<circle cx="116" cy="362" r="27" fill="#171A2B" stroke="#B9A7FF" stroke-width="16"/>
<circle cx="222" cy="224" r="32" fill="#171A2B" stroke="#B9A7FF" stroke-width="17"/>
<circle cx="380" cy="139" r="22" fill="#171A2B" stroke="#ECE8FF" stroke-width="14"/>
<path d="M324 365H422" stroke="#676382" stroke-width="12" stroke-linecap="round"/>
<path d="M373 328L410 365L373 402L336 365Z" fill="#FFBA77"/>
</svg>'''
(asset / "icon.svg").write_text(svg + "\n", encoding="utf-8")
scale = 4
im = Image.new("RGBA", (512*scale, 512*scale))
d = ImageDraw.Draw(im)
def box(values): return tuple(int(v*scale) for v in values)
def poly(points, color): d.polygon([box(p) for p in points], fill=color)
d.rounded_rectangle(box((16,16,496,496)), radius=108*scale, fill="#171A2B")
poly([(116,362),(201,186),(238,247)], "#B9A7FF")
poly([(222,224),(380,139),(329,221)], "#ECE8FF")
for x,y,r,w,color in [(116,362,27,16,"#B9A7FF"),(222,224,32,17,"#B9A7FF"),(380,139,22,14,"#ECE8FF")]:
 d.ellipse(box((x-r-w/2,y-r-w/2,x+r+w/2,y+r+w/2)), fill=color)
 d.ellipse(box((x-r+w/2,y-r+w/2,x+r-w/2,y+r-w/2)), fill="#171A2B")
d.line([box((324,365)),box((422,365))],fill="#676382",width=12*scale)
poly([(373,328),(410,365),(373,402),(336,365)],"#FFBA77")
im = im.resize((512,512), Image.Resampling.LANCZOS)
im.save(asset / "icon.png")
im.save(root / "main/app_icon.png")
im.save(root / "platform/windows/godot.ico", sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print("AgeSkeleton SVG, PNG and Windows ICO generated")
