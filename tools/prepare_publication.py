#!/usr/bin/env python3
"""Create a standalone source bundle excluding local state and reference projects."""
import argparse
from pathlib import Path
import tarfile
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
files=[ROOT/name for name in ('CMakeLists.txt','README.md','resources.qrc','.gitignore','CONTRIBUTING.md','CHANGELOG.md','LICENSE')]
for name in ('src','tests','cmake','resources','share','docs','.github','tools','branding','data'):
    files.extend(p for p in (ROOT/name).rglob('*') if p.is_file())
# Config examples are inspected separately: do not copy active switcher.json.
files.extend(p for p in (ROOT/'config/examples').glob('*') if p.is_file())
args.output.parent.mkdir(parents=True,exist_ok=True)
with tarfile.open(args.output,'w:gz') as archive:
    for p in sorted(set(files)):
        if '__pycache__' in p.parts or p.suffix in ('.pyc','.log'): continue
        if p.is_symlink(): raise ValueError(f'Unexpected source symlink: {p}')
        archive.add(p,arcname='kavtor/'+str(p.relative_to(ROOT)),recursive=False)
print(f'Prepared {args.output}; source bundle prepared; inspect before publishing.')
