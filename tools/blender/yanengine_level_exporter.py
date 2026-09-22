"""YanEngine level authoring. Blender 4.4+; install this file as a legacy add-on."""
bl_info = {"name": "YanEngine Level", "author": "YanEngine", "version": (1, 0, 0),
           "blender": (4, 4, 0), "location": "View3D > Sidebar > YanEngine Level", "category": "Import-Export"}
import bpy
from bpy.props import BoolProperty, StringProperty, EnumProperty, FloatProperty, IntProperty, PointerProperty, CollectionProperty
from mathutils import Matrix, Vector
import json
import math
import os
from pathlib import Path
import re
import shutil
import uuid

# Blender -> glTF: (x,z,-y). Model.cpp mirrors glTF X for vertices AND node transforms.
# Thus engine point=(-x,z,-y). Matrix conjugation handles parent transforms and rotations.
ENGINE_BASIS = Matrix(((-1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))


def finite(values, label):
    if any(not math.isfinite(float(v)) or abs(float(v)) > 1e6 for v in values):
        raise ValueError(f"{label}: invalid number (NaN/Infinity/out of range)")


def engine_transform(matrix):
    """The sole coordinate conversion helper. Returns engine TRS and column-vector matrix."""
    converted = ENGINE_BASIS @ matrix @ ENGINE_BASIS.inverted()
    position, quaternion, scale = converted.decompose()
    finite([v for row in converted for v in row], "Transform")
    if min(scale) <= 1e-5:
        raise ValueError("Gameplay transforms require positive nonzero scale; apply mirrored scale first")
    rebuilt = Matrix.LocRotScale(position, quaternion, scale)
    if max(abs(converted[i][j] - rebuilt[i][j]) for i in range(4) for j in range(4)) > 1e-4:
        raise ValueError("Sheared gameplay transform; apply transforms or remove nonuniform scaled parent")
    return {"position": list(position), "rotation": list(quaternion.to_euler('XYZ')), "scale": list(scale)}, converted


def bounds(obj, depsgraph):
    if obj.type == 'EMPTY':
        if obj.empty_display_type != 'CUBE':
            raise ValueError(f"{obj.name}: use a Cube Empty for volume roles")
        s = obj.empty_display_size
        points = [Vector((x*s, y*s, z*s)) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
    elif obj.type == 'MESH':
        evaluated = obj.evaluated_get(depsgraph)
        points = [Vector(p) for p in evaluated.bound_box]
    else:
        raise ValueError(f"{obj.name}: volume requires a Mesh or Cube Empty")
    points = [(ENGINE_BASIS @ p.to_4d()).to_3d() for p in points]
    low = [min(p[i] for p in points) for i in range(3)]
    high = [max(p[i] for p in points) for i in range(3)]
    finite(low+high, obj.name)
    if min(high[i]-low[i] for i in range(3)) <= 1e-5:
        raise ValueError(f"{obj.name}: collider/volume size is zero")
    return {"min": low, "max": high}


def box_data(obj, depsgraph):
    trs, matrix = engine_transform(obj.matrix_world)
    return dict(trs, localBounds=bounds(obj, depsgraph)), matrix


def trigger_volume(obj, depsgraph):
    data, matrix = box_data(obj, depsgraph)
    # Existing trigger systems are AABBs. Reject tilted volumes rather than silently inflate them.
    rotation = matrix.to_3x3().normalized()
    for row in rotation:
        if sum(abs(v) > 1e-4 for v in row) != 1:
            raise ValueError(f"{obj.name}: Trigger/Goal must be axis-aligned (90 degree rotations allowed)")
    b = data['localBounds']
    corners = [matrix @ Vector((x, y, z, 1)) for x in (b['min'][0], b['max'][0])
               for y in (b['min'][1], b['max'][1]) for z in (b['min'][2], b['max'][2])]
    low = [min(p[i] for p in corners) for i in range(3)]
    high = [max(p[i] for p in corners) for i in range(3)]
    return {"position": [(a+b)*.5 for a, b in zip(low, high)], "size": [b-a for a, b in zip(low, high)]}


def object_id(obj):
    return obj.yan_level.identifier.strip() or obj.name


def pool_data(pool, label, known=None):
    entries = []
    for row in pool:
        key = row.identifier.strip()
        finite([row.weight], label)
        if not key or row.weight < 0:
            raise ValueError(f"{label}: empty ID or negative weight")
        if known is not None and key not in known:
            raise ValueError(f"{label}: unknown ID {key}")
        entries.append({"id": key, "weight": row.weight})
    if not entries or sum(e['weight'] for e in entries) <= 0:
        raise ValueError(f"{label}: add a pool entry with positive weight")
    return entries


def known_ids(scene, filename, key):
    root = scene.yan_level.project_root
    if not root:
        return None  # Standalone add-on: IDs stay editable without an engine checkout.
    path = Path(bpy.path.abspath(root)) / 'resources' / 'Data' / filename
    if not path.is_file():
        raise ValueError(f"Definition file missing: {path}")
    return {entry['id'] for entry in json.loads(path.read_text(encoding='utf-8'))[key]}


def build_level(scene, depsgraph):
    settings = scene.yan_level
    stage_id = settings.stage_id.strip()
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]*', stage_id):
        raise ValueError("Stage ID must start with a letter; use letters, digits, '-' or '_'")
    objects = sorted((o for o in scene.objects if o.yan_level.role != 'IGNORE'), key=lambda o: o.name)
    ids = set()
    for obj in objects:
        key = object_id(obj)
        if key in ids:
            raise ValueError(f"Duplicate ID: {key}")
        ids.add(key)
        finite([v for row in obj.matrix_world for v in row], obj.name)
    players = [o for o in objects if o.yan_level.role == 'PLAYER']
    if len(players) != 1:
        raise ValueError(f"Exactly one Player Spawn required (found {len(players)})")
    enemies = known_ids(scene, 'enemies.json', 'enemies')
    weapons = known_ids(scene, 'weapons.json', 'weapons')
    data = {"version": 1, "stage": {"id": stage_id, "model": f"levels/{stage_id}/{stage_id}.gltf"},
            "playerSpawn": {}, "colliders": [], "enemyRandom": {"useFixedSeed": settings.fixed_seed, "seed": settings.seed},
            "spawnPoints": [], "spawnTriggers": [], "weaponRandom": {"useFixedSeed": settings.fixed_seed, "seed": settings.seed},
            "weaponSpawnPoints": [], "goalTriggers": []}
    groups = {}
    geometry = []
    for obj in objects:
        cfg = obj.yan_level
        key = object_id(obj)
        if cfg.role == 'STATIC':
            if obj.type != 'MESH':
                raise ValueError(f"{key}: Static Mesh requires a Mesh object")
            geometry.append(obj)
            if cfg.collision == 'CUSTOM':
                target = cfg.custom_collider
                if target is None or target.name not in scene.objects or target.yan_level.role != 'COLLIDER':
                    raise ValueError(f"{key}: Custom collision requires a Collider object in this scene")
            if cfg.collision != 'BOX':
                continue
        if cfg.role == 'COLLIDER' or (cfg.role == 'STATIC' and cfg.collision == 'BOX'):
            box, _ = box_data(obj, depsgraph)
            data['colliders'].append(dict(id=key, **box))
        elif cfg.role in {'PLAYER', 'ENEMY', 'WEAPON'}:
            trs, _ = engine_transform(obj.matrix_world)
            pose = {"position": trs['position'], "rotation": trs['rotation']}
            if cfg.role == 'PLAYER':
                data['playerSpawn'] = pose
            elif cfg.role == 'ENEMY':
                if not cfg.group.strip():
                    raise ValueError(f"{key}: Spawn Group required")
                groups.setdefault(cfg.group.strip(), []).append(key)
                data['spawnPoints'].append(dict(id=key, **pose, enemyPool=pool_data(cfg.pool, key, enemies)))
            else:
                data['weaponSpawnPoints'].append(dict(id=key, **pose, weaponPool=pool_data(cfg.pool, key, weapons)))
        elif cfg.role == 'GOAL':
            data['goalTriggers'].append(dict(id=key, **trigger_volume(obj, depsgraph)))
    for obj in objects:
        cfg = obj.yan_level
        if cfg.role != 'TRIGGER':
            continue
        points = groups.get(cfg.group.strip(), [])
        if not points:
            raise ValueError(f"{obj.name}: Spawn Group '{cfg.group}' has no Enemy Spawn")
        finite([cfg.spawn_interval, cfg.initial_delay], obj.name)
        if cfg.spawn_count < 1 or cfg.max_alive < 1 or cfg.spawn_interval < 0 or cfg.initial_delay < 0:
            raise ValueError(f"{obj.name}: invalid count/timing")
        data['spawnTriggers'].append(dict(id=object_id(obj), **trigger_volume(obj, depsgraph), spawnPointIds=points,
            spawnCount=cfg.spawn_count, spawnInterval=cfg.spawn_interval, initialDelay=cfg.initial_delay,
            maxAlive=cfg.max_alive, selection=cfg.selection, oneShot=cfg.one_shot))
    if not geometry:
        raise ValueError("At least one Static Mesh is required")
    json.dumps(data, allow_nan=False)
    return data, geometry


def output_path(scene):
    cfg = scene.yan_level
    root = Path(bpy.path.abspath(cfg.project_root)).resolve() if cfg.project_root else None
    output = Path(bpy.path.abspath(cfg.output_directory)) if cfg.output_directory.startswith('//') else Path(cfg.output_directory)
    if not output.is_absolute():
        if root is None:
            raise ValueError("Set Project Root, or an absolute Output Directory")
        output = root / output
    output = output.resolve()
    if output.name != cfg.stage_id:
        raise ValueError("Output Directory's last folder must match Stage ID")
    if root and output != root / 'resources' / 'levels' / cfg.stage_id:
        raise ValueError("Use Project Root/resources/levels/StageID as output so game model paths resolve")
    return output


def export_level(context):
    if context.mode != 'OBJECT':
        raise ValueError("Switch to Object Mode before export")
    context.view_layer.update()
    depsgraph = context.evaluated_depsgraph_get()
    data, geometry = build_level(context.scene, depsgraph)
    target = output_path(context.scene)
    target.parent.mkdir(parents=True, exist_ok=True)
    scratch = target.with_name(f'.{target.name}-export-{uuid.uuid4().hex}')
    scratch.mkdir()
    backup = target.with_name(f'.{target.name}-backup-{uuid.uuid4().hex}')
    temporary_scene = None
    source_scene = context.window.scene
    try:
        # Preserve unrelated author files; only generated stage assets are replaced.
        if target.exists():
            shutil.copytree(target, scratch, dirs_exist_ok=True)
        temporary_scene = bpy.data.scenes.new('__YanEngineExport')
        context.window.scene = temporary_scene
        # Bake evaluated geometry to world space, isolated from parent/helper visibility.
        # glTF conversion then Model.cpp's X reflection equals ENGINE_BASIS exactly.
        for source in geometry:
            mesh = bpy.data.meshes.new_from_object(source.evaluated_get(depsgraph), depsgraph=depsgraph)
            finite([v for vertex in mesh.vertices for v in vertex.co], source.name)
            mesh.transform(source.matrix_world)
            if source.matrix_world.determinant() < 0:
                mesh.flip_normals()
            obj = bpy.data.objects.new(source.name, mesh)
            temporary_scene.collection.objects.link(obj)
        context.view_layer.update()
        path = scratch / f"{data['stage']['id']}.gltf"
        result = bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLTF_SEPARATE',
            use_active_scene=True, export_yup=True, export_animations=False, export_skins=False,
            export_cameras=False, export_lights=False, export_extras=False)
        if 'FINISHED' not in result:
            raise RuntimeError("glTF export did not finish")
        gltf = json.loads(path.read_text(encoding='utf-8'))
        if not gltf.get('meshes'):
            raise ValueError("Exported glTF contains no meshes")
        for entry in gltf.get('buffers', []) + gltf.get('images', []):
            uri = entry.get('uri', '')
            if uri and not uri.startswith('data:') and not (scratch / uri).is_file():
                raise ValueError(f"Missing exported dependency: {uri}")
        (scratch / f"{data['stage']['id']}.json").write_text(json.dumps(data, indent=2, allow_nan=False)+'\n', encoding='utf-8')
        # Publish the entire dependency set together, with rollback on rename failure.
        if target.exists():
            os.replace(target, backup)
        try:
            os.replace(scratch, target)
        except Exception:
            if backup.exists():
                os.replace(backup, target)
            raise
        if backup.exists():
            shutil.rmtree(backup)
        return target
    finally:
        context.window.scene = source_scene
        if temporary_scene:
            for obj in list(temporary_scene.objects):
                mesh = obj.data
                bpy.data.objects.remove(obj, do_unlink=True)
                if mesh.users == 0:
                    bpy.data.meshes.remove(mesh)
            bpy.data.scenes.remove(temporary_scene)
        if scratch.exists():
            shutil.rmtree(scratch)


