#!/usr/bin/env python3
"""Reserve independent SuperSource render channels without replacing server settings."""
import argparse,copy,os,tempfile,time
from pathlib import Path
import xml.etree.ElementTree as ET

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config',type=Path,help='Existing CasparCG configuration file')
    parser.add_argument('--dry-run',action='store_true',help='Check and report without writing')
    args=parser.parse_args();path=args.config.resolve()
    root=ET.fromstring(path.read_bytes(),parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))
    channels=root.find('channels')
    if channels is None:parser.error('Configuration has no channels element')
    existing=channels.findall('channel')
    if len(existing)>=110:print(f'No changes: {len(existing)} channels already available');return
    if len(existing)<38:parser.error('The current kavtor configuration must provide its existing 38 channels first')
    mode=existing[0].find('video-mode');group=existing[0].find('sync-group')
    if mode is None or group is None or not (group.text or '').strip():parser.error('Source channels must have a video mode and shared sync-group')
    for _ in range(110-len(existing)):
        channel=ET.SubElement(channels,'channel');channel.append(copy.deepcopy(group));channel.append(copy.deepcopy(mode))
    if args.dry_run:print(f'Would extend {len(existing)} to 110 channels; existing settings retained');return
    backup=path.with_name(path.name+f'.before-ss-{time.time_ns()}');backup.write_bytes(path.read_bytes())
    ET.indent(root,space='    ');data=ET.tostring(root,encoding='utf-8',xml_declaration=True)
    fd,name=tempfile.mkstemp(prefix=path.name+'.',dir=path.parent)
    try:
        with os.fdopen(fd,'wb') as stream:stream.write(data);stream.flush();os.fsync(stream.fileno())
        os.chmod(name,path.stat().st_mode);os.replace(name,path)
    finally:
        if os.path.exists(name):os.unlink(name)
    print(f'Extended {len(existing)} to 110 channels. Backup: {backup}. Restart CasparCG to apply.')
if __name__=='__main__':main()
