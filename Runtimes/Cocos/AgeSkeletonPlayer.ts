// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
import { _decorator, Component, JsonAsset, Texture2D, Material, Node, Mesh, MeshRenderer, utils, Vec3, gfx, SpriteAtlas } from 'cc';
import { MeshClip, Player } from './MeshClip';
import { Batcher } from './Batcher';
const { ccclass, property } = _decorator;

@ccclass('AgeSkeletonPlayer')
export class AgeSkeletonPlayer extends Component {
    @property(JsonAsset) animationData:JsonAsset|null=null;
    @property([Texture2D]) textures:Texture2D[]=[];
    @property(SpriteAtlas) spriteAtlas:SpriteAtlas|null=null;
    @property(Material) material:Material|null=null;
    @property pixelsPerUnit=100;
    @property initialAnimation='';
    runtime:Player|null=null;
    private parts:{node:Node;mesh:Mesh;renderer:MeshRenderer;positions:Float32Array;colors:Float32Array;uvs:Float32Array;indices16?:Uint16Array;indices32?:Uint32Array;minPos:Vec3;maxPos:Vec3}[]=[];
    private materials:Material[]=[];
    private batcher=new Batcher();private pageMap:number[]=[];private mappedUvs:Float32Array[]=[];
    get batchCount():number{return this.batcher.batches.length;}
    start():void {if(!this.runtime)this.load();}
    load():void {
        if(!this.animationData||!Number.isFinite(this.pixelsPerUnit)||this.pixelsPerUnit<=0)throw new Error('Assign data and positive pixelsPerUnit');
        const next=new Player(this.animationData.json as MeshClip);
        if(!this.spriteAtlas&&(this.textures.length!==next.data.textures.length||this.textures.some(t=>!t)))throw new Error('Texture page order must match JSON');
        this.release();this.runtime=next;this.batcher=new Batcher();this.pageMap=[];this.mappedUvs=[];
        const frames=next.data.textures.map(name=>this.spriteAtlas?.getSpriteFrame(name.replace(/\.[^.]+$/,''))??null);
        const actual:Texture2D[]=[];
        for(let i=0;i<frames.length;i++){
            const f=frames[i];if(this.spriteAtlas&&!f)throw new Error('Native atlas lacks page sprite: '+next.data.textures[i]);
            if(f&&(f.rotated||f.rect.width!==f.originalSize.width||f.rect.height!==f.originalSize.height||f.offset.x!==0||f.offset.y!==0))throw new Error('Native atlas page sprites require rotation and trimming disabled');
            const texture=f?f.texture:this.textures[i];if(!(texture instanceof Texture2D))throw new Error('Expected Texture2D');
            let index=actual.indexOf(texture);if(index<0){index=actual.length;actual.push(texture);}this.pageMap.push(index);
        }
        for(const a of next.data.attachments){const uv=new Float32Array(a.uv),f=frames[a.texture];if(f){for(let v=0;v<a.count;v++){
            const u=f.flipUVX?1-uv[v*2]:uv[v*2],w=f.flipUVY?1-uv[v*2+1]:uv[v*2+1];uv[v*2]=(f.rect.x+u*f.rect.width)/f.texture.width;uv[v*2+1]=(f.rect.y+w*f.rect.height)/f.texture.height;}}this.mappedUvs.push(uv);}
        this.materials=actual.map(texture=>{const m=new Material();if(this.material)m.copy(this.material);else m.initialize({effectName:'builtin-unlit',technique:1,defines:{USE_TEXTURE:true,USE_VERTEX_COLOR:true}});m.overridePipelineStates({rasterizerState:{cullMode:gfx.CullMode.NONE},depthStencilState:{depthWrite:false}});m.setProperty('mainTexture',texture);return m;});
        if(this.initialAnimation)next.play(this.initialAnimation);this.apply();
    }
    update(dt:number):void {if(this.runtime){this.runtime.update(dt);this.apply();const events=this.runtime.events.slice();for(const event of events){if(!this.isValid)break;this.node.emit("ageskeleton-event",event);}}}
    crossFade(name:string,duration=.2,restart=true):void{this.runtime!.crossFade(name,duration,restart);this.apply();}
    get isBlending():boolean{return this.runtime?.isBlending??false;}
    get blendProgress():number{return this.runtime?.blendProgress??1;}
    play(name:string):void {this.runtime!.play(name);this.apply();}
    pause():void {this.runtime!.playing=false;}
    resume():void {this.runtime!.playing=true;}
    stop():void {this.runtime!.stop();this.apply();}
    seek(time:number):void {this.runtime!.seek(time);this.apply();}
    setSkin(name:string):void {this.runtime!.setSkin(name);this.apply();}
    setWardrobe(group:string,skin:string):void {this.runtime!.setWardrobe(group,skin);this.apply();}
    setSlotVisible(slot:string,visible:boolean):void {this.runtime!.setSlotVisible(slot,visible);this.apply();}
    setAttachment(slot:string,key:string|null):void {this.runtime!.setAttachment(slot,key);this.apply();}
    // Coordinates are exported skeleton-local pixels, Y down.
    setIKTarget(bone:string,x:number,y:number,chainLength=2,mix=1,iterations=24,tolerance=.1):void{this.runtime!.setIKTarget(bone,x,y,chainLength,mix,iterations,tolerance);this.apply();}
    clearIKTarget(bone:string):void{this.runtime!.clearIKTarget(bone);this.apply();}
    getBoneTip(bone:string):[number,number]{return this.runtime!.getBoneTip(bone);}
    private apply():void {
        const r=this.runtime!;const changed=this.batcher.update(r,this.pageMap,this.mappedUvs);
        if(changed){
            for(const p of this.parts){p.node.active=false;p.node.destroy();p.mesh.destroy();}this.parts=[];
            for(const b of this.batcher.batches){
                const count=b.positions.length/2,node=new Node('AgeSkeleton batch');node.layer=this.node.layer;this.node.addChild(node);
                const geometry={positions:new Float32Array(count*3),colors:b.colors,uvs:b.uvs,indices16:count<=65535?new Uint16Array(b.indices):undefined,indices32:count>65535?b.indices:undefined,minPos:new Vec3(),maxPos:new Vec3()};
                const mesh=utils.MeshUtils.createDynamicMesh(0,geometry,undefined,{maxSubMeshes:1,maxSubMeshVertices:count,maxSubMeshIndices:b.indices.length});
                const renderer=node.addComponent(MeshRenderer);renderer.mesh=mesh;renderer.setMaterial(this.materials[b.texture],0);this.parts.push({node,mesh,renderer,...geometry});
            }
        }
        for(let i=0;i<this.parts.length;i++){
            const p=this.parts[i],b=this.batcher.batches[i];p.node.setPosition(0,0,i*0.0001);p.minPos.set(Infinity,Infinity,0);p.maxPos.set(-Infinity,-Infinity,0);
            for(let v=0;v<b.positions.length/2;v++){
                const x=b.positions[v*2]/this.pixelsPerUnit,y=-b.positions[v*2+1]/this.pixelsPerUnit;p.positions[v*3]=x;p.positions[v*3+1]=y;
                p.minPos.x=Math.min(p.minPos.x,x);p.minPos.y=Math.min(p.minPos.y,y);p.maxPos.x=Math.max(p.maxPos.x,x);p.maxPos.y=Math.max(p.maxPos.y,y);
            }
            p.mesh.updateSubMesh(0,p);p.renderer.onGeometryChanged();
        }
    }
    onDestroy():void {this.release();}
    private release():void {for(const p of this.parts){p.node.active=false;p.node.destroy();p.mesh.destroy();}for(const m of this.materials)m.destroy();this.parts=[];this.materials=[];this.runtime=null;}
}
