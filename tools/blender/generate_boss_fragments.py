"""Offline fragment generation; called by split_boss_parts.py --fragments-only.
Existing part glTF/bin files are read-only. Blender 4.4/5.0 compatible APIs.
"""
import argparse
import hashlib
import json
import random
import sys
import tempfile
from pathlib import Path
import bpy
from mathutils import Matrix
from validate_boss_parts import triangles, ROOT

SUFFIXES = ['head','body','left_arm','right_arm','left_leg','right_leg']

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fragments-only', action='store_true')
    parser.add_argument('--counts', type=int, nargs=6, default=[4,6,4,4,4,4])
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    assert all(1 <= n <= 32 for n in args.counts)
    parts = ROOT / 'resources/enemy/boss/parts'
    hashes = {p:hashlib.sha256(p.read_bytes()).hexdigest() for p in parts.iterdir() if p.is_file()}
    old_scene = bpy.context.window.scene
    scene = bpy.data.scenes.new('BossFragmentsTemporary')
    bpy.context.window.scene = scene
    report = {'blender':bpy.app.version_string,'parts':{}}
    manifest = {}
    try:
        material = bpy.data.materials.new('FragmentWhite')
        if bpy.app.version < (6,0,0): material.use_nodes = True
        bsdf = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        bsdf.inputs['Base Color'].default_value = (1,1,1,1)
        bsdf.inputs['Metallic'].default_value = 0
        bsdf.inputs['Roughness'].default_value = .8
        (ROOT/'generated').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT/'generated',prefix='boss_fragments_') as temp:
            stage = Path(temp)
            for suffix, count in zip(SUFFIXES,args.counts):
                faces = triangles(parts/f'boss_{suffix}.gltf')
                centers = [tuple(sum(v[a] for v in face)/3 for a in range(3)) for face in faces]
                def distance(a,b): return sum((x-y)**2 for x,y in zip(a,b))
                rng = random.Random(431 + SUFFIXES.index(suffix))
                seeds = [centers[rng.randrange(len(centers))]]
                while len(seeds) < count:
                    seeds.append(max(centers,key=lambda c:min(distance(c,s) for s in seeds)))
                # Localized, irregular surface chunks; no cutting or caps.
                for _ in range(12):
                    groups = [[] for _ in seeds]
                    for i,c in enumerate(centers): groups[min(range(count),key=lambda j:distance(c,seeds[j]))].append(i)
                    assert all(groups), 'Empty fragment'
                    seeds = [tuple(sum(centers[i][a] for i in group)/len(group) for a in range(3)) for group in groups]
                manifest[suffix] = []
                report['parts'][suffix] = []
                for index, group in enumerate(groups):
                    name = f'boss_{suffix}_frag_{index:02d}'
                    points = [v for i in group for v in faces[i]]
                    mesh = bpy.data.meshes.new(name)
                    mesh.from_pydata([(x,-z,y) for x,y,z in points],[],[tuple(range(i,i+3)) for i in range(0,len(points),3)])
                    mesh.materials.append(material)
                    mesh.update()
                    obj = bpy.data.objects.new(name,mesh)
                    scene.collection.objects.link(obj)
                    obj.matrix_world = Matrix.Identity(4)
                    bpy.ops.object.select_all(action='DESELECT')
                    obj.select_set(True)
                    bpy.context.view_layer.objects.active = obj
                    folder = stage/suffix
                    folder.mkdir(exist_ok=True)
                    output = folder/f'{name}.gltf'
                    bpy.ops.export_scene.gltf(filepath=str(output),export_format='GLTF_SEPARATE',use_selection=True,
                        use_active_scene=True,export_animations=False,export_skins=False,export_yup=True)
                    data = json.loads(output.read_text())
                    assert len(data['meshes']) == 1 and not data.get('skins') and not data.get('animations')
                    for node in data['nodes']:
                        assert node.get('translation',[0,0,0]) == [0,0,0] and node.get('rotation',[0,0,0,1]) == [0,0,0,1]
                        assert node.get('scale',[1,1,1]) == [1,1,1] and 'matrix' not in node
                    exported = triangles(output)
                    remaining = [faces[i] for i in group]
                    for face in exported:
                        def same(other):
                            return any(all(abs(face[i][a]-other[(i+s)%3][a]) < 1e-5 for i in range(3) for a in range(3)) for s in range(3))
                        match = next((i for i,f in enumerate(remaining) if same(f)),None)
                        assert match is not None
                        remaining.pop(match)
                    assert not remaining
                    info = {'vertices':len(points),'faces':len(group),'bounds':[[min(v[a] for v in points) for a in range(3)], [max(v[a] for v in points) for a in range(3)]]}
                    report['parts'][suffix].append(info)
                    manifest[suffix].append(f'enemy/boss/fragments/{suffix}/{name}.gltf')
                    print(name,info)
                assert sum(len(g) for g in groups) == len(faces)
            assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
            destination = ROOT/'resources/enemy/boss/fragments'
            for p in stage.rglob('*'):
                if p.is_file():
                    output = destination/p.relative_to(stage)
                    output.parent.mkdir(parents=True,exist_ok=True)
                    output.write_bytes(p.read_bytes())
            (destination/'manifest.json').write_text(json.dumps(manifest,indent=2))
            (destination/'generation_report.json').write_text(json.dumps(report,indent=2))
            print(f'PASS: all {sum(args.counts)} fragments validated; original parts unchanged; triangle coverage and winding preserved.')
    finally:
        bpy.context.window.scene = old_scene
        for obj in list(scene.objects): bpy.data.objects.remove(obj,do_unlink=True)
        bpy.data.scenes.remove(scene)

if __name__ == '__main__': main()
