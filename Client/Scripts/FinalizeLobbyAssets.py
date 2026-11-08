"""Finalize the interrupted lobby migration. Run with UnrealEditor-Cmd -run=pythonscript.
Only known lobby assets are modified. Existing source/content backup is required.
"""
import unreal
from toolset_registry.helpers import compile_blueprint

ROOT = "/Game/RPG"
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

def clear_graph(bp, maximum_nodes):
    graph = unreal.BlueprintEditorLibrary.find_event_graph(bp)
    if not graph:
        return
    editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
    nodes = list(editor.list_all_nodes())
    assert len(nodes) <= maximum_nodes, "Unexpected additional graph logic: " + bp.get_path_name()
    unreal.log("Lobby migration removes nodes: " + ", ".join(n.get_node_title() for n in nodes))
    if nodes:
        editor.remove_nodes(nodes)
    compile_blueprint(bp)
    assert not list(editor.list_all_nodes())

world = unreal.EditorLoadingAndSavingUtils.load_map(ROOT + "/Maps/Lobby/L_Lobby")
assert world
bp = unreal.load_object(None, ROOT + "/Maps/Lobby/L_Lobby.L_Lobby:PersistentLevel.L_Lobby")
assert bp
clear_graph(bp, 8)
world.get_world_settings().set_editor_property(
    "default_game_mode", unreal.load_class(None, ROOT + "/Core/GameModes/BP_LobbyGameMode.BP_LobbyGameMode_C"))
assert unreal.EditorLoadingAndSavingUtils.save_map(world, ROOT + "/Maps/Lobby/L_Lobby")

for name in ["WBP_Login", "WBP_CharacterSelect", "WBP_CharacterCreate", "WBP_CharacterSlot", "WBP_LobbyRoot"]:
    widget_bp = unreal.load_asset(ROOT + "/UI/Lobby/" + name)
    assert widget_bp
    clear_graph(widget_bp, 3)
    if name == "WBP_LobbyRoot":
        unreal.get_default_object(widget_bp.generated_class()).set_editor_property("is_focusable", True)
    compile_blueprint(widget_bp)
    assert assets.save_loaded_asset(widget_bp, False)

for path in [ROOT + "/Core/GameInstance/BP_GameMain", ROOT + "/Core/GameModes/BP_LobbyGameMode"]:
    bp = unreal.load_asset(path)
    compile_blueprint(bp)
    assert assets.save_loaded_asset(bp, False)

unreal.log("LOBBY_FINALIZE_OK")

