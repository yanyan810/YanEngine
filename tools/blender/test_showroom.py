"""Read-only showroom authoring/export consistency checks; no game UI."""
import sys, json, math
from pathlib import Path
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e
e.register()
root=Path(__file__).resolve().parents[2]
bpy.ops.wm.open_mainfile(filepath=str(root/'resources/levels/showroom/showroom.blend'))
bpy.context.scene.yan_level.project_root=str(root)
bpy.context.view_layer.update()
data, geometry=e.build_level(bpy.context.scene,bpy.context.evaluated_depsgraph_get())
saved=json.loads((root/'resources/levels/showroom/showroom.json').read_text(encoding='utf-8'))
assert data==saved
assert not data['spawnTriggers'] and not data['goalTriggers']
assert len(data['spawnPoints'])==5 and len(data['weaponSpawnPoints'])==7
assert len(geometry)==15 and len(data['colliders'])==5
assert all(obj.data.materials for obj in geometry)
# Every initial lineup center fits the nominal 60-degree, 16:9 start view horizontally.
p=data['playerSpawn']['position']
for point in data['spawnPoints']+data['weaponSpawnPoints']:
    x,y,z=point['position']
    assert z>p[2] and abs((x-p[0])/(z-p[2])) < math.tan(math.pi/6)*16/9
print('SHOWROOM_BLENDER_TESTS_PASSED: saved/export agreement, roles, materials, initial view coverage')
