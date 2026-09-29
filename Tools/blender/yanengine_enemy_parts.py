"""Independent Enemy Asset authoring add-on. No dependency on the level exporter.
Face-domain integer IDs survive polygon reordering; IDs are never reused on Add.
Exports a static/rest-pose triangle snapshot, including normals and UVs, in engine space.
"""
bl_info = {"name": "YanEngine Enemy Parts", "author": "YanEngine", "version": (1, 0, 0),
           "blender": (4, 4, 0), "location": "View3D > Sidebar > Enemy Parts", "category": "Object"}
import json
import math
from pathlib import Path
import bpy
import bmesh
from bpy.props import (StringProperty, IntProperty, FloatProperty, BoolProperty,
                       EnumProperty, CollectionProperty)
from bpy_extras.io_utils import ExportHelper

ATTRIBUTE = "yan_enemy_part"
ROLES = ["Generic", "Head", "Body", "Arm", "Leg", "Core", "Armor"]


def active_mesh(context):
    obj = context.active_object
    if not obj or obj.type != 'MESH':
        raise ValueError("Select a mesh object")
    return obj


def edit_faces(obj, action, part_id=0):
    """One integer per face guarantees mutually exclusive assignment."""
    editing = obj.mode == 'EDIT'
    bm = bmesh.from_edit_mesh(obj.data) if editing else bmesh.new()
    if not editing:
        bm.from_mesh(obj.data)
    layer = bm.faces.layers.int.get(ATTRIBUTE) or bm.faces.layers.int.new(ATTRIBUTE)
    for face in bm.faces:
        if action == 'SELECT':
            face.select_set(face[layer] == part_id)
        elif action == 'REMOVE':
            if face[layer] == part_id:
                face[layer] = 0
        elif face.select:
            face[layer] = part_id if action == 'ASSIGN' else 0
    if action == 'SELECT':
        bm.select_mode = {'FACE'}
        if editing:
            bpy.context.tool_settings.mesh_select_mode = (False, False, True)
        bm.select_flush_mode()
    if editing:
        bmesh.update_edit_mesh(obj.data)
    else:
        bm.to_mesh(obj.data)
        bm.free()
    obj.data.update()


def add_part(mesh, name="Part", role="Generic"):
    mesh.yan_enemy_next_id += 1
    part = mesh.yan_enemy_parts.add()
    part.part_id = mesh.yan_enemy_next_id
    part.name = name
    part.role = role
    mesh.yan_enemy_part_index = len(mesh.yan_enemy_parts) - 1
    return part


def export_asset(obj, filepath):
    # FACE attributes have no RNA data while edit BMesh owns them. Flush and
    # temporarily leave edit mode; always restore it, including validation failure.
    editing = obj.mode == 'EDIT'
    if editing:
        bpy.ops.object.mode_set(mode='OBJECT')
    try:
        return _export_asset(obj, filepath)
    finally:
        if editing:
            bpy.ops.object.mode_set(mode='EDIT')


