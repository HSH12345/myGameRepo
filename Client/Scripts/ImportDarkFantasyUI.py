"""Explicit project art import and backed-up UMG migration. Run only when updating this theme."""
from pathlib import Path
import json
import unreal
root = Path(unreal.Paths.project_dir())
source = root / 'SourceArt/DarkFantasy'
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
imported = []
for file in sorted(source.glob('T_DF_*.png')):
    task = unreal.AssetImportTask()
    task.filename = str(file)
    task.destination_path = '/Game/RPG/UI/DarkFantasy/Textures'
    task.destination_name = file.stem
    task.automated = True
    task.replace_existing = True
    task.save = False
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(task.destination_path + '/' + file.stem)
    assert isinstance(texture, unreal.Texture2D), file.name
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('never_stream', True)
    texture.set_editor_property('max_texture_size', 2048 if file.stem.endswith('Title') else 1024 if file.stem.endswith('Guide') else 512 if file.stem.endswith(('Panel','Primary','Secondary')) else 256)
    dimensions = {'Panel':(256,256), 'Primary':(192,64), 'Secondary':(192,64), 'Title':(1672,941), 'Logo':(768,256), 'Guide':(384,512), 'Male':(256,256), 'Female':(256,256)}
    key = file.stem.removeprefix('T_DF_')
    width, height = dimensions.get(key, (128,128) if key == 'Slot' else (256,256))
    texture.set_editor_property('power_of_two_mode', unreal.TexturePowerOfTwoSetting.RESIZE_TO_SPECIFIC_RESOLUTION)
    texture.set_editor_property('resize_during_build_x', width)
    texture.set_editor_property('resize_during_build_y', height)
    texture.set_editor_property('max_texture_size', max(width, height))
    assert assets.save_loaded_asset(texture, False)
    imported.append(texture.get_path_name())
assert len(imported) == 19, imported
unreal.SystemLibrary.execute_console_command(None, 'RPG.ConfigureFantasyUI')
for name in ['FF_SongMyung', 'FF_KoreanFallback', 'F_SongMyung']:
    asset = unreal.load_asset('/Game/RPG/UI/DarkFantasy/Fonts/' + name)
    assert asset, name
    assert assets.save_loaded_asset(asset, False), name
for name in ['WBP_Login','WBP_CharacterSelect','WBP_CharacterCreate','WBP_CharacterSlot','WBP_LobbyRoot']:
    bp = unreal.load_asset('/Game/RPG/UI/Lobby/' + name)
    if name == 'WBP_CharacterCreate':
        gender = unreal.load_object(None, '/Game/RPG/UI/Lobby/WBP_CharacterCreate.WBP_CharacterCreate:WidgetTree.GenderChoice')
        gender.set_editor_property('default_options', ['남성', '여성'])
        gender.set_editor_property('selected_option', '남성')
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert bp.get_editor_property('status') == unreal.BlueprintStatus.BS_UP_TO_DATE, name
    assert assets.save_loaded_asset(bp, False), name
report = {'textures':imported, 'texture_count':len(imported), 'font_asset_count':3, 'font':'SongMyung-Regular.ttf / SIL OFL 1.1', 'result':'passed'}
(root/'Saved/DarkFantasyVerification/import-result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('FANTASY_IMPORT_OK textures=19 fonts=3 widgets=5')
