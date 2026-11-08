"""Bake combat including death from the already installed Orc pack onto Goblins."""
import unreal
tools=unreal.AssetToolsHelpers.get_asset_tools()
def require(path):
    a=unreal.load_asset(path)
    if not a:raise RuntimeError('Missing '+path)
    return a
def asset(name,path,cls,factory):
    return unreal.load_asset(path+'/'+name) or tools.create_asset(name,path,cls,factory)
root='/Game/Characters/GoblinLegionnaire'
source_mesh=require('/Game/Characters/Orcs/Meshes/Orc_Warrior/Combined/SK_Orc_Warrior_Combined1')
rig=asset('IK_CombatOrc',root,unreal.IKRigDefinition,unreal.IKRigDefinitionFactory())
sc=unreal.IKRigController.get_controller(rig);sc.set_skeletal_mesh(source_mesh);sc.set_retarget_root('pelvis')
chains=[('Spine','spine_01','spine_03','Spine','Spine02'),('Head','neck_01','head','neck','Head'),
        ('LeftArm','upperarm_l','hand_l','LeftArm','LeftHand'),('RightArm','upperarm_r','hand_r','RightArm','RightHand'),
        ('LeftLeg','thigh_l','ball_l','LeftUpLeg','LeftToeBase'),('RightLeg','thigh_r','ball_r','RightUpLeg','RightToeBase')]
for name,a,b,c,d in chains:sc.add_retarget_chain(name,a,b,'')
animations={'AS_Idle':'AS_05_Orc_Warrior_Idle_Battle1','AS_Run':'AS_04_Orc_Warrior_Run',
            'AS_Attack':'AS_07_Orc_Warrior_Attack1','AS_Hit':'AS_09_Orc_Warrior_Hit','AS_Death':'AS_10_Orc_Warrior_Death'}
for model in ['Soldier','Veteran']:
    path=root+'/'+model;mesh=require(path+'/SK_'+model);target=require(path+'/IK_'+model)
    tc=unreal.IKRigController.get_controller(target);tc.set_retarget_root('Hips')
    for name,a,b,c,d in chains:tc.add_retarget_chain(name,c,d,'')
    retarget=asset('RTG_Orc_'+model,path,unreal.IKRetargeter,unreal.IKRetargetFactory())
    rc=unreal.IKRetargeterController.get_controller(retarget)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE,rig);rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET,target)
    rc.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE,source_mesh);rc.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET,mesh)
    rc.add_default_ops();rc.auto_map_chains(unreal.AutoMapChainType.EXACT,True)
    rc.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    for dest,source in animations.items():
        src=require('/Game/Characters/Orcs/Animations/Orc_Warrior/'+source)
        inputs=unreal.IKRetargetBatchOperationInputs()
        inputs.assets_to_retarget=[unreal.EditorAssetLibrary.find_asset_data(src.get_path_name())]
        inputs.source_mesh=source_mesh;inputs.target_mesh=mesh;inputs.ik_retarget_asset=retarget
        inputs.target_path=path;inputs.search=source;inputs.replace=dest;inputs.include_referenced_assets=False;inputs.overwrite_existing_files=True
        created=unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
        if not created or not unreal.EditorAssetLibrary.does_asset_exist(path+'/'+dest):raise RuntimeError('Retarget failed '+model+'/'+dest)
        sequence=require(path+'/'+dest)
        unreal.log('COMBAT_RETARGET '+model+'/'+dest+' seconds='+str(sequence.get_play_length()))
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
unreal.log('COMBAT_RETARGET_OK')
