// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
using System;
using UnityEngine;
using UnityEditor;
using AgeSkeleton;
public static class RuntimeMotionValidation {
    static void Check(bool ok,string name){if(!ok)throw new Exception(name);}
    static void Events(Player p,string expected){string got="";foreach(var e in p.Events)got+=e.name+",";Check(got==expected,"events "+got+" expected "+expected);}
    public static void Validate(){
        var json=AssetDatabase.LoadAssetAtPath<TextAsset>("Assets/MotionFixture/skeleton.ageskel.json");Check(json,"motion fixture import");
        var data=JsonUtility.FromJson<MeshClip>(json.text);var p=new Player(data);var rest=(float[])p.Positions.Clone();
        p.SetIKTarget("Tip",1,1,2,1,64,.0001f);p.GetBoneTip("Tip",out float x,out float y);Check(Math.Sqrt((x-1)*(x-1)+(y-1)*(y-1))<.001,"IK tip");Check(Math.Abs(p.Positions[0]-1)<.001&&Math.Abs(p.Positions[1]-1)<.001,"weighted IK");
        p.ClearIKTarget("Tip");Check(p.Positions[0]==rest[0]&&p.Positions[1]==rest[1],"clear baseline");p.SetIKTarget("Tip",1,1,2,0);Check(p.Positions[0]==rest[0],"zero mix");p.SetIKTarget("Tip",1,1,2,.5f);Check(p.Positions[0]>1.1f&&p.Positions[1]>.1f&&p.Positions[1]<.99f,"partial mix");
        bool rejected=false;try{p.SetIKTarget("Tip",float.NaN,1);}catch(ArgumentException){rejected=true;}Check(rejected,"NaN");rejected=false;try{p.SetIKTarget("Tip",1,1,3);}catch(ArgumentException){rejected=true;}Check(rejected,"chain");p.ClearIKTarget("Tip");
        p.Play("Loop");p.Update(0);Events(p,"");p.Update(.25f);Events(p,"start,quarter,");Check(p.Events[0].intValue==int.MinValue&&p.Events[0].stringValue=="测试 event","payload");
        p.Update(.75f);Events(p,"late,start,end,");p.Update(0);Events(p,"");p.Seek(.5f);p.Speed=-1;p.Update(.75f);Events(p,"quarter,end,start,late,");
        p.Seek(0);p.Speed=1;p.Update(2.25f);Events(p,"quarter,late,start,end,quarter,late,start,end,quarter,");
        p.Playing=false;p.Update(1);Events(p,"");p.Playing=true;p.Speed=0;p.Update(1);Events(p,"");p.Speed=1;p.Seek(0);rejected=false;try{p.Update(2000);}catch(ArgumentException){rejected=true;}Check(rejected&&p.Time==0&&p.Events.Count==0,"catch-up atomic");
        p.Play("Once");p.Update(2);Events(p,"start,quarter,late,end,");Check(!p.Playing&&p.Time==1,"endpoint");p.Stop();Events(p,"");
        data.rig.parents[0]=1;rejected=false;try{new Player(data);}catch(ArgumentException){rejected=true;}Check(rejected,"cycle");data.rig.parents[0]=-1;data.version=1;var legacy=new Player(data);legacy.Play("Loop");legacy.Update(.25f);Check(legacy.Events.Count==0,"legacy");
        var go=new GameObject("Motion adapter test");var component=go.AddComponent<AgeSkeletonPlayer>();component.animationData=json;var texture=new Texture2D(2,2);component.textures=new[]{texture};component.Load();int callbacks=0;component.AnimationEvent+=e=>callbacks++;component.Play("Loop");component.Advance(.25f);Check(callbacks==2,"component callbacks");component.Pause();component.Advance(1);Check(callbacks==2,"pause callbacks");component.SetIKTarget("Tip",1,1,2,1,64,.0001f);Check(Vector2.Distance(component.GetBoneTip("Tip"),Vector2.one)<.001f,"component IK");
        UnityEngine.Object.DestroyImmediate(go);UnityEngine.Object.DestroyImmediate(texture);
        Debug.Log("AGESKELETON_MOTION_UNITY_PASS core_ik events adapter_callbacks version="+Application.unityVersion);
    }
}
