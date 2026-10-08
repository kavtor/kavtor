#!/usr/bin/env python3
"""Validate exported SuperSource commands against an isolated CasparMIX server."""
import argparse
import array
import json
import math
from pathlib import Path
import socket
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--fixture',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=a.output.resolve();root.mkdir(parents=True,exist_ok=True)
for name in ('media','log','data','template','cache'):(root/name).mkdir(exist_ok=True)
plans=json.loads((a.fixture/'plans.json').read_text())
for name,frequency in [('gradient',400),('green',1000)]:
    video='color=c=black:s=640x360:r=50:d=3,format=gbrp,geq=r=255*X/W:g=0:b=0' if name=='gradient' else 'color=c=green:s=640x360:r=50:d=3'
    subprocess.run(['ffmpeg','-y','-v','error','-f','lavfi','-i',video,'-f','lavfi','-i',f'sine=frequency={frequency}:sample_rate=48000:duration=3','-c:v','ffv1','-pix_fmt','bgra','-c:a','pcm_s16le','-shortest',str(root/'media'/f'{name}.mkv')],check=True)
with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
config=root/'caspar.config';config.write_text(f'<configuration><paths><media-path>{root}/media</media-path><log-path>{root}/log</log-path><data-path>{root}/data</data-path><template-path>{root}/template</template-path></paths><html><cache-path>{root}/cache</cache-path></html><ndi><auto-load>false</auto-load></ndi><channels>'+('<channel><video-mode>720p5000</video-mode><sync-group>ss-test</sync-group></channel>'*4)+f'</channels><controllers><tcp><port>{port}</port><protocol>AMCP</protocol></tcp></controllers></configuration>')
results={}
with (root/'server.log').open('w') as log:
    proc=subprocess.Popen([str(a.binary.resolve()),str(config)],cwd=root,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            if proc.poll() is not None:raise RuntimeError('Server exited')
            try:s=socket.create_connection(('127.0.0.1',port),.2);break
            except OSError:time.sleep(.2)
        else:raise RuntimeError('No AMCP')
        s.settimeout(10);reader=s.makefile('rb')
        with (root/'amcp.log').open('w') as transcript:
            def cmd(c):
                s.sendall((c+'\r\n').encode());reply=reader.readline().decode().strip();transcript.write(c+' -> '+reply+'\n');transcript.flush()
                if not reply.startswith('2'):raise RuntimeError(reply)
                if reply.startswith('201'):reader.readline()
            def capture(name,channel=3):
                path=root/(name+'.mkv');cmd(f'ADD {channel} FILE "{path}" -codec:v ffv1 -codec:a pcm_s16le');time.sleep(1.4);cmd(f'REMOVE {channel} FILE "{path}"');time.sleep(1)
                raw=subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-vf','select=eq(n\\,30)','-frames:v','1','-pix_fmt','bgra','-f','rawvideo','-'])
                assert len(raw)==1280*720*4
                subprocess.run(['ffmpeg','-y','-v','error','-i',str(path),'-vf','select=eq(n\\,30)','-frames:v','1',str(root/(name+'.png'))],check=True)
                return raw,path
            def pixel(data,x,y):
                offset=(round(y*720)*1280+round(x*1280))*4;b,g,r,alpha=data[offset:offset+4];return [r,g,b,alpha]
            cmd('PLAY 1-1 gradient.mkv LOOP');cmd('PLAY 2-1 green.mkv LOOP');time.sleep(.5)
            for name,commands in plans.items():
                for c in commands:cmd(c)
                time.sleep(.4);raw,path=capture(name)
                if name=='split':
                    left,right=pixel(raw,.25,.5),pixel(raw,.75,.5);assert 115<=left[0]<=140 and left[1]<10,left;assert right[1]>100 and right[0]<10,right
                    audio=subprocess.check_output(['ffmpeg','-v','error','-i',str(path),'-map','0:a:0','-ac','1','-ar','48000','-f','f32le','-']);samples=array.array('f');samples.frombytes(audio);samples=samples[4800:52800]
                    def amplitude(freq):
                        c=sum(value*math.cos(2*math.pi*freq*i/48000) for i,value in enumerate(samples));d=sum(value*math.sin(2*math.pi*freq*i/48000) for i,value in enumerate(samples));return 2*math.hypot(c,d)/len(samples)
                    primary,muted=amplitude(400),amplitude(1000);assert primary>.01 and muted<primary*.1,(primary,muted);results['audio']={'primary':primary,'muted':muted}
                elif name=='zoom':
                    colour=pixel(raw,.25,.5);assert 145<=colour[0]<=172,colour
                elif name=='pip':
                    centre,overlay=pixel(raw,.5,.5),pixel(raw,.75,.75);assert 115<=centre[0]<=140 and overlay[1]>100,(centre,overlay)
                elif name=='image':
                    colour=pixel(raw,.5,.5);assert 35<colour[0]<100 and colour[2]>105,colour
                results[name]=True
            if 'transparent' in plans:
                for c in plans['transparent']:cmd(c)
                cmd('PLAY 4-0 #ff0000');cmd('PLAY 4-1 route://3 RENDERED');cmd('MIXER 4-1 OPACITY 1');time.sleep(.4)
                rgba,_=capture('transparent-reentry',4);inside,outside=pixel(rgba,.5,.5),pixel(rgba,.25,.25)
                assert 110<=inside[0]<=145 and 110<=inside[2]<=145,inside
                assert outside[0]>240 and outside[1]<10 and outside[2]<10,outside
                results['transparent_reentry']=True
            for c in plans['split']:cmd(c)
            cmd('PLAY 4-0 #ffffff');cmd('PLAY 4-1 route://3 RENDERED');cmd('MIXER 4-1 OPACITY 0.5');time.sleep(.4)
            raw,_=capture('flattened-reentry',4);left,right=pixel(raw,.25,.5),pixel(raw,.75,.5)
            assert 178<=left[0]<=205 and 115<=left[1]<=140 and 115<=left[2]<=140,left
            assert 115<=right[0]<=140 and right[1]>175 and 115<=right[2]<=140,right
            results['flattened_reentry']=True
    finally:
        if proc.poll() is None:
            proc.stdin.write(b'q\n');proc.stdin.flush()
            try:proc.wait(timeout=8)
            except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=8)
(root/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
