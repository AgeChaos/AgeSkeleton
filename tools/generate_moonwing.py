"""Build Moonwing's original vector artwork, rig and animation without external assets.

All geometry and motion are defined here, independently of any third-party example.
Run from the source checkout. Requires only Python's standard library.
"""
import argparse
import json
import math
from pathlib import Path


class Raw(str):
    pass


def number(value):
    return f"{value:.6f}".rstrip("0").rstrip(".") or "0"


def value(item):
    if isinstance(item, Raw):
        return str(item)
    if isinstance(item, str):
        return json.dumps(item, ensure_ascii=False)
    if isinstance(item, dict):
        return "{\n" + ",\n".join(f"{value(k)}: {value(v)}" for k, v in item.items()) + "\n}"
    if isinstance(item, list):
        return "[" + ", ".join(value(v) for v in item) + "]"
    return number(item)


def packed(kind, values):
    return Raw(kind + "(" + ", ".join(number(v) for v in values) + ")")


def vec(x=0, y=0, z=0):
    return packed("Vector3", [x, y, z])


def generate(output):
    assets = output / "Assets"
    assets.mkdir(parents=True, exist_ok=True)
    # A slender lunar glider: long swept fins, amber sail wings, smooth violet body.
    # Paths are authored in local joint coordinates, including full hidden overlaps.
    art = {
        "body": ((-102, -56, 212, 112), '''
<path d="M-95 6C-89-35-47-52 8-40C44-33 73-35 96-12C116 9 73 35 36 36C-13 55-69 45-95 6Z" fill="url(#skin)"/>
<path d="M-83 13C-40 44 29 24 92-5C89 27 31 48-20 43C-53 44-73 32-83 13Z" fill="#adbce9"/>
<path d="M-74-19Q-49-53-26-32L-11-54L5-34L22-48L38-30" fill="#decaff"/>
<path d="M-55-12Q-10-30 47-16" fill="none" stroke="#c8baff" stroke-width="5"/>
<path d="M-28 27L-23 39M-8 22L-1 39M14 15L23 33M35 8L43 25" stroke="#7885b5" stroke-width="3"/>
<circle cx="-56" cy="4" r="4" fill="#fcd398"/><circle cx="-40" cy="-3" r="3" fill="#fcd398"/>'''),
        "head": ((-18, -73, 161, 122), '''
<path d="M5-11L-12-67Q20-69 30-32L48-35L83-72Q99-47 87-25C109-23 121-7 129 3L139 8Q136 29 113 30L67 26Q47 50 17 31Q-3 18 5-11Z" fill="url(#skin)"/>
<path d="M17-34L2-60Q18-56 23-35M61-36L85-59L78-31" fill="#ffcc91"/>
<path d="M67 12Q105 25 136 10Q129 34 103 31L61 24Z" fill="#c7d1f2"/>
<path d="M29-11Q48-25 69-8Q46 13 29-11Z" fill="#181c36"/>
<ellipse cx="50" cy="-9" rx="10" ry="11" fill="#ffd795"/><ellipse cx="53" cy="-10" rx="3" ry="8" fill="#28203e"/>
<circle cx="48" cy="-13" r="3" fill="#fff5d8"/><path d="M109 0L116 2" stroke="#2b254b" stroke-width="4" stroke-linecap="round"/>
<path d="M4 12L-12 35L12 28M13 23L-2 45L26 31" fill="#8c7bd0"/>
<path d="M103 30L107 37L112 31" fill="#fff0d6"/>'''),
        "tail": ((-12, -32, 151, 65), '''
<path d="M-6-22Q56-25 130-2Q66 24-6 26Z" fill="url(#skin)"/>
<path d="M2 13Q56 13 125 0Q65 32 0 24Z" fill="#a6b5df"/>
<path d="M29-20L47-32L51-17M68-13L83-26L87-9" fill="#dac4ff"/>'''),
        "tail_tip": ((-10, -52, 150, 107), '''
<path d="M0-12Q45-12 80 0Q52 20 0 16Z" fill="url(#skin)"/>
<path d="M48 0Q80-13 114-48L98-7L137 0L97 12L113 48Q80 19 48 0Z" fill="#968bdf"/>
<path d="M67 0L107-30L92 0L107 29Z" fill="#ffca8b"/>
<path d="M48 0H129" stroke="#ddd3ff" stroke-width="3"/>'''),
        "wing": ((-18, -74, 216, 166), '''
<path d="M0 0Q64-73 181-68L156-38L194-18Q131 11 174 39Q99 31 100 81Q40 37 0 0Z" fill="url(#sail)" stroke="#9b7cc9" stroke-width="4"/>
<path d="M0 0Q52-20 181-68M0 0Q64 5 194-18M0 0Q65 22 174 39M0 0L100 81" fill="none" stroke="#8271bc" stroke-width="5" stroke-linecap="round"/>
<path d="M8-4Q71-58 164-63" fill="none" stroke="#ddd3ff" stroke-width="7" stroke-linecap="round"/>
<path d="M51-18L61-33M94-35L102-44M137-52L143-56" stroke="#fff0ce" stroke-width="3"/>
<circle cx="1" cy="0" r="12" fill="#8b82ce"/>'''),
        "leg": ((-17, -24, 110, 64), '''
<path d="M-4-18Q24-27 47-5L65 0L86-4L87 9L68 16L56 27L35 20L21 8L-6 13Z" fill="url(#skin)"/>
<path d="M34 12L56 19L83 5L74 22L59 32L40 26Z" fill="#c0c9eb"/>
<path d="M71 15L88 11L82 20M62 24L77 22L70 31" fill="#fff0d6"/>'''),
    }
    for name, (bounds, paths) in art.items():
        x, y, width, height = bounds
        svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width*3}" height="{height*3}" viewBox="{x} {y} {width} {height}">
