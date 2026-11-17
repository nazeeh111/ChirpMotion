"""Render preserved native outputs; requires existing NumPy and Matplotlib.

No measurement is run here. All CSV bins are retained; simplification is disabled.
"""
import csv,hashlib,json,struct,xml.etree.ElementTree as E
from pathlib import Path
import numpy as np
import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
HERE=Path(__file__).resolve().parent;DATA=HERE/'data';manifest=json.loads((DATA/'manifest.json').read_text())
for name,record in manifest['files'].items():assert hashlib.sha256((DATA/name).read_bytes()).hexdigest()==record['sha256']
with mpl.rc_context({'font.family':['DejaVu Sans','Arial','sans-serif'],'font.size':17,'svg.fonttype':'none','svg.hashsalt':'chirpmotion-screening','path.simplify':False,'axes.spines.top':False,'axes.spines.right':False,'axes.labelcolor':'#253642','text.color':'#253642','xtick.color':'#253642','ytick.color':'#253642'}):
 for mobile in (False,True):
  fig,axs=plt.subplots(4 if mobile else 2,1 if mobile else 2,figsize=(8,18) if mobile else (13,9),layout='constrained');axs=np.array(axs).reshape(-1);fig.set_facecolor('#fffdf8');fig.set_constrained_layout_pads(w_pad=.12,h_pad=.15,hspace=.04,wspace=.04);colors=['#245d89','#94551a']
  for i,name in enumerate(('single','two')):
   raw=np.array(struct.unpack('<961h',(DATA/name/'input.pcm').read_bytes()));rows=list(csv.DictReader((DATA/name/'spectra.csv').open()));frequency=np.array([float(r['frequency_hz']) for r in rows]);magnitude=np.array([float(r['magnitude']) for r in rows]);assert len(rows)==513
   wave=axs[i*2] if mobile else axs[i];spectrum=axs[i*2+1] if mobile else axs[i+2]
   wave.plot(np.arange(961)/48,raw,color=colors[i],lw=.8);wave.set(xlim=(0,20),ylim=(-13000,13000),xlabel='Time (ms)',ylabel='PCM int16 units',title='One delayed component' if i==0 else 'Two competing components');wave.set_facecolor('#fffdf8');wave.axhline(0,color='#aab5b9',lw=.6,zorder=0)
   spectrum.plot(frequency,magnitude,color=colors[i],lw=2);spectrum.set(xlim=(0,1500),ylim=(0,1200000),xlabel='Beat frequency (Hz)',ylabel='Unnormalized magnitude');spectrum.set_facecolor('#fffdf8');spectrum.ticklabel_format(axis='y',style='sci',scilimits=(0,0),useMathText=False)
   row=manifest['rows'][name];peak=float(row['peak_hz']);value=float(row['peak_magnitude']);spectrum.plot([peak],[value],marker='o',color=colors[i],ms=7)
   spectrum.set_title('usable · 298.828125 Hz' if i==0 else 'ambiguous · delay/path null',color=colors[i],fontweight='bold',fontsize=18)
  if mobile:
   for ax in axs:
    ax.tick_params(labelsize=20);ax.xaxis.label.set_size(20);ax.yaxis.label.set_size(20);ax.title.set_size(21)
  fig.suptitle('A peak is not yet a path.',fontsize=28,fontweight='bold')
  stem='screening-mobile' if mobile else 'screening';fig.savefig(HERE/(stem+'.png'),dpi=120,facecolor=fig.get_facecolor(),metadata={'Software':'ChirpMotion source-derived evidence'});fig.savefig(HERE/(stem+'.svg'),facecolor=fig.get_facecolor(),metadata={'Date':None,'Creator':'ChirpMotion source-derived evidence'})
  p=HERE/(stem+'.svg');E.register_namespace('','http://www.w3.org/2000/svg');root=E.fromstring(p.read_bytes());ns='{http://www.w3.org/2000/svg}';title=E.Element(ns+'title',{'id':stem+'-title'});title.text='ChirpMotion: inspect a peak before reporting a path';desc=E.Element(ns+'desc',{'id':stem+'-desc'});desc.text='Two synthetic 961-sample inputs and all 513 beat-band bins per case; the single component is usable, while competing components retain their spectrum with null delay and path.';root.insert(0,title);root.insert(1,desc);root.set('role','img');root.set('aria-labelledby',stem+'-title '+stem+'-desc')
  for node in root.iter():
   if node.tag in (ns+'text',ns+'tspan'):node.set('font-family','DejaVu Sans, Arial, Helvetica, sans-serif')
  p.write_bytes(E.tostring(root,encoding='utf-8',xml_declaration=True));plt.close(fig)
figures={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in HERE.glob('screening*') if p.suffix in ('.png','.svg')}
(HERE/'figure-manifest.json').write_text(json.dumps({'source_commit':manifest['source_commit'],'input_manifest_sha256':hashlib.sha256((DATA/'manifest.json').read_bytes()).hexdigest(),'encoded_samples_per_input':961,'encoded_spectrum_vertices_per_case':513,'transformations':['PCM16LE decoded as raw signed counts','time sample index divided by 48000 and multiplied by 1000','all stored frequency/magnitude bins plotted without normalization, smoothing, filtering or simplification'],'uncertainty':'None estimated; heuristic screening is not a probability or physical calibration.','figures':figures},indent=2)+'\n')
svg=(HERE/'screening.svg').read_text().split('?>',1)[-1];mobile=(HERE/'screening-mobile.svg').read_text().split('?>',1)[-1]
(HERE/'screening.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ChirpMotion native evidence</title><style>body{margin:0;background:#fffdf8}main{max-width:1040px;margin:auto}svg{width:100%;height:auto;display:block}.mobile{display:none}@media(max-width:640px){.wide{display:none}.mobile{display:block}}</style><main><div class="wide">'+svg+'</div><div class="mobile">'+mobile+'</div></main></html>')
print(json.dumps(figures,indent=2))
