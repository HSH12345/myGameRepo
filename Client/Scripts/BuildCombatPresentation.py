"""Create dedicated presentation assets and import the ten generated skill icons."""
from pathlib import Path
import unreal
tools=unreal.AssetToolsHelpers.get_asset_tools();lib=unreal.MaterialEditingLibrary
root='/Game/RPG/Combat'
def material(name,domain,blend):
    m=unreal.load_asset(root+'/'+name) or tools.create_asset(name,root,unreal.Material,unreal.MaterialFactoryNew())
    # Run through CodexCombatArtBuild.uproject: runtime constructor-loaded
    # expressions can be in the rooted startup set and cannot be deleted.
    # Snapshot the mutable collection before individual node removal.
    for expression in list(lib.get_material_expressions(m)):
        lib.delete_material_expression(m,expression)
    m.set_editor_property('material_domain',domain);m.set_editor_property('blend_mode',blend)
    m.set_editor_property('two_sided',True)
    if domain==unreal.MaterialDomain.MD_SURFACE:m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    return m
def node(m,cls):return lib.create_material_expression(m,cls)
def scalar(m,name,value):
    n=node(m,unreal.MaterialExpressionScalarParameter);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',value);return n
def color(m,name,value):
    n=node(m,unreal.MaterialExpressionVectorParameter);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',unreal.LinearColor(*value,1));return n
def custom(m,code,inputs,output):
    n=node(m,unreal.MaterialExpressionCustom);args=[]
    for name in inputs:
        a=unreal.CustomInput();a.set_editor_property('input_name',name);args.append(a)
    n.set_editor_property('inputs',args);n.set_editor_property('output_type',output);n.set_editor_property('code',code)
    for name,v in inputs.items():
        source,pin=v if isinstance(v,tuple) else (v,'')
        if not lib.connect_material_expressions(source,pin,n,name):raise RuntimeError('Custom input connection failed '+name)
    return n
def save(m):
    lib.recompile_material(m)
    if not unreal.EditorAssetLibrary.save_loaded_asset(m):raise RuntimeError('Material save failed '+m.get_name())

m=material('M_SkillIndicatorV2',unreal.MaterialDomain.MD_DEFERRED_DECAL,unreal.BlendMode.BLEND_TRANSLUCENT)
uv=node(m,unreal.MaterialExpressionTextureCoordinate)
p=scalar(m,'Progress',0);angle=scalar(m,'Angle',360);rect=scalar(m,'Rectangle',0);fade=scalar(m,'Opacity',1)
tint=color(m,'Color',(.03,.75,1))
code='''float2 p=(UV-.5)*2;
float progress=saturate(Progress), edge, inside, distance;
if(Rectangle>.5){
 edge=max(abs(p.x),abs(p.y));inside=1-smoothstep(.99,1.01,edge);
 distance=saturate(p.x*.5+.5);
}else{
 float r=length(p);float h=Angle*.00872664626;
 float a=abs(atan2(p.y,p.x));
 inside=(1-smoothstep(.99,1.01,r))*lerp(1-smoothstep(h-.012,h+.012,a),1,step(359,Angle));
 edge=max(r,lerp(a/max(h,.001),0,step(359,Angle)));
 distance=r;
}
float border=smoothstep(.955,.985,edge);
float filled=(1-smoothstep(progress-.012,progress+.012,distance))*step(.001,progress);
float front=(1-smoothstep(.008,.025,abs(distance-progress)))*step(.001,progress)*step(progress,.998);
float arrow=0;
if(Rectangle>.5 || Angle<170){arrow=(1-smoothstep(.025,.055,abs(abs(p.y)-(.9-p.x)*.9)))*step(.64,p.x)*step(p.x,.9);}
return inside*saturate((.025+filled*.25+front*.38+border*.62+arrow*.4)*Opacity);'''
mask=custom(m,code,dict(UV=uv,Progress=p,Angle=angle,Rectangle=rect,Opacity=fade),unreal.CustomMaterialOutputType.CMOT_FLOAT1)
lib.connect_material_property(mask,'',unreal.MaterialProperty.MP_OPACITY);lib.connect_material_property(tint,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR);save(m)

