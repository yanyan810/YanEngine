"""Real Blender generation/ownership/export regression; writes generated/ only."""
import sys, json, shutil, time
from pathlib import Path
import bpy
import bmesh
from mathutils import Vector, Matrix
sys.path.insert(0, str(Path(__file__).parent))
import yanengine_level_exporter as e
ROOT = Path(__file__).resolve().parents[2]
e.register()
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)


def shape(name, cells, stretch=(1,1,1), dissolve=False):
    # One watertight connected surface, with internal cell faces removed.
    vertices, indices, polygons = [], {}, []
    directions = [((1,0,0),[(1,0,0),(1,1,0),(1,1,1),(1,0,1)]),
                  ((-1,0,0),[(0,0,0),(0,0,1),(0,1,1),(0,1,0)]),
                  ((0,1,0),[(0,1,0),(0,1,1),(1,1,1),(1,1,0)]),
                  ((0,-1,0),[(0,0,0),(1,0,0),(1,0,1),(0,0,1)]),
                  ((0,0,1),[(0,0,1),(1,0,1),(1,1,1),(0,1,1)]),
                  ((0,0,-1),[(0,0,0),(0,1,0),(1,1,0),(1,0,0)])]
    for cell in sorted(cells):
        for delta, corners in directions:
            if tuple(cell[a]+delta[a] for a in range(3)) in cells:
                continue
            face = []
            for corner in corners:
                point = tuple((cell[a]+corner[a])*stretch[a] for a in range(3))
                if point not in indices:
                    indices[point] = len(vertices)
                    vertices.append(point)
                face.append(indices[point])
            polygons.append(face)
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], polygons)
    mesh.update()
    if dissolve:
        bm = bmesh.new(); bm.from_mesh(mesh)
        bmesh.ops.dissolve_limit(bm, angle_limit=.001, verts=list(bm.verts), edges=list(bm.edges))
        bm.to_mesh(mesh); bm.free(); mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.yan_level.role = 'STATIC'
    return obj


def covers(boxes, point):
    p = Vector((*point,1))
    return any(all(abs(v) <= 1.00001 for v in (box.matrix_world.inverted() @ p)[:3]) for box in boxes)


def check(obj, expected, solid_samples, empty_samples):
    start = time.monotonic()
    boxes = e._generate_compound_boxes(obj, 8, 0)
    bpy.context.view_layer.update()
    assert len(boxes) == expected, (obj.name, len(boxes), expected)
    assert all(covers(boxes, p) for p in solid_samples), obj.name+' loses solid material'
    assert not any(covers(boxes, p) for p in empty_samples), obj.name+' fills a hole'
    assert [b.name for b in boxes] == [f'COL_{obj.name}_{i:02}' for i in range(expected)]
    print(obj.name, len(boxes), 'boxes', round(time.monotonic()-start,3), 'seconds', flush=True)
    return boxes


cube = shape('Cube', {(0,0,0)})
check(cube, 1, [(x,y,z) for x in (.1,.5,.9) for y in (.1,.5,.9) for z in (.1,.5,.9)], [])
long_box = shape('Long', {(0,0,0)}, stretch=(20,1,1))
check(long_box, 1, [(x,.5,.5) for x in (.1,10,19.9)], [])
l_cells = {(x,0,0) for x in range(4)} | {(0,0,z) for z in range(4)}
l_shape = shape('LShape', l_cells, dissolve=True)
check(l_shape, 2, [(x+.5,.5,z+.5) for x,y,z in l_cells], [(2,.5,2)])
gate_cells = {(x,0,z) for x in range(6) for z in range(4) if x in (0,5) or z==3}
gate = shape('Gate', gate_cells, dissolve=True)
empty = [(x,.5,z) for x in (1.1,2,3,4,4.9) for z in (.1,1,2,2.9)]
check(gate, 3, [(x+.5,.5,z+.5) for x,y,z in gate_cells], empty)
# Boolean modifiers must use visible evaluated geometry, not the original cube.
boolean_gate = shape('BooleanGate', {(0,0,0)}, stretch=(6,1,4))
cutter = shape('Cutter', {(0,0,0)}, stretch=(4,3,3.2))
cutter.location = (1,-1,-.2)
cutter.yan_level.role = 'IGNORE'
modifier = boolean_gate.modifiers.new('Door opening', 'BOOLEAN')
modifier.operation = 'DIFFERENCE'; modifier.object = cutter
check(boolean_gate, 3, [(x+.5,.5,z+.5) for x,y,z in gate_cells], empty)
assert len(boolean_gate.data.polygons)==6 and len(boolean_gate.modifiers)==1

disconnected = shape('Disconnected' , {(0,0,0),(5,0,0)})
check(disconnected, 2, [(.5,.5,.5),(5.5,.5,.5)], [(3,.5,.5)])
# The same concavity in every orientation, not just the longest axis.
for axis in (0,1,2):
    turned = shape('Turned'+str(axis), {tuple(c[(a+axis)%3] for a in range(3)) for c in gate_cells}, dissolve=True)
    transformed = [tuple(p[(a+axis)%3] for a in range(3)) for p in empty]
    check(turned, 3, [], transformed)
