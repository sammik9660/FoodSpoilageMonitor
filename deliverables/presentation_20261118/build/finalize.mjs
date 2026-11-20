import fs from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const skill='C:/Users/user/.codex/plugins/cache/openai-primary-runtime/presentations/26.1112.11217/skills/presentations';
const { finalizePresentation }=await import(pathToFileURL(path.join(skill,'container_tools/artifact_tool_utils.mjs')).href);
const workspace='C:/Users/user/Desktop/FoodSpoilageMonitor/deliverables/presentation_20261118';
const python='C:/Program Files/WindowsApps/OpenAI.CodexPrimaryRuntime.v26-1112-737-0_26.1112.737.0_x64__3k8sg7r9htsxt/dependencies/python/python.exe';
const items=[
  {draft:'deck-draft.pptx',output:'스마트_음식_보관_발표_10장.pptx',size:'12192000,6858000',count:10},
  {draft:'poster-draft.pptx',output:'스마트_음식_보관_A1_포스터.pptx',size:'21384006,30275975',count:1},
];
for(const item of items){
  const finalPath=path.join(workspace,'output','print_ready',item.output);
  await fs.mkdir(path.dirname(finalPath),{recursive:true});
  const result=await finalizePresentation({
    workspaceDir:workspace,
    candidatePath:path.join(workspace,'build',item.draft),
    finalPath,
    pythonExecutable:python,
    integrityValidatorPath:path.join(skill,'container_tools/inspect_presentation_package_integrity.py'),
    layoutValidatorPath:path.join(skill,'container_tools/inspect_presentation_layout_geometry.py'),
    layoutArgs:['--expected-slide-size-emu',item.size,'--validate-heading-fit'],
    requiredNativeTableOwnerSlides:[],
    fontPolicy:{basis:'design',families:['Arial','Malgun Gothic']},
    verifyArtifactToolImport:true,
    receiptPath:path.join(workspace,'build','print-ready-'+item.output+'.validation.json'),
  });
  console.log(item.output,JSON.stringify(result));
}
