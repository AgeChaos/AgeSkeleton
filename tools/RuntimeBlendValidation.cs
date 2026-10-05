// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
using System;
using UnityEngine;
using UnityEditor;
using AgeSkeleton;
public static class RuntimeBlendValidation {
    static void Check(bool ok,string name){if(!ok)throw new Exception(name);}
    static void Near(float v,float e){Check(Math.Abs(v-e)<.0002f,v+" != "+e);}
    public static void Validate(){
        var json=AssetDatabase.LoadAssetAtPath<TextAsset>("Assets/BlendFixture/skeleton.ageskel.json");Check(json,"blend fixture");var data=JsonUtility.FromJson<MeshClip>(json.text);var p=new Player(data);
        p.Play("Source");p.Update(.25f);p.CrossFade("Destination",1);Near(p.Positions[0],2.5f);Check(p.IsBlending&&p.BlendProgress==0,"active");
        p.Update(.25f);Near(p.Positions[0],5.25f);Near(p.Colors[1],.75f);Near(p.Colors[3],.875f);Check(p.Visible[0]&&p.Orders[0]==0,"source keys");Check(p.Events.Count==2&&p.Events[0].animation=="Destination","target events");
        float before=p.Positions[0],progress=p.BlendProgress;p.Playing=false;p.Update(1);Near(p.Positions[0],before);Near(p.BlendProgress,progress);Check(p.Events.Count==0,"pause events");p.Playing=true;p.Speed=0;p.Update(1);Near(p.BlendProgress,progress);p.Speed=1;
        foreach(var invalid in new[]{-1f,float.NaN}){bool rejected=false;try{p.CrossFade("Third",invalid);}catch(ArgumentException){rejected=true;}Check(rejected,"invalid duration");}bool missing=false;try{p.CrossFade("Missing",1);}catch(ArgumentException){missing=true;}Check(missing,"unknown clip");Near(p.Positions[0],before);
        p.Update(.25f);Near(p.Positions[0],7.75f);Check(!p.Visible[0]&&p.Orders[0]==5,"target keys");p.SetAttachment("Slot","visible");Check(p.Visible[0],"override");p.SetSlotVisible("Slot",false);Check(!p.Visible[0],"hidden");p.SetSlotVisible("Slot",true);p.SetAttachment("Slot",null);
        p.CrossFade("Third",1);Near(p.Positions[0],7.75f);p.Update(.5f);Near(p.Positions[0],14.875f);p.Update(.5f);Near(p.Positions[0],22);Check(!p.IsBlending&&p.BlendProgress==1,"complete");
        p.CrossFade("Source",1);p.Seek(.5f);Check(!p.IsBlending,"seek");Near(p.Positions[0],3);p.CrossFade("Third",1);p.Stop();Check(!p.IsBlending,"stop");Near(p.Positions[0],data.rest.positions[0]);p.CrossFade("Destination",0);Near(p.Positions[0],12);Check(!p.IsBlending,"instant");
        p.Play("Source");p.Seek(.75f);p.CrossFade("Source",1,false);Near(p.Time,.75f);p.Speed=-1;p.Update(.25f);Near(p.Time,.5f);Near(p.BlendProgress,.25f);Near(p.Positions[0],3);
        p.Speed=1;p.Play("Source");p.CrossFade("Once",2);p.Update(1);Check(p.Playing&&p.IsBlending,"hold endpoint");p.Update(1);Check(!p.Playing&&!p.IsBlending,"finish endpoint");
        p.Play("Source");p.CrossFade("Destination",1);p.Update(.25f);before=p.Positions[0];progress=p.BlendProgress;bool overflow=false;try{p.Update(2000);}catch(ArgumentException){overflow=true;}Check(overflow&&p.Events.Count==0,"overflow");Near(p.Positions[0],before);Near(p.BlendProgress,progress);
        p.SetIKTarget("Tip",4.25f,1,2,1,64,.0001f);p.GetBoneTip("Tip",out float x,out float y);Near(x,4.25f);Near(y,1);p.ClearIKTarget("Tip");Near(p.Positions[0],before);
        data.version=1;var legacy=new Player(data);legacy.Play("Source");legacy.CrossFade("Destination",1);legacy.Update(.5f);Near(legacy.Positions[0],7.5f);
        var go=new GameObject("Blend adapter test");var component=go.AddComponent<AgeSkeletonPlayer>();var tex=new Texture2D(2,2);component.animationData=json;component.textures=new[]{tex};component.Load();component.Play("Source");component.CrossFade("Destination",1);component.Advance(.25f);Check(component.IsBlending,"adapter");Near(component.BlendProgress,.25f);Near(component.Runtime.Positions[0],4.875f);component.Pause();component.Advance(1);Near(component.BlendProgress,.25f);
        UnityEngine.Object.DestroyImmediate(go);UnityEngine.Object.DestroyImmediate(tex);
        Debug.Log("AGESKELETON_BLEND_UNITY_PASS advancing_source interrupt pause reverse slots colors events IK legacy adapter version="+Application.unityVersion);
    }
}
