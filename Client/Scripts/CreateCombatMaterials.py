"""Create disposable combat preview materials; the ring boundary never changes size."""
import unreal
tools=unreal.AssetToolsHelpers.get_asset_tools();lib=unreal.MaterialEditingLibrary
root='/Game/RPG/Combat'
def material(name):
    return unreal.load_asset(root+'/'+name) or tools.create_asset(name,root,unreal.Material,unreal.MaterialFactoryNew())
def expr(m,cls):return lib.create_material_expression(m,cls)
m=material('M_CastIndicator')
lib.delete_all_material_expressions(m)
m.set_editor_property('material_domain',unreal.MaterialDomain.MD_DEFERRED_DECAL)
m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
uv=expr(m,unreal.MaterialExpressionTextureCoordinate)
progress=expr(m,unreal.MaterialExpressionScalarParameter);progress.set_editor_property('parameter_name','Progress');progress.set_editor_property('default_value',0)
angle=expr(m,unreal.MaterialExpressionScalarParameter);angle.set_editor_property('parameter_name','Angle');angle.set_editor_property('default_value',360)
color=expr(m,unreal.MaterialExpressionVectorParameter);color.set_editor_property('parameter_name','Color');color.set_editor_property('default_value',unreal.LinearColor(.03,.75,1,1))
custom=expr(m,unreal.MaterialExpressionCustom)
inputs=[]
for name in ['UV','Progress','Angle']:
    entry=unreal.CustomInput();entry.set_editor_property('input_name',name);inputs.append(entry)
custom.set_editor_property('inputs',inputs)
custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
custom.set_editor_property('code','''float2 p=(UV-0.5)*2; float r=length(p); float a=atan2(p.y,p.x); float halfAngle=Angle*0.00872664626;
float isCircle=step(359.0,Angle); float shape=lerp(step(abs(a),halfAngle),1,isCircle)*step(r,1.0);
float sweep=lerp((a+halfAngle)/max(0.001,halfAngle*2),frac((a+6.283185307)/6.283185307),isCircle);
float border=step(0.94,r); float coneEdge=(1-isCircle)*step(halfAngle-0.025,abs(a));
return shape*max(max(border,coneEdge)*0.8,step(sweep,saturate(Progress))*0.32);''')
for node,name in [(uv,'UV'),(progress,'Progress'),(angle,'Angle')]:lib.connect_material_expressions(node,'',custom,name)
lib.connect_material_property(custom,'',unreal.MaterialProperty.MP_OPACITY)
lib.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
lib.recompile_material(m);unreal.EditorAssetLibrary.save_loaded_asset(m)
m=material('M_TestOrb');lib.delete_all_material_expressions(m)
m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
color=expr(m,unreal.MaterialExpressionVectorParameter);color.set_editor_property('parameter_name','Color');color.set_editor_property('default_value',unreal.LinearColor(.03,.75,1,1))
lib.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
lib.recompile_material(m);unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.log('COMBAT_MATERIALS_OK')
