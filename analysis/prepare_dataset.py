"""Read-only CSV audit and HP-354 feature export. No synthetic samples/labels.
Usage: python analysis/prepare_dataset.py PAGE.csv ... --output data/audit-NEW
Optional --sessions JSON maps session IDs to {food, started_at, split}.
Labels are human metadata, never derived from elapsed time or humidity.
"""
import argparse
import csv
import hashlib
import json
import math
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from fingerprints import reconstruct

def instant(value):
    t = datetime.fromisoformat(value.replace('Z', '+00:00'))
    if t.tzinfo is None:
        raise ValueError('timestamp timezone missing')
    return t.astimezone(timezone.utc)

def prepare(paths, output, metadata=None, gap_seconds=1200):
    output = Path(output)
    if output.exists():
        raise ValueError('Output already exists; choose a new directory')
    metadata = metadata or {}
    unique, conflicts, rejected, duplicates, inputs = {}, set(), [], 0, []
    tainted=set()
    scan_key=lambda r:(r.get('device_id',''),r.get('session_id',''),r.get('nrf_boot',''),r.get('scan_seq',''),r.get('profile_id',''))
    for path in paths:
        raw = Path(path).read_bytes()
        inputs.append({'path':str(path), 'sha256':hashlib.sha256(raw).hexdigest()})
        with Path(path).open(encoding='utf-8-sig', newline='') as stream:
            for line, row in enumerate(csv.DictReader(stream), 2):
                uid = row.get('sample_uid', '')
                key = (row.get('device_id', '') or 'legacy', uid)
                if not uid:
                    rejected.append({'file':str(path), 'line':line, 'reason':'missing UID'}); continue
                if key in unique:
                    if unique[key] != row:
                        conflicts.add(key)
                        tainted.update([scan_key(row),scan_key(unique[key])])
                        rejected.append({'uid':uid, 'reason':'conflicting duplicate UID'})
                    else: duplicates += 1
                    continue
                unique[key] = row
    rows=[]
    for key,row in unique.items():
        if key in conflicts or (str(row.get('sample_schema'))=='3' and scan_key(row) in tainted): continue
        try:
            schema=int(row.get('sample_schema') or 2)
            if schema not in (2,3,4): raise ValueError('unknown schema')
            instant(row['timestamp']); instant(row['received_at'])
            for name,lo,hi in [('temperature',-40,85),('humidity',0,100),('pressure',300,1100)]:
                value=float(row[name])
                if not math.isfinite(value) or not lo<=value<=hi: raise ValueError('invalid '+name+' or units')
            if schema!=4 and (not math.isfinite(float(row['gas_resistance'])) or float(row['gas_resistance'])<=0):
                raise ValueError('invalid gas')
            if row.get('capture_quality')!='ntp': raise ValueError('capture time unconfirmed')
            rows.append(row)
        except (ValueError,KeyError,TypeError) as error:
            rejected.append({'uid':key[1], 'reason':str(error)})
    groups=defaultdict(list)
    for row in rows: groups[(row.get('device_id') or 'legacy',row.get('session_id',''))].append(row)
    gaps=[]; features=[]; scan_rejections=[]; sessions=[]
    for (device,session),items in sorted(groups.items()):
        items.sort(key=lambda r:instant(r['timestamp']))
        # Gas-only cadence cannot be inferred from interleaved ENV rows.
        gas=[r for r in items if str(r.get('sample_schema'))=='3']
        for prior,current in zip(gas,gas[1:]):
            seconds=(instant(current['timestamp'])-instant(prior['timestamp'])).total_seconds()
            if seconds>gap_seconds:gaps.append({'device_id':device,'session_id':session,
                'from':prior['timestamp'],'to':current['timestamp'],'seconds':seconds,'kind':'observed_gas_row_gap',
                'prior_received_at':prior['received_at'],'next_received_at':current['received_at'],
                'prior_boot':prior.get('nrf_boot',''),'next_boot':current.get('nrf_boot',''),
                'prior_seq':prior.get('nrf_seq',''),'next_seq':current.get('nrf_seq',''),
                'prior_ms':prior.get('nrf_ms',''),'next_ms':current.get('nrf_ms','')})
        good,bad=reconstruct(items)
        scan_rejections.extend({'device_id':device,'key':key,'reason':reason} for key,reason in bad)
        meta=metadata.get(session,{})
        start=instant(meta['started_at']) if meta.get('started_at') else None
        sessions.append({'device_id':device,'session_id':session,'rows':len(items),
            'complete_scans':len(good),'rejected_groups':len(bad),'food':meta.get('food',''),
            'first_capture':items[0]['timestamp'],'last_capture':items[-1]['timestamp'],
            'nrf_boots':sorted(set(r.get('nrf_boot','') for r in items)),
            'group_id':session})
        for key,scan in good:
            first=instant(scan[0]['timestamp'])
            feature={'device_id':device,'session_id':session,'group_id':session,
                'nrf_boot':key[1],'scan_seq':key[2],'profile_id':key[3],
                'captured_at':first.isoformat(),'elapsed_s':(first-start).total_seconds() if start else '',
                'food':meta.get('food','')}
            for i,row in enumerate(scan):
                feature['gas_kohm_'+str(i)]=float(row['gas_resistance'])
                feature['log_gas_'+str(i)]=math.log(float(row['gas_resistance']))
            for name in ('temperature','humidity','pressure'):
                feature[name+'_mean']=sum(float(r[name]) for r in scan)/10
            features.append(feature)
    report={'inputs':inputs,'rows_unique_valid':len(rows),'identical_duplicates':duplicates,
        'conflicting_uids':len(conflicts),'rejected_rows':rejected,'rejected_scans':scan_rejections,
        'complete_scans':len(features),'sessions':sessions,'gaps':gaps,
        'limitations':['CSV does not identify firmware, SSID, Notify state or continuous connectivity.',
          'Boot ID changes are observed; they do not prove why a restart occurred.',
          'No interpolation, missing-step substitution, or generated spoilage labels.',
          'Split by group_id; do not use random row splits. Fit preprocessing only on training sessions.',
          'Food labels and repeated independent sessions are required before classification evaluation.']}
    split_manifest={s['session_id']:metadata.get(s['session_id'],{}).get('split','UNASSIGNED') for s in sessions}
    if any(v not in ('train','validation','test','UNASSIGNED') for v in split_manifest.values()):
        raise ValueError('Invalid session split')
    output.mkdir(parents=True)
    (output/'split_manifest.json').write_text(json.dumps(split_manifest,indent=2),encoding='utf-8')
    (output/'quality_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    for name,data in [('features.csv',features),('sessions.csv',sessions),('gaps.csv',gaps)]:
        with (output/name).open('w',encoding='utf-8-sig',newline='') as stream:
            if data:
                writer=csv.DictWriter(stream,fieldnames=list(data[0]));writer.writeheader();writer.writerows(data)
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv',nargs='+');parser.add_argument('--output',required=True)
    parser.add_argument('--sessions');parser.add_argument('--gap-seconds',type=float,default=1200)
    args=parser.parse_args()
    meta=json.loads(Path(args.sessions).read_text(encoding='utf-8')) if args.sessions else None
    result=prepare(args.csv,args.output,meta,args.gap_seconds)
    print(json.dumps({k:result[k] for k in ('rows_unique_valid','identical_duplicates','conflicting_uids','complete_scans')},ensure_ascii=False))
    print('quality_report.json, features.csv, sessions.csv, gaps.csv written; source CSV unchanged')
