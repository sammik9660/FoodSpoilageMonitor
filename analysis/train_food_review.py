"""Exposure holdout food classification and explicitly NON-spoilage temporal proxy A/B.
No random row split, no original files overwritten, no proxy deployed as a food-state model.
"""
import argparse, collections, hashlib, json, math
from pathlib import Path
import numpy as np

CLASSES=['banana','chicken','egg','rice','sprouts','tofu']
def fit(x,y,groups,k):
    mean=x.mean(0);std=np.maximum(x.std(0),1e-6);z=np.c_[np.ones(len(x)),(x-mean)/std]
    sizes=collections.Counter(groups);w=np.array([1/sizes[g] for g in groups]);w*=len(w)/w.sum()
    penalty=np.eye(z.shape[1])*5;penalty[0,0]=1e-6
    coef=np.linalg.solve(z.T@(w[:,None]*z)+penalty,z.T@(w[:,None]*np.eye(k)[y]))
    return dict(mean=mean.tolist(),std=std.tolist(),coef=coef.tolist())
def scores(m,x):return np.c_[np.ones(len(x)),(x-np.array(m['mean']))/np.array(m['std'])]@np.array(m['coef'])
def metrics(y,p,k):
    cm=np.zeros((k,k),dtype=int)
    for a,b in zip(y,p):cm[a,b]+=1
    recall=[float(cm[i,i]/cm[i].sum()) if cm[i].sum() else None for i in range(k)]
    return dict(accuracy=float(np.mean(y==p)),confusion_matrix=cm.tolist(),class_recall=recall)
def evaluate(x,y,groups,k):
    pred=np.zeros(len(y),dtype=int);folds=[]
    for g in sorted(set(groups)):
        test=np.array([a==g for a in groups]);m=fit(x[~test],y[~test],[v for v,t in zip(groups,test) if not t],k)
        pred[test]=np.argmax(scores(m,x[test]),axis=1)
        folds.append(dict(exposure=g,n=int(test.sum()),accuracy=float(np.mean(pred[test]==y[test])),
            unseen_classes=sorted(set(y[test].tolist())-set(y[~test].tolist()))))
    return dict(**metrics(y,pred,k),folds=folds),pred
def main():
    p=argparse.ArgumentParser();p.add_argument('scans',type=Path);p.add_argument('output',type=Path);p.add_argument('--cloud-model',type=Path);a=p.parse_args()
    raw=json.loads(a.scans.read_text(encoding='utf8')); rows=sorted([r for r in raw if r['quality']=='OK' and r['food'] in CLASSES],key=lambda r:(r['exposure'],r['at']))
    grouped=collections.defaultdict(list)
    for r in rows:grouped[r['exposure']].append(r)
    food_x=[];base_x=[];gas_x=[];food_y=[];phase_y=[];groups=[];row_ids=[]
    for g,rs in grouped.items():
        start=rs[0]['at'];duration=max(1,rs[-1]['at']-start);baseline=[];dose=0;covered=0;previous=None
        for r in rs:
            logs=np.log10(r['gas']);centered=logs-logs.mean()
            if previous:
                gap=(r['at']-previous['at'])/3600000
                if 0<gap<=.5: dose+=previous['t']*gap;covered+=gap
            previous=r
            if len(baseline)<3:baseline.append(logs);continue
            age=(r['at']-start)/3600000;ref=np.mean(baseline,axis=0);delta=logs-ref
            food_x.append([*centered,r['t'],r['h'],r['p']]);food_y.append(CLASSES.index(r['food']))
            base=[*np.eye(6)[CLASSES.index(r['food'])],age,dose,covered,age-covered,r['t']]
            base_x.append(base);gas_x.append([*base,*centered,*delta,r['h'],r['p']])
            phase_y.append(min(2,int((r['at']-start)/duration*3)));groups.append(g);row_ids.append([r['session'],r['at']])
    x=np.array(food_x);y=np.array(food_y);groups=np.array(groups);py=np.array(phase_y)
    food,pred=evaluate(x,y,groups,6);a_result,_=evaluate(np.array(base_x),py,groups,3);b_result,_=evaluate(np.array(gas_x),py,groups,3)
    model=fit(x,y,groups,6)
    support={c:len({g for r,g in zip(y,groups) if r==i}) for i,c in enumerate(CLASSES)}
    model.update(classes=CLASSES,version='food-ridge-exposure-20261115',validated_for_spoilage=False,
        food_release_approved=False,exposure_support=support,source_sha256=hashlib.sha256(a.scans.read_bytes()).hexdigest(),
        input_schema=['centered_log10_gas_'+str(i) for i in range(10)]+['temperature_c','humidity_rh','pressure_hpa'])
    report=dict(food_classes=CLASSES,food=food,exposure_support=support,
        status_ground_truth=dict(usable_timestamped_positive_scans=0,usable_timestamped_negative_scans=0,
            actual_spoilage_accuracy=None,actual_spoilage_confusion_matrix=None),
        temporal_proxy_only=dict(classes=['EARLY_PHASE','MIDDLE_PHASE','LATE_PHASE'],
            warning='Labels use final exposure duration retrospectively. These are NOT deterioration states and are NOT deployed.',A=a_result,B=b_result),
        misclassifications=[dict(session=row_ids[i][0],at=row_ids[i][1],truth=CLASSES[y[i]],prediction=CLASSES[pred[i]]) for i in range(len(y)) if y[i]!=pred[i]][:30])
    a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'evaluation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    (a.output/'food_model.json').write_text(json.dumps(model,indent=2),encoding='utf8')
    if a.cloud_model:a.cloud_model.write_text('// Experimental food identity model; never a spoilage classifier.\nconst FOOD_REVIEW_MODEL_ = '+json.dumps(model,separators=(',',':'))+';\n',encoding='utf8')
    print(json.dumps(dict(food=food['accuracy'],food_recall=food['class_recall'],A_proxy=a_result['accuracy'],B_proxy=b_result['accuracy'],support=support)))
if __name__=='__main__':main()
