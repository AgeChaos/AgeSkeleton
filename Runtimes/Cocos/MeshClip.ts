// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
import { MotionPose, RuntimeRig, AnimationEvent, collectEvents } from './Motion';
export interface Attachment { name:string; texture:number; offset:number; count:number; slot:number; uv:number[]; color:number[]; triangles:number[]; }
export interface Binding {slot:number; key:string; attachment:number;}
export interface Skin {name:string; bindings:Binding[];}
export interface Frame {time:number; matrices?:number[]; influencePositions?:number[]; positions:number[]; keys:string[]; colors:number[]; orders:number[];}
export interface Clip {name:string; duration:number; loop:boolean; frames:Frame[]; events?:AnimationEvent[];}
export interface MeshClip {format:string; version:number; rig?:RuntimeRig; name:string; vertexCount:number; fps:number; textures:string[]; slots:string[]; attachments:Attachment[]; skins:Skin[]; defaultSkins:string[]; rest:Frame; clips:Clip[];}
function requireData(ok:boolean, reason:string):void {if(!ok) throw new Error(`Invalid AgeSkeleton data: ${reason}`);}
export function validate(data:MeshClip):void {
    requireData(!!data && data.format==='ageskeleton.meshclip' && (data.version===1||data.version===2),'version');
    requireData(Number.isInteger(data.vertexCount) && data.vertexCount>0 && data.vertexCount<=100000 && data.fps>=1 && data.fps<=120,'limits');
    for(const array of [data.textures,data.slots,data.attachments,data.skins,data.defaultSkins,data.clips]) requireData(Array.isArray(array),'missing arrays');
    for(const name of data.textures) requireData(typeof name==='string' && name.length>0 && !/[\\/:]|\.\./.test(name),'texture filename');
    requireData(new Set(data.slots).size===data.slots.length && data.slots.every(s=>typeof s==='string' && !!s),'slots');
    const numbers=(a:number[],n:number):void=>requireData(Array.isArray(a) && a.length===n && a.every(Number.isFinite),'numbers');
    if(data.version===2)MotionPose.validate(data.rig!,data.vertexCount);
    let end=0;
    for(const a of data.attachments) {
        requireData(a.offset===end && Number.isInteger(a.count) && a.count>=3 && a.count<=data.vertexCount-end,'vertex range');end+=a.count;
        requireData(Number.isInteger(a.texture) && a.texture>=0 && a.texture<data.textures.length && Number.isInteger(a.slot) && a.slot>=-1 && a.slot<data.slots.length,'reference');
        numbers(a.uv,a.count*2); numbers(a.color,4);
        requireData(Array.isArray(a.triangles) && a.triangles.length>0 && a.triangles.length%3===0 && a.triangles.every(i=>Number.isInteger(i)&&i>=0&&i<a.count),'triangles');
    }
    requireData(end===data.vertexCount,'vertex count');
    const names=new Set<string>();
    for(const s of data.skins) {
        requireData(!!s.name && !names.has(s.name) && Array.isArray(s.bindings),'skin'); names.add(s.name);
        for(const b of s.bindings) requireData(Number.isInteger(b.slot)&&b.slot>=0&&b.slot<data.slots.length&&!!b.key&&Number.isInteger(b.attachment)&&b.attachment>=0&&b.attachment<data.attachments.length&&data.attachments[b.attachment].slot===b.slot,'binding');
    }
    requireData(data.defaultSkins.every(s=>names.has(s)),'default skins');
    const frame=(f:Frame):void=>{
        requireData(!!f && Number.isFinite(f.time) && f.time>=0,'frame time'); if(data.version===2){numbers(f.matrices!,data.rig!.names.length*6);numbers(f.influencePositions!,data.vertexCount*8);}numbers(f.positions,data.vertexCount*2); numbers(f.colors,data.slots.length*4);
        requireData(Array.isArray(f.keys)&&f.keys.length===data.slots.length&&f.keys.every(k=>typeof k==='string'),'keys');
        requireData(Array.isArray(f.orders)&&f.orders.length===data.slots.length&&f.orders.every(Number.isInteger),'orders');
    };
    frame(data.rest); names.clear();
    for(const c of data.clips) {
        requireData(!!c.name && !names.has(c.name) && Number.isFinite(c.duration)&&c.duration>0&&Array.isArray(c.frames)&&c.frames.length>=2,'clip');names.add(c.name);
        if(data.version===2){requireData(Array.isArray(c.events)&&c.events.length<=100000,'events');let prior=-1;for(const e of c.events!){requireData(!!e&&typeof e.name==='string'&&!!e.name&&Number.isFinite(e.time)&&e.time>=prior&&e.time>=0&&e.time<=c.duration&&Number.isInteger(e.intValue)&&e.intValue>=-2147483648&&e.intValue<=2147483647&&Number.isFinite(e.floatValue)&&Math.abs(e.floatValue)<=3.4028234663852886e38&&typeof e.stringValue==='string','event');prior=e.time;}}
        let last=-1;for(const f of c.frames){frame(f);requireData(f.time>last,'frame order');last=f.time;}
        requireData(c.frames[0].time===0&&Math.abs(last-c.duration)<.0001,'endpoints');
    }
}
export class Player {
    readonly positions:Float32Array; readonly colors:Float32Array; readonly visible:boolean[]; readonly orders:Int32Array;
    playing=false; speed=1; time=0; clip:Clip|null=null;
    events:AnimationEvent[]=[];private motion:MotionPose;private eventStart=false;
    private active:string[]; private overrides:(string|null)[]; private hidden:boolean[];
    constructor(readonly data:MeshClip) {
        validate(data);this.motion=new MotionPose(data.version===2?data.rig:undefined);this.positions=new Float32Array(data.vertexCount*2);this.colors=new Float32Array(data.attachments.length*4);this.visible=new Array(data.attachments.length).fill(false);this.orders=new Int32Array(data.attachments.length);
        this.active=data.defaultSkins.slice();this.overrides=new Array(data.slots.length).fill(null);this.hidden=new Array(data.slots.length).fill(false);this.evaluate();
    }
    play(name:string,restart=true):void {const next=this.data.clips.find(c=>c.name===name);if(!next)throw new Error('Unknown animation');if(restart||this.clip!==next){this.time=0;this.eventStart=true;}this.events=[];this.clip=next;this.playing=true;this.evaluate();}
    stop():void {this.events=[];this.eventStart=false;this.clip=null;this.time=0;this.playing=false;this.evaluate();}
    seek(time:number):void {if(!Number.isFinite(time))throw new Error('Invalid time');this.events=[];this.eventStart=false;this.time=this.clip?Math.max(0,Math.min(this.clip.duration,time)):0;this.evaluate();}
    update(delta:number):void {
        this.events=[];if(!this.playing||!this.clip)return;if(!Number.isFinite(delta)||delta<0||!Number.isFinite(this.speed))throw new Error('Invalid playback step');
        let t=this.time+delta*this.speed;if(!Number.isFinite(t))throw new Error('Playback overflow');
        const to=this.clip.loop?t:Math.max(0,Math.min(this.clip.duration,t));this.events=this.data.version===2?collectEvents(this.clip,this.time,to,this.eventStart):[];if(to!==this.time)this.eventStart=false;
        if(this.clip.loop)t=((t%this.clip.duration)+this.clip.duration)%this.clip.duration;
        else {if(t>=this.clip.duration||t<0)this.playing=false;t=Math.max(0,Math.min(this.clip.duration,t));}
        this.time=t;this.evaluate();
    }
    setIKTarget(bone:string,x:number,y:number,chainLength=2,mix=1,iterations=24,tolerance=.1):void{this.motion.setTarget(bone,x,y,chainLength,mix,iterations,tolerance);this.evaluate();}
    clearIKTarget(bone:string):void{this.motion.clearTarget(bone);this.evaluate();}
    getBoneTip(bone:string):[number,number]{return this.motion.getTip(bone);}
    setSkin(name:string):void {this.skin(name);this.active=[name];this.evaluate();}
    setWardrobe(group:string,name:string):void {
        if(!group||group.includes('/')||!this.data.skins.some(s=>s.name.startsWith(group+'/')))throw new Error('Unknown group');
        if(name){this.skin(name);if(!name.startsWith(group+'/'))throw new Error('Wrong group');}
        this.active=this.active.filter(s=>!s.startsWith(group+'/'));if(name)this.active.push(name);this.evaluate();
    }
    setSlotVisible(name:string,visible:boolean):void {this.hidden[this.slot(name)]=!visible;this.evaluate();}
    setAttachment(name:string,key:string|null):void {const s=this.slot(name);if(key&&!this.data.skins.some(skin=>skin.bindings.some(b=>b.slot===s&&b.key===key)))throw new Error('Unknown attachment');this.overrides[s]=key;this.evaluate();}
    private slot(name:string):number {const i=this.data.slots.indexOf(name);if(i<0)throw new Error('Unknown slot');return i;}
    private skin(name:string):Skin {const skin=this.data.skins.find(s=>s.name===name);if(!skin)throw new Error('Unknown skin');return skin;}
    private resolve(skin:string,slot:number,key:string,fallback:number):number {const found=this.data.skins.find(s=>s.name===skin);if(found)for(const b of found.bindings)if(b.slot===slot&&b.key===key)fallback=b.attachment;return fallback;}
    private evaluate():void {
        let a=this.data.rest,b=a,t=0;
        if(this.clip){let lo=0,hi=this.clip.frames.length-1;while(lo<hi){const mid=(lo+hi+1)>>1;if(this.clip.frames[mid].time<=this.time)lo=mid;else hi=mid-1;}a=this.clip.frames[lo];b=this.clip.frames[Math.min(lo+1,this.clip.frames.length-1)];t=b.time>a.time?(this.time-a.time)/(b.time-a.time):0;}
        for(let i=0;i<this.positions.length;i++)this.positions[i]=a.positions[i]+(b.positions[i]-a.positions[i])*t;
        this.motion.apply(a,b,t,this.positions);
        for(let i=0;i<this.data.attachments.length;i++){const m=this.data.attachments[i],s=m.slot;this.visible[i]=s<0;this.orders[i]=s<0?i:a.orders[s];for(let k=0;k<4;k++)this.colors[i*4+k]=m.color[k]*(s<0?1:a.colors[s*4+k]+(b.colors[s*4+k]-a.colors[s*4+k])*t);}
        for(let s=0;s<this.data.slots.length;s++){if(this.hidden[s])continue;const key=this.overrides[s]??a.keys[s];if(!key)continue;let i=this.resolve('default',s,key,-1);for(const skin of this.active)i=this.resolve(skin,s,key,i);if(i>=0)this.visible[i]=true;}
    }
}
