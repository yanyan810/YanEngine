"""Validate exported triangle geometry against original Boss (no Blender needed)."""
import base64
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def triangles(path):
    g = json.loads(path.read_text())
    def accessor(index):
        a = g['accessors'][index]
        v = g['bufferViews'][a['bufferView']]
        uri = g['buffers'][v['buffer']]['uri']
        data = base64.b64decode(uri.split(',')[1]) if uri.startswith('data:') else (path.parent / uri).read_bytes()
        code = {5126: 'f', 5125: 'I', 5123: 'H', 5121: 'B'}[a['componentType']]
        count = {'VEC3': 3, 'SCALAR': 1}[a['type']]
        fmt = '<' + code * count
        start = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = v.get('byteStride', struct.calcsize(fmt))
        return [struct.unpack_from(fmt, data, start + i * stride) for i in range(a['count'])]
    result = []
    for mesh in g['meshes']:
        for p in mesh['primitives']:
            assert p.get('mode', 4) == 4
            points = accessor(p['attributes']['POSITION'])
            indices = [x[0] for x in accessor(p['indices'])]
            result.extend(tuple(points[i] for i in indices[n:n+3]) for n in range(0, len(indices), 3))
    return result


def main():
    source = triangles(ROOT / 'resources/enemy/boss/boss.gltf')
    remaining = list(source)
    total = 0
    for path in sorted((ROOT / 'resources/enemy/boss/parts').glob('boss_*.gltf')):
        faces = triangles(path)
        assert faces
        for face in faces:
            # Permit cyclic vertex order, preserve winding, tolerate importer float error.
            def same(candidate):
                return any(all(abs(face[i][axis] - candidate[(i+shift)%3][axis]) < 1e-5
                    for i in range(3) for axis in range(3)) for shift in range(3))
            index = next((i for i, candidate in enumerate(remaining) if same(candidate)), None)
            assert index is not None, f'Unexpected/duplicated triangle in {path.name}'
            remaining.pop(index)
        print(path.name, len(faces), 'triangles match source')
        total += len(faces)
    assert not remaining, 'Missing source faces'
    print('PASS:', total, 'source triangles preserved exactly once (1e-5 tolerance), including winding.')


if __name__ == '__main__':
    main()
