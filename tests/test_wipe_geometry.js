const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync(process.argv[2],'utf8');
const start=source.indexOf('        function contourExtrema('),end=source.indexOf('        function applyBorderOverlay(',start);
const context={Uint8Array,Int32Array,Math};vm.createContext(context);vm.runInContext(source.slice(start,end),context);
// Compare spatial filters to a direct neighbourhood reference, including
// radius larger than the image and clamped edges (no picture-frame border).
for(let w=1;w<12;w++)for(let h=1;h<8;h++)for(let radius=1;radius<7;radius++)for(const maximum of [true,false]){
 const image=Uint8Array.from({length:w*h},(_,i)=>(i*73+w*11+h*5)%256);
 const actual=context.contourExtrema(image,w,h,radius,maximum);
 for(let y=0;y<h;y++)for(let x=0;x<w;x++){
  let expected=maximum?0:255;
  for(let dy=-radius;dy<=radius;dy++)for(let dx=-radius;dx<=radius;dx++){
   const value=image[Math.max(0,Math.min(h-1,y+dy))*w+Math.max(0,Math.min(w-1,x+dx))];
   expected=maximum?Math.max(expected,value):Math.min(expected,value);
  }
  assert.equal(actual[y*w+x],expected,`${w}x${h} r${radius} ${maximum} at ${x},${y}`);
 }
}
for(const value of [0,255]){
 const flat=new Uint8Array(400).fill(value);
 const outer=context.contourExtrema(flat,20,20,5,true),inner=context.contourExtrema(flat,20,20,5,false);
 assert(outer.every((v,i)=>v-inner[i]===0),'Uniform masks must have no border');
}
console.log('Spatial wipe contour, constant masks and clamped boundaries: PASS');

// Bursts of T-bar samples must schedule one render of the latest position.
const queued=[];let paints=0;
const runtime={progress:0,autorunTimer:0,pendingPaint:0,clamp01:v=>Math.max(0,Math.min(1,v)),
 requestAnimationFrame:cb=>{queued.push(cb);return queued.length;},
 cancelAnimationFrame:()=>{},paint:()=>{paints++;}};
vm.createContext(runtime);
const manualStart=source.indexOf('        function setProgress(t)'),
 manualEnd=source.indexOf('        function playWipe(',manualStart);
vm.runInContext(source.slice(manualStart,manualEnd),runtime);
for(let i=0;i<=100;i++) runtime.setProgress(i/100);
assert.equal(queued.length,1);assert.equal(paints,0);assert.equal(runtime.progress,1);
queued.shift()();assert.equal(paints,1);assert.equal(runtime.pendingPaint,0);
console.log('T-bar sample coalescing: PASS');
