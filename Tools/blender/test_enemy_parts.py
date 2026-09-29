"""Headless integration: real edit-mode assignments -> exporter -> C++ fixture.
Also creates editable single-mesh normal/boss examples without touching legacy models.
"""
import sys
from pathlib import Path
import json
import bpy
import bmesh
from mathutils import Vector
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import yanengine_enemy_parts as addon
addon.register()
OUTPUT = ROOT / 'generated/enemy-asset-tests'
OUTPUT.mkdir(parents=True, exist_ok=True)
EXAMPLES = ROOT / 'resources/enemy/boss' if '--write-examples' in sys.argv else OUTPUT


def new_mesh(name, triangles):
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT')
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([v for tri in triangles for v in tri], [], [tuple(range(i*3,i*3+3)) for i in range(len(triangles))])
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    return obj


def select_faces(obj, indices):
    bm = bmesh.from_edit_mesh(obj.data)
    bm.faces.ensure_lookup_table()
    for face in bm.faces:
        face.select_set(face.index in indices)
    bmesh.update_edit_mesh(obj.data)


# Eight freely named parts plus an unassigned face. Independent HP groups and roles.
obj = new_mesh('VariablePartsTest', [[(i*2,0,0),(i*2+1,0,0),(i*2,0,1)] for i in range(9)])
mesh = obj.data
for i in range(8):
    p = addon.add_part(mesh, 'Core' + str(i), 'Core')
    p.hp_mode = 'LocalAndShared'
    p.shared_group = 'BossMain'
    p.local_hp = 300
    p.shared_rate = .5 if i == 1 else 1
    p.breakable = i != 2
p = mesh.yan_enemy_groups.add()
p.name, p.max_hp = 'BossMain', 5000
bpy.ops.object.mode_set(mode='EDIT')
for i, p in enumerate(mesh.yan_enemy_parts):
    select_faces(obj, {i})
    addon.edit_faces(obj, 'ASSIGN', p.part_id)
# Reassignment is replacement, never overlapping membership.
select_faces(obj, {0})
addon.edit_faces(obj, 'ASSIGN', mesh.yan_enemy_parts[1].part_id)
addon.edit_faces(obj, 'ASSIGN', mesh.yan_enemy_parts[0].part_id)
addon.edit_faces(obj, 'SELECT', mesh.yan_enemy_parts[1].part_id)
bm = bmesh.from_edit_mesh(mesh)
assert [f.index for f in bm.faces if f.select] == [1]
select_faces(obj, {8})
addon.edit_faces(obj, 'ASSIGN', mesh.yan_enemy_parts[0].part_id)
addon.edit_faces(obj, 'CLEAR')
data = addon.export_asset(obj, OUTPUT / 'variable.enemy.json')
assert len(data['parts']) == 9
assert data['parts'][0]['faces'] == [0]
assert data['parts'][1]['sharedDamageRate'] == .5
assert data['parts'][-1]['name'] == '__Unassigned'
assert len({f for p in data['parts'] for f in p['faces']}) == 9
assert data['parts'][0]['triangles'][0]['vertices'][0]['p'] == [0,1,0]
# Remove then Add: surviving face IDs remain stable; removed faces become unassigned.
removed = mesh.yan_enemy_parts[3].part_id
mesh.yan_enemy_part_index = 3
assert bpy.ops.yan.enemy_part(action='REMOVE') == {'FINISHED'}
added = addon.add_part(mesh, 'WingL', 'Generic')
assert added.part_id > removed
select_faces(obj, {3})
addon.edit_faces(obj, 'ASSIGN', added.part_id)
data = addon.export_asset(obj, OUTPUT / 'removed-added.enemy.json')
assert next(p for p in data['parts'] if p['name']=='WingL')['faces'] == [3]
# Round-trip the same mesh with custom properties / integer face attributes.
bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT / 'assignment-roundtrip.blend'))
bpy.ops.wm.open_mainfile(filepath=str(OUTPUT / 'assignment-roundtrip.blend'))
assert addon.export_asset(bpy.context.active_object, OUTPUT / 'roundtrip.enemy.json') == data
# Invalid group is rejected, and the last export is untouched.
obj = bpy.context.active_object
before = (OUTPUT / 'roundtrip.enemy.json').read_bytes()
obj.data.yan_enemy_parts[0].shared_group = 'Missing'
try:
    addon.export_asset(obj, OUTPUT / 'roundtrip.enemy.json')
    raise AssertionError('Missing group accepted')
