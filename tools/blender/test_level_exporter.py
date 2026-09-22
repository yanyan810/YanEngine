"""Headless integration checks; never operates the Blender or game UI."""
import sys
from pathlib import Path
import json
import math
import hashlib
import struct
import bpy
from mathutils import Matrix, Vector, Euler
sys.path.insert(0,str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e
root=Path(__file__).resolve().parents[2]
e.register()
bpy.ops.wm.open_mainfile(filepath=str(root/'resources/levels/stage01/stage01.blend'))
ctx=bpy.context
scene=ctx.scene
assert scene.yan_level.stage_id=='stage01' and scene.objects['SP_A_03'].yan_level.pool[0].identifier=='fast'
scene.yan_level.project_root=str(root)
ctx.view_layer.update()
valid, geometry=e.build_level(scene,ctx.evaluated_depsgraph_get())
assert len(geometry)==9 and len(valid['colliders'])==8
assert len(valid['spawnPoints'])==9 and len(valid['spawnTriggers'])==2
assert valid['spawnTriggers'][0]['spawnPointIds']==[f'SP_A_{i:02}' for i in range(1,6)]
assert valid['playerSpawn']['position']==[3,0,-6]


def rejected(change, restore):
    try:
        change(); ctx.view_layer.update()
        try:
            e.build_level(scene,ctx.evaluated_depsgraph_get())
        except (ValueError, TypeError):
            pass
        else:
            raise AssertionError('Invalid authoring data accepted')
    finally:
        restore(); ctx.view_layer.update()


player=scene.objects['PlayerSpawn']; enemy=scene.objects['SP_A_01']; trigger=scene.objects['Trigger_A']
rejected(lambda:setattr(player.yan_level,'role','IGNORE'),lambda:setattr(player.yan_level,'role','PLAYER'))
rejected(lambda:setattr(enemy.yan_level,'role','PLAYER'),lambda:setattr(enemy.yan_level,'role','ENEMY'))
rejected(lambda:setattr(enemy.yan_level,'identifier','PlayerSpawn'),lambda:setattr(enemy.yan_level,'identifier',''))
rejected(lambda:setattr(trigger.yan_level,'group','missing'),lambda:setattr(trigger.yan_level,'group','A'))
rejected(lambda:setattr(enemy.yan_level.pool[0],'weight',0),lambda:setattr(enemy.yan_level.pool[0],'weight',1))
rejected(lambda:setattr(enemy.yan_level.pool[0],'identifier','missing'),lambda:setattr(enemy.yan_level.pool[0],'identifier','normal'))
weapon=scene.objects['WeaponSpawn_01']; weights=[p.weight for p in weapon.yan_level.pool]
rejected(lambda:[setattr(p,'weight',0) for p in weapon.yan_level.pool],lambda:[setattr(p,'weight',w) for p,w in zip(weapon.yan_level.pool,weights)])
goal=scene.objects['Goal_Main']; oldscale=goal.scale.copy()
rejected(lambda:setattr(goal,'scale',(0,1,1)),lambda:setattr(goal,'scale',oldscale))
oldrotation=trigger.rotation_euler.copy()
rejected(lambda:setattr(trigger,'rotation_euler',(.3,.2,0)),lambda:setattr(trigger,'rotation_euler',oldrotation))
wall=scene.objects['Wall_Rotated']; oldscale=wall.scale.copy()
rejected(lambda:setattr(wall,'scale',(0,1,1)),lambda:setattr(wall,'scale',oldscale))
building=scene.objects['Building_Custom']; custom=building.yan_level.custom_collider
rejected(lambda:setattr(building.yan_level,'custom_collider',None),lambda:setattr(building.yan_level,'custom_collider',custom))
for value in [float('nan'),float('inf')]:
    try: e.finite([value],'test')
    except ValueError: pass
    else: raise AssertionError('Nonfinite accepted')
# General Euler rotation, parent transform and nonuniform scale: local/world paths agree.
parent=Matrix.LocRotScale(Vector((2,4,-7)),Euler((.2,.4,-.3)).to_quaternion(),Vector((2,2,2)))
child=Matrix.LocRotScale(Vector((3,-1,2)),Euler((-.3,.1,.5)).to_quaternion(),Vector((1,3,2)))
trs,m=e.engine_transform(parent@child)
rebuilt=Matrix.LocRotScale(Vector(trs['position']),Euler(trs['rotation'],'XYZ').to_quaternion(),Vector(trs['scale']))
for point in [Vector((1,2,3,1)),Vector((-2,1,-1,1))]:
    expected=e.ENGINE_BASIS@(parent@child)@point
    actual=rebuilt@(e.ENGINE_BASIS@point)
    assert (expected-actual).length<1e-4
# Shear is explicitly rejected rather than silently decomposed incorrectly.
shear=Matrix.Identity(4); shear[0][1]=.5
try: e.engine_transform(shear)
except ValueError: pass
else: raise AssertionError('Shear accepted')

scene.yan_level.project_root=''
scene.yan_level.output_directory=str(root/'generated/blender-tests/stage01')
assert bpy.ops.yanengine.validate()=={'FINISHED'}
selected=list(ctx.selected_objects); source_scene=ctx.window.scene
out=e.export_level(ctx)
assert ctx.window.scene==source_scene and list(ctx.selected_objects)==selected
output=json.loads((out/'stage01.json').read_text())
gltf=json.loads((out/'stage01.gltf').read_text())
assert len(gltf['meshes'])==9
assert not any(n.get('name','').startswith(('COL_','SP_','Trigger_','Goal_','PlayerSpawn','WeaponSpawn')) for n in gltf['nodes'])
# Exported mesh bounds equal engine JSON collider bounds, including the rotated wall.
for collider in output['colliders']:
    name='Building_Custom' if collider['id']=='COL_Building' else collider['id']
    node=next(n for n in gltf['nodes'] if n.get('name','').startswith(name))
    accessor=gltf['accessors'][gltf['meshes'][node['mesh']]['primitives'][0]['attributes']['POSITION']]
    a,b=accessor['min'],accessor['max']; mesh_min=Vector((-b[0],a[1],a[2])); mesh_max=Vector((-a[0],b[1],b[2]))
    matrix=Matrix.LocRotScale(Vector(collider['position']),Euler(collider['rotation'],'XYZ').to_quaternion(),Vector(collider['scale']))
    low,high=collider['localBounds']['min'],collider['localBounds']['max']
    corners=[matrix@Vector((x,y,z,1)) for x in (low[0],high[0]) for y in (low[1],high[1]) for z in (low[2],high[2])]
    for i in range(3):
        assert abs(mesh_min[i]-min(p[i] for p in corners))<1e-4
        assert abs(mesh_max[i]-max(p[i] for p in corners))<1e-4

def fingerprints():
    return {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()}
before=fingerprints()
# Invalid export never touches the previous files.
trigger.yan_level.group='missing'
try: e.export_level(ctx)
except ValueError: pass
else: raise AssertionError('Invalid export wrote files')
trigger.yan_level.group='A'
assert before==fingerprints()
# Simulate failure in the publication rename, and verify complete restoration.
original=e.os.replace
count=0

def fail_second(source,destination):
    global count
    count+=1
    if count==2: raise OSError('injected publication failure')
    return original(source,destination)
e.os.replace=fail_second
try:
    try: e.export_level(ctx)
    except OSError: pass
    else: raise AssertionError('Expected injected failure')
finally: e.os.replace=original
assert before==fingerprints() and ctx.window.scene==source_scene
# Verify .blend property persistence and registration lifecycle.
blend=root/'generated/blender-tests/properties.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(blend))
bpy.ops.wm.open_mainfile(filepath=str(blend))
assert bpy.context.scene.objects['SP_A_03'].yan_level.pool[0].identifier=='fast'
e.unregister(); e.register()
print('BLENDER_LEVEL_TESTS_PASSED: roles, validation, groups/pools, transforms, glTF filtering, atomic rollback, saved properties, registration')
