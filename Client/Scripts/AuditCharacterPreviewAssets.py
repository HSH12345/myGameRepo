"""Read-only audit for the imported character-preview assets and lobby scene."""

import unreal


paths = {
    "male_mesh": "/Game/Characters/Quaternius/Base/Superhero_Male_FullBody/SkeletalMeshes/SuperHero_Male",
    "male_eyes": "/Game/Characters/Quaternius/Base/Superhero_Male_FullBody/SkeletalMeshes/Eyes",
    "male_brows": "/Game/Characters/Quaternius/Base/Superhero_Male_FullBody/SkeletalMeshes/Eyebrows",
    "male_hair": "/Game/Characters/Quaternius/Hair/Hair_SimpleParted/SkeletalMeshes/Hair_SimpleParted",
    "female_mesh": "/Game/Characters/Quaternius/Base/Superhero_Female_FullBody/SkeletalMeshes/Superhero_Female",
    "female_eyes": "/Game/Characters/Quaternius/Base/Superhero_Female_FullBody/SkeletalMeshes/Eyes",
    "female_brows": "/Game/Characters/Quaternius/Base/Superhero_Female_FullBody/SkeletalMeshes/Eyebrows",
    "female_hair": "/Game/Characters/Quaternius/Hair/Hair_Buns/SkeletalMeshes/Hair_Buns",
    "idle": "/Game/Characters/Quaternius/Animations/UAL1/UAL1_Standard/SkeletalMeshes/UAL1_StandardIdle_Loop",
}

loaded = {}
for name, path in paths.items():
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing imported asset: {path}")
    loaded[name] = asset
    try:
        skeleton = asset.get_editor_property("skeleton")
    except Exception:
        skeleton = asset.get_skeleton() if hasattr(asset, "get_skeleton") else None
    bounds = asset.get_bounds() if hasattr(asset, "get_bounds") else None
    unreal.log(f"PREVIEW_AUDIT {name} class={asset.get_class().get_name()} skeleton={skeleton} bounds={bounds}")

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/RPG/Maps/Lobby/L_Lobby")
if not world:
    raise RuntimeError("Could not load L_Lobby")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
for actor in actors:
    unreal.log(
        f"LOBBY_ACTOR label={actor.get_actor_label()} class={actor.get_class().get_name()} "
        f"location={actor.get_actor_location()}"
    )

widget_path = "/Game/RPG/UI/Lobby/WBP_CharacterCreate.WBP_CharacterCreate:WidgetTree."
for widget_name in (
    "ScreenBackground",
    "ContentWidth",
    "ContentLayout",
    "CharacterNameInput",
    "GenderChoice",
    "SubmitButton",
    "ConfirmPendingButton",
    "BackButton",
    "StatusText",
):
    widget = unreal.load_object(None, widget_path + widget_name)
    if not widget:
        raise RuntimeError(f"Missing expected create widget: {widget_name}")
    slot = widget.get_editor_property("slot")
    slot_owner = slot.get_outer() if slot and hasattr(slot, "get_outer") else None
    unreal.log(
        f"CREATE_WIDGET name={widget_name} class={widget.get_class().get_name()} "
        f"slot={slot.get_class().get_name() if slot else '<root>'} "
        f"slot_owner={slot_owner.get_name() if slot_owner else '<none>'}"
    )

content_layout = unreal.load_object(None, widget_path + "ContentLayout")
screen_background = unreal.load_object(None, widget_path + "ScreenBackground")
unreal.log(
    f"CREATE_BACKGROUND brush_color={screen_background.get_editor_property('brush_color')} "
    f"background={screen_background.get_editor_property('background')}"
)
for index in range(content_layout.get_children_count()):
    child = content_layout.get_child_at(index)
    details = ""
    if isinstance(child, unreal.TextBlock):
        details = f" text={child.get_text()}"
    unreal.log(
        f"CREATE_LAYOUT_CHILD index={index} name={child.get_name()} "
        f"class={child.get_class().get_name()}{details}"
    )
unreal.log("CHARACTER_PREVIEW_AUDIT_OK")
