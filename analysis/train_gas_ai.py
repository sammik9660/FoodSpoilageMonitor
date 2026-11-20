"""Reproducible local six-food research models and causal gas anomaly states.
Original workbook is never written. A held-out physical exposure with no class
example in training is reported as untestable, never counted as a correct guess.
"""
import argparse,collections,hashlib,json,math
from pathlib import Path
import numpy as np

CLASSES=['banana','chicken','egg','rice','sprouts','tofu']
SCHEMA=[f'centered_log10_gas_{i}' for i in range(10)]+['temperature_c','humidity_percent','pressure_hpa']
def inputs(rows):
    x=[];y=[];g=[];records=[]
    for r in rows:
        if r['quality']!='OK' or r['food'] not in CLASSES:continue
        v=np.log10(np.array(r['gas'],float));x.append([*(v-v.mean()),r['t'],r['h'],r['p']]);
        y.append(CLASSES.index(r['food']));g.append(r['exposure']);records.append(r)
    return np.array(x),np.array(y),np.array(g),records
def scale_fit(x):return x.mean(0),np.maximum(x.std(0),1e-5)
def fit_ridge(z,y,groups,k):
    counts=collections.Counter(groups);w=np.array([1/counts[g] for g in groups]);w*=len(w)/w.sum()
    a=np.c_[np.ones(len(z)),z];pen=np.eye(a.shape[1])*5;pen[0,0]=1e-6
    return np.linalg.solve(a.T@(w[:,None]*a)+pen,a.T@(w[:,None]*np.eye(k)[y]))
def fit_centroid(z,y,k):
    centers=np.zeros((k,z.shape[1]));seen=[]
    for i in range(k):
        rows=z[y==i];centers[i]=rows.mean(0) if len(rows) else 0;seen.append(bool(len(rows)))
    return centers,seen
def fit_mlp(z,y,groups,k,seed=42,epochs=140):
    rng=np.random.default_rng(seed);dims=[z.shape[1],16,8,k]
    weights=[rng.normal(0,np.sqrt(2/dims[i]),(dims[i],dims[i+1])) for i in range(3)]
    bias=[np.zeros(dims[i+1]) for i in range(3)]
    counts=collections.Counter(groups);sw=np.array([1/counts[g] for g in groups]);sw/=sw.sum()
    target=np.eye(k)[y];mw=[np.zeros_like(a) for a in weights];vw=[np.zeros_like(a) for a in weights]
    mb=[np.zeros_like(a) for a in bias];vb=[np.zeros_like(a) for a in bias]
    for step in range(1,epochs+1):
        h1=np.maximum(0,z@weights[0]+bias[0]);h2=np.maximum(0,h1@weights[1]+bias[1]);o=h2@weights[2]+bias[2]
        exp=np.exp(o-o.max(1,keepdims=True));p=exp/exp.sum(1,keepdims=True);d=(p-target)*sw[:,None]
        gw=[None]*3;gb=[None]*3
        gw[2]=h2.T@d+0.002*weights[2];gb[2]=d.sum(0)
        d=(d@weights[2].T)*(h2>0);gw[1]=h1.T@d+0.002*weights[1];gb[1]=d.sum(0)
        d=(d@weights[1].T)*(h1>0);gw[0]=z.T@d+0.002*weights[0];gb[0]=d.sum(0)
        for i in range(3):
            for param,grad,m,v in [(weights[i],gw[i],mw[i],vw[i]),(bias[i],gb[i],mb[i],vb[i])]:
                m[:]=.9*m+.1*grad;v[:]=.999*v+.001*grad*grad
                param-=.01*(m/(1-.9**step))/(np.sqrt(v/(1-.999**step))+1e-8)
    return weights,bias
def predict(kind,model,z):
    if kind=='ridge':return np.c_[np.ones(len(z)),z]@model
    if kind=='centroid':
        centers,seen=model;s=-np.sum((z[:,None,:]-centers[None,:,:])**2,axis=2);s[:,~np.array(seen)]=-1e9;return s
    w,b=model;return np.maximum(0,np.maximum(0,z@w[0]+b[0])@w[1]+b[1])@w[2]+b[2]
def evaluate(kind,x,y,group):
    k=len(CLASSES);pred=np.full(len(y),-1);unsupported=np.zeros(len(y),bool);folds=[]
    for exposure in dict.fromkeys(group):
        test=group==exposure;train=~test;seen=set(y[train]);unknown=~np.isin(y[test],list(seen))
        mean,std=scale_fit(x[train]);z=(x[train]-mean)/std;zt=(x[test]-mean)/std
        model=fit_ridge(z,y[train],group[train],k) if kind=='ridge' else fit_centroid(z,y[train],k) if kind=='centroid' else fit_mlp(z,y[train],group[train],k)
        scores=predict(kind,model,zt);pred[test]=scores.argmax(1);unsupported[test]=unknown
        valid=~unknown;folds.append({'exposure':exposure,'n':int(test.sum()),'untestable':int(unknown.sum()),
            'accuracy_on_supported':float(np.mean(pred[test][valid]==y[test][valid])) if valid.any() else None})
    eligible=~unsupported;cm=np.zeros((k,k),int)
    for a,b in zip(y[eligible],pred[eligible]):cm[a,b]+=1
    return dict(eligible=int(eligible.sum()),untestable=int(unsupported.sum()),accuracy=float(np.mean(pred[eligible]==y[eligible])),
        confusion_matrix=cm.tolist(),recall=[float(cm[i,i]/cm[i].sum()) if cm[i].sum() else None for i in range(k)],folds=folds),pred
