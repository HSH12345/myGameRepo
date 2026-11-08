"""Build a separate PeaceWorld town layout from the two installed environment packs.

Run in UnrealEditor-Cmd with -run=pythonscript. The existing PeaceWorld map and
runtime world data are deliberately left untouched. This creates a new map.
"""

import math
import unreal


TARGET = "/Game/RPG/Maps/PeaceWorld/L_PeaceWorld_Hub_02"
TEMPLE = "/Game/AncientTempleRuins/Environment"
CRYPT = "/Game/RuinedCrypt/Environment"

ASSETS = {
    "base": "/Engine/BasicShapes/Cube",
    "grid": "/Engine/EngineMaterials/WorldGridMaterial",
    "floor": TEMPLE + "/Construction/Floor/SM_Floor_01_Linear_256_256_01",
    "engraved": TEMPLE + "/Construction/Floor/SM_Floor_Engraved_800_800_01_A",
    "pillar": TEMPLE + "/Construction/Pillar/SM_Pillar_01_128_128_512_01",
    "stone_head": TEMPLE + "/Prop/StoneHead/SM_StoneHead_01_A",
    "rock": TEMPLE + "/Rock/Rock01/SM_Rock01",
    "tree": TEMPLE + "/Foliage/Ceiba_Tree_01/SM_Ceiba_Tree_01_A",
    "pots": TEMPLE + "/Prop/Pots/SM_Pot_01_A",
    "crypt_entrance": CRYPT + "/CryptEntrance_01/sm_CryptEntrance_01_01",
    "crypt_wall": CRYPT + "/CryptWall_01/sm_CryptWall_01_01",
    "grave": CRYPT + "/Grave_01/sm_Grave_01_01",
    "crypt_column": CRYPT + "/CryptColumn_01/sm_CryptColumn_01_01",
    "crypt_stairs": CRYPT + "/Stairs_01/sm_Stairs_01_01",
    "ground_stones": CRYPT + "/GroundStones_01/sm_GroundStones_01_01",
}

assets = {name: unreal.load_asset(path) for name, path in ASSETS.items()}
missing = [name for name, asset in assets.items() if not asset]
if missing:
    raise RuntimeError("Missing required assets: " + ", ".join(missing))
if unreal.EditorAssetLibrary.does_asset_exist(TARGET):
    raise RuntimeError("Target already exists; refusing to overwrite: " + TARGET)

if not unreal.EditorLevelLibrary.new_level(TARGET):
    raise RuntimeError("Could not create " + TARGET)
world = unreal.EditorLevelLibrary.get_editor_world()
if not world:
    raise RuntimeError("New map did not become the editor world")

counts = {}


def place(name, asset, x, y, z=0, yaw=0, scale=(1, 1, 1)):
    # The default game camera has yaw 0: screen-up is world +X and
    # screen-right is world +Y. The design coordinates use X right/Y up.
    world_yaw = yaw if asset in ("base", "floor") else 90 - yaw
    actor = unreal.EditorLevelLibrary.spawn_actor_from_object(
        assets[asset], unreal.Vector(y, x, z), unreal.Rotator(yaw=world_yaw)
    )
    if not actor:
        raise RuntimeError("Spawn failed: " + name)
    actor.set_actor_label(name)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    counts[asset] = counts.get(asset, 0) + 1
    return actor


# A pale grid gives the same clear editing surface as the pack overview.
# Its top is z=0. It is only a visual underlay, not gameplay collision data.
base = place("00_Base_Work_Surface", "base", 0, 0, -12, scale=(140, 120, 0.24))
base.static_mesh_component.set_material(0, assets["grid"])

# The floor mesh is 256 cm wide with a corner pivot. Reusing it at unit scale
# keeps texel density and seams consistent across all five gathering areas.
occupied = set()


def tile(x, y, label):
    key = (round(x / 256), round(y / 256))
    if key in occupied:
        return
    occupied.add(key)
    place(label + "_Floor_%03d" % len(occupied), "floor", key[0] * 256, key[1] * 256)


def pad(label, cx, cy, radius):
    for ix in range(-radius, radius + 1):
        for iy in range(-radius, radius + 1):
            tile(cx + ix * 256, cy + iy * 256, label)


def road(label, x1, y1, x2, y2, width=1):
    steps = int(math.hypot(x2 - x1, y2 - y1) / 128) + 1
    for i in range(steps + 1):
        t = i / steps
        x = x1 * (1 - t) + x2 * t
        y = y1 * (1 - t) + y2 * t
        for w in range(-width, width + 1):
            tile(x + w * 256, y, label)
            tile(x, y + w * 256, label)


