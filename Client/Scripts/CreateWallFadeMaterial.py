"""Create one translucent variant and tag existing dungeon walls; preserve geometry."""
import json,shutil
from pathlib import Path
import unreal
project=Path(unreal.Paths.project_dir())
backup=Path('C:/Users/hsh/Desktop/TcpIP/IOCP/MyRpgServer/artifacts/WallFade-20261018/backup/client')
target='/Game/RPG/Maps/Dungeon/L_Dungeon_01'
file=project/'Content/RPG/Maps/Dungeon/L_Dungeon_01.umap'
saved=backup/file.relative_to(project)
if not saved.exists():saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,saved)
path='/Game/RPG/Camera/M_WallFade'
m=unreal.load_asset(path)
if not m:
    m=unreal.EditorAssetLibrary.duplicate_asset('/Engine/EngineMaterials/WorldGridMaterial',path)
    if not m:raise RuntimeError('Grid material variant duplication failed')
    m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE)
    m.set_editor_property('translucency_pass',unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF)
    lib=unreal.MaterialEditingLibrary
    alpha=lib.create_material_expression(m,unreal.MaterialExpressionScalarParameter)
    alpha.set_editor_property('parameter_name','WallOpacity');alpha.set_editor_property('default_value',1)
    lib.connect_material_property(alpha,'',unreal.MaterialProperty.MP_OPACITY)
    lib.set_material_usage(m,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    lib.recompile_material(m)
    if not unreal.EditorAssetLibrary.save_loaded_asset(m):raise RuntimeError('Wall fade save failed')
if not unreal.EditorLevelLibrary.load_level(target):raise RuntimeError('Dungeon level missing')
actors=[a for a in unreal.EditorLevelLibrary.get_all_level_actors() if a.get_actor_label().startswith('DungeonMaze_Wall_')]
if not actors:raise RuntimeError('Expected generated dungeon walls')
before=[str(a.get_actor_transform()) for a in actors]
for a in actors:
    tags=list(a.get_editor_property('tags'))
    if 'RPG.CameraFade' not in [str(t) for t in tags]:tags.append('RPG.CameraFade')
    a.set_editor_property('tags',tags)
if not unreal.EditorLoadingAndSavingUtils.save_map(unreal.EditorLevelLibrary.get_editor_world(),target):raise RuntimeError('Tag save failed')
assert before==[str(a.get_actor_transform()) for a in actors]
for a in actors:
    assert a.static_mesh_component.get_collision_profile_name()=='BlockAll'
    assert a.static_mesh_component.get_material(0).get_name()=='WorldGridMaterial'
unreal.log('WALL_FADE_SAVED tagged='+str(len(actors))+' geometry/collision/material-preserved')
