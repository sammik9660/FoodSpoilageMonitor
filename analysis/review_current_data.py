"""Read-only workbook audit and complete-scan export for food model evaluation."""
import argparse, collections, hashlib, json, math
from pathlib import Path
from datetime import datetime, timezone
import openpyxl

def canonical(label):
    for text, food in [('바나나','banana'),('닭가슴살','chicken'),('두부','tofu'),('밥','rice'),('계란','egg'),('달걀','egg'),('숙주','sprouts')]:
        if text in str(label): return food
    return None

def main():
    p=argparse.ArgumentParser();p.add_argument('workbook',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    original=hashlib.sha256(a.workbook.read_bytes()).hexdigest()
    w=openpyxl.load_workbook(a.workbook,read_only=True,data_only=True)
    data={}
    for s in w:
        it=s.values; heads=next(it,()); data[s.title]=[dict(zip(heads,r)) for r in it if any(x is not None for x in r)]
    sessions={r['session_id']:r for r in data['Sessions'] if r.get('session_id')}
    groups=collections.defaultdict(list); seen={}; rejects=collections.Counter(); duplicates=0
    for n,r in enumerate(data['SensorData'],2):
        if r.get('sample_schema')!=3: continue
        uid=r.get('sample_uid'); key=(r.get('device_id'),r.get('nrf_boot'),r.get('scan_seq'),r.get('profile_id'))
        if uid in seen:
            duplicates+=1
            if seen[uid]!=r: groups[key].append({'conflict':True})
            continue
        seen[uid]=r; groups[key].append(dict(r,source_row=n))
    scans=[]
    def stamp(v):
        if isinstance(v,datetime): return v.replace(tzinfo=v.tzinfo or timezone.utc).timestamp()*1000
        return datetime.fromisoformat(str(v).replace('Z','+00:00')).timestamp()*1000
    for key,rows in groups.items():
        try:
            rows.sort(key=lambda r:r['heater_step']); sid=rows[0].get('session_id'); session=sessions.get(sid,{})
            assert key[0] and key[1] is not None and key[2] is not None and key[3]==354
            assert len(rows)==10 and [r['heater_step'] for r in rows]==list(range(10))
            assert len({r.get('session_id') for r in rows})==1 and sid
            assert all(r['gas_index']==i and int(r['sensor_status'])&0xb0==0xb0 for i,r in enumerate(rows))
            assert all(str(r[k]).lower() in ('true','1','1.0') for r in rows for k in ('gas_valid','heater_stable','measurement_valid'))
            assert all(0<(int(rows[i]['meas_index'])-int(rows[i-1]['meas_index']))%256<128 and rows[i]['nrf_seq']==rows[i-1]['nrf_seq']+1 for i in range(1,10))
            gas=[float(r['gas_resistance']) for r in rows]
            assert all(math.isfinite(g) and g>0 for g in gas)
            assert all(math.isfinite(float(r[k])) and lo<=float(r[k])<=hi for r in rows for k,lo,hi in [('temperature',-40,85),('humidity',0,100),('pressure',300,1100)])
            times=[stamp(r['timestamp']) for r in rows]; assert 0<=max(times)-min(times)<=180000
            low=sum(abs(g-5.68)<=.01 for g in gas)>=8 or max(map(math.log10,gas))-min(map(math.log10,gas))<.05
            food=canonical(session.get('experiment_label'))
            label=str(session.get('experiment_label') or '')
            # Conservatively group repeated same-food/storage/device sessions as one exposure.
            exposure=f"{food}:{'cold' if '냉장' in label else 'ambient'}:{key[0]}" if food else str(sid)
            scans.append(dict(device=key[0],boot=key[1],scan=key[2],session=sid,exposure=exposure,food=food,
                label=label,at=max(times),gas=gas,t=sum(r['temperature'] for r in rows)/10,
                h=sum(r['humidity'] for r in rows)/10,p=sum(r['pressure'] for r in rows)/10,
                quality='LOW_INFORMATION' if low else 'OK',source_rows=[r['source_row'] for r in rows],
                uids=[r['sample_uid'] for r in rows]))
        except (AssertionError,KeyError,TypeError,ValueError,OverflowError): rejects['incomplete_or_invalid_scan']+=1
    a.output.mkdir(parents=True,exist_ok=True)
    report=dict(source_sha256=original,sheets={k:len(v) for k,v in data.items()},sessions=list(sessions.values()),
        duplicate_uids=duplicates,rejected_groups=dict(rejects),complete_scans=len(scans),
        quality=dict(collections.Counter(s['quality'] for s in scans)),food_scans=dict(collections.Counter(str(s['food']) for s in scans)))
    (a.output/'audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2,default=str),encoding='utf8')
    (a.output/'scans.json').write_text(json.dumps(scans,ensure_ascii=False),encoding='utf8')
    assert original==hashlib.sha256(a.workbook.read_bytes()).hexdigest()
    print(json.dumps({k:v for k,v in report.items() if k!='sessions'},ensure_ascii=False))
    for s in sessions.values(): print(json.dumps(s,ensure_ascii=False,default=str))
if __name__=='__main__':main()