fire_task=unreal.AssetImportTask();fire_task.set_editor_property('filename',str(Path(unreal.Paths.project_dir())/'ArtSource/CombatVFX/FireWisp.png'))
fire_task.set_editor_property('destination_path',root);fire_task.set_editor_property('destination_name','T_FireWisp')
fire_task.set_editor_property('automated',True);fire_task.set_editor_property('replace_existing',True);fire_task.set_editor_property('save',True);tools.import_asset_tasks([fire_task])
fire_texture=unreal.load_asset(root+'/T_FireWisp')
if not fire_texture:raise RuntimeError('Generated fire sprite import failed')
fire_texture.set_editor_property('power_of_two_mode',unreal.TexturePowerOfTwoSetting.RESIZE_TO_SPECIFIC_RESOLUTION)
fire_texture.set_editor_property('resize_during_build_x',512);fire_texture.set_editor_property('resize_during_build_y',512)
fire_texture.set_editor_property('max_texture_size',512);unreal.EditorAssetLibrary.save_loaded_asset(fire_texture)
m=material('M_CombatRibbon',unreal.MaterialDomain.MD_SURFACE,unreal.BlendMode.BLEND_ADDITIVE)
uv=node(m,unreal.MaterialExpressionTextureCoordinate);vc=node(m,unreal.MaterialExpressionVertexColor)
age=scalar(m,'Age',0);flame=scalar(m,'Flame',0);power=scalar(m,'Power',2);projectile=scalar(m,'Projectile',0)
sprite_uv=custom(m,'return float2(UV.y,lerp(1-UV.x,UV.x,Projectile));',dict(UV=uv,Projectile=projectile),unreal.CustomMaterialOutputType.CMOT_FLOAT2)
sprite=node(m,unreal.MaterialExpressionTextureSampleParameter2D);sprite.set_editor_property('parameter_name','FireSprite');sprite.set_editor_property('texture',fire_texture)
if not lib.connect_material_expressions(sprite_uv,'',sprite,''):raise RuntimeError('Fire UV connection failed')
tint=color(m,'Color',(.03,.5,1));core=color(m,'Core',(.75,.95,1))
code='''float y=abs(UV.y*2-1);
float soft=pow(saturate(1-y),1.4);
float hot=pow(saturate(1-y),8);
float tips=smoothstep(0,.06,UV.x)*smoothstep(0,.08,1-UV.x);
float flow=.82+.18*sin(UV.x*43-Age*34);
float noise=.65+.35*sin(UV.x*29+sin(UV.y*19+Age*15)*2-Age*20);
float fire=lerp(1,noise*lerp(1,1-UV.x,.35),Flame);
float life=smoothstep(0,.05,Age)*(1-smoothstep(.45,1,Age));
float spriteMask=saturate(max(Sprite.r,max(Sprite.g,Sprite.b))*3)*SpriteAlpha;
float alpha=soft*tips*flow*fire*life*VertexAlpha*lerp(1,spriteMask,Flame);
float3 rgb=lerp(Color,Core,hot)*Power*Vertex.rgb;
rgb=lerp(rgb,Sprite.rgb*Power*1.4,Flame);
return float4(rgb,alpha);'''
fx=custom(m,code,dict(UV=uv,Vertex=vc,VertexAlpha=(vc,'A'),Age=age,Flame=flame,Power=power,Color=tint,Core=core,Sprite=sprite,SpriteAlpha=(sprite,'A')),unreal.CustomMaterialOutputType.CMOT_FLOAT4)
rgb=node(m,unreal.MaterialExpressionComponentMask)
for channel in ['r','g','b']:rgb.set_editor_property(channel,True)
rgb.set_editor_property('a',False)
a=node(m,unreal.MaterialExpressionComponentMask)
for channel in ['r','g','b']:a.set_editor_property(channel,False)
a.set_editor_property('a',True)
if not lib.connect_material_expressions(fx,'',rgb,''):raise RuntimeError('Ribbon RGB connection failed')
if not lib.connect_material_expressions(fx,'',a,''):raise RuntimeError('Ribbon alpha connection failed')
lib.connect_material_property(rgb,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR);lib.connect_material_property(a,'',unreal.MaterialProperty.MP_OPACITY);save(m)

source=Path(unreal.Paths.project_dir())/'ArtSource/Skills'
tasks=[]
for skill in [3,4,10,11,12,13,14,15,16,17]:
    name=f'Skill{skill:02}';file=source/(name+'.png')
    if not file.exists():raise RuntimeError('Generated icon missing '+str(file))
    task=unreal.AssetImportTask();task.set_editor_property('filename',str(file));task.set_editor_property('destination_path','/Game/RPG/UI/Skills')
    task.set_editor_property('destination_name','T_'+name);task.set_editor_property('automated',True);task.set_editor_property('replace_existing',True);task.set_editor_property('save',True);tasks.append(task)
tools.import_asset_tasks(tasks)
for task in tasks:
    for path in task.get_editor_property('imported_object_paths'):
        texture=unreal.load_asset(path);texture.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property('power_of_two_mode',unreal.TexturePowerOfTwoSetting.RESIZE_TO_SPECIFIC_RESOLUTION)
        texture.set_editor_property('resize_during_build_x',512);texture.set_editor_property('resize_during_build_y',512)
        texture.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_UI);texture.set_editor_property('max_texture_size',512)
        texture.set_editor_property('srgb',True);unreal.EditorAssetLibrary.save_loaded_asset(texture)
unreal.log('COMBAT_PRESENTATION_ASSETS_SAVED materials=2 unique_icons=10')
