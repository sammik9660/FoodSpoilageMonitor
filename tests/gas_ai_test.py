import hashlib,json,math,sys
from pathlib import Path
root=Path(__file__).parents[1];sys.path.insert(0,str(root/'analysis'))
from infer_gas_ai import infer
folder=root/'data/gas-ai-20261116';scans=folder/'scans.json'
report=json.loads((folder/'evaluation.json').read_text(encoding='utf8'))
model=json.loads((folder/'food_model.json').read_text(encoding='utf8'))
rows=json.loads(scans.read_text(encoding='utf8'))
assert model['source_sha256']==hashlib.sha256(scans.read_bytes()).hexdigest()
assert len(model['features'])==13 and len(model['classes'])==6
assert report['sample_count']==2973 and report['exposure_groups']==10
assert len(rows)==3385 and sum(r['quality']=='LOW_INFORMATION' for r in rows)==292
for name,entry in report['models'].items():
    assert entry['eligible']+entry['untestable']==2973 and entry['untestable']==930
    assert sum(map(sum,entry['confusion_matrix']))==entry['eligible']
    assert 0<=entry['accuracy']<=1
assert not model['release_approved'] and model['not_spoilage_model'] and not model['confidence_calibrated']
for r in rows[-25:]:
    result=infer(model,r);assert result['spoilage_prediction'] is None
    if r['quality']!='OK':assert result['status']=='ABSTAIN_INVALID_GAS'
assert infer(model,dict(rows[-1],gas=[0]*10))['status']=='ABSTAIN_INVALID_GAS'
examples=json.loads((folder/'inference_predictions.json').read_text(encoding='utf8'))
assert len(examples)==10 and any(x['status']=='ABSTAIN_ONE_EXPOSURE' for x in examples)
states=json.loads((folder/'anomaly_state.json').read_text(encoding='utf8'))
assert states['baseline_scans']==6 and len(states['states'])==14
assert all(x['spoilage_prediction'] is None for x in json.loads((folder/'inference_examples.json').read_text(encoding='utf8')))
print('PASS independent exposure holdout, model integrity, low-quality abstention and anomaly-only outputs')
