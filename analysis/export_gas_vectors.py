"""Generate deterministic Python reference vectors from the saved local scans/model."""
import json,math,sys
from pathlib import Path
import numpy as np
from infer_gas_ai import infer
rows=json.loads(Path(sys.argv[1]).read_text(encoding='utf8'))
model=json.loads(Path(sys.argv[2]).read_text(encoding='utf8'))
groups={}
for r in rows:groups.setdefault((r['device'],r['exposure']),[]).append(r)
with Path(sys.argv[3]).open('w',encoding='ascii') as out:
    for group,key in enumerate(sorted(groups)):
        rs=sorted(groups[key],key=lambda r:r['at']);training=[]
        for r in rs:
            pred=infer(model,r);valid=r['quality']=='OK';anomaly=-1.
            if valid:
                v=np.log10(np.array(r['gas'],float))
                if len(training)<6:training.append(v)
                else:
                    first=np.array(training);mean=first.mean(0);std=np.maximum(first.std(0,ddof=1),.05)
                    anomaly=float(np.mean(np.abs((v-mean)/std)))
            candidate=model['classes'].index(pred['food_prediction']) if pred['food_prediction'] else -1
            margin=pred.get('score_gap_not_probability',-1.)
            fields=[group,int(r['at']),r['t'],r['h'],r['p'],*r['gas'],int(valid),candidate,margin,anomaly]
            out.write(' '.join(str(v) for v in fields)+'\n')
print('vectors',len(rows),'groups',len(groups))
