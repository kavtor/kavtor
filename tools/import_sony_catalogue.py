import argparse,hashlib,json,re
from pathlib import Path
parser=argparse.ArgumentParser(description='Import the supplied Sony pattern catalogue as an English reference inventory.')
parser.add_argument('source',type=Path)
parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
a=parser.parse_args();source=a.source;text=source.read_text();rows=[];section=0;family=''
families={1:'Standard',2:'Enhanced',3:'Rotary',4:'Mosaic',5:'Random',6:'DME',7:'DME',8:'DME',9:'Resizer'}
groups=['Page Turn','Page Roll','Flip Tumble','Frame in-out','Picture-in-picture','Crop Slide','Split Slide','3D Trans','2D Trans','Slide','Squeeze','Door','Mirror','Brick','Sparkle']
for lineno,line in enumerate(text.splitlines(),1):
 h=re.match(r'^## (\d+)\.',line)
 if h:section=int(h.group(1));family=families.get(section,'User register')
 if line.startswith('###') and section>=6:
  family=next((g for g in groups if g.lower() in line.lower()),'Surface/video effects')
  if 'Roll de' in line:family='Page Roll'
 m=re.match(r'^- \*\*(\d+) — ',line)
 if not m or section>9:continue
 code=int(m.group(1));namespace='wipe' if section<=5 else 'resizer' if section==9 else 'dme';channels=None if namespace=='resizer' else 1 if namespace=='wipe' else section-5
 rows.append({'code':code,'namespace':namespace,'family':family,'label':f'Sony {family} {code}','channels':channels,'source_line':lineno,'implementation':'reference-only'})
assert len(rows)==385 and len({r['code'] for r in rows})==385
counts={kind:sum(r['namespace']==kind for r in rows) for kind in ['wipe','dme','resizer']};assert counts=={'wipe':116,'dme':238,'resizer':31},counts
root=a.root;(root/'data').mkdir(exist_ok=True)
(root/'data/sony-patterns-reference.json').write_text(json.dumps({'schema':1,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'note':'Reference identifiers only. Labels are development descriptions, not official Sony names. Capability lists must use validated implementations, not this inventory.','sources':['https://pro.sony/s3/cms-static-content/operation-manual/3704674111.pdf','Sony XVS manual 50135021M.pdf'],'predefined_counts':counts,'patterns':rows,'user_registers':[{'first':b+1,'last':b+99,'channels':c,'content':'User-authored keyframes; not fixed presets'} for b,c in [(1900,1),(2900,2),(3900,3)]],'open_questions':['Page Turn outgoing/incoming surface assignment differs between DVS and XVS manuals.','Random, Karaoke and mosaic phase timing require animated references.','Frame in-out and Picture-in-picture retain an intermediate state and cannot use a generic one-shot A-to-B completion rule.','Two-channel page backsides and DME external background signals are separate resources.']},indent=2)+'\n')
(root/'docs/sony-pattern-reference.md').write_text('''# Sony pattern reference inventory

The user-supplied comparison of DVS-9000 and XVS manuals has been imported into
`data/sony-patterns-reference.json`: 385 predefined identifiers (116 wipes, 238
DME and 31 Resizer DME), plus three separate 99-register user-effect ranges.
The input document is fingerprinted by SHA-256; each record retains its source
line for traceability. All exported labels and metadata are English. Generic
labels are development labels, not claimed official Sony pattern names.

This is an inventory, not an implementation or a capability advertisement. Native
MOVE/CUBE/PAGE primitives are not automatically equivalent to a numbered Sony
preset. Only verified patterns may be enabled on the panel or reported by the API.

Wipes reveal an unchanged texture through a moving mask; DME transforms the
video texture. Programme must match the intended endpoint, except for families
such as Frame in-out/Picture-in-picture that deliberately retain an intermediate
state. Do not manufacture fixed effects for the user-register ranges.

DVS describes an outgoing page revealing the new image; XVS describes the new
page moving over the old image. Keep that model distinction open until animated
references establish the exact texture/backside assignments. Both describe Roll
as the incoming image unrolling over the old one. Background fill and a two-channel
page backside are independent signals; selecting a background must not implicitly
change the backside texture.

Primary reference: [Sony DVS-9000 manual](https://pro.sony/s3/cms-static-content/operation-manual/3704674111.pdf), printed pages 353–372;
XVS-9000/8000/7000/6000 manual 50135021M, pages 497–503.
''')
print(counts)
