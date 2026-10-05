// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.U2D;
namespace AgeSkeleton {
    [AddComponentMenu("AgeSkeleton/Player")]
    public sealed class AgeSkeletonPlayer : MonoBehaviour {
        public TextAsset animationData;
        public Texture2D[] textures;
        [Tooltip("Optional native Sprite Atlas. Page sprites must keep their full original rectangle. / 可选原生图集，页面精灵须保留完整矩形。")]
        public SpriteAtlas spriteAtlas;
        public Material material;
        public float pixelsPerUnit=100;
        public string initialAnimation="";
        public int sortingOrder;
        public Player Runtime {get;private set;}
        public int BatchCount {get;private set;}
        sealed class Batch {
            public GameObject node;public Mesh mesh;public MeshRenderer renderer;
            public readonly List<int> attachments=new List<int>();
            public Vector3[] vertices;public Color[] colors;
        }
        readonly List<Batch> batches=new List<Batch>();
        readonly List<int> order=new List<int>(),previous=new List<int>();
        readonly List<Sprite> ownedSprites=new List<Sprite>();
        Material[] materials;int[] pageMap;Vector2[][] mappedUvs;
        void Start(){if(Runtime==null)Load();}
        public void Load(){
            if(!animationData||(float.IsNaN(pixelsPerUnit)||float.IsInfinity(pixelsPerUnit))||pixelsPerUnit<=0)throw new ArgumentException("Assign data and positive pixelsPerUnit");
            var next=new Player(JsonUtility.FromJson<MeshClip>(animationData.text));
            if(!spriteAtlas&&(textures==null||textures.Length!=next.Data.textures.Length))throw new ArgumentException("Texture pages do not match data");
            Shader shader=material?material.shader:Shader.Find("AgeSkeleton/Unlit");if(!shader)throw new ArgumentException("Missing AgeSkeleton shader");
            Release();Runtime=next;pageMap=new int[next.Data.textures.Length];mappedUvs=new Vector2[next.Data.attachments.Length][];
            var actualPages=new List<Texture2D>();var sprites=new Sprite[pageMap.Length];
            for(int i=0;i<pageMap.Length;i++){
                Texture2D page;
                if(spriteAtlas){
                    var name=System.IO.Path.GetFileNameWithoutExtension(next.Data.textures[i]);var sprite=spriteAtlas.GetSprite(name);
                    if(!sprite)throw new ArgumentException("Native atlas lacks page sprite: "+name);ownedSprites.Add(sprite);sprites[i]=sprite;page=sprite.texture;
                    if(sprite.vertices.Length!=4)throw new ArgumentException("Native atlas pages require Full Rect sprites (no tight mesh)");
                }else page=textures[i];
                if(!page)throw new ArgumentException("Missing texture page");
                int index=actualPages.IndexOf(page);if(index<0){index=actualPages.Count;actualPages.Add(page);}pageMap[i]=index;
            }
            materials=new Material[actualPages.Count];for(int i=0;i<materials.Length;i++){materials[i]=material?new Material(material):new Material(shader);materials[i].mainTexture=actualPages[i];}
            for(int i=0;i<next.Data.attachments.Length;i++){
                var a=next.Data.attachments[i];var uv=new Vector2[a.count];
                for(int v=0;v<a.count;v++){var point=new Vector2(a.uv[v*2],1-a.uv[v*2+1]);uv[v]=sprites[a.texture]?MapSprite(sprites[a.texture],point):point;}
                mappedUvs[i]=uv;
            }
            if(!string.IsNullOrEmpty(initialAnimation))next.Play(initialAnimation);Apply();
        }
        // Solve the sprite's affine UV mapping, including native atlas rotation.
        static Vector2 MapSprite(Sprite s,Vector2 point){
            var v=s.vertices;var uv=s.uv;var size=s.rect.size;var pivot=s.pivot;
            var p0=(v[0]*s.pixelsPerUnit+pivot)/size;var p1=(v[1]*s.pixelsPerUnit+pivot)/size;var p2=(v[2]*s.pixelsPerUnit+pivot)/size;
            var a=p1-p0;var b=p2-p0;float det=a.x*b.y-a.y*b.x;if(Mathf.Abs(det)<1e-8f)throw new ArgumentException("Degenerate sprite mapping");
            var q=point-p0;float x=(q.x*b.y-q.y*b.x)/det,y=(a.x*q.y-a.y*q.x)/det;return uv[0]+(uv[1]-uv[0])*x+(uv[2]-uv[0])*y;
        }
        public event Action<AgeSkeleton.AnimationEvent> AnimationEvent;
        void Update(){Advance(Time.deltaTime);}
        public void Advance(float delta){if(Runtime==null)return;Runtime.Update(delta);Apply();if(Runtime.Events.Count==0)return;var events=Runtime.Events.ToArray();foreach(var e in events){if(!this)break;AnimationEvent?.Invoke(e);}}
        // Coordinates are exported skeleton-local pixels, Y down.
        public void SetIKTarget(string bone,float x,float y,int chainLength=2,float mix=1,int iterations=24,float tolerance=.1f){Runtime.SetIKTarget(bone,x,y,chainLength,mix,iterations,tolerance);Apply();}
        public void ClearIKTarget(string bone){Runtime.ClearIKTarget(bone);Apply();}
        public Vector2 GetBoneTip(string bone){Runtime.GetBoneTip(bone,out float x,out float y);return new Vector2(x,y);}
        public void CrossFade(string clip,float duration=.2f,bool restart=true){Runtime.CrossFade(clip,duration,restart);Apply();}
        public bool IsBlending=>Runtime!=null&&Runtime.IsBlending;
        public float BlendProgress=>Runtime?.BlendProgress??1;
        public void Play(string clip){Runtime.Play(clip);Apply();}public void Pause(){Runtime.Playing=false;}public void Resume(){Runtime.Playing=true;}
        public void Stop(){Runtime.Stop();Apply();}public void Seek(float t){Runtime.Seek(t);Apply();}
        public void SetSkin(string s){Runtime.SetSkin(s);Apply();}public void SetWardrobe(string g,string s){Runtime.SetWardrobe(g,s);Apply();}
        public void SetSlotVisible(string s,bool v){Runtime.SetSlotVisible(s,v);Apply();}public void SetAttachment(string s,string k){Runtime.SetAttachment(s,k);Apply();}
        void Apply(){
            order.Clear();for(int i=0;i<Runtime.Visible.Length;i++)if(Runtime.Visible[i])order.Add(i);
            order.Sort((a,b)=>{int c=Runtime.Orders[a].CompareTo(Runtime.Orders[b]);return c!=0?c:a.CompareTo(b);});
            bool changed=order.Count!=previous.Count;for(int i=0;!changed&&i<order.Count;i++)changed=order[i]!=previous[i];
            if(changed){
                previous.Clear();previous.AddRange(order);BatchCount=0;int page=-1;
                foreach(var batch in batches){batch.attachments.Clear();batch.renderer.enabled=false;}
                foreach(int i in order){int next=pageMap[Runtime.Data.attachments[i].texture];if(next!=page){
                    page=next;if(BatchCount==batches.Count){var node=new GameObject("Batch "+BatchCount);node.transform.SetParent(transform,false);var mesh=new Mesh();mesh.MarkDynamic();node.AddComponent<MeshFilter>().sharedMesh=mesh;var renderer=node.AddComponent<MeshRenderer>();renderer.shadowCastingMode=UnityEngine.Rendering.ShadowCastingMode.Off;renderer.receiveShadows=false;batches.Add(new Batch{node=node,mesh=mesh,renderer=renderer});}
                    batches[BatchCount].renderer.sharedMaterial=materials[page];batches[BatchCount].renderer.enabled=true;BatchCount++;
                }batches[BatchCount-1].attachments.Add(i);}
                for(int k=0;k<BatchCount;k++){
                    var b=batches[k];int count=0;foreach(int i in b.attachments)count+=Runtime.Data.attachments[i].count;
                    b.vertices=new Vector3[count];b.colors=new Color[count];var uvs=new Vector2[count];var indices=new List<int>();int offset=0;
                    foreach(int i in b.attachments){var a=Runtime.Data.attachments[i];Array.Copy(mappedUvs[i],0,uvs,offset,a.count);foreach(int t in a.triangles)indices.Add(offset+t);offset+=a.count;}
                    b.mesh.Clear();b.mesh.indexFormat=count>65535?UnityEngine.Rendering.IndexFormat.UInt32:UnityEngine.Rendering.IndexFormat.UInt16;b.mesh.vertices=b.vertices;b.mesh.uv=uvs;b.mesh.SetTriangles(indices,0,false);
                }
            }
            for(int k=0;k<BatchCount;k++){
                var b=batches[k];b.renderer.sortingOrder=sortingOrder+k;int offset=0;
                foreach(int i in b.attachments){var a=Runtime.Data.attachments[i];var tint=new Color(Runtime.Colors[i*4],Runtime.Colors[i*4+1],Runtime.Colors[i*4+2],Runtime.Colors[i*4+3]);
                    for(int v=0;v<a.count;v++){int p=(a.offset+v)*2;b.vertices[offset]=new Vector3(Runtime.Positions[p]/pixelsPerUnit,-Runtime.Positions[p+1]/pixelsPerUnit,0);b.colors[offset++]=tint;}}
                b.mesh.vertices=b.vertices;b.mesh.colors=b.colors;b.mesh.RecalculateBounds();
            }
        }
        void OnDestroy(){Release();}static void DisposeOwned(UnityEngine.Object o){if(Application.isPlaying)Destroy(o);else DestroyImmediate(o);}
        void Release(){foreach(var b in batches){b.node.SetActive(false);DisposeOwned(b.node);DisposeOwned(b.mesh);}batches.Clear();if(materials!=null)foreach(var m in materials)if(m)DisposeOwned(m);foreach(var s in ownedSprites)if(s)DisposeOwned(s);ownedSprites.Clear();order.Clear();previous.Clear();Runtime=null;materials=null;BatchCount=0;}
    }
}
