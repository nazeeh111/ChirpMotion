"""Synthetic offline CLI evidence; no audio devices or inherited recordings."""
import csv, hashlib, math, random, struct, subprocess, sys, tempfile, unittest
from pathlib import Path

BINARY = str(Path(sys.argv.pop(1)).resolve())
RATE=48000

def chirp(j, beat):
    t=j/RATE; u=t-beat/300000
    return math.cos(2*math.pi*(17000*u+150000*u*u))*(.5-.5*math.cos(2*math.pi*j/960))

def fixture(kind):
    rng=random.Random(20261116)
    functions={'single':lambda j:12000*chirp(j,300), 'offbin':lambda j:12000*chirp(j,317.3),
        'two':lambda j:6000*(chirp(j,300)+chirp(j,850)), 'noise':lambda j:rng.uniform(-12000,12000),
        'silence':lambda j:0,'outband':lambda j:12000*chirp(j,1900),
        'low':lambda j:400*chirp(j,300),'high':lambda j:25000*chirp(j,300)}
    return b''.join(struct.pack('<h',round(functions[kind](j))) for j in range(961))

class Quality(unittest.TestCase):
    def test_quality_and_evidence(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for kind in ('single','offbin','two','noise','silence','outband','low','high'):
                with self.subTest(kind=kind):
                    source=root/(kind+'.pcm'); source.write_bytes(fixture(kind)); before=hashlib.sha256(source.read_bytes()).digest()
                    out=root/kind
                    p=subprocess.run([BINARY,'--input',str(source),'--output',str(out),'--start-sample','1','--spectra-csv'],capture_output=True,text=True)
                    self.assertEqual(p.returncode,0,p.stderr)
                    self.assertEqual(before,hashlib.sha256(source.read_bytes()).digest())
                    with (out/'cycles.csv').open() as f: row=next(csv.DictReader(f))
                    with (out/'spectra.csv').open() as f: spectra=list(csv.DictReader(f))
                    expected='no_signal' if kind=='silence' else 'ambiguous' if kind in ('two','noise','outband') else 'usable'
                    self.assertEqual(row['quality'],expected)
                    if kind=='silence': self.assertEqual(row['peak_hz'],'null')
                    else:
                        peak=max(spectra,key=lambda x:float(x['magnitude']))
                        self.assertEqual(float(row['peak_hz']),float(peak['frequency_hz']))
                        self.assertAlmostEqual(float(row['peak_magnitude']),float(peak['magnitude']))
                    if expected!='usable': self.assertEqual((row['relative_delay_seconds'],row['relative_path_meters']),('null','null'))
                    else:
                        target=317.3 if kind=='offbin' else 300
                        self.assertLessEqual(abs(float(row['peak_hz'])-target),RATE/16384)
                        self.assertAlmostEqual(float(row['relative_path_meters']),float(row['peak_hz'])/300000*340)
    def test_short_signal_refused_and_fft_boundaries(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); source=root/'s.pcm';source.write_bytes(fixture('single')[:-2])
            p=subprocess.run([BINARY,'--input',str(source),'--output',str(root/'short'),'--start-sample','1'],capture_output=True)
            self.assertNotEqual(p.returncode,0);self.assertFalse((root/'short'/'summary.json').exists())
            source.write_bytes(fixture('single'))
            for n in (1024,65536):
                out=root/str(n);p=subprocess.run([BINARY,'--input',str(source),'--output',str(out),'--start-sample','1','--fft-length',str(n)],capture_output=True)
                self.assertEqual(p.returncode,0,p.stderr)
                with (out/'cycles.csv').open() as f: row=next(csv.DictReader(f))
                self.assertEqual(row['quality'],'usable');self.assertLessEqual(abs(float(row['peak_hz'])-300),RATE/n)

    def test_resolved_competitors_inside_guard_and_single_controls(self):
        def tone(j, beat, phase=0, window=True):
            t=j/RATE
            envelope=(.5-.5*math.cos(2*math.pi*j/960)) if window else 1
            return math.cos(2*math.pi*(17000*t+150000*t*t-beat*(t-.01))+phase)*envelope
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            cases=[('review-default',16384,lambda j:6000*(tone(j,560,window=False)+tone(j,665,math.pi,False)),'ambiguous'),
                   ('review-short-fft',1024,lambda j:6000*(tone(j,560)+tone(j,685,math.pi)),'ambiguous')]
            for n in (1024,16384,65536):
                cases.append(('single-'+str(n),n,lambda j:10000*tone(j,411.7),'usable'))
            for name,n,signal,expected in cases:
                with self.subTest(name=name):
                    src=root/(name+'.pcm');src.write_bytes(b''.join(struct.pack('<h',round(signal(j))) for j in range(961)))
                    out=root/name
                    result=subprocess.run([BINARY,'--input',str(src),'--output',str(out),'--start-sample','1','--fft-length',str(n),'--spectra-csv'],capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,result.stderr)
                    with (out/'cycles.csv').open() as f: row=next(csv.DictReader(f))
                    self.assertEqual(row['quality'],expected)
                    self.assertNotEqual(row['peak_hz'],'null')
                    if expected=='ambiguous':
                        self.assertEqual(row['relative_path_meters'],'null')
                        self.assertGreaterEqual(float(row['resolved_competing_magnitude']),.65*float(row['peak_magnitude']))
                        self.assertLessEqual(float(row['resolved_valley_magnitude']),.5*float(row['resolved_competing_magnitude']))
                    else:
                        self.assertLessEqual(abs(float(row['peak_hz'])-411.7),RATE/n)

    def test_one_sided_endpoint_competitors_and_endpoint_only(self):
        def signal(j, beat, second=None):
            t=j/RATE
            phase=2*math.pi*(17000*t+150000*t*t)
            first=math.cos(phase-2*math.pi*beat*(t-.01))
            other=0 if second is None else (0.4 if second==0 else 0.8)*math.cos(phase-2*math.pi*second*(t-.01)+math.pi)
            return round(11000*(first+other))
        cases=[('low',16384,152.34375,0),('low-small',1024,187.5,0),
               ('high',1024,1312.5,1500),('dc-only',16384,0,None),('top-only',1024,1500,None)]
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for name,n,beat,second in cases:
                with self.subTest(name=name):
                    src=root/(name+'.pcm');src.write_bytes(b''.join(struct.pack('<h',signal(j,beat,second)) for j in range(961)))
                    before=hashlib.sha256(src.read_bytes()).digest();out=root/name
                    result=subprocess.run([BINARY,'--input',str(src),'--output',str(out),'--start-sample','1','--fft-length',str(n),'--spectra-csv'],capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertEqual(before,hashlib.sha256(src.read_bytes()).digest())
                    with (out/'cycles.csv').open() as f: row=next(csv.DictReader(f))
                    with (out/'spectra.csv').open() as f: spectrum=list(csv.DictReader(f))
                    self.assertEqual(row['quality'],'ambiguous')
                    self.assertEqual(row['relative_path_meters'],'null')
                    peak=max(spectrum,key=lambda x:float(x['magnitude']))
                    self.assertEqual(float(row['peak_hz']),float(peak['frequency_hz']))
                    if second is not None:
                        endpoint=spectrum[0 if second==0 else -1]
                        self.assertAlmostEqual(float(row['resolved_competing_magnitude']),float(endpoint['magnitude']),places=6)
                        self.assertGreaterEqual(float(row['resolved_competing_magnitude']),.65*float(row['peak_magnitude']))
                        self.assertLessEqual(float(row['resolved_valley_magnitude']),.5*float(row['resolved_competing_magnitude']))

if __name__=='__main__': unittest.main()
