"""Compare atlas pixels/UVs and sampled animation against a loose-texture export."""
import argparse,json
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser();p.add_argument("original",type=Path);p.add_argument("atlas",type=Path);a=p.parse_args()
old=json.loads((a.original/"skeleton.ageskel.json").read_text());new=json.loads((a.atlas/"skeleton.ageskel.json").read_text())
assert old["rest"]==new["rest"] and old["clips"]==new["clips"] and old["skins"]==new["skins"],"Animation or skin changed"
pages=[Image.open(a.atlas/f).convert("RGBA") for f in new["textures"]];regions={}
for before,after in zip(old["attachments"],new["attachments"]):
 source=Image.open(a.original/old["textures"][before["texture"]]).convert("RGBA");page=pages[after["texture"]];origin=[]
 for axis in (0,1):
  uv=before["uv"][axis::2];mapped=after["uv"][axis::2];lo=min(range(len(uv)),key=uv.__getitem__);hi=max(range(len(uv)),key=uv.__getitem__)
  scale=(mapped[hi]-mapped[lo])/(uv[hi]-uv[lo]);assert abs(scale*page.size[axis]-source.size[axis])<.01
  offset=(mapped[lo]-uv[lo]*scale)*page.size[axis];assert abs(offset-round(offset))<.01;origin.append(round(offset))
 x,y=origin;w,h=source.size
 assert page.crop((x,y,x+w,y+h)).tobytes()==source.tobytes(),before["name"]
 for dx,dy in [(-2,-2),(w+1,-2),(-2,h+1),(w+1,h+1),(-1,h//2),(w,h//2)]:
  assert page.getpixel((x+dx,y+dy))==source.getpixel((max(0,min(w-1,dx)),max(0,min(h-1,dy)))),"Extrusion"
 region=(after["texture"],x-2,y-2,x+w+2,y+h+2);regions[before["texture"]]=region
for i,a in regions.items():
 for j,b in regions.items():
  if i<j and a[0]==b[0]:assert a[3]<=b[1] or b[3]<=a[1] or a[4]<=b[2] or b[4]<=a[2],"Overlapping atlas regions"
print("AGESKELETON_ATLAS_PASS pixels uv padding nonoverlap animation skins",len(regions),"sources",len(pages),"pages",[p.size for p in pages])