def _export_asset(obj, filepath):
    if obj.mode == 'EDIT':
        obj.update_from_editmode()
    mesh = obj.data
    # Evaluating topology modifiers would detach polygon IDs from their authored faces.
    if any(m.type != 'ARMATURE' and (m.show_viewport or m.show_render) for m in obj.modifiers):
        raise ValueError("Apply topology/deformation modifiers before assigning parts; armature exports rest mesh only")
    if abs(obj.matrix_world.to_3x3().determinant()) < 1e-8:
        raise ValueError("Object transform is singular")
    groups = []
    group_ids = set()
    for group in mesh.yan_enemy_groups:
        if not group.name or group.name in group_ids or not math.isfinite(group.max_hp) or group.max_hp <= 0:
            raise ValueError("HP group names must be unique, and max HP positive")
        group_ids.add(group.name)
        groups.append(dict(id=group.name, maxHp=group.max_hp, deathOnZero=group.death_on_zero))
    parts = {}
    names = set()
    for part in mesh.yan_enemy_parts:
        if not part.name or part.name == '__Unassigned' or part.name in names:
            raise ValueError("Part names must be unique; __Unassigned is reserved")
        if part.part_id <= 0 or part.part_id in parts:
            raise ValueError("Invalid/duplicate face assignment ID")
        names.add(part.name)
        local = part.hp_mode in {'Local', 'LocalAndShared'}
        shared = part.hp_mode in {'Shared', 'LocalAndShared'}
        if local and (not math.isfinite(part.local_hp) or part.local_hp <= 0):
            raise ValueError("Local HP must be positive")
        if not math.isfinite(part.shared_rate) or part.shared_rate < 0:
            raise ValueError("Shared Damage Rate must be nonnegative")
        if shared and part.shared_group not in group_ids:
            raise ValueError("Unknown HP group for " + part.name)
        parts[part.part_id] = dict(name=part.name, role=part.role,
            localHp=part.local_hp if local else None,
            sharedHpGroup=part.shared_group if shared else '', sharedDamageRate=part.shared_rate,
            breakable=part.breakable, deathOnZero=part.death_on_zero and local, faces=[], triangles=[])
    attr = mesh.attributes.get(ATTRIBUTE)
    if attr and (attr.domain != 'FACE' or attr.data_type != 'INT'):
        raise ValueError("Enemy Part attribute must be FACE / INT")
    for polygon in mesh.polygons:
        part_id = attr.data[polygon.index].value if attr else 0
        if part_id and part_id not in parts:
            raise ValueError("Face references a removed/missing Part; use Clear Assignment")
        if part_id == 0 and 0 not in parts:
            parts[0] = dict(name='__Unassigned', role='Generic', localHp=None,
                sharedHpGroup='', sharedDamageRate=0, breakable=False, deathOnZero=False, faces=[], triangles=[])
        parts[part_id]['faces'].append(polygon.index)
    mesh.calc_loop_triangles()
    matrix = obj.matrix_world
    normal_matrix = matrix.to_3x3().inverted().transposed()
    uv = mesh.uv_layers.active
    # Blender (X,Y,Z) -> engine (-X,Z,-Y), including winding parity.
    def engine(v):
        result = [-float(v.x), float(v.z), -float(v.y)]
        if not all(math.isfinite(x) for x in result):
            raise ValueError("Non-finite geometry")
        return result
    for triangle in mesh.loop_triangles:
        face = triangle.polygon_index
        part_id = attr.data[face].value if attr else 0
        loops = list(triangle.loops)
        if matrix.to_3x3().determinant() > 0:
            loops.reverse()
        vertices = []
        for loop_index in loops:
            loop = mesh.loops[loop_index]
            p = matrix @ mesh.vertices[loop.vertex_index].co
            n = (normal_matrix @ mesh.corner_normals[loop_index].vector).normalized()
            texcoord = uv.data[loop_index].uv if uv else (0, 0)
            vertices.append(dict(p=engine(p), n=engine(n), uv=[float(texcoord[0]), 1-float(texcoord[1])]))
        parts[part_id]['triangles'].append(dict(face=face, vertices=vertices))
    if not parts or any(not part['triangles'] for part in parts.values()):
        raise ValueError("Each Part needs faces; assign faces or remove empty Parts")
    data = dict(version=1, coordinateSystem='yanengine', texture=mesh.yan_enemy_texture,
                hpGroups=groups, parts=list(parts.values()))
    output = Path(filepath)
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = output.with_suffix(output.suffix + '.tmp')
    temp.write_text(json.dumps(data, ensure_ascii=False, separators=(',', ':'), allow_nan=False), encoding='utf-8')
    temp.replace(output)
    return data


class YAN_EnemyPart(bpy.types.PropertyGroup):
    part_id: IntProperty(default=0)
    role: EnumProperty(items=[(x, x, '') for x in ROLES], default='Generic')
    hp_mode: EnumProperty(items=[(x, x, '') for x in ['Local', 'Shared', 'LocalAndShared', 'Invincible']], default='Local')
    local_hp: FloatProperty(name='Local HP', default=60, min=.001, max=1e8)
    shared_group: StringProperty(name='Shared HP Group')
    shared_rate: FloatProperty(name='Shared Damage Rate', default=1, min=0, max=1e8)
    breakable: BoolProperty(name='Breakable', default=True)
    death_on_zero: BoolProperty(name='Death on Local HP Zero', default=False)


class YAN_EnemyHpGroup(bpy.types.PropertyGroup):
    max_hp: FloatProperty(name='Max HP', default=5000, min=.001, max=1e8)
    death_on_zero: BoolProperty(name='Death on Group HP Zero', default=True)


