"""Blender 4.4/5.0 offline face data export, preserving existing classified parts."""
import hashlib
import json
from pathlib import Path
import bpy
from validate_boss_parts import triangles, ROOT

def main():
    folder = ROOT/'resources/enemy/boss/parts'
    before = {p:hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.iterdir() if p.is_file()}
    data = {'version':1,'coordinate_system':'gltf','blender':bpy.app.version_string,'parts':{}}
    for part in ['head','body','left_arm','right_arm','left_leg','right_leg']:
        faces = triangles(folder/f'boss_{part}.gltf')
        assert faces and all(len(f)==3 for f in faces)
        data['parts'][part] = faces
        print(part, len(faces), 'triangles')
    assert sum(len(f) for f in data['parts'].values()) == 712
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in before.items())
    output = ROOT/'resources/enemy/boss/faces'
    output.mkdir(exist_ok=True)
    (output/'faces.json').write_text(json.dumps(data,separators=(',',':')))
    print('PASS: 712 original classified triangles in one JSON; source Assets unchanged.')

if __name__ == '__main__': main()
