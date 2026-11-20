"""Read-only extraction of real StorageTelemetry ENV points for local alert replay."""
import argparse,json
from datetime import datetime,timezone
from pathlib import Path
import openpyxl
p=argparse.ArgumentParser();p.add_argument('workbook',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
w=openpyxl.load_workbook(a.workbook,read_only=True,data_only=True)
rows=w['StorageTelemetry'].values;header=next(rows);points=[]
for row in rows:
    r=dict(zip(header,row));at=r.get('captured_at');temp=r.get('temperature_c')
    if r.get('device_id')!='24e27bd4db1c' or not isinstance(temp,(float,int)) or not at:continue
    if isinstance(at,datetime):ms=at.replace(tzinfo=at.tzinfo or timezone.utc).timestamp()*1000
    else:
        try:ms=datetime.fromisoformat(str(at).replace('Z','+00:00')).timestamp()*1000
        except ValueError:continue
    points.append({'at':int(ms),'t':float(temp),'storage':r.get('storage_session_id')})
points.sort(key=lambda x:x['at']);a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(points,ensure_ascii=False),encoding='utf8')
print('points',len(points),'first',points[0]['at'] if points else None,'last',points[-1]['at'] if points else None)