<defs><linearGradient id="skin" x1="0" y1="0" x2="0.2" y2="1"><stop stop-color="#a79ce9"/><stop offset="1" stop-color="#5b579c"/></linearGradient>
<linearGradient id="sail" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#ffe3ab"/><stop offset="0.55" stop-color="#e9b192"/><stop offset="1" stop-color="#bd8bca"/></linearGradient></defs>
<g stroke-linejoin="round" stroke-linecap="round">{paths}</g></svg>\n'''
        (assets / f"{name}.svg").write_text(svg, encoding="utf-8")

    bones = [
        ("Core", 0, 0, 0, 0, 90),
        ("Neck", 1, 75, -18, -.12, 85),
        ("Tail Base", 1, -84, 9, 3.0, 112),
        ("Tail Fin", 3, 112, 0, -.30, 105),
        ("Far Sail", 1, -16, -24, -1.05, 87),
        ("Far Wrist", 5, 87, 0, 0, 96),
        ("Near Sail", 1, -5, -24, -2.05, 87),
        ("Near Wrist", 7, 87, 0, 0, 96),
        ("Far Foreleg", 1, 49, 20, .78, 68),
        ("Far Hindleg", 1, -56, 20, 1.10, 68),
        ("Near Hindleg", 1, -56, 29, 1.52, 68),
        ("Near Foreleg", 1, 51, 22, 1.14, 68),
    ]
    entities = [{}]
    world = {0: (0, 0, 0)}
    for i, (name, parent, x, y, angle, length) in enumerate(bones, 1):
        px, py, pa = world[parent]
        world[i] = (px + math.cos(pa)*x - math.sin(pa)*y, py + math.sin(pa)*x + math.cos(pa)*y, pa+angle)
        entities.append({"name": name, "parent": parent, "position": vec(x, y), "rotation": vec(0, 0, angle), "bone_2d": {"length": length}})
    layers = [("Far Wing", "wing", 5, 6, .66), ("Far Back Paw", "leg", 10, 0, .64), ("Far Front Paw", "leg", 9, 0, .64),
              ("Tail", "tail", 3, 0, 1), ("Tail Fan", "tail_tip", 4, 0, 1), ("Body", "body", 1, 0, 1),
              ("Near Back Paw", "leg", 11, 0, 1), ("Head", "head", 2, 0, 1), ("Near Front Paw", "leg", 12, 0, 1),
              ("Near Wing", "wing", 7, 8, 1)]
    ext = {name: str(i+1) for i, name in enumerate(art)}
    slots, skin = [], {}
    for z, (name, asset, bone, tip, tint) in enumerate(layers):
        left, top, width, height = art[asset][0]
        bx, by, angle = world[bone]
        points, uvs, indices, weights, triangles = [], [], [], [], []
        columns = 5 if tip else 2
        rows = 4 if tip else 2
        for row in range(rows):
            for col in range(columns):
                u, v = col/(columns-1), row/(rows-1)
                x, y = left+u*width, top+v*height
                points.extend([bx+math.cos(angle)*x-math.sin(angle)*y, by+math.sin(angle)*x+math.cos(angle)*y])
                uvs.extend([u, v])
                blend = max(0, min(1, (x-52)/110)) if tip else 0
                indices.extend([bone-1, tip-1 if tip else 0, 0, 0])
                weights.extend([1-blend, blend, 0, 0])
        for row in range(rows-1):
            for col in range(columns-1):
                n = row*columns+col
                triangles.extend([n, n+1, n+columns, n+1, n+columns+1, n+columns])
        index = len(entities)
        entities.append({"name": name, "parent": 0, "polygon_2d": {"polygon": packed("PackedVector2Array", points), "uv": packed("PackedVector2Array", uvs),
                         "triangles": packed("PackedInt32Array", triangles), "texture": Raw(f'ExtResource("{ext[asset]}")'),
                         "skeleton": 0, "bones": packed("PackedInt32Array", indices), "weights": packed("PackedFloat32Array", weights),
                         "color": Raw(f"Color({tint}, {tint}, {tint}, 1)"), "z_index": z}})
        slots.append({"name": name, "bone": bone-1, "attachment": name, "z_index": z})
        skin[name] = {name: index}

    clips = []
    targets = list(range(1, len(bones)+1)) + [1]
    for clip_id, length, strength in [("Soar", 2.4, 1), ("Glide", 3.2, .28)]:
        lines = [f'[sub_resource type="Animation" id="{clip_id}"]', f'resource_name = "{clip_id}"', f'length = {length}', 'loop_mode = 1']
        for track in range(len(targets)):
            field = "rotation" if track < len(bones) else "position"
            samples = []
            for k in range(9):
                phase = math.tau*k/8
                if field == "position":
                    samples.append(vec(0, math.sin(phase)*9*strength))
                else:
                    base = bones[track][4]
                    amplitude = [.035, .07, .10, .17, .28, .15, .36, .21, .12, .09, .11, .10][track]*strength
                    shift = [.0, .7, 1.3, 2.0, .3, 1.1, 0, .8, .4, .7, 1.0, 1.3][track]
                    samples.append(vec(0, 0, base + math.sin(phase+shift)*amplitude))
            keys = {"times": packed("PackedFloat32Array", [k*length/8 for k in range(9)]), "transitions": packed("PackedFloat32Array", [1]*9), "update": 0, "values": samples}
            lines += [f'tracks/{track}/type = "value"', f'tracks/{track}/path = NodePath(".:{field}")', f'tracks/{track}/interp = 2', f'tracks/{track}/keys = {value(keys)}']
        clips.append("\n".join(lines))
    entities[0] = {"name": "Moonwing", "skeleton_2d": {"bones": packed("PackedInt64Array", range(1, len(bones)+1)), "bind_poses": [packed("Transform2D", [math.cos(a), math.sin(a), -math.sin(a), math.cos(a), x, y]) for x, y, a in (world[i] for i in range(1, len(bones)+1))], "slots": slots,
                    "skins": {"default": skin}, "skin": "default", "attachment_library": packed("PackedInt64Array", range(len(bones)+1, len(entities)))},
                   "animation": {"clip": Raw('SubResource("Soar")'), "state": "Soar", "states": {"Soar": Raw('SubResource("Soar")'), "Glide": Raw('SubResource("Glide")')}, "targets": packed("PackedInt64Array", targets)}}
    text = f'[gd_resource type="ECSScene" load_steps={len(art)+3} format=3]\n\n'
    text += "\n".join(f'[ext_resource type="Texture2D" path="res://Assets/{name}.svg" id="{identifier}"]' for name, identifier in ext.items())
    text += "\n\n" + "\n\n".join(clips) + "\n\n[resource]\nentities = " + value(entities) + "\n"
    (assets / "Moonwing.ecsrig.tres").write_text(text, encoding="utf-8")
    (output / "project.godot").write_text('config_version=5\n[application]\nconfig/name="Moonwing — AgeSkeleton"\nrun/main_loop_type="ECSMainLoop"\n[display]\nwindow/size/viewport_width=1480\nwindow/size/viewport_height=920\n[ecs]\nrun/scene="res://Assets/Moonwing.ecsrig.tres"\n[rendering]\nrenderer/rendering_method="gl_compatibility"\n', encoding="utf-8")
    provenance = {"name": "Moonwing", "copyright": "2026 AgeSkeleton contributors", "license": "MIT", "generator": "tools/generate_moonwing.py",
                  "source": "Vector paths, joint layout, skin weights and periodic motion authored in this generator; no imported third-party character, rig or motion data.",
                  "bones": len(bones), "attachments": len(layers), "animations": ["Soar", "Glide"]}
    (output / "provenance.json").write_text(json.dumps(provenance, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    (output / "LICENSE.txt").write_text((Path(__file__).resolve().parents[1]/"LICENSE.txt").read_text(encoding="utf-8"), encoding="utf-8")
    print(f"Created {output}: {len(bones)} bones, {len(layers)} skinned attachments, 2 animations")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1]/"samples/moonwing")
    args = parser.parse_args()
    generate(args.output)
