// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
import { Player } from './MeshClip';
export interface Batch {texture:number;attachments:number[];positions:Float32Array;colors:Float32Array;uvs:Float32Array;indices:Uint32Array;}
export class Batcher {
    batches:Batch[]=[];revision=0;
    private previous:number[]=[];private order:number[]=[];
    update(p:Player,pageMap?:number[],uvs?:Float32Array[]):boolean {
        this.order.length=0;for(let i=0;i<p.visible.length;i++)if(p.visible[i])this.order.push(i);
        this.order.sort((a,b)=>p.orders[a]-p.orders[b]||a-b);
        const changed=this.order.length!==this.previous.length||this.order.some((v,i)=>v!==this.previous[i]);
        if(changed){
            this.previous=this.order.slice();this.revision++;this.batches=[];
            const groups:{texture:number;attachments:number[]}[]=[];
            for(const i of this.order){const texture=pageMap?pageMap[p.data.attachments[i].texture]:p.data.attachments[i].texture;
                if(!groups.length||groups[groups.length-1].texture!==texture)groups.push({texture,attachments:[]});groups[groups.length-1].attachments.push(i);}
            for(const group of groups){let count=0,triangles=0;for(const i of group.attachments){count+=p.data.attachments[i].count;triangles+=p.data.attachments[i].triangles.length;}
                const b:Batch={...group,positions:new Float32Array(count*2),colors:new Float32Array(count*4),uvs:new Float32Array(count*2),indices:new Uint32Array(triangles)};let offset=0,k=0;
                for(const i of group.attachments){const a=p.data.attachments[i];b.uvs.set(uvs?uvs[i]:a.uv,offset*2);for(const t of a.triangles)b.indices[k++]=offset+t;offset+=a.count;}this.batches.push(b);}
        }
        for(const b of this.batches){let offset=0;for(const i of b.attachments){const a=p.data.attachments[i];for(let v=0;v<a.count;v++){
            b.positions[(offset+v)*2]=p.positions[(a.offset+v)*2];b.positions[(offset+v)*2+1]=p.positions[(a.offset+v)*2+1];for(let c=0;c<4;c++)b.colors[(offset+v)*4+c]=p.colors[i*4+c];}offset+=a.count;}}
        return changed;
    }
}
