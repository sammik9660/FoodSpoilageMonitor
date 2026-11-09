import csv,json,tempfile,unittest,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'analysis'))
from prepare_dataset import prepare
from fingerprint_test import FingerprintsTest

class DatasetTest(unittest.TestCase):
    def test_merge_gap_device_isolation_and_conflicts(self):
        fixture=FingerprintsTest();fixture.setUp()
        rows=[r|{'device_id':'one','timestamp':'2026-11-04T12:00:00Z','received_at':'2026-11-04T12:00:05Z','capture_quality':'ntp'} for r in fixture.rows]
        rows+= [r|{'sample_uid':r['sample_uid']+'later','scan_seq':'3','timestamp':'2026-11-04T17:00:00Z','received_at':'2026-11-04T17:00:05Z'} for r in rows[:10]]
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'a.csv'
            with p.open('w',newline='',encoding='utf-8') as f:
                w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
            raw=p.read_bytes();report=prepare([p,p],Path(td)/'out')
            self.assertEqual(report['complete_scans'],2);self.assertEqual(report['identical_duplicates'],20)
            self.assertEqual(report['gaps'][0]['seconds'],18000);self.assertEqual(p.read_bytes(),raw)
            with self.assertRaises(ValueError):prepare([p],Path(td)/'out')
            with p.open('a',newline='',encoding='utf-8') as f:
                csv.DictWriter(f,fieldnames=list(rows[0])).writerow(rows[0]|{'gas_resistance':'99'})
            self.assertEqual(prepare([p],Path(td)/'bad')['complete_scans'],1)

if __name__=='__main__':unittest.main()