def role_changed(self, context):
    obj = self.id_data
    if not isinstance(obj, bpy.types.Object):
        return
    if obj.type == 'EMPTY':
        obj.empty_display_type = 'CUBE' if self.role in {'COLLIDER', 'TRIGGER', 'GOAL'} else 'ARROWS'
        obj.empty_display_size = 1.0
    if self.role == 'COLLIDER':
        obj.display_type = 'WIRE'
    colors = {'PLAYER': (.1, 1, .2, 1), 'ENEMY': (1, .2, .2, 1), 'WEAPON': (1, .8, .1, 1),
              'TRIGGER': (.1, .7, 1, 1), 'GOAL': (1, .2, 1, 1)}
    obj.color = colors.get(self.role, (1, 1, 1, 1))


class YAN_PoolEntry(bpy.types.PropertyGroup):
    identifier: StringProperty(name="ID", default="normal")
    weight: FloatProperty(name="Weight", default=1, min=0)


class YAN_ObjectSettings(bpy.types.PropertyGroup):
    role: EnumProperty(name="YanEngine Object Type", default='IGNORE', update=role_changed, items=[
        ('STATIC', 'Static Mesh', ''), ('COLLIDER', 'Collider', ''), ('PLAYER', 'Player Spawn', ''),
        ('ENEMY', 'Enemy Spawn', ''), ('TRIGGER', 'Spawn Trigger', ''), ('WEAPON', 'Weapon Spawn', ''),
        ('GOAL', 'Goal', ''), ('IGNORE', 'Ignore', '')])
    identifier: StringProperty(name="ID", description="Blank uses Object name")
    group: StringProperty(name="Spawn Group", default="A")
    collision: EnumProperty(name="Collision", items=[('NONE', 'None', ''), ('BOX', 'Box', ''), ('CUSTOM', 'Custom', '')])
    custom_collider: PointerProperty(name="Collider Object", type=bpy.types.Object)
    pool: CollectionProperty(type=YAN_PoolEntry)
    spawn_count: IntProperty(name="Spawn Count", default=5, min=1, max=10000)
    spawn_interval: FloatProperty(name="Spawn Interval", default=.5, min=0)
    initial_delay: FloatProperty(name="Initial Delay", default=0, min=0)
    max_alive: IntProperty(name="Max Alive", default=5, min=1, max=10000)
    selection: EnumProperty(name="Selection", items=[('Random', 'Random', ''), ('RoundRobin', 'RoundRobin', '')])
    one_shot: BoolProperty(name="One Shot", default=True)


