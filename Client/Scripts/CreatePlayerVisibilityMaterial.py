"""Create only the own-player occlusion reveal material; no map or source-art changes."""
from pathlib import Path
import shutil
import unreal

project=Path(unreal.Paths.project_dir())
backup=Path(r'C:/Users/hsh/Desktop/TcpIP/IOCP/MyRpgServer/artifacts/CameraHudFix-20261018/backup/client')
file=project/'Content/RPG/Camera/M_PlayerVisibility.uasset'
if file.exists():
    saved=backup/file.relative_to(project)
    if not saved.exists():saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,saved)
tools=unreal.AssetToolsHelpers.get_asset_tools();lib=unreal.MaterialEditingLibrary
m=unreal.load_asset('/Game/RPG/Camera/M_PlayerVisibility') or tools.create_asset('M_PlayerVisibility','/Game/RPG/Camera',unreal.Material,unreal.MaterialFactoryNew())
lib.delete_all_material_expressions(m)
m.set_editor_property('material_domain',unreal.MaterialDomain.MD_POST_PROCESS)
m.set_editor_property('blendable_location',unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
custom=lib.create_material_expression(m,unreal.MaterialExpressionCustom)
custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
names=['Scene','Depth','OwnDepth','Stencil']
inputs=[]
for name in names:
    entry=unreal.CustomInput();entry.set_editor_property('input_name',name);inputs.append(entry)
custom.set_editor_property('inputs',inputs)
custom.set_editor_property('code','''
float own = 1.0-step(0.5,abs(Stencil.r-241.0));
float behind = step(Depth.r+4.0,OwnDepth.r);
float mask = own*behind;
return lerp(Scene.rgb,float3(0.05,0.85,1.0),mask*0.85);
''')
for name,id in zip(names,['PPI_POST_PROCESS_INPUT0','PPI_SCENE_DEPTH','PPI_CUSTOM_DEPTH','PPI_CUSTOM_STENCIL']):
    node=lib.create_material_expression(m,unreal.MaterialExpressionSceneTexture)
    node.set_editor_property('scene_texture_id',getattr(unreal.SceneTextureId,id))
    lib.connect_material_expressions(node,'Color',custom,name)
lib.connect_material_property(custom,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
lib.recompile_material(m)
if not unreal.EditorAssetLibrary.save_loaded_asset(m):raise RuntimeError('Visibility material save failed')
unreal.log('PLAYER_VISIBILITY_MATERIAL_SAVED stencil=241 after-tonemapping occluded-only')
