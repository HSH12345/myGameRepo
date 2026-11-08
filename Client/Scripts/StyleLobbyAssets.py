"""Apply readable default lobby styling without changing any vendor content."""
import unreal
from toolset_registry.helpers import compile_blueprint
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
base = "/Game/RPG/UI/Lobby/"
buttons = {
    "WBP_Login": ["StartButton"],
    "WBP_CharacterSelect": ["CreateButton", "RefreshButton"],
    "WBP_CharacterCreate": ["SubmitButton", "ConfirmPendingButton", "BackButton"],
    "WBP_CharacterSlot": ["SelectButton", "EnterGameButton"],
}
for name, names in buttons.items():
    path = base + name + "." + name
    for widget_name in names:
        button = unreal.load_object(None, path + ":WidgetTree." + widget_name)
        assert button
        button.set_editor_property("background_color", unreal.LinearColor(0.08, 0.15, 0.22, 1))
    bp = unreal.load_asset(base + name)
    if name != "WBP_CharacterSlot":
        text = unreal.load_object(None, path + ":WidgetTree.StatusText")
        text.set_editor_property("auto_wrap_text", False)
        text.set_editor_property("wrap_text_at", 740.0 if name == "WBP_CharacterSelect" else 600.0)
    if name == "WBP_CharacterCreate":
        entry = unreal.load_object(None, path + ":WidgetTree.CharacterNameInput")
        style = entry.get_editor_property("widget_style")
        style.set_editor_property("foreground_color", unreal.SlateColor(specified_color=unreal.LinearColor(0.02, 0.02, 0.02, 1)))
        entry.set_editor_property("widget_style", style)

        # Keep the form clear of the full-height 3D preview rendered on the left.
        content_width = unreal.load_object(None, path + ":WidgetTree.ContentWidth")
        content_slot = content_width.get_editor_property("slot")
        content_slot.set_editor_property("horizontal_alignment", unreal.HorizontalAlignment.H_ALIGN_RIGHT)
        content_slot.set_editor_property("vertical_alignment", unreal.VerticalAlignment.V_ALIGN_CENTER)
        content_slot.set_editor_property("padding", unreal.Margin(0.0, 0.0, 80.0, 0.0))

        appearance_note = unreal.load_object(None, path + ":WidgetTree.AppearanceNote")
        appearance_note.set_text("Race: Human  /  Select Male or Female to preview.")

        # The 3D world is the preview canvas; retain a dark readability veil without hiding it.
        screen_background = unreal.load_object(None, path + ":WidgetTree.ScreenBackground")
        screen_background.set_editor_property(
            "brush_color", unreal.LinearColor(0.018, 0.026, 0.040, 0.35))
    compile_blueprint(bp)
    assert assets.save_loaded_asset(bp, False)
unreal.log("LOBBY_STYLE_OK")