class YAN_OT_enemy_part(bpy.types.Operator):
    bl_idname = 'yan.enemy_part'
    bl_label = 'Enemy Part'
    bl_options = {'REGISTER', 'UNDO'}
    action: EnumProperty(items=[(x, x, '') for x in ['ADD', 'REMOVE', 'ASSIGN', 'SELECT', 'CLEAR', 'GROUP_ADD', 'GROUP_REMOVE']])

    def execute(self, context):
        try:
            obj = active_mesh(context)
            mesh = obj.data
            if self.action == 'ADD':
                add_part(mesh, 'Part' + str(mesh.yan_enemy_next_id + 1))
            elif self.action == 'GROUP_ADD':
                group = mesh.yan_enemy_groups.add()
                group.name = 'Group' + str(len(mesh.yan_enemy_groups))
                mesh.yan_enemy_group_index = len(mesh.yan_enemy_groups) - 1
            elif self.action == 'GROUP_REMOVE':
                if mesh.yan_enemy_groups:
                    mesh.yan_enemy_groups.remove(mesh.yan_enemy_group_index)
                    mesh.yan_enemy_group_index = max(0, len(mesh.yan_enemy_groups)-1)
            elif self.action == 'CLEAR':
                edit_faces(obj, 'CLEAR')
            elif mesh.yan_enemy_parts:
                index = mesh.yan_enemy_part_index
                part = mesh.yan_enemy_parts[index]
                edit_faces(obj, self.action, part.part_id)
                if self.action == 'REMOVE':
                    mesh.yan_enemy_parts.remove(index)
                    mesh.yan_enemy_part_index = max(0, min(index, len(mesh.yan_enemy_parts)-1))
            return {'FINISHED'}
        except (ValueError, IndexError) as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_OT_enemy_export(bpy.types.Operator, ExportHelper):
    bl_idname = 'yan.export_enemy_parts'
    bl_label = 'Export Enemy Asset'
    filename_ext = '.enemy.json'
    filter_glob: StringProperty(default='*.json', options={'HIDDEN'})

    def execute(self, context):
        try:
            data = export_asset(active_mesh(context), self.filepath)
            self.report({'INFO'}, 'Exported %d parts' % len(data['parts']))
            return {'FINISHED'}
        except (ValueError, OSError) as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_PT_enemy_parts(bpy.types.Panel):
    bl_label = 'Enemy Parts'
    bl_idname = 'YAN_PT_enemy_parts'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Enemy Parts'

    @classmethod
    def poll(cls, context):
        return context.active_object and context.active_object.type == 'MESH'

    def draw(self, context):
        layout = self.layout
        mesh = context.active_object.data
        layout.label(text='Static / rest mesh; independent of Level Editor')
        layout.template_list('UI_UL_list', 'enemy_parts', mesh, 'yan_enemy_parts', mesh, 'yan_enemy_part_index')
        row = layout.row()
        row.operator('yan.enemy_part', text='Add Part').action = 'ADD'
        row.operator('yan.enemy_part', text='Remove Part').action = 'REMOVE'
        if mesh.yan_enemy_parts:
            part = mesh.yan_enemy_parts[mesh.yan_enemy_part_index]
            layout.prop(part, 'name', text='Part Name')
            layout.prop(part, 'role', text='Role')
            layout.operator('yan.enemy_part', text='Assign Selected Faces').action = 'ASSIGN'
            layout.operator('yan.enemy_part', text='Select Part Faces').action = 'SELECT'
            layout.prop(part, 'hp_mode', text='HP Mode')
            if part.hp_mode in {'Local', 'LocalAndShared'}:
                layout.prop(part, 'local_hp')
                layout.prop(part, 'breakable')
                layout.prop(part, 'death_on_zero')
            if part.hp_mode in {'Shared', 'LocalAndShared'}:
                layout.prop_search(part, 'shared_group', mesh, 'yan_enemy_groups')
                layout.prop(part, 'shared_rate')
        layout.operator('yan.enemy_part', text='Clear Assignment (selected faces)').action = 'CLEAR'
        layout.separator()
        layout.label(text='HP Groups')
        layout.template_list('UI_UL_list', 'enemy_groups', mesh, 'yan_enemy_groups', mesh, 'yan_enemy_group_index', rows=3)
        row = layout.row()
        row.operator('yan.enemy_part', text='Add Group').action = 'GROUP_ADD'
        row.operator('yan.enemy_part', text='Remove Group').action = 'GROUP_REMOVE'
        if mesh.yan_enemy_groups:
            group = mesh.yan_enemy_groups[mesh.yan_enemy_group_index]
            layout.prop(group, 'name', text='Group ID')
            layout.prop(group, 'max_hp')
            layout.prop(group, 'death_on_zero')
        layout.prop(mesh, 'yan_enemy_texture', text='Texture (game-relative)')
        layout.operator('yan.export_enemy_parts')


CLASSES = [YAN_EnemyPart, YAN_EnemyHpGroup, YAN_OT_enemy_part, YAN_OT_enemy_export, YAN_PT_enemy_parts]


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Mesh.yan_enemy_parts = CollectionProperty(type=YAN_EnemyPart)
    bpy.types.Mesh.yan_enemy_groups = CollectionProperty(type=YAN_EnemyHpGroup)
    bpy.types.Mesh.yan_enemy_part_index = IntProperty(default=0, min=0)
    bpy.types.Mesh.yan_enemy_group_index = IntProperty(default=0, min=0)
    bpy.types.Mesh.yan_enemy_next_id = IntProperty(default=0, min=0)
    bpy.types.Mesh.yan_enemy_texture = StringProperty(default='resources/white1x1.png')


def unregister():
    for name in ['yan_enemy_parts', 'yan_enemy_groups', 'yan_enemy_part_index', 'yan_enemy_group_index', 'yan_enemy_next_id', 'yan_enemy_texture']:
        delattr(bpy.types.Mesh, name)
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)


if __name__ == '__main__':
    register()
