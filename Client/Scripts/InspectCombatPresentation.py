import unreal
lib=unreal.MaterialEditingLibrary
for name in ['M_CombatRibbon','M_SkillIndicatorV2']:
 m=unreal.load_asset('/Game/RPG/Combat/'+name)
 unreal.log('ARTDIAG '+name+' blend='+str(m.get_editor_property('blend_mode'))+' domain='+str(m.get_editor_property('material_domain')))
 for p in [unreal.MaterialProperty.MP_OPACITY,unreal.MaterialProperty.MP_EMISSIVE_COLOR]:
  unreal.log('ARTDIAG '+str(p)+' node='+str(lib.get_material_property_input_node(m,p))+' pin='+str(lib.get_material_property_input_node_output_name(m,p)))
 for e in lib.get_material_expressions(m):
  unreal.log('ARTDIAG '+e.get_name()+' class='+e.get_class().get_name()+' inputs='+str(lib.get_material_expression_input_names(e)))
