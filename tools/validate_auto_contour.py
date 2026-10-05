"""Exercise sparse alpha contours on a copied Moonwing fixture, never user projects."""
import json
import shutil
import subprocess
import time
from collections import Counter
from pathlib import Path
from project_paths import editor_binary, data_root, SOURCE_ROOT
from validate_portable_project import client_for


def main():
    report = data_root() / 'Reports' / time.strftime('SparseContour-%Y%m%d-%H%M%S')
    host = report / 'Host'
    host.mkdir(parents=True)
    source = SOURCE_ROOT / 'samples/moonwing'
    shutil.copy2(source / 'project.godot', host / 'project.godot')
    shutil.copytree(source / 'Assets', host / 'Assets')
    doc = report / 'Moonwing/Moonwing.ageskeleton'
    with (report / 'validation.log').open('wb') as log:
        process = subprocess.Popen([str(editor_binary()), '--headless', '--path', str(host), '--', '--ecs-ai-editor'], stdout=log, stderr=subprocess.STDOUT)
        try:
            c = client_for(host, process)
            before = c.call('inspect', entity=22)['entity']
            tracks = c.call('tracks', entity=0, animation='Soar')['tracks']
            c.call('auto_contour', entity=22)
            after = c.call('inspect', entity=22)['entity']
            count = len(after['polygon_2d']['uv']['items'])
            (report / 'mesh.json').write_text(json.dumps(after, ensure_ascii=False, indent=2), encoding='utf-8')
            assert count < 60, count  # Previous algorithm generated 112 on this fixture.
            mesh = after['polygon_2d']
            uv = [point['value'] for point in mesh['uv']['items']]
            faces = mesh['triangles']['items']
            edges = Counter()
            for start in range(0, len(faces), 3):
                triangle = faces[start:start + 3]
                a, b, d = (uv[index] for index in triangle)
                assert abs((b[0]-a[0])*(d[1]-a[1])-(b[1]-a[1])*(d[0]-a[0])) > 1e-9
                edges.update(tuple(sorted((triangle[j], triangle[(j+1)%3]))) for j in range(3))
            assert set(edges.values()) <= {1, 2}
            boundary_degree = Counter(vertex for edge, uses in edges.items() if uses == 1 for vertex in edge)
            assert set(boundary_degree.values()) == {2}
            validation = c.call('validate')
            assert validation['ok']
            c.call('undo')
            assert c.call('inspect', entity=22)['entity'] == before
            c.call('redo')
            assert c.call('inspect', entity=22)['entity'] == after
            c.call('auto_contour', entity=22)
            repeated = c.call('inspect', entity=22)['entity']
            assert len(repeated['polygon_2d']['uv']['items']) <= count
            c.call('undo')
            c.call('save', path=str(doc))
            c.call('open', path=str(doc), replace=True)
            loaded = c.call('inspect', entity=22)['entity']
            for key in ('polygon', 'uv', 'triangles', 'weights', 'bones'):
                assert loaded['polygon_2d'][key] == after['polygon_2d'][key], key
            assert c.call('tracks', entity=0, animation='Soar')['tracks'] == tracks
            assert c.call('validate')['ok']
            result = dict(ok=True, vertices=count, original_vertices=len(before['polygon_2d']['uv']['items']), validation=validation,
                          checks=['sparse', 'topology', 'weights', 'undo', 'redo', 'repeat', 'save_reload', 'animation_tracks'], document=str(doc))
            (report / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
            (report / 'mesh.json').write_text(json.dumps(after, ensure_ascii=False, indent=2), encoding='utf-8')
            print(json.dumps(result, ensure_ascii=False))
            print(report)
        finally:
            process.terminate()
            process.wait(timeout=15)


if __name__ == '__main__':
    main()