# Sketch coordinates: north is positive Y; the entrance is due south.
zones = [
    ("Quest_11", -3328, 3072),
    ("Equipment_01", 3328, 3072),
    ("GeneralGoods_07", -3328, -2816),
    ("Skills_05", 3328, -2816),
]
pad("Central_Plaza", 0, 0, 5)
for label, x, y in zones:
    pad(label, x, y, 3)
    road(label + "_Road", 0, 0, x, y)
road("Dungeon_Road", 0, -256, 0, -4864, 2)
pad("Dungeon_Approach", 0, -5120, 3)

# Low visual landmarks around each NPC reserve. Keep their centers and roads
# clear so a top-down camera and future interaction targets remain readable.
for label, x, y in zones:
    for dx, dy in ((-1050, -1050), (1050, -1050), (-1050, 1050), (1050, 1050)):
        place(label + "_Corner_Pillar", "pillar", x + dx, y + dy, 0,
              scale=(0.7, 0.7, 0.55))

# Quest: a ruined altar and two stones. Equipment: an open stone display.
place("Quest_Stone_Altar", "stone_head", -3328, 3328, 80, yaw=180, scale=(0.65, 0.65, 0.65))
for dx in (-950, 950):
    place("Quest_Rock", "rock", -3328 + dx, 4100, 0, scale=(0.65, 0.65, 0.65))
place("Equipment_Display", "engraved", 3328, 3328, 75, scale=(0.55, 0.55, 0.5))
for dx in (-900, 900):
    place("Equipment_Column", "crypt_column", 3328 + dx, 3900, 0, scale=(0.65, 0.65, 0.6))

# Goods: pots and a small open square. Skills: a crypt-style ritual setting.
for dx, dy in ((-700, 250), (750, 350), (-450, -600), (550, -650)):
    place("GeneralGoods_Pots", "pots", -3328 + dx, -2816 + dy, 75,
          yaw=(dx + dy) % 360, scale=(0.65, 0.65, 0.65))
place("Skills_Altar", "engraved", 3328, -2816, 75, scale=(0.55, 0.55, 0.5))
for dx in (-750, 750):
    place("Skills_Grave_Stone", "grave", 3328 + dx, -2150, 60,
          yaw=180, scale=(0.55, 0.55, 0.55))

# RuinedCrypt silhouettes frame the dungeon threshold at six o'clock.
place("Dungeon_Crypt_Entrance", "crypt_entrance", 0, -5480, 75,
      yaw=90, scale=(2.2, 2.2, 2.0))
for dx in (-1250, 1250):
    place("Dungeon_Ruined_Wall", "crypt_wall", dx, -5600, 0,
          yaw=90, scale=(1.0, 1.0, 0.8))
    place("Dungeon_Crypt_Column", "crypt_column", dx, -4800, 0,
          scale=(0.8, 0.8, 0.8))
place("Dungeon_Stairs", "crypt_stairs", 0, -5200, 55,
      yaw=90, scale=(1.2, 1.2, 0.7))

# Peripheral foliage, away from movement corridors and service platforms.
for x, y in ((-5700, 4700), (-5500, 600), (-5600, -4300),
             (5700, 4600), (5500, 300), (5650, -4100)):
    place("Perimeter_Tree", "tree", x, y, 0, yaw=(x + y) % 360,
          scale=(0.75, 0.75, 0.75))
for x, y in ((-4800, 4400), (-4900, 1800), (-4850, -1500),
             (4800, 4400), (4950, 1700), (4700, -1400),
             (-1900, -5150), (1800, -5200)):
    place("Perimeter_Ground_Stones", "ground_stones", x, y, 10,
          yaw=(x - y) % 360, scale=(0.7, 0.7, 0.7))

start = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.PlayerStart, unreal.Vector(-3600, 0, 130), unreal.Rotator(yaw=0)
)
start.set_actor_label("PeaceWorld_PlayerStart_South")

sun = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.DirectionalLight, unreal.Vector(0, 0, 3000), unreal.Rotator(pitch=-48, yaw=-55)
)
sun.set_actor_label("PeaceWorld_Sun")
sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SkyLight, unreal.Vector(0, 0, 500), unreal.Rotator()
)
sky.set_actor_label("PeaceWorld_SkyLight")
atmosphere = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator()
)
atmosphere.set_actor_label("PeaceWorld_SkyAtmosphere")

if not unreal.EditorLoadingAndSavingUtils.save_map(world, TARGET):
    raise RuntimeError("Save failed: " + TARGET)

unreal.log("CODEX_PEACEWORLD_CREATED %s tiles=%d actors=%d" %
           (TARGET, len(occupied), sum(counts.values()) + 4))
unreal.log("CODEX_PEACEWORLD_ASSETS %s" % counts)
