// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Reproduce the immutable V15 VMEM bank. No network or device access.
import fs from 'node:fs';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import '../sdk/WebUpdate.js';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const pkg=fs.readFileSync(new URL('../sdk/firmware/FM-1_015.fwsc',import.meta.url));
assert.equal(sha(pkg),'db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a');
const raw=FM1Web.inspectPackage(new Uint8Array(pkg),true).factory;
assert.equal(sha(raw),'ecad0b8620f9d784d46c8da1e3c5de02c40214649ed8dec218026431046aad25');
// VMEM literals are seven-bit. A high bit starts a match of 3..66 bytes:
// 128..191 take a one/two-byte base-128 backward distance, 192..255 take
// one byte k for a distance of 128*(k+1), the same field one or more voices
// back. Optimal parse (shortest stream), decoded without a heap.
const n=raw.length,INF=1e9,heads=new Map(),matches=[];
for(let i=0;i<n;i++){
 matches.push([]);
 if(i+3>n)continue;
 const key=raw[i]|raw[i+1]<<8|raw[i+2]<<16,list=heads.get(key)||[],best={};
 for(let c=list.length-1,seen=0;c>=0&&seen<600;c--,seen++){
  const j=list[c],d=i-j;if(d>=16384)break;
  let L=0;while(L<66&&i+L<n&&raw[j+L]===raw[i+L])L++;
  if(L<3)continue;
  const k=d%128===0&&d/128<=128?'a':d<128?'s':'l';
  if(!best[k]||L>best[k][0])best[k]=[L,d];
 }
 for(const k in best)matches[i].push([best[k][0],best[k][1],k==='l'?3:2,k]);
 list.push(i);heads.set(key,list);
}
const cost=new Array(n+1).fill(INF),pick=new Array(n+1).fill(null);cost[n]=0;
for(let i=n-1;i>=0;i--){
 cost[i]=cost[i+1]+1;
 for(const [L,d,c,k] of matches[i])for(let l=3;l<=L;l++)if(c+cost[i+l]<cost[i]){cost[i]=c+cost[i+l];pick[i]=[l,d,k];}
}
const packed=[];
for(let i=0;i<n;){
 if(cost[i]===cost[i+1]+1){assert(raw[i]<128);packed.push(raw[i++]);continue;}
 const [l,d,k]=pick[i];
 if(k==='a')packed.push(192+l-3,d/128-1);
 else if(k==='s')packed.push(128+l-3,d);
 else packed.push(128+l-3,(d&127)|128,d>>7);
 i+=l;
}
const decoded=[];
for(let i=0;i<packed.length;){const token=packed[i++];if(token<128)decoded.push(token);else{
 const n=(token&63)+3,lo=packed[i++],distance=token>=192?(lo+1)*128:(lo&127)|(lo>=128?packed[i++]<<7:0);
 assert(distance>0&&distance<=decoded.length);
 for(let k=0;k<n;k++)decoded.push(decoded[decoded.length-distance]);
}}
assert.deepEqual(Buffer.from(decoded),Buffer.from(raw));
// Second stage: the token stream is Huffman coded (canonical, MSB first).
// counts[l] codes of length l, symbols in code order, then the bit stream.
const freq=new Map();for(const v of packed)freq.set(v,(freq.get(v)||0)+1);
let nodes=[...freq].map(([v,f])=>({f,syms:[v]}));const len=new Map([...freq.keys()].map(v=>[v,0]));
while(nodes.length>1){nodes.sort((x,y)=>x.f-y.f||x.syms[0]-y.syms[0]);const a=nodes.shift(),b=nodes.shift();
 for(const v of [...a.syms,...b.syms])len.set(v,len.get(v)+1);nodes.push({f:a.f+b.f,syms:[...a.syms,...b.syms]});}
assert(Math.max(...len.values())<16);
const order=[...len].sort((x,y)=>x[1]-y[1]||x[0]-y[0]),counts=new Array(16).fill(0),code=new Map();
for(const [,l] of order)counts[l]++;
for(let c=0,l=1,i=0;l<16;l++,c<<=1)for(let n=0;n<counts[l];n++,c++)code.set(order[i++][0],[c,l]);
const bits=[];let acc=0,nb=0;
for(const v of packed){const [c,l]=code.get(v);for(let k=l-1;k>=0;k--){acc=acc<<1|(c>>k&1);if(++nb===8){bits.push(acc);acc=nb=0;}}}
if(nb)bits.push(acc<<(8-nb));
{ // Decode both stages again, exactly as fm_factory.c does.
 let pos=0;const bit=()=>(bits[pos>>3]>>(7-(pos++&7)))&1;
 const sym=()=>{let c=0,first=0,index=0;for(let l=1;l<16;l++){c|=bit();if(c-counts[l]<first)return order[index+c-first][0];index+=counts[l];first+=counts[l];first<<=1;c<<=1;}throw new Error('bad code');};
 const out=[];while(out.length<raw.length){const t=sym();if(t<128){out.push(t);continue;}
  const lo=sym();const d=t>=192?(lo+1)*128:(lo&127)|(lo>=128?sym()<<7:0);for(let k=(t&63)+3;k;k--)out.push(out[out.length-d]);}
 assert.deepEqual(Buffer.from(out),Buffer.from(raw));
}
const arr=(name,a)=>{const l=[];for(let i=0;i<a.length;i+=24)l.push('    '+a.slice(i,i+24).join(',')+',');return 'static const uint8_t '+name+'[] = {\n'+l.join('\n')+'\n};\n';};
const content='/* SPDX-License-Identifier: Apache-2.0\n'+
 ' * M-VAVE original V15 factory DX7 bank; lossless encoding, 2026-10-19 (3310: optimal LZ parse with\n'+
 ' * voice-aligned matches, then canonical Huffman).\n'+
 ' * Regenerate: node tools/Generate-Factory-DX7.mjs\n'+
 ' * VMEM SHA-256: '+sha(raw)+'\n'+
 ' * Source and license: THIRD_PARTY.md (FM-1_015.fwsc). */\n'+
 arr('factory_counts',counts)+arr('factory_symbols',order.map(x=>x[0]))+arr('factory_bits',bits);
const target=new URL('../app/fm_factory.inc',import.meta.url);
if(process.argv.includes('--check'))assert.equal(fs.readFileSync(target,'utf8'),content);
else fs.writeFileSync(target,content);
console.log(`V15 factory bank verified: 128 voices, ${raw.length} VMEM bytes, ${packed.length} LZ bytes, ${bits.length+order.length+16} Huffman bytes.`);
