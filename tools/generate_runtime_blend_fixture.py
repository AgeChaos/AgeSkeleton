"""Derive deterministic animated crossfade/slot data from the motion fixture."""
import copy
import json
import sys
from pathlib import Path

data = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
data['slots'] = ['Slot']
data['attachments'][0]['slot'] = 0
data['skins'] = [dict(name='default', bindings=[dict(slot=0, key='visible', attachment=0)])]
data['defaultSkins'] = ['default']
for frame in [data['rest'], *(f for c in data['clips'] for f in c['frames'])]:
    frame.update(keys=['visible'], colors=[1, 1, 1, 1], orders=[0])
base = data['clips'][0]
for name, start, end in [('Source', 0, 2), ('Destination', 10, 10), ('Third', 20, 20)]:
    clip = copy.deepcopy(base)
    clip['name'] = name
    for frame, shift in zip(clip['frames'], [start, end]):
        frame['positions'][::2] = [v + shift for v in frame['positions'][::2]]
        for bone in range(len(data['rig']['names'])):
            frame['matrices'][bone * 6 + 4] += shift
        if name == 'Destination':
            frame.update(keys=[''], colors=[1, 0, 0, .5], orders=[5])
    data['clips'].append(clip)
out = Path(sys.argv[2])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
print(out)