class YAN_SceneSettings(bpy.types.PropertyGroup):
    stage_id: StringProperty(name="Stage ID", default="stage01")
    project_root: StringProperty(name="Project Root", description="Engine project folder; // paths are relative to the blend file")
    output_directory: StringProperty(name="Output Directory", default="resources/levels/stage01", subtype='DIR_PATH')
    fixed_seed: BoolProperty(name="Fixed Seed", default=False)
    seed: IntProperty(name="Seed", default=12345, min=0)


class YAN_OT_pool(bpy.types.Operator):
    bl_idname = 'yanengine.pool'
    bl_label = 'Edit Pool'
    bl_options = {'UNDO'}
    index: IntProperty(default=-1)
    def execute(self, context):
        cfg = context.object.yan_level
        if self.index < 0:
            row = cfg.pool.add()
            row.identifier = 'pistol' if cfg.role == 'WEAPON' else 'normal'
        elif self.index < len(cfg.pool):
            cfg.pool.remove(self.index)
        return {'FINISHED'}


class YAN_OT_validate(bpy.types.Operator):
    bl_idname = 'yanengine.validate'
    bl_label = 'Validate Level'
    def execute(self, context):
        try:
            context.view_layer.update()
            build_level(context.scene, context.evaluated_depsgraph_get())
            output_path(context.scene)
            self.report({'INFO'}, "Level valid")
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_OT_export(bpy.types.Operator):
    bl_idname = 'yanengine.export'
    bl_label = 'Export Level'
    def execute(self, context):
        try:
            target = export_level(context)
            self.report({'INFO'}, f"Exported {target}")
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_PT_level(bpy.types.Panel):
    bl_label = 'YanEngine Level'
    bl_idname = 'YAN_PT_level'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'YanEngine Level'
    def draw(self, context):
        layout = self.layout
        cfg = context.scene.yan_level
        for key in ('stage_id', 'project_root', 'output_directory', 'fixed_seed'):
            layout.prop(cfg, key)
        if cfg.fixed_seed:
            layout.prop(cfg, 'seed')
        layout.separator()
        obj = context.object
        if obj:
            settings = obj.yan_level
            layout.prop(settings, 'role')
            if settings.role != 'IGNORE':
                layout.prop(settings, 'identifier')
            if settings.role == 'STATIC':
                layout.prop(settings, 'collision')
                if settings.collision == 'CUSTOM':
                    layout.prop(settings, 'custom_collider')
            if settings.role in {'ENEMY', 'TRIGGER'}:
                layout.prop(settings, 'group')
            if settings.role in {'ENEMY', 'WEAPON'}:
                layout.label(text='Enemy Pool' if settings.role == 'ENEMY' else 'Weapon Pool')
                for index, entry in enumerate(settings.pool):
                    row = layout.row(align=True)
                    row.prop(entry, 'identifier', text='')
                    row.prop(entry, 'weight', text='')
                    row.operator('yanengine.pool', text='', icon='REMOVE').index = index
                layout.operator('yanengine.pool', text='Add Entry', icon='ADD').index = -1
            if settings.role == 'TRIGGER':
                for key in ('spawn_count', 'spawn_interval', 'initial_delay', 'max_alive', 'selection', 'one_shot'):
                    layout.prop(settings, key)
            if settings.role in {'COLLIDER', 'TRIGGER', 'GOAL'}:
                layout.label(text='Resize the Cube / Cube Empty in the viewport')
        layout.separator()
        layout.operator('yanengine.validate', icon='CHECKMARK')
        layout.operator('yanengine.export', icon='EXPORT')


CLASSES = (YAN_PoolEntry, YAN_ObjectSettings, YAN_SceneSettings, YAN_OT_pool, YAN_OT_validate, YAN_OT_export, YAN_PT_level)


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Object.yan_level = PointerProperty(type=YAN_ObjectSettings)
    bpy.types.Scene.yan_level = PointerProperty(type=YAN_SceneSettings)


def unregister():
    del bpy.types.Scene.yan_level
    del bpy.types.Object.yan_level
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)


if __name__ == '__main__':
    register()
