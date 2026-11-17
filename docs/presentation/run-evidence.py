"""Generate the existing single/two-component fixtures and invoke the native CLI.

Run: python3 docs/presentation/run-evidence.py native/build/chirpmotion-native NEW_DIRECTORY
Requires the complete source checkout, Python 3 standard library and the built C17 CLI.
Never overwrites the destination. Does not read devices or bundled recordings.
"""
import csv,hashlib,json,runpy,struct,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1]).resolve();dest=Path(sys.argv[2]).resolve();dest.mkdir()
original_argv=sys.argv[:];sys.argv=[str(ROOT/'native/tests/test_quality.py'),str(binary)]
fixture=runpy.run_path(str(ROOT/'native/tests/test_quality.py'))['fixture'];sys.argv=original_argv
commands=[];rows={}
for name in ('single','two'):
 case=dest/name;case.mkdir();pcm=case/'input.pcm';pcm.write_bytes(fixture(name));before=hashlib.sha256(pcm.read_bytes()).hexdigest()
 args=[str(binary),'--input',str(pcm),'--start-sample','1','--spectra-csv','--output',str(case/'analysis')]
 p=subprocess.run(args,text=True,capture_output=True);commands.append({'case':name,'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr});assert p.returncode==0,p.stderr
 assert hashlib.sha256(pcm.read_bytes()).hexdigest()==before
 with (case/'analysis/cycles.csv').open() as f:row=next(csv.DictReader(f))
 rows[name]=row
 assert row['quality']==('usable' if name=='single' else 'ambiguous')
 assert row['relative_path_meters']!='null' if name=='single' else row['relative_path_meters']=='null'
 for n in ('cycles.csv','spectra.csv','summary.json'):
  (case/n).write_bytes((case/'analysis'/n).read_bytes())
 # Only our just-generated output directory, copied above.
 import shutil;shutil.rmtree(case/'analysis')
files={p.relative_to(dest).as_posix():{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in dest.rglob('*') if p.is_file()}
manifest={'schema':'chirpmotion-presentation-evidence-v1','source_commit':'db81f1af36f09498c8b3eaeb4368e93347420225','source_tree':'1193e9fac8e7e51afe0e4d7dafed1f1311b2db85','source_external_url':'https://github.com/nazeeh111/ChirpMotion/tree/db81f1af36f09498c8b3eaeb4368e93347420225','fixture_source':'native/tests/test_quality.py fixture(single), fixture(two) in the external complete source checkout','sample_rate_hz':48000,'samples_per_case':961,'mode':'up','known_start_sample':1,'rows':rows,'files':files,'limits':'Two generated fixtures; heuristic screening and modeled relative path, not physical calibration or generic accuracy.'}
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'manifest':manifest,'commands':commands},indent=2))
