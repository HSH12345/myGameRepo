"""Repair only the wall/door fade material; never load or save a level."""
import unreal

path='/Game/RPG/Camera/M_WallFade'
material=unreal.load_asset(path)
if not material:
    raise RuntimeError('Existing wall fade material missing')
before=bool(material.get_editor_property('used_with_instanced_static_meshes'))
unreal.MaterialEditingLibrary.set_material_usage(material,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
if not material.get_editor_property('used_with_instanced_static_meshes'):
    raise RuntimeError('Instanced static mesh material usage was not enabled')
unreal.MaterialEditingLibrary.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material):
    raise RuntimeError('Wall/door fade material save failed')
unreal.log('DOOR_FADE_REPAIR instanced_before='+str(before)+' instanced_after=True saved='+path)
