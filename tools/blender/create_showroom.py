"""Run with Blender --background --python this_file.py to rebuild the editable example."""
import sys
from pathlib import Path
import json
import bpy
from mathutils import Matrix, Vector, Euler
sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as exporter
exporter.register()
root = Path(__file__).resolve().parents[2]
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.name = 'Showroom'
scene.yan_level.project_root = str(root)
scene.yan_level.stage_id = 'showroom'
scene.yan_level.output_directory = 'resources/levels/showroom'
parent = bpy.data.collections.new('Showroom')
scene.collection.children.link(parent)
collections = {}
for name in ['Geometry','Colliders','EnemySpawns','Triggers','WeaponSpawns','PlayerSpawn','Goal']:
    collection = bpy.data.collections.new(name)
    parent.children.link(collection)
    collections[name] = collection


def pose(obj, position, rotation=(0,0,0), scale=(1,1,1)):
    engine = Matrix.LocRotScale(Vector(position), Euler(rotation,'XYZ').to_quaternion(), Vector(scale))
    obj.matrix_world = exporter.ENGINE_BASIS.inverted() @ engine @ exporter.ENGINE_BASIS


def make(name, role, collection, position, size=None, rotation=(0,0,0), mesh=False):
    if mesh:
        bpy.ops.mesh.primitive_cube_add(size=2)
        obj = bpy.context.object
        for c in list(obj.users_collection): c.objects.unlink(obj)
    else:
        obj = bpy.data.objects.new(name, None)
    obj.name = name
    collections[collection].objects.link(obj)
    obj.yan_level.role = role
    pose(obj,position,rotation,tuple(v*.5 for v in size) if size else (1,1,1))
    return obj


def material(name, color):
    mat=bpy.data.materials.new(name)
    mat.diffuse_color=(*color,1)
    if bpy.app.version < (5,0,0): mat.use_nodes=True
    shader=next(node for node in mat.node_tree.nodes if node.type=='BSDF_PRINCIPLED')
    next(socket for socket in shader.inputs if socket.identifier=='Base Color').default_value=(*color,1)
    next(socket for socket in shader.inputs if socket.identifier=='Roughness').default_value=.8
    return mat
floor_material=material('Floor Gray',(.48,.5,.53))
wall_material=material('Wall White',(.82,.84,.87))
strip_material=material('Range Strip',(.09,.13,.18))
plinth_material=material('Pickup Plinth',(.7,.72,.75))
# Open 48m x 56m room; Y=0 is the shared feet/floor reference.
for name,pos,size in [
    ('Floor',(0,-.25,12),(48,.5,56)),
    ('Wall_Left',(-24.25,3,12),(.5,6,56)),
    ('Wall_Right',(24.25,3,12),(.5,6,56)),
    ('Wall_Back',(0,3,-16.25),(48,6,.5)),
    ('Wall_Target',(0,3,40.25),(48,6,.5))]:
    obj=make(name,'STATIC','Geometry',pos,size,mesh=True)
    obj.yan_level.collision='BOX'
    obj.data.materials.append(floor_material if name=='Floor' else wall_material)
make('PlayerSpawn','PLAYER','PlayerSpawn',(-2,0,-14))
# The showroom reads these points directly. No Trigger or Goal objects exist.
for i,enemy in enumerate(['normal','ranged','fast','tank','bomber']):
    obj=make(f'Enemy_{i:02}_{enemy}','ENEMY','EnemySpawns',(-2+4*i,0,14),rotation=(0,-1.57079632679,0))
    obj.yan_level.group='Display'
    row=obj.yan_level.pool.add(); row.identifier=enemy; row.weight=1
weapons=json.loads((root/'resources/Data/weapons.json').read_text(encoding='utf-8'))['weapons']
order=['pistol','smg','rifle','shotgun','pump_shotgun','auto_shotgun','burst_rifle']
order += [w['id'] for w in weapons if w['id'] not in order]
for i,weapon in enumerate(order):
    x=-18+4*(i%4); z=2+4*(i//4)
    obj=make(f'Weapon_{i:02}_{weapon}','WEAPON','WeaponSpawns',(x,.9,z))
    row=obj.yan_level.pool.add(); row.identifier=weapon; row.weight=1
    # Low plinths mark each pickup, without blocking walking/shooting.
    obj=make(f'Plinth_{i:02}','STATIC','Geometry',(x,.1,z),(1.5,.2,1.5),mesh=True)
    obj.data.materials.append(plinth_material)
# Range strips are visual references, never colliders.
for z in [-1,4,9]:
    obj=make(f'RangeStrip_{z+1:02}','STATIC','Geometry',(6,.012,z),(20,.024,.08),mesh=True)
    obj.data.materials.append(strip_material)
bpy.context.view_layer.update()
exporter.export_level(bpy.context)
scene.yan_level.project_root='//../../../'
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(root/'resources/levels/showroom/showroom.blend'))
print('SHOWROOM_CREATED')
