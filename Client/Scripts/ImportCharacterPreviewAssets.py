"""Import the curated Quaternius character-preview sources.

The source files live outside Content so Unreal keeps a stable reimport path.
This script is intentionally idempotent and never imports the root-motion bundle.
"""

from pathlib import Path
import unreal


PROJECT_ROOT = Path(unreal.Paths.project_dir())
SOURCE_ROOT = PROJECT_ROOT / "SourceArt" / "Quaternius"
ASSET_ROOT = "/Game/Characters/Quaternius"


def import_source(filename: Path, destination: str) -> list[str]:
    if not filename.is_file():
        raise RuntimeError(f"Missing character source: {filename}")

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(filename))
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", False)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    imported = [str(path) for path in task.get_editor_property("imported_object_paths")]
    unreal.log(f"CHARACTER_IMPORT {filename.name}: {imported}")
    return imported


imported_paths: list[str] = []
for source in ("Superhero_Male_FullBody.gltf", "Superhero_Female_FullBody.gltf"):
    imported_paths += import_source(SOURCE_ROOT / "Base" / source, ASSET_ROOT + "/Base")

for source in ("Hair_SimpleParted.gltf", "Hair_Buns.gltf"):
    imported_paths += import_source(SOURCE_ROOT / "Hair" / source, ASSET_ROOT + "/Hair")

imported_paths += import_source(
    SOURCE_ROOT / "Animations" / "UAL1_Standard.glb",
    ASSET_ROOT + "/Animations/UAL1",
)

if not imported_paths:
    # Idempotent reruns can return no paths when every destination already exists.
    unreal.log_warning("No new assets were reported; auditing existing destination assets.")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([ASSET_ROOT], True)
assets = registry.get_assets_by_path(ASSET_ROOT, True)
for asset in assets:
    unreal.log(f"CHARACTER_ASSET {asset.package_name} class={asset.asset_class_path.asset_name}")

if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True):
    raise RuntimeError("Failed to save one or more imported character packages")

unreal.log("CHARACTER_IMPORT_OK")
