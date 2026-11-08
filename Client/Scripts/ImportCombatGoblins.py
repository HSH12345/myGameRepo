"""Import only the two authorized Goblins; inspect source rigs before retargeting."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir())
tools=unreal.AssetToolsHelpers.get_asset_tools()
results={}
for name in ['Soldier','Veteran']:
    task=unreal.AssetImportTask()
    task.filename=str(ROOT/'SourceArt/GoblinLegionnaire'/name/'Character_output.fbx')
    task.destination_path='/Game/Characters/GoblinLegionnaire/'+name
    task.destination_name='SK_'+name
    task.automated=True
    task.save=True
    options=unreal.FbxImportUI()
    options.automated_import_should_detect_type=False
    options.import_as_skeletal=True
    options.mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_mesh=True
    options.import_animations=False
    options.import_materials=True
    options.import_textures=True
    task.options=options
    if not unreal.EditorAssetLibrary.does_asset_exist(task.destination_path+'/'+task.destination_name):
        tools.import_asset_tasks([task])
    mesh=unreal.load_asset(task.destination_path+'/'+task.destination_name)
    if not mesh:raise RuntimeError('Goblin import failed: '+name)
    controller=unreal.IKRigController.get_controller(tools.create_asset('IK_'+name,task.destination_path,unreal.IKRigDefinition,unreal.IKRigDefinitionFactory())
        if not unreal.EditorAssetLibrary.does_asset_exist(task.destination_path+'/IK_'+name) else unreal.load_asset(task.destination_path+'/IK_'+name))
    controller.set_skeletal_mesh(mesh)
    results[name]={'mesh':mesh.get_path_name(),'bounds':str(mesh.get_bounds()),'controllerMethods':[x for x in dir(controller) if any(s in x for s in ['bone','chain','skeleton'])]}
source=unreal.load_asset('/Game/Characters/Orcs/Rig/Orc_Warrior/IK_Orc_Warrior')
controller=unreal.IKRigController.get_controller(source)
results['Orc']={'mesh':str(controller.get_skeletal_mesh()),'chains':str(controller.get_retarget_chains())}
results['batchMethods']=unreal.IKRetargetBatchOperation.run_batch_retarget.__doc__
results['retargetMethods']=[x for x in dir(unreal.IKRetargeterController) if any(s in x for s in ['chain','pose','op','align'])]
(ROOT/'Saved/CombatAssetAudit.json').write_text(json.dumps(results,indent=2),encoding='utf8')
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
unreal.log('COMBAT_GOBLIN_IMPORT_OK '+json.dumps(results))