for maximum in (1,2,3,4,8):
    assert 1 <= len(e._generate_compound_boxes(gate,maximum,0)) <= maximum
# Final padding is capped at facing boxes so even excessive margins leave a passage.
for padding in (.02,10):
    boxes=e._generate_compound_boxes(gate,8,padding)
    bpy.context.view_layer.update()
    assert len(boxes)==3 and not covers(boxes,(3,.5,1.5))
    assert all(covers(boxes,(x+.5,.5,z+.5)) for x,y,z in gate_cells)
# A face exactly on a candidate cut must not disappear (open shelf plus support).
ledge = shape('LedgeSupport', {(0,0,0)}, stretch=(1,1,4))
vertices = [tuple(v.co) for v in ledge.data.vertices] + [(0,0,2),(8,0,2),(8,1,2),(0,1,2)]
faces = [tuple(p.vertices) for p in ledge.data.polygons] + [(8,9,10,11)]
ledge.data.clear_geometry(); ledge.data.from_pydata(vertices,[],faces); ledge.data.update()
boxes = e._generate_compound_boxes(ledge,8,0)
bpy.context.view_layer.update()
assert 1 <= len(boxes) <= 8
assert all(covers(boxes,(x,.5,2)) for x in (1,3,5,7.9))
# Rotation/nonuniform scale and editable independent objects survive export.
gate.matrix_world=Matrix.Translation((2,3,4)) @ Matrix.Rotation(.3,4,'Z') @ Matrix.Diagonal((2,1,1.5,1))
boxes=e._generate_compound_boxes(gate,8,.02)
bpy.context.view_layer.update()
assert not covers(boxes,tuple((gate.matrix_world@Vector((3,.5,1.5,1)))[:3]))
manual=bpy.data.objects.new('COL_Manual',None); bpy.context.scene.collection.objects.link(manual)
manual.empty_display_type='CUBE'; manual.yan_level.role='COLLIDER'
manual['yan_auto_collider_owner']=gate.name # Owner tag alone never permits deletion.
# Generated empties remain editable, and edits appear in the existing Box schema.
boxes[0]['previous_generation'] = True
boxes[0].location.x += .25
boxes[0].rotation_euler.z += .1
boxes[0].scale *= 1.1
bpy.context.view_layer.update()
modified, _ = e.box_data(boxes[0], bpy.context.evaluated_depsgraph_get())
assert set(modified)=={'position','rotation','scale','localBounds'}
old_names=[o.name for o in boxes]
other_names=[o.name for o in e._generate_compound_boxes(disconnected,8,0)]
for o in bpy.context.selected_objects: o.select_set(False)
gate.select_set(True); bpy.context.view_layer.objects.active=gate
gate.yan_level.auto_collider_count=8
assert bpy.ops.yanengine.generate_auto_colliders()=={'FINISHED'}
assert all(bpy.data.objects.get(n) for n in old_names+other_names+['COL_Manual'])
assert gate.yan_level.collision=='NONE'
assert not any(bpy.data.objects[n].get('previous_generation') for n in old_names)
assert len([o for o in bpy.data.objects if o.get('yan_auto_collider_generated') and o.get('yan_auto_collider_owner')==gate.name])==3
# Invalid regeneration leaves previous objects intact.
try: e._generate_compound_boxes(gate,0,0)
except ValueError: pass
else: raise AssertionError('Invalid count accepted')
assert all(bpy.data.objects.get(n) for n in old_names)
bpy.context.view_layer.objects.active=gate
assert bpy.ops.yanengine.clear_auto_colliders()=={'FINISHED'}
assert not any(bpy.data.objects.get(n) for n in old_names)
assert all(bpy.data.objects.get(n) for n in other_names+['COL_Manual'])
# Isolated gate level exported through the unchanged full exporter.
for obj in list(bpy.data.objects):
    if obj!=gate: bpy.data.objects.remove(obj,do_unlink=True)
gate.matrix_world=Matrix.Identity(4)
e._generate_compound_boxes(gate,8,.02)
player=bpy.data.objects.new('Player',None); bpy.context.scene.collection.objects.link(player)
player.yan_level.role='PLAYER'; player.location=(3,-2,0)
project=ROOT/'generated/compound-tests/project'
(project/'resources/Data').mkdir(parents=True,exist_ok=True)
for name in ('enemies.json','weapons.json'):
    shutil.copyfile(ROOT/'resources/Data'/name,project/'resources/Data'/name)
settings=bpy.context.scene.yan_level
settings.project_root=str(project); settings.stage_id='compound'; settings.output_directory='resources/levels/compound'
bpy.context.view_layer.update()
out=e.export_level(bpy.context)
data=json.loads((out/'compound.json').read_text())
assert data['version']==1 and len(data['colliders'])==3
assert all(set(c)=={'id','position','rotation','scale','localBounds'} for c in data['colliders'])
assert all(c['id'].startswith('COL_Gate_') for c in data['colliders'])
print('PASS',bpy.app.version_string, 'shape counts 1/1/2/3/2, holes, all axes, budget, padding, transforms, regenerate, manual protection, clear, unchanged JSON + glTF export',flush=True)
