"""Generate an original modular outfit sample using the native skin stack.

SVG artwork, rig, weights and motion are authored here; no third-party artwork.
"""
import argparse
import json
import math
from pathlib import Path
from generate_moonwing import Raw, packed, value, vec


def generate(output):
    assets = output / "Assets"
    assets.mkdir(parents=True, exist_ok=True)
    # World-space rest joints; local bone transforms are derived from their parent.
    joints = [
        ("Pelvis", 0, 0, 40, -math.pi/2, 110),
        ("Chest", 1, 0, -70, -math.pi/2, 75),
        ("Head", 2, 0, -145, -math.pi/2, 55),
        ("Left Upper Arm", 2, -58, -80, 1.82, 70),
        ("Left Forearm", 4, -75, -12, math.pi/2, 64),
        ("Right Upper Arm", 2, 58, -80, 1.32, 70),
        ("Right Forearm", 6, 75, -12, math.pi/2, 64),
        ("Left Thigh", 1, -27, 43, math.pi/2, 85),
        ("Left Shin", 8, -27, 128, math.pi/2, 83),
        ("Right Thigh", 1, 27, 43, math.pi/2, 85),
        ("Right Shin", 10, 27, 128, math.pi/2, 83),
    ]
    entities, world = [{}], {0: (0, 0, 0)}
    for i, (name, parent, x, y, angle, length) in enumerate(joints, 1):
        px, py, pa = world[parent]
        dx, dy = x-px, y-py
        entities.append({"name": name, "parent": parent,
                         "position": vec(math.cos(pa)*dx+math.sin(pa)*dy, -math.sin(pa)*dx+math.cos(pa)*dy),
                         "rotation": vec(0, 0, angle-pa), "bone_2d": {"length": length}})
        world[i] = x, y, angle

    skins = {"default": {}}
    slots, slot_ids, resources, library = [], {}, [], []

    def attach(skin, slot, name, bounds, svg, bone, tip=0, z=0):
        left, top, width, height = bounds
        if slot == "Weapon":
            left += 42
            svg = '<g transform="translate(42 0)">'+svg+'</g>'
        asset = name.lower().replace(" ", "_")
        (assets / f"{asset}.svg").write_text(
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width*3}" height="{height*3}" viewBox="{left} {top} {width} {height}">'
            '<g stroke="#253348" stroke-width="3" stroke-linejoin="round" stroke-linecap="round">' + svg + '</g></svg>', encoding="utf-8")
        rid = str(len(resources)+1)
        resources.append(f'[ext_resource type="Texture2D" path="res://Assets/{asset}.svg" id="{rid}"]')
        points, uv, indices, weights, triangles = [], [], [], [], []
        cols, rows = (3, 7) if tip else (2, 2)
        for row in range(rows):
            for col in range(cols):
                u, v = col/(cols-1), row/(rows-1)
                x, y = left+u*width, top+v*height
                points.extend([x, y]); uv.extend([u, v])
                blend = max(0, min(1, (y-(world[tip][1]-28))/56)) if tip else 0
                indices.extend([bone-1, tip-1 if tip else 0, 0, 0])
                weights.extend([1-blend, blend, 0, 0])
        for row in range(rows-1):
            for col in range(cols-1):
                n = row*cols+col
                triangles.extend([n, n+1, n+cols, n+1, n+cols+1, n+cols])
        eid = len(entities); library.append(eid)
        entities.append({"name": name, "parent": 0, "polygon_2d": {
            "polygon": packed("PackedVector2Array", points), "uv": packed("PackedVector2Array", uv),
            "triangles": packed("PackedInt32Array", triangles), "texture": Raw(f'ExtResource("{rid}")'),
            "skeleton": 0, "bones": packed("PackedInt32Array", indices), "weights": packed("PackedFloat32Array", weights), "z_index": z}})
        if slot not in slot_ids:
            slot_ids[slot] = len(slots)
            slots.append({"name": slot, "bone": bone-1, "attachment": "appearance", "z_index": z})
        skins.setdefault(skin, {}).setdefault(slot, {})["appearance"] = eid

    # A warm-toned explorer, with clothing silhouettes that visibly differ.
    attach("default", "Head", "Explorer Face", (-56, -231, 112, 114),
           '<path d="M-17-149V-125H17V-149" fill="#d99a72"/>'
           '<ellipse cx="0" cy="-178" rx="43" ry="45" fill="#f5c49a"/>'
           '<path d="M-42-177Q-54-223-7-226Q42-230 45-186L28-198L20-180L4-200L-14-186L-22-201Z" fill="#304b70"/>'
           '<path d="M-27-179L-12-181M12-181L27-179" fill="none"/>'
           '<ellipse cx="-18" cy="-174" rx="3" ry="5" fill="#253348"/><ellipse cx="18" cy="-174" rx="3" ry="5" fill="#253348"/>'
           '<path d="M-10-152Q0-146 10-152" fill="none"/>', 3, z=30)
    tops = [("default", "Undershirt", "#d8e1e4", "#99adb7"),
            ("Tops/Scout Tunic", "Scout", "#428979", "#bcd99b"),
            ("Tops/Travel Jacket", "Jacket", "#b6464e", "#efd091"),
            ("Tops/Steel Armor", "Armor", "#8eabc0", "#e0ebee")]
    for skin, name, fill, trim in tops:
        extra = '<path d="M0-104V39M-38-19H-9M9-19H38" fill="none" stroke="'+trim+'" stroke-width="5"/>'
        if name == "Armor":
            extra = '<path d="M-45-77L0-98L45-77L34-19L0 7L-34-19Z" fill="'+trim+'"/><path d="M0-92V2M-31 18H31M-26 30H26" fill="none"/>'
        attach(skin, "Torso", name+" Body", (-70, -124, 140, 181),
               f'<path d="M-20-118L-55-99L-47-51L-37 42Q0 57 37 42L47-51L55-99L20-118L0-95Z" fill="{fill}"/>'+extra+
               '<path d="M-38 28H38V43H-38Z" fill="#514249"/><rect x="-9" y="28" width="18" height="15" rx="2" fill="#d7b464"/>', 2, z=12)
        for side, bone, tip in [("Left", 4, 5), ("Right", 6, 7)]:
            flip = '' if side == "Right" else ' transform="scale(-1 1)"'
            arm = f'<g{flip}><path d="M49-89Q62-108 74-81L87-14L93 48Q92 69 73 68L63 54L59-7L42-65Z" fill="#edb98c"/>'
            sleeve_end = -22 if name in ("Scout", "Undershirt") else 43
            arm += f'<path d="M49-96Q67-107 77-80L87 {sleeve_end}L61 {sleeve_end+4}L43-66Z" fill="{fill}"/>'
            if name == "Armor": arm += f'<path d="M44-88Q62-115 81-84L79-62L48-57Z" fill="{trim}"/><path d="M64 4L86 1L88 39L66 42Z" fill="{trim}"/>'
            arm += '</g>'
            attach(skin, side+" Sleeve", name+" "+side+" Sleeve", (-99 if side=="Left" else 34, -116, 65, 193), arm, bone, tip, z=14)
    pants = [("default", "Base", "#59677f", "#30394b"),
             ("Bottoms/Canvas Trousers", "Canvas", "#c3a274", "#715046"),
             ("Bottoms/Ranger Boots", "Ranger", "#506477", "#503c35")]
    for skin, name, fill, boot in pants:
        for side, x, bone, tip in [("Left", -27, 8, 9), ("Right", 27, 10, 11)]:
            boot_top = 143 if name=="Ranger" else 189
            shape = f'<path d="M{x-21} 37H{x+21}L{x+17} 125L{x+15} 211H{x-17}L{x-20} 123Z" fill="{fill}"/>'
            shape += f'<path d="M{x-19} {boot_top}H{x+18}L{x+19} 204L{x+28} 212V226H{x-23}V209Z" fill="{boot}"/>'
            shape += f'<path d="M{x-15} {boot_top+10}H{x+14}M{x-21} 219H{x+25}" stroke="#dbcaa2" fill="none"/>'
            attach(skin, side+" Leg", name+" "+side+" Leg", (x-30, 30, 64, 204), shape, bone, tip, z=8)
    attach("Headwear/Feather Cap", "Headwear", "Feather Cap", (-65, -270, 142, 115),
           '<path d="M-48-211Q-48-249 1-246Q34-244 47-208L66-198L-57-196Z" fill="#477c65"/>'
           '<path d="M15-222Q28-269 54-265L43-225Z" fill="#efce8b"/><path d="M20-219L48-260" fill="none"/>', 3, z=40)
    attach("Headwear/Knight Helm", "Headwear", "Knight Helm", (-61, -268, 122, 152),
           '<path d="M-47-184Q-60-239 0-243Q60-239 47-184L43-142L27-128V-190H-27V-128L-43-142Z" fill="#8eabc0"/>'
           '<path d="M0-244V-206M-43-199H43" stroke="#dbe9ec" stroke-width="6" fill="none"/>'
           '<path d="M-6-246L0-263L6-246" fill="#b6464e"/>', 3, z=40)
    attach("Weapons/Short Sword", "Weapon", "Short Sword", (-160, -122, 85, 231),
           '<path d="M-126 46L-128-80L-113-115L-101-80L-106 46Z" fill="#dcebf0"/>'
           '<path d="M-114-96L-114 40" stroke="#779bb1" fill="none"/>'
           '<path d="M-148 43H-85V56H-148Z" fill="#deb75d"/><path d="M-124 57H-109V92H-124Z" fill="#67413f"/><circle cx="-117" cy="96" r="8" fill="#deb75d"/>', 5, z=24)
    attach("Weapons/Oak Staff", "Weapon", "Oak Staff", (-153, -233, 80, 355),
           '<path d="M-121 112L-121-162Q-154-180-132-211Q-105-241-82-207Q-69-181-93-166" stroke="#956947" stroke-width="12" fill="none"/>'
           '<path d="M-114-203L-97-220L-81-204L-99-184Z" fill="#79dcd0"/><path d="M-133-146L-107-134M-133-132L-107-120" stroke="#dbbc7e" stroke-width="5"/>', 5, z=24)

    clips = []
    for clip, strength, length in [("Idle", .025, 2.4), ("Walk", .14, 1.2)]:
        lines = [f'[sub_resource type="Animation" id="{clip}"]', f'resource_name = "{clip}"', f'length = {length}', 'loop_mode = 1']
        for t, joint in enumerate(joints):
            base = joint[4]-world[joint[1]][2]
            amp = strength*(.3 if t<3 else 1)
            keys = {"times": packed("PackedFloat32Array", [k*length/16 for k in range(17)]),
                    "transitions": packed("PackedFloat32Array", [1]*17), "update": 0,
                    "values": [vec(0, 0, base+math.sin(k*math.tau/16+(0 if t%2 else math.pi))*amp) for k in range(17)]}
            lines += [f'tracks/{t}/type = "value"', f'tracks/{t}/path = NodePath(".:rotation")', f'tracks/{t}/interp = 2', f'tracks/{t}/keys = {value(keys)}']
        clips.append('\n'.join(lines))
    entities[0] = {"name": "Wayfarer", "skeleton_2d": {
        "bones": packed("PackedInt64Array", range(1,len(joints)+1)),
        "bind_poses": [packed("Transform2D", [math.cos(a),math.sin(a),-math.sin(a),math.cos(a),x,y]) for x,y,a in (world[i] for i in range(1,len(joints)+1))],
        "slots": slots, "skins": skins, "skin": "default",
        "active_skins": Raw('PackedStringArray("Tops/Scout Tunic", "Bottoms/Canvas Trousers", "Headwear/Feather Cap", "Weapons/Short Sword")'),
        "attachment_library": packed("PackedInt64Array", library)},
        "animation": {"clip": Raw('SubResource("Idle")'), "state": "Idle", "states": {"Idle": Raw('SubResource("Idle")'), "Walk": Raw('SubResource("Walk")')}, "targets": packed("PackedInt64Array", range(1,len(joints)+1))}}
    text = f'[gd_resource type="ECSScene" load_steps={len(resources)+3} format=3]\n\n'+'\n'.join(resources)+'\n\n'+'\n\n'.join(clips)+'\n\n[resource]\nentities = '+value(entities)+'\n'
    (assets/'Wayfarer.ecsrig.tres').write_text(text,encoding='utf-8')
    (output/'project.godot').write_text('config_version=5\n[application]\nconfig/name="Wayfarer Wardrobe"\nrun/main_loop_type="ECSMainLoop"\n[ecs]\nrun/scene="res://Assets/Wayfarer.ecsrig.tres"\n[rendering]\nrenderer/rendering_method="gl_compatibility"\n',encoding='utf-8')
    (output/'provenance.json').write_text(json.dumps({"name":"Wayfarer", "license":"MIT", "source":"Original SVG paths, rest joints, weights and motion in tools/generate_wardrobe.py", "bones":len(joints),"attachments":len(library),"skins":list(skins),"animations":["Idle","Walk"]},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (output/'LICENSE.txt').write_text((Path(__file__).resolve().parents[1]/'samples/wardrobe/LICENSE.txt').read_text(encoding='utf-8'),encoding='utf-8')
    print(f'Created {output}: {len(joints)} bones, {len(library)} attachments, {len(skins)} skins')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'samples/wardrobe')
    generate(parser.parse_args().output)