def anomaly(rows):
    groups=collections.defaultdict(list)
    for r in rows:groups[(r['device'],r['exposure'])].append(r)
    states=[];examples=[]
    for (device,exposure),rr in groups.items():
        rr.sort(key=lambda r:r['at']);valid=[r for r in rr if r['quality']=='OK']
        if len(valid)<7:continue
        first=np.log10(np.array([r['gas'] for r in valid[:6]],float));mean=first.mean(0);std=np.maximum(first.std(0,ddof=1),.05)
        series=[];temp_observed=0;temp_above4=0;gap_h=0;previous=None
        for r in valid[6:]:
            v=np.log10(np.array(r['gas'],float));score=float(np.mean(np.abs((v-mean)/std)))
            series.append((r['at'],score))
            if previous:
                span=(r['at']-previous['at'])/3600000
                if 0<span<=.5:
                    temp_observed+=span
                    if previous['t']>4:temp_above4+=span
                elif span>.5:gap_h+=span
            previous=r
        last=series[-1];slope=(last[1]-series[-3][1])/((last[0]-series[-3][0])/3600000) if len(series)>=3 and last[0]>series[-3][0] else None
        states.append(dict(device=device,exposure=exposure,baseline_scans=6,mean_log10=mean.tolist(),std_log10=std.tolist()))
        examples.append(dict(device=device,exposure=exposure,session=valid[-1]['session'],at=last[0],
            anomaly_index=last[1],recent_slope_per_hour=slope,temperature_c=valid[-1]['t'],humidity_pct=valid[-1]['h'],
            temperature_observed_h=temp_observed,temperature_above_4c_h=temp_above4,unobserved_gap_h=gap_h,
            output_kind='gas_pattern_anomaly_only',spoilage_prediction=None))
    return states,examples
def main():
    p=argparse.ArgumentParser();p.add_argument('scans',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    rows=json.loads(a.scans.read_text(encoding='utf8'));x,y,g,records=inputs(rows);a.output.mkdir(parents=True,exist_ok=True)
    reports={};predictions={}
    for kind in ('ridge','centroid','mlp'):
        reports[kind],predictions[kind]=evaluate(kind,x,y,g)
    # Pick on eligible exposure holdout only; no claim for one-exposure foods.
    chosen=max(reports,key=lambda k:reports[k]['accuracy']);mean,std=scale_fit(x);z=(x-mean)/std
    model=fit_ridge(z,y,g,len(CLASSES)) if chosen=='ridge' else fit_centroid(z,y,len(CLASSES)) if chosen=='centroid' else fit_mlp(z,y,g,len(CLASSES))
    support={c:len(set(g[y==i])) for i,c in enumerate(CLASSES)}
    payload={'schema':1,'model':chosen,'classes':CLASSES,'features':SCHEMA,'mean':mean.tolist(),'std':std.tolist(),
        'weights':model.tolist() if chosen=='ridge' else {'centers':model[0].tolist(),'seen':model[1]} if chosen=='centroid' else {'w':[v.tolist() for v in model[0]],'b':[v.tolist() for v in model[1]]},
        'source_sha256':hashlib.sha256(a.scans.read_bytes()).hexdigest(),'exposure_support':support,
        'release_approved':False,'confidence_calibrated':False,'not_spoilage_model':True}
    states,examples=anomaly(rows);research={'classes':CLASSES,'sample_count':len(y),'exposure_groups':len(set(g)),
        'food_counts':dict(collections.Counter(CLASSES[i] for i in y)),
        'models':reports,'chosen_experimental_model':chosen,'spoilage_ground_truth_accuracy':None,
        'warning':'Single-exposure classes are excluded from supported-class accuracy; physical specimen identity is not independently recorded.'}
    for name,obj in [('food_model.json',payload),('evaluation.json',research),('anomaly_state.json',{'kind':'per-exposure-diagonal-loggas','baseline_scans':6,'states':states}),('inference_examples.json',examples)]:
        (a.output/name).write_text(json.dumps(obj,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'scans':len(rows),'eligible':len(y),'groups':len(set(g)),'results':{k:(v['eligible'],v['untestable'],round(v['accuracy'],4)) for k,v in reports.items()},'chosen':chosen,'anomaly_examples':len(examples)}))
if __name__=='__main__':main()
