"""Import new equipment sources and create local presentation definitions. Run explicitly."""
import json
import os
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir())
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
SUBSYSTEM = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
CLASS = unreal.load_class(None, '/Script/RPG.EquipmentVisualDefinition')
if not CLASS:
    raise RuntimeError('Build RPGEditor before preparing equipment assets')
report = []


def imported(source, destination):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([destination], True)
    existing = [unreal.load_asset(str(data.package_name)) for data in registry.get_assets_by_path(destination, True)
                if str(data.asset_class_path.asset_name) == 'SkeletalMesh']
    refresh_head = 'EquipmentHeads' in str(source) and os.environ.get('RPG_REFRESH_EQUIPMENT_HEADS') == '1'
    if not refresh_head and any(mesh.get_name() == source.stem or mesh.get_name().startswith(source.stem + '_') for mesh in existing):
        return existing
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(source))
    task.set_editor_property('destination_path', destination)
    task.set_editor_property('automated', True)
    # Preserve hand edits on subsequent invocations. New sources only.
    task.set_editor_property('replace_existing', refresh_head)
    task.set_editor_property('save', True)
    TOOLS.import_asset_tasks([task])
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([destination], True)
    meshes = []
    for data in registry.get_assets_by_path(destination, True):
        if str(data.asset_class_path.asset_name) == 'SkeletalMesh':
            meshes.append(unreal.load_asset(str(data.package_name)))
    if not meshes:
        raise RuntimeError(f'No skeletal meshes imported: {source}')
    return meshes


def definition(appearance, gender):
    name = f'DA_Equipment_{appearance}_{gender}'
    path = '/Game/RPG/Items/Visuals/' + name
    value = unreal.load_asset(path)
    if not value:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', CLASS)
        value = TOOLS.create_asset(name, '/Game/RPG/Items/Visuals', CLASS, factory)
    if not value:
        raise RuntimeError(f'Cannot create {path}')
    value.set_editor_property('appearance_id', appearance)
    value.set_editor_property('gender', gender)
    return value


for gender, label in [(0, 'Male'), (1, 'Female')]:
    heads = imported(ROOT / f'SourceArt/RPG/EquipmentHeads/RPG_{label}_Head.gltf', '/Game/RPG/Characters/Human/EquipmentHeads')
    head = next((m for m in heads if m.get_name() == f'RPG_{label}_Head'), None)
    if not head:
        raise RuntimeError(f'Missing derived {label} head')
    for appearance, style in [(2002, 'Peasant'), (2003, 'Ranger')]:
        meshes = imported(ROOT / f'SourceArt/Quaternius/Outfits/{label}_{style}.gltf', f'/Game/Characters/Quaternius/Outfits/{label}_{style}')
        parts = [m for m in meshes if m.get_name().startswith(f'{label}_{style}_')]
        expected = 4 if style == 'Peasant' else 9
        if len(parts) != expected:
            raise RuntimeError(f'Expected {expected} outfit parts, got {len(parts)}')
        value = definition(appearance, gender)
        value.set_editor_property('outfit_head', head)
        value.set_editor_property('outfit_parts', parts)
        value.set_editor_property('hide_hair', style == 'Ranger')
        if not SUBSYSTEM.save_loaded_asset(value, False):
            raise RuntimeError('Equipment definition save failed')
        report.append({'appearance': appearance, 'gender': gender, 'head': head.get_path_name(), 'parts': [p.get_path_name() for p in parts]})
    sword = unreal.load_asset('/Game/Characters/Dark_Knight/Dark_Knight_Male/Meshes/SM_DKM_Sword')
    if not sword:
        raise RuntimeError('Training sword source unavailable')
    value = definition(2001, gender)
    value.set_editor_property('weapon', sword)
    value.set_editor_property('weapon_idle', unreal.load_asset('/Game/Characters/Quaternius/Animations/UAL1/UAL1_Standard/SkeletalMeshes/UAL1_StandardSword_Idle'))
    value.set_editor_property('weapon_bone', 'hand_r')
    # Imported hand axes put the palm along local +Z; the bone origin is the wrist.
    value.set_editor_property('weapon_transform', unreal.Transform(location=unreal.Vector(0.0, 0.0, 8.0 if gender == 0 else 7.0)))
    if not SUBSYSTEM.save_loaded_asset(value, False):
        raise RuntimeError('Sword definition save failed')
    report.append({'appearance': 2001, 'gender': gender, 'weapon': sword.get_path_name(), 'gripOffsetCm': [0, 0, 8 if gender == 0 else 7]})

output = ROOT / 'Saved/EquipmentVerification'
output.mkdir(parents=True, exist_ok=True)
(output / 'import-report.json').write_text(json.dumps(report, indent=2), encoding='utf8')
unreal.log('EQUIPMENT_IMPORT_OK definitions=6 parts=26')
