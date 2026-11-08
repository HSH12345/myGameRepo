"""Explicit UI-only animated gold selection border; no gameplay assets are changed."""
import unreal
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
path = '/Game/RPG/UI/DarkFantasy/Materials/M_SelectionGlow'
m = unreal.load_asset(path) or tools.create_asset('M_SelectionGlow', path.rsplit('/', 1)[0], unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
lib.delete_all_material_expressions(m)
uv = lib.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate)
time = lib.create_material_expression(m, unreal.MaterialExpressionTime)
effect = lib.create_material_expression(m, unreal.MaterialExpressionCustom)
inputs = []
for name in ['UV', 'Time']:
    entry = unreal.CustomInput()
    entry.set_editor_property('input_name', name)
    inputs.append(entry)
effect.set_editor_property('inputs', inputs)
effect.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
effect.set_editor_property('code', '''
float2 pixel = max(fwidth(UV), float2(0.00001, 0.00001));
float2 edge = min(UV, 1-UV) / pixel;
float d = min(edge.x, edge.y);
float core = 1-smoothstep(1.4, 2.8, abs(d-3.5));
float halo = exp(-abs(d-3.5)*0.24)*0.48;
float pulse = 0.84+0.16*sin(Time*2.8);
float spark = pow(saturate(0.5+0.5*sin((UV.x+UV.y)*12-Time*2.2)), 12);
return float4(1, 0.58+core*0.24, 0.12+core*0.32, saturate((core+halo+spark*halo)*pulse));
''')
assert lib.connect_material_expressions(uv, '', effect, 'UV')
assert lib.connect_material_expressions(time, '', effect, 'Time')
rgb = lib.create_material_expression(m, unreal.MaterialExpressionComponentMask)
for key in ['r', 'g', 'b']: rgb.set_editor_property(key, True)
assert lib.connect_material_expressions(effect, '', rgb, '')
alpha = lib.create_material_expression(m, unreal.MaterialExpressionComponentMask)
for key in ['r', 'g', 'b']: alpha.set_editor_property(key, False)
alpha.set_editor_property('a', True)
assert lib.connect_material_expressions(effect, '', alpha, '')
assert lib.connect_material_property(rgb, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
assert lib.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)
lib.recompile_material(m)
assert unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.log('SELECTION_GLOW_SAVED ' + path)
