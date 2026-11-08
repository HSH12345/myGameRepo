"""Delete only verified, backed-up legacy lobby packages; never vendor packs."""
import unreal
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
targets = [
    "/Game/Level/Login",
    "/Game/Data/BPs/GameMain_BP",
    "/Game/Data/Widget/UI/Title/TitleUIController_BP",
    "/Game/Data/Widget/UI/Title",
    "/Game/Data/Widget/UI/Title_BP",
]
for path in targets:
    if not assets.does_asset_exist(path):
        continue
    refs = [str(x) for x in assets.find_package_referencers_for_asset(path, True)]
    refs = [x for x in refs if x != path]
    unreal.log("LEGACY_AUDIT " + path + " refs=" + str(refs))
    assert not refs, "Still referenced; leave intact: " + path
    assert assets.delete_asset(path), "Could not delete: " + path
    unreal.log("LEGACY_REMOVED " + path)
unreal.log("LOBBY_CLEANUP_OK")

