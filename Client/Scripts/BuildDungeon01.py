"""Create the empty Dungeon 01 test level. Refuses to overwrite existing assets.

Run with UnrealEditor-Cmd RPG.uproject -run=pythonscript -script=<this file>.
This creates the level only; server manifests and portal routing are separate.
"""

import json
from pathlib import Path

import unreal


TARGET = "/Game/RPG/Maps/Dungeon/L_Dungeon_01"
SIZE_CM = 10000.0
THICKNESS_CM = 100.0
REPORT = Path(unreal.Paths.project_saved_dir()) / "Dungeon01Verification" / "creation.json"

if unreal.EditorAssetLibrary.does_asset_exist(TARGET):
    raise RuntimeError("Refusing to overwrite existing level: " + TARGET)

cube = unreal.load_asset("/Engine/BasicShapes/Cube")
material = unreal.load_asset("/Engine/EngineMaterials/WorldGridMaterial")
if not cube or not material:
    raise RuntimeError("Required engine floor assets are unavailable")
if not unreal.EditorLevelLibrary.new_level(TARGET):
    raise RuntimeError("Could not create " + TARGET)

world = unreal.EditorLevelLibrary.get_editor_world()


def spawn(actor_class, label, position, rotation=None):
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        actor_class, unreal.Vector(*position), rotation or unreal.Rotator()
    )
    if not actor:
        raise RuntimeError("Could not spawn " + label)
    actor.set_actor_label(label)
    return actor


# Engine cube is 100 cm per axis. The top face is exactly Z=0.
floor = spawn(unreal.StaticMeshActor, "Dungeon01_Ground_100m", (0, 0, -THICKNESS_CM / 2))
component = floor.static_mesh_component
component.set_mobility(unreal.ComponentMobility.STATIC)
component.set_static_mesh(cube)
component.set_material(0, material)
component.set_collision_profile_name("BlockAll")
component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
floor.set_actor_scale3d(unreal.Vector(SIZE_CM / 100, SIZE_CM / 100, THICKNESS_CM / 100))

# PlayerStart's capsule center is above the floor; it adds no runtime obstacle.
start = spawn(unreal.PlayerStart, "Dungeon01_PlayerStart", (0, 0, 100))
start.set_editor_property("player_start_tag", "1")
start.set_editor_property("tags", ["RPG.Spawn.default"])

sun = spawn(
    unreal.DirectionalLight, "Dungeon01_Sun", (0, 0, 3000),
    unreal.Rotator(pitch=-48, yaw=-35)
)
sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sun.light_component.set_editor_property("intensity", 10.0)
sun.light_component.set_editor_property("atmosphere_sun_light", True)
sky = spawn(unreal.SkyLight, "Dungeon01_SkyLight", (0, 0, 1000))
sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property("real_time_capture", True)
spawn(unreal.SkyAtmosphere, "Dungeon01_SkyAtmosphere", (0, 0, 0))

unreal.EditorLevelLibrary.set_level_viewport_camera_info(
    unreal.Vector(8000, -10000, 11000), unreal.Rotator(pitch=-40.66, yaw=128.66)
)
if not unreal.EditorLoadingAndSavingUtils.save_map(world, TARGET):
    raise RuntimeError("Could not save " + TARGET)

# Reload the saved package so these checks inspect the delivered asset.
if not unreal.EditorLevelLibrary.load_level(TARGET):
    raise RuntimeError("Could not reload saved level")
actors = unreal.EditorLevelLibrary.get_all_level_actors()
meshes = [actor for actor in actors if isinstance(actor, unreal.StaticMeshActor)]
starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
assert len(meshes) == 1, "Level must contain exactly one ground mesh"
assert len(starts) == 1, "Level must contain one PlayerStart"
saved_floor = meshes[0]
origin, extent = saved_floor.get_actor_bounds(False)
assert abs(extent.x * 2 - SIZE_CM) < 0.1
assert abs(extent.y * 2 - SIZE_CM) < 0.1
assert abs(origin.z + extent.z) < 0.1, "Floor surface must be Z=0"
assert saved_floor.static_mesh_component.get_collision_profile_name() == "BlockAll"
assert saved_floor.static_mesh_component.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
assert saved_floor.static_mesh_component.get_editor_property("mobility") == unreal.ComponentMobility.STATIC
assert starts[0].get_actor_location().z == 100

REPORT.parent.mkdir(parents=True, exist_ok=True)
REPORT.write_text(json.dumps({
    "map": TARGET,
    "size_m": [SIZE_CM / 100, SIZE_CM / 100],
    "floor_surface_z_cm": 0,
    "floor_thickness_cm": THICKNESS_CM,
    "static_mesh_count": len(meshes),
    "player_start_count": len(starts),
    "collision": "BlockAll / QueryAndPhysics",
    "actors": [{"label": actor.get_actor_label(), "class": actor.get_class().get_name()} for actor in actors],
    "saved_and_reloaded": True,
    "server_transition_registered": False,
}, indent=2), encoding="utf-8")
unreal.log("DUNGEON01_CREATED_AND_VERIFIED " + TARGET)
