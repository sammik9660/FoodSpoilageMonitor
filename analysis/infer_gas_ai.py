"""Offline inference from audited complete scans. Never calls live services."""
import argparse,json,math,sys
from pathlib import Path
import numpy as np
from train_gas_ai import predict
def infer(model,record):
    gas=record.get('gas')
    if record.get('quality')!='OK' or not isinstance(gas,list) or len(gas)!=10 or not all(isinstance(x,(int,float)) and math.isfinite(x) and x>0 for x in gas):
        return {'status':'ABSTAIN_INVALID_GAS','food_prediction':None,'spoilage_prediction':None}
    v=np.log10(np.array(gas,float));x=np.array([*(v-v.mean()),record['t'],record['h'],record['p']],float)
    if not np.isfinite(x).all():return {'status':'ABSTAIN_INVALID_ENV','food_prediction':None,'spoilage_prediction':None}
    z=(x-np.array(model['mean']))/np.array(model['std'])
    weights=model['weights'];kind=model['model']
    compiled=np.array(weights) if kind=='ridge' else (np.array(weights['centers']),weights['seen']) if kind=='centroid' else ([np.array(w) for w in weights['w']],[np.array(b) for b in weights['b']])
    logits=predict(kind,compiled,z[None,:])[0];order=np.argsort(-logits);food=model['classes'][order[0]]
    return {'status':'EXPERIMENTAL_ONLY' if model['exposure_support'][food]>=2 else 'ABSTAIN_ONE_EXPOSURE',
        'food_prediction':food,'score_gap_not_probability':float(logits[order[0]]-logits[order[1]]),
        'spoilage_prediction':None,'model_version':model['schema']}
def main():
    a=argparse.ArgumentParser();a.add_argument('scans',type=Path);a.add_argument('model',type=Path);a.add_argument('output',type=Path);x=a.parse_args()
    rows=json.loads(x.scans.read_text(encoding='utf8'));model=json.loads(x.model.read_text(encoding='utf8'))
    last={}
    for r in rows:
        if r.get('food') and (r['exposure'] not in last or r['at']>last[r['exposure']]['at']):last[r['exposure']]=r
    result=[dict(device=r['device'],session=r['session'],exposure=r['exposure'],at=r['at'],input_quality=r['quality'],
                 observed_food_from_experiment_name=r['food'],**infer(model,r)) for r in last.values()]
    x.output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print('offline examples',len(result),'abstained',sum(v['status'].startswith('ABSTAIN') for v in result))
if __name__=='__main__':main()
