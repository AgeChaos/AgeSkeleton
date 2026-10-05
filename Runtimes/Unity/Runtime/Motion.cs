// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
using System;
using System.Collections.Generic;
namespace AgeSkeleton {
[Serializable] public sealed class RuntimeRig {public string[] names;public int[] parents,vertexBones;public float[] lengths,weights;}
[Serializable] public sealed class AnimationEvent {public string name,animation,stringValue;public float time,floatValue;public int intValue;}
internal sealed class MotionPose {
    struct Target {public bool enabled;public float x,y,mix,tolerance;public int chain,iterations;}
    readonly RuntimeRig rig;readonly Target[] targets;readonly float[] basis,posed,backup;
    public MotionPose(RuntimeRig r){rig=r;int n=r?.names.Length??0;targets=new Target[n];basis=new float[n*6];posed=new float[n*6];backup=new float[n*6];}
    static bool Finite(float v)=>!float.IsNaN(v)&&!float.IsInfinity(v);
    public static void Validate(RuntimeRig r,int vertices){
        if(r==null||r.names==null||r.names.Length<1||r.names.Length>256||r.parents==null||r.parents.Length!=r.names.Length||r.lengths==null||r.lengths.Length!=r.names.Length||r.vertexBones==null||r.weights==null||r.vertexBones.Length!=vertices*4||r.weights.Length!=vertices*4)throw new ArgumentException("Invalid runtime rig");
        var names=new HashSet<string>();for(int i=0;i<r.names.Length;i++){if(string.IsNullOrEmpty(r.names[i])||!names.Add(r.names[i])||!Finite(r.lengths[i])||r.lengths[i]<0)throw new ArgumentException("Invalid bone");int p=i,count=0;while(p>=0){if(p>=r.names.Length||++count>r.names.Length)throw new ArgumentException("Cyclic bone hierarchy");p=r.parents[p];if(p < -1)throw new ArgumentException("Invalid parent");}}
        for(int i=0;i<r.weights.Length;i++)if(!Finite(r.weights[i])||r.weights[i]<0||r.weights[i]>1||r.vertexBones[i]<-1||r.vertexBones[i]>=r.names.Length||(r.vertexBones[i]<0&&r.weights[i]!=0))throw new ArgumentException("Invalid influence");
    }
    int Bone(string name){int i=rig==null?-1:Array.IndexOf(rig.names,name);if(i<0)throw new ArgumentException("Unknown runtime bone; re-export with runtime format v2");return i;}
    public void SetTarget(string name,float x,float y,int chain,float mix,int iterations,float tolerance){int i=Bone(name);if(!Finite(x)||!Finite(y)||!Finite(mix)||mix<0||mix>1||chain<1||chain>16||iterations<1||iterations>128||!Finite(tolerance)||tolerance<0)throw new ArgumentException("Invalid IK target");int p=i;for(int j=0;j<chain;j++){if(p<0)throw new ArgumentException("IK chain exceeds hierarchy");p=rig.parents[p];}targets[i]=new Target{enabled=true,x=x,y=y,chain=chain,mix=mix,iterations=iterations,tolerance=tolerance};}
    public void ClearTarget(string name){int i=Bone(name);targets[i].enabled=false;}
    public void Tip(string name,out float x,out float y){Tip(Bone(name),out x,out y);}
    void Tip(int bone,out float x,out float y){int k=bone*6;x=posed[k+4]+posed[k]*rig.lengths[bone];y=posed[k+5]+posed[k+1]*rig.lengths[bone];}
    bool Child(int node,int ancestor){for(int i=node;i>=0;i=rig.parents[i])if(i==ancestor)return true;return false;}
    void Rotate(int bone,float angle){float c=(float)Math.Cos(angle),s=(float)Math.Sin(angle),x=posed[bone*6+4],y=posed[bone*6+5];for(int n=0;n<targets.Length;n++)if(Child(n,bone)){int k=n*6;for(int axis=0;axis<2;axis++){float a=posed[k+axis*2],b=posed[k+axis*2+1];posed[k+axis*2]=c*a-s*b;posed[k+axis*2+1]=s*a+c*b;}float dx=posed[k+4]-x,dy=posed[k+5]-y;posed[k+4]=x+c*dx-s*dy;posed[k+5]=y+s*dx+c*dy;}}
    readonly int[] chainBuffer=new int[16];readonly float[] angles=new float[16];
    public void Apply(Frame a,Frame b,float t,float[] positions){if(rig==null)return;for(int i=0;i<basis.Length;i++)basis[i]=posed[i]=a.matrices[i]+(b.matrices[i]-a.matrices[i])*t;bool changed=false;
        for(int tip=0;tip<targets.Length;tip++){var target=targets[tip];if(!target.enabled||target.mix==0)continue;changed=true;int p=tip;Array.Clear(angles,0,angles.Length);for(int j=0;j<target.chain;j++){chainBuffer[j]=p;p=rig.parents[p];}Array.Copy(posed,backup,posed.Length);
            for(int iteration=0;iteration<target.iterations;iteration++){Tip(tip,out float ex,out float ey);if(Math.Sqrt((ex-target.x)*(ex-target.x)+(ey-target.y)*(ey-target.y))<=target.tolerance)break;for(int j=0;j<target.chain;j++){int k=chainBuffer[j]*6;Tip(tip,out ex,out ey);float ax=ex-posed[k+4],ay=ey-posed[k+5],bx=target.x-posed[k+4],by=target.y-posed[k+5];if(ax*ax+ay*ay<1e-12f||bx*bx+by*by<1e-12f)continue;float angle=(float)Math.Atan2(ax*by-ay*bx,ax*bx+ay*by);angles[j]+=angle;Rotate(chainBuffer[j],angle);}}
            if(target.mix<1){Array.Copy(backup,posed,posed.Length);for(int j=target.chain-1;j>=0;j--)Rotate(chainBuffer[j],(float)Math.Atan2(Math.Sin(angles[j]),Math.Cos(angles[j]))*target.mix);}
        }
        if(!changed)return;for(int i=0;i<rig.vertexBones.Length;i++){int bone=rig.vertexBones[i];float w=rig.weights[i];if(bone<0||w==0)continue;int k=bone*6,v=(i/4)*2;float x=a.influencePositions[i*2]+(b.influencePositions[i*2]-a.influencePositions[i*2])*t,y=a.influencePositions[i*2+1]+(b.influencePositions[i*2+1]-a.influencePositions[i*2+1])*t;positions[v]+=w*((posed[k]-basis[k])*x+(posed[k+2]-basis[k+2])*y+posed[k+4]-basis[k+4]);positions[v+1]+=w*((posed[k+1]-basis[k+1])*x+(posed[k+3]-basis[k+3])*y+posed[k+5]-basis[k+5]);}
    }
}
internal static class EventTraversal {
    public static void Collect(Clip clip,double from,double to,bool start,List<AnimationEvent> output){output.Clear();if(from==to||clip.events==null)return;bool forward=to>from;double low=Math.Min(from,to),high=Math.Max(from,to);var hits=new List<Tuple<double,int>>();
        for(int i=0;i<clip.events.Length;i++){double at=clip.events[i].time,first=clip.loop?Math.Ceiling((low-at)/clip.duration):0,last=clip.loop?Math.Floor((high-at)/clip.duration):0;if(last-first>4096||Math.Abs(first)>9e15||Math.Abs(last)>9e15)throw new ArgumentException("Event catch-up exceeds 4096; use a smaller playback step");for(double k=first;k<=last;k++){double t=at+k*clip.duration;if((forward?t>from&&t<=to:t<from&&t>=to)||(start&&t==from&&k==0)){if(hits.Count>=4096)throw new ArgumentException("Event catch-up exceeds 4096");hits.Add(Tuple.Create(t,i));}}}
        hits.Sort((a,b)=>{int c=a.Item1.CompareTo(b.Item1);if(c==0)c=a.Item2.CompareTo(b.Item2);return forward?c:-c;});foreach(var hit in hits){var e=clip.events[hit.Item2];output.Add(new AnimationEvent{name=e.name,animation=clip.name,time=e.time,intValue=e.intValue,floatValue=e.floatValue,stringValue=e.stringValue});}
    }
}
}
