"""Create an isolated, deterministic two-bone IK and event fixture."""
import copy
import json
import sys
from pathlib import Path

frame = dict(time=0, positions=[2, 0, 1, 0, 0, 0], keys=[], colors=[], orders=[],
             matrices=[1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0],
             influencePositions=[1, 0, 0, 0, 0, 0, 0, 0,
                                 0, 0, 1, 0, 0, 0, 0, 0,
                                 0, 0, 0, 0, 0, 0, 0, 0])
end = copy.deepcopy(frame)
end['time'] = 1
events = [dict(name=name, time=at, intValue=-2147483648 if at == 0 else 7,
               floatValue=1.25, stringValue='测试 event')
          for name, at in [('start', 0), ('quarter', .25), ('late', .75), ('end', 1)]]
clip = dict(name='Loop', duration=1, loop=True, frames=[frame, end], events=events)
once = copy.deepcopy(clip)
once.update(name='Once', loop=False)
data = dict(format='ageskeleton.meshclip', version=2, name='MotionFixture', fps=30,
            vertexCount=3, textures=['atlas_000.png'], slots=[], skins=[], defaultSkins=[],
            attachments=[dict(name='triangle', offset=0, count=3, texture=0, slot=-1,
                              uv=[0, 0, 1, 0, 0, 1], color=[1, 1, 1, 1], triangles=[0, 1, 2])],
            rig=dict(names=['Root', 'Tip'], parents=[-1, 0], lengths=[1, 1],
                     vertexBones=[1, -1, -1, -1, 1, 0, -1, -1, 0, -1, -1, -1],
                     weights=[1, 0, 0, 0, .5, .5, 0, 0, 1, 0, 0, 0]),
            rest=frame, clips=[clip, once])
out = Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
print(out)
