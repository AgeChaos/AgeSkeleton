// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
import type { Frame, Clip } from './MeshClip';
export interface RuntimeRig {names:string[];parents:number[];lengths:number[];vertexBones:number[];weights:number[];}
export interface AnimationEvent {name:string;animation?:string;time:number;intValue:number;floatValue:number;stringValue:string;}
interface Target {x:number;y:number;chain:number;mix:number;iterations:number;tolerance:number;}
export class MotionPose {
    private targets:(Target|null)[];private basis:Float64Array;private posed:Float64Array;private backup:Float64Array;
    constructor(private rig:RuntimeRig|undefined){const n=rig?.names.length??0;this.targets=new Array(n).fill(null);this.basis=new Float64Array(n*6);this.posed=new Float64Array(n*6);this.backup=new Float64Array(n*6);}
    static validate(r:RuntimeRig,vertices:number):void {
        const fail=():never=>{throw new Error('Invalid runtime rig');};
        if(!r||!Array.isArray(r.names)||r.names.length<1||r.names.length>256||!Array.isArray(r.parents)||r.parents.length!==r.names.length||!Array.isArray(r.lengths)||r.lengths.length!==r.names.length||!Array.isArray(r.vertexBones)||r.vertexBones.length!==vertices*4||!Array.isArray(r.weights)||r.weights.length!==vertices*4)fail();
        const names=new Set<string>();
        for(let i=0;i<r.names.length;i++){if(typeof r.names[i]!=='string'||!r.names[i]||names.has(r.names[i])||!Number.isFinite(r.lengths[i])||r.lengths[i]<0)fail();names.add(r.names[i]);let p=i,count=0;while(p>=0){if(p>=r.names.length||++count>r.names.length)fail();p=r.parents[p];if(!Number.isInteger(p)||p < -1)fail();}}
        for(let i=0;i<r.weights.length;i++)if(!Number.isFinite(r.weights[i])||r.weights[i]<0||r.weights[i]>1||!Number.isInteger(r.vertexBones[i])||r.vertexBones[i]<-1||r.vertexBones[i]>=r.names.length||(r.vertexBones[i]<0&&r.weights[i]!==0))fail();
    }
    private bone(name:string):number{const i=this.rig?.names.indexOf(name)??-1;if(i<0)throw new Error('Unknown runtime bone; re-export with runtime format v2');return i;}
    setTarget(name:string,x:number,y:number,chain:number,mix:number,iterations:number,tolerance:number):void {const i=this.bone(name);if(![x,y,mix,tolerance].every(Number.isFinite)||mix<0||mix>1||!Number.isInteger(chain)||chain<1||chain>16||!Number.isInteger(iterations)||iterations<1||iterations>128||tolerance<0)throw new Error('Invalid IK target');let p=i;for(let j=0;j<chain;j++){if(p<0)throw new Error('IK chain exceeds hierarchy');p=this.rig!.parents[p];}this.targets[i]={x,y,chain,mix,iterations,tolerance};}
    clearTarget(name:string):void{this.targets[this.bone(name)]=null;}
    getTip(name:string):[number,number]{return this.tip(this.bone(name));}
    private tip(bone:number):[number,number]{const k=bone*6,l=this.rig!.lengths[bone];return [this.posed[k+4]+this.posed[k]*l,this.posed[k+5]+this.posed[k+1]*l];}
    private child(node:number,ancestor:number):boolean{for(let i=node;i>=0;i=this.rig!.parents[i])if(i===ancestor)return true;return false;}
    private rotate(bone:number,angle:number):void{const c=Math.cos(angle),s=Math.sin(angle),x=this.posed[bone*6+4],y=this.posed[bone*6+5];for(let n=0;n<this.targets.length;n++)if(this.child(n,bone)){const k=n*6;for(let axis=0;axis<2;axis++){const a=this.posed[k+axis*2],b=this.posed[k+axis*2+1];this.posed[k+axis*2]=c*a-s*b;this.posed[k+axis*2+1]=s*a+c*b;}const dx=this.posed[k+4]-x,dy=this.posed[k+5]-y;this.posed[k+4]=x+c*dx-s*dy;this.posed[k+5]=y+s*dx+c*dy;}}
    private chain=new Int32Array(16);private angles=new Float64Array(16);
    apply(a:Frame,b:Frame,t:number,positions:Float32Array):void {if(!this.rig)return;const am=a.matrices!,bm=b.matrices!;for(let i=0;i<this.basis.length;i++)this.basis[i]=this.posed[i]=am[i]+(bm[i]-am[i])*t;let changed=false;
        for(let tip=0;tip<this.targets.length;tip++){const target=this.targets[tip];if(!target||target.mix===0)continue;changed=true;let p=tip;this.angles.fill(0);for(let j=0;j<target.chain;j++){this.chain[j]=p;p=this.rig.parents[p];}this.backup.set(this.posed);
            for(let iteration=0;iteration<target.iterations;iteration++){let [ex,ey]=this.tip(tip);if(Math.hypot(ex-target.x,ey-target.y)<=target.tolerance)break;for(let j=0;j<target.chain;j++){const k=this.chain[j]*6;[ex,ey]=this.tip(tip);const ax=ex-this.posed[k+4],ay=ey-this.posed[k+5],bx=target.x-this.posed[k+4],by=target.y-this.posed[k+5];if(ax*ax+ay*ay<1e-12||bx*bx+by*by<1e-12)continue;const angle=Math.atan2(ax*by-ay*bx,ax*bx+ay*by);this.angles[j]+=angle;this.rotate(this.chain[j],angle);}}
            if(target.mix<1){this.posed.set(this.backup);for(let j=target.chain-1;j>=0;j--)this.rotate(this.chain[j],Math.atan2(Math.sin(this.angles[j]),Math.cos(this.angles[j]))*target.mix);}
        }
        if(!changed)return;const la=a.influencePositions!,lb=b.influencePositions!;for(let i=0;i<this.rig.vertexBones.length;i++){const bone=this.rig.vertexBones[i],w=this.rig.weights[i];if(bone<0||w===0)continue;const k=bone*6,v=Math.floor(i/4)*2,x=la[i*2]+(lb[i*2]-la[i*2])*t,y=la[i*2+1]+(lb[i*2+1]-la[i*2+1])*t;positions[v]+=w*((this.posed[k]-this.basis[k])*x+(this.posed[k+2]-this.basis[k+2])*y+this.posed[k+4]-this.basis[k+4]);positions[v+1]+=w*((this.posed[k+1]-this.basis[k+1])*x+(this.posed[k+3]-this.basis[k+3])*y+this.posed[k+5]-this.basis[k+5]);}
    }
}
export function collectEvents(clip:Clip,from:number,to:number,start:boolean):AnimationEvent[]{
    if(from===to||!clip.events?.length)return [];const forward=to>from,low=Math.min(from,to),high=Math.max(from,to),hits:{at:number;index:number}[]=[];
    for(let i=0;i<clip.events.length;i++){const at=clip.events[i].time,first=clip.loop?Math.ceil((low-at)/clip.duration):0,last=clip.loop?Math.floor((high-at)/clip.duration):0;if(!Number.isFinite(first)||!Number.isFinite(last)||last-first>4096||Math.abs(first)>9e15||Math.abs(last)>9e15)throw new Error('Event catch-up exceeds 4096; use a smaller playback step');for(let k=first;k<=last;k++){const t=at+k*clip.duration;if((forward?t>from&&t<=to:t<from&&t>=to)||(start&&t===from&&k===0)){if(hits.length>=4096)throw new Error('Event catch-up exceeds 4096');hits.push({at:t,index:i});}}}
    hits.sort((a,b)=>(forward?1:-1)*(a.at-b.at||a.index-b.index));return hits.map(h=>({...clip.events![h.index],animation:clip.name}));
}
