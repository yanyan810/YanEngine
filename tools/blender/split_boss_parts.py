"""Blender 4.4+: blender --background --python tools/blender/split_boss_parts.py

Runs in an isolated temporary scene; never saves or overwrites the source Asset.
Use -- --output PATH to change the output directory. Paths default relative to this script.
"""
import argparse
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path

import bpy
from mathutils import Matrix

ROOT = Path(__file__).resolve().parents[2]
NAMES = ['Head', 'Body', 'LeftArm', 'RightArm', 'LeftLeg', 'RightLeg']
FILES = ['head', 'body', 'left_arm', 'right_arm', 'left_leg', 'right_leg']


def bounds(points):
    return [[min(p[i] for p in points) for i in range(3)],
            [max(p[i] for p in points) for i in range(3)]]


def main():
    args = argparse.ArgumentParser()
    args.add_argument('--output', type=Path, default=ROOT / 'resources/enemy/boss/parts')
    options = args.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    source = ROOT / 'resources/enemy/boss/boss.gltf'
    original = source.read_bytes()
    gltf = json.loads(original)
    # Read the actual C++ partition rules, failing rather than silently drifting.
    cpp = (ROOT / 'Game/enemy/EnemyParts.h').read_text()
    matches = re.findall(r'EnemyPartType::(\w+),box\(([^)]+)\)', cpp)
    rules = {name: tuple(float(x.strip().rstrip('f')) for x in values.split(','))
             for name, values in matches}
    assert set(rules) == set(NAMES), 'Unsupported C++ partition layout'
    accessors = [gltf['accessors'][p['attributes']['POSITION']]
                 for m in gltf['meshes'] for p in m['primitives']]
    raw_bounds = bounds([a[k] for a in accessors for k in ['min', 'max']])
    lo, hi = raw_bounds
    old_scene = bpy.context.window.scene
    scene = bpy.data.scenes.new('BossSplitTemporary')
    bpy.context.window.scene = scene
    try:
        bpy.ops.import_scene.gltf(filepath=str(source))
        meshes = [o for o in scene.objects if o.type == 'MESH' and any(m.type == 'ARMATURE' for m in o.modifiers)]
        assert meshes, 'No imported mesh'
        vertices, faces = [], []
        for obj in meshes:
            # Importer adjusts skinned vertices by inverse bind matrices. Bake its
            # object matrix to recover source rest positions; validate against raw bounds.
            base = len(vertices)
            vertices.extend(tuple(obj.matrix_world @ v.co) for v in obj.data.vertices)
            faces.extend(tuple(base + i for i in p.vertices) for p in obj.data.polygons)
        # glTF Y-up -> Blender Z-up is (x, -z, y). No per-part recentering.
        source_points = [(x, z, -y) for x, y, z in vertices]
        actual = bounds(source_points)
        assert all(abs(actual[j][i] - raw_bounds[j][i]) < 1e-4
                   for i in range(3) for j in range(2)), 'Importer coordinate convention changed'
        groups = {name: [] for name in NAMES}
        fallback_faces = 0
        for face in faces:
            center = [sum(source_points[v][i] for v in face) / len(face) for i in range(3)]
            y = (center[1] - lo[1]) / (hi[1] - lo[1])
            z = (center[2] - lo[2]) / (hi[2] - lo[2])
            def distance(name):
                y0, y1, z0, z1 = rules[name]
                # Distance to the same local boxes; Y/Z restored to model units.
                return (max(y0-y, 0, y-y1)*(hi[1]-lo[1]))**2 + (max(z0-z, 0, z-z1)*(hi[2]-lo[2]))**2
            part = min(NAMES, key=distance)  # same stable tie order as C++
            fallback_faces += distance(part) > 1e-12
            groups[part].append(face)
        assert all(groups.values()), 'A part would be empty'
        assert sum(map(len, groups.values())) == len(faces)
        material = bpy.data.materials.new('BossPartWhite')
        # 4.4 requires enabling nodes; 5.0 keeps this API (deprecated for 6.0).
        if bpy.app.version < (6, 0, 0):
            material.use_nodes = True
        bsdf = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        bsdf.inputs['Base Color'].default_value = (1, 1, 1, 1)
        bsdf.inputs['Metallic'].default_value = 0
        bsdf.inputs['Roughness'].default_value = .8
        report = {'blender': bpy.app.version_string, 'source_sha256': hashlib.sha256(original).hexdigest(),
                  'source_bounds_gltf': raw_bounds, 'source_faces': len(faces),
                  'nearest_box_fallback_faces': fallback_faces, 'rules': rules, 'parts': {}}
        # Stage all six exports, validate, then publish. Existing output is untouched on export failure.
        (ROOT / 'generated').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='boss_split_', dir=ROOT / 'generated') as temporary:
            stage = Path(temporary)
            for name, suffix in zip(NAMES, FILES):
                selected_faces = groups[name]
                used = sorted({v for face in selected_faces for v in face})
                remap = {v: i for i, v in enumerate(used)}
                mesh = bpy.data.meshes.new('boss_' + suffix)
                mesh.from_pydata([vertices[v] for v in used], [],
                                 [tuple(remap[v] for v in face) for face in selected_faces])
                mesh.update()
                obj = bpy.data.objects.new(mesh.name, mesh)
                scene.collection.objects.link(obj)
                obj.matrix_world = Matrix.Identity(4)
                mesh.materials.append(material)
                bpy.ops.object.select_all(action='DESELECT')
                obj.select_set(True)
                bpy.context.view_layer.objects.active = obj
                path = stage / (mesh.name + '.gltf')
                bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLTF_SEPARATE',
                    use_selection=True, use_active_scene=True, export_animations=False, export_skins=False,
                    export_yup=True, export_materials='EXPORT')
                result = json.loads(path.read_text())
                assert result.get('meshes') and not result.get('skins') and not result.get('animations')
                assert all('skin' not in n for n in result['nodes'])
                for node in result['nodes']:
                    assert node.get('translation', [0,0,0]) == [0,0,0]
                    assert node.get('scale', [1,1,1]) == [1,1,1]
                    assert node.get('rotation', [0,0,0,1]) == [0,0,0,1]
                    assert 'matrix' not in node
                output_bounds = bounds([result['accessors'][p['attributes']['POSITION']][k]
                    for m in result['meshes'] for p in m['primitives'] for k in ['min', 'max']])
                expected = bounds([source_points[v] for v in used])
                assert all(abs(output_bounds[j][i]-expected[j][i]) < 1e-4 for j in range(2) for i in range(3))
                for buffer in result['buffers']:
                    assert (stage / buffer['uri']).is_file()
                report['parts'][name] = {'vertices': len(used), 'faces': len(selected_faces), 'bounds_gltf': output_bounds}
                print(name, report['parts'][name])
            assert source.read_bytes() == original, 'Source modified'
            options.output.mkdir(parents=True, exist_ok=True)
            for path in stage.iterdir():
                (options.output / path.name).write_bytes(path.read_bytes())
            (options.output / 'split_report.json').write_text(json.dumps(report, indent=2))
        print('PASS: all source faces assigned exactly once; common local coordinates preserved; source unchanged.')
    finally:
        bpy.context.window.scene = old_scene
        for obj in list(scene.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.scenes.remove(scene)


if __name__ == '__main__':
    if '--faces-only' in sys.argv:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from generate_boss_faces import main as face_main
        face_main()
    elif '--fragments-only' in sys.argv:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from generate_boss_fragments import main as fragment_main
        fragment_main()
    else:
        main()




