"""Create data-driven Human previews and place the reusable preview actor in L_Lobby."""

import unreal


ASSET_ROOT = "/Game/Characters/Quaternius"
DATA_ROOT = "/Game/RPG/Characters/Human/Data"
LOBBY_MAP = "/Game/RPG/Maps/Lobby/L_Lobby"

MALE = {
    "body": ASSET_ROOT + "/Base/Superhero_Male_FullBody/SkeletalMeshes/SuperHero_Male",
    "eyes": ASSET_ROOT + "/Base/Superhero_Male_FullBody/SkeletalMeshes/Eyes",
    "brows": ASSET_ROOT + "/Base/Superhero_Male_FullBody/SkeletalMeshes/Eyebrows",
    "hair": ASSET_ROOT + "/Hair/Hair_SimpleParted/SkeletalMeshes/Hair_SimpleParted",
    "hair_skeleton": ASSET_ROOT + "/Hair/Hair_SimpleParted/SkeletalMeshes/Hair_SimpleParted_Skeleton",
    "skeleton": ASSET_ROOT + "/Base/Superhero_Male_FullBody/SkeletalMeshes/Eyebrows_Skeleton",
}
FEMALE = {
    "body": ASSET_ROOT + "/Base/Superhero_Female_FullBody/SkeletalMeshes/Superhero_Female",
    "eyes": ASSET_ROOT + "/Base/Superhero_Female_FullBody/SkeletalMeshes/Eyes",
    "brows": ASSET_ROOT + "/Base/Superhero_Female_FullBody/SkeletalMeshes/Eyebrows",
    "hair": ASSET_ROOT + "/Hair/Hair_Buns/SkeletalMeshes/Hair_Buns",
    "hair_skeleton": ASSET_ROOT + "/Hair/Hair_Buns/SkeletalMeshes/Hair_Buns_Skeleton",
    "skeleton": ASSET_ROOT + "/Base/Superhero_Female_FullBody/SkeletalMeshes/Eyebrows_Skeleton",
}
IDLE = ASSET_ROOT + "/Animations/UAL1/UAL1_Standard/SkeletalMeshes/UAL1_StandardIdle_Loop"
ANIMATION_SKELETON = ASSET_ROOT + "/Animations/UAL1/UAL1_Standard/SkeletalMeshes/UAL1_Standard_Skeleton"


def require(path: str):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Required character asset is missing: {path}")
    return asset


asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
asset_subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
definition_class = unreal.load_class(None, "/Script/RPG.CharacterAppearanceDefinition")
preview_class = unreal.load_class(None, "/Script/RPG.CharacterPreviewActor")
if not definition_class or not preview_class:
    raise RuntimeError("Character preview C++ classes are unavailable; build RPGEditor first")


def create_or_update(name: str, gender: int, sources: dict):
    asset_path = DATA_ROOT + "/" + name
    definition = unreal.load_asset(asset_path)
    if not definition:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", definition_class)
        definition = asset_tools.create_asset(name, DATA_ROOT, definition_class, factory)
    if not definition:
        raise RuntimeError(f"Could not create {asset_path}")
    definition.set_editor_property("race", 0)
    definition.set_editor_property("gender", gender)
    definition.set_editor_property("preview_mesh", require(sources["body"]))
    definition.set_editor_property(
        "additional_meshes",
        [require(sources["eyes"]), require(sources["brows"]), require(sources["hair"])],
    )
    definition.set_editor_property("preview_idle", require(IDLE))
    definition.set_editor_property(
        "mesh_transform",
        unreal.Transform(
            location=unreal.Vector(0.0, 0.0, 0.0),
            rotation=unreal.Rotator(0.0, 0.0, 0.0),
            scale=unreal.Vector(1.0, 1.0, 1.0),
        ),
    )
    if not asset_subsystem.save_loaded_asset(definition, False):
        raise RuntimeError(f"Could not save {asset_path}")
    return definition


male = create_or_update("DA_Human_Male", 0, MALE)
female = create_or_update("DA_Human_Female", 1, FEMALE)

# The packs have an identical 65-bone hierarchy but import as separate USkeleton assets.
# Record compatibility in both directions so editor filters and runtime validation agree.
male_skeleton = require(MALE["skeleton"])
female_skeleton = require(FEMALE["skeleton"])
animation_skeleton = require(ANIMATION_SKELETON)
male_hair_skeleton = require(MALE["hair_skeleton"])
female_hair_skeleton = require(FEMALE["hair_skeleton"])
male_skeleton.set_editor_property("compatible_skeletons", [animation_skeleton, male_hair_skeleton])
female_skeleton.set_editor_property("compatible_skeletons", [animation_skeleton, female_hair_skeleton])
male_hair_skeleton.set_editor_property("compatible_skeletons", [male_skeleton, animation_skeleton])
female_hair_skeleton.set_editor_property("compatible_skeletons", [female_skeleton, animation_skeleton])
animation_skeleton.set_editor_property(
    "compatible_skeletons",
    [male_skeleton, female_skeleton, male_hair_skeleton, female_hair_skeleton],
)
for skeleton in (
    male_skeleton,
    female_skeleton,
    male_hair_skeleton,
    female_hair_skeleton,
    animation_skeleton,
):
    if not asset_subsystem.save_loaded_asset(skeleton, False):
        raise RuntimeError(f"Could not save compatible skeleton {skeleton.get_path_name()}")

world = unreal.EditorLoadingAndSavingUtils.load_map(LOBBY_MAP)
if not world:
    raise RuntimeError("Could not load the lobby map")
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
preview_actors = [
    actor
    for actor in actor_subsystem.get_all_level_actors()
    if actor.get_class() == preview_class
]
if len(preview_actors) > 1:
    raise RuntimeError("Multiple CharacterPreviewActor instances exist in L_Lobby")
if preview_actors:
    preview = preview_actors[0]
else:
    preview = actor_subsystem.spawn_actor_from_class(
        preview_class, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0)
    )
if not preview:
    raise RuntimeError("Could not place CharacterPreviewActor in L_Lobby")
preview.set_actor_label("CharacterPreview")
preview.set_folder_path("Lobby/Preview")
preview.set_editor_property("appearances", [male, female])

if not unreal.EditorLoadingAndSavingUtils.save_map(world, LOBBY_MAP):
    raise RuntimeError("Could not save L_Lobby")
unreal.log("CHARACTER_PREVIEW_CONFIG_OK")
