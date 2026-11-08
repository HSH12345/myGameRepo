"""Targeted, explicit migration. Back up the three UMG assets before running."""
import unreal

unreal.SystemLibrary.execute_console_command(None, "RPG.ConfigureLobbyEntry")
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
for name in ("WBP_Login", "WBP_CharacterSelect", "WBP_CharacterSlot", "WBP_LobbyRoot"):
    blueprint = unreal.load_asset("/Game/RPG/UI/Lobby/" + name)
    assert blueprint
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assert blueprint.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE, name
    assert assets.save_loaded_asset(blueprint, False), name
unreal.log("LOBBY_ENTRY_ASSETS_OK")