except ValueError:
    pass
assert (OUTPUT / 'roundtrip.enemy.json').read_bytes() == before

# Connected polygon mesh, triangulation, and mirrored transform/winding.
bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.mesh.primitive_cube_add()
cube = bpy.context.active_object
cube.scale.x = -2
part = addon.add_part(cube.data, 'ArmorChest', 'Armor')
part.hp_mode = 'Shared'
part.shared_group = 'Armor'
group = cube.data.yan_enemy_groups.add()
group.name, group.max_hp, group.death_on_zero = 'Armor', 1000, False
bpy.ops.object.mode_set(mode='EDIT')
select_faces(cube, {0, 1})
addon.edit_faces(cube, 'ASSIGN', part.part_id)
addon.edit_faces(cube, 'SELECT', part.part_id)
assert {f.index for f in bmesh.from_edit_mesh(cube.data).faces if f.select} == {0, 1}
data = addon.export_asset(cube, OUTPUT / 'quads.enemy.json')
assert data['parts'][0]['faces'] == [0, 1]
assert len(data['parts'][0]['triangles']) == 4
assert sum(len(p['triangles']) for p in data['parts']) == 12
assert data['parts'][0]['localHp'] is None and not data['hpGroups'][0]['deathOnZero']

# Normal compatibility sample from the SAME 712 original, already-classified faces.
# A single mesh is authored; no separate glTF files are produced/required by the new asset.
legacy = json.loads((ROOT / 'resources/enemy/boss/faces/faces.json').read_text())
keys = ['head','body','left_arm','right_arm','left_leg','right_leg']
names = ['Head','Body','LeftArm','RightArm','LeftLeg','RightLeg']
roles = ['Head','Body','Arm','Arm','Leg','Leg']
hps = [50,100,60,60,70,70]
faces = [tri for key in keys for tri in legacy['parts'][key]]
obj = new_mesh('NormalFaceParts', [[(x,-z,y) for x,y,z in tri] for tri in faces])
for name,role,hp in zip(names,roles,hps):
    p = addon.add_part(obj.data, name, role)
    p.local_hp = hp
    p.death_on_zero = name in {'Head','Body'}
bpy.ops.object.mode_set(mode='EDIT')
start = 0
for i,key in enumerate(keys):
    count = len(legacy['parts'][key])
    select_faces(obj, set(range(start,start+count)))
    addon.edit_faces(obj, 'ASSIGN', obj.data.yan_enemy_parts[i].part_id)
    start += count
normal = addon.export_asset(obj, EXAMPLES / 'normal.enemy.json')
assert sum(len(p['triangles']) for p in normal['parts']) == 712
assert [len(p['triangles']) for p in normal['parts']] == [len(legacy['parts'][key]) for key in keys]
bpy.ops.object.mode_set(mode='OBJECT')
# Keep only example object in saved file; preserve source assets on disk.
for other in list(bpy.data.objects):
    if other != obj:
        bpy.data.objects.remove(other, do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=str(EXAMPLES / 'normal-parts.blend'))
# Boss sample: same editable geometry, independent local arms + shared boss life.
group = obj.data.yan_enemy_groups.add()
group.name, group.max_hp = 'BossMain', 5000
for p in obj.data.yan_enemy_parts:
    p.hp_mode, p.shared_group = 'LocalAndShared', 'BossMain'
    p.local_hp = 800 if p.role == 'Body' else 300
    p.death_on_zero = False
obj.data.yan_enemy_parts[1].breakable = False
addon.export_asset(obj, EXAMPLES / 'shared-boss.enemy.json')
print('PASS: edit-mode assign/select/clear/remove, stable IDs, 9 parts, unique membership, unassigned faces, roundtrip, invalid group, six-part normal and shared boss exports.')
