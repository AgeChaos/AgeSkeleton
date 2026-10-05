// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
// Copied to an isolated Unity project's Assets/Editor by validate_runtimes.py.
using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using AgeSkeleton;
public static class RuntimeUnityValidation {
    static void Check(bool ok,string message){if(!ok)throw new Exception(message);}
    public static void Run(){
        try{
            RuntimeMotionValidation.Validate();
            var json=AssetDatabase.LoadAssetAtPath<TextAsset>("Assets/Wayfarer/skeleton.ageskel.json");Check(json,"fixture import");
            var d=JsonUtility.FromJson<MeshClip>(json.text);var p=new Player(d);p.Play("Walk");p.Seek(.35f);float x=p.Positions[0];
            p.SetWardrobe("Tops","Tops/Steel Armor");Check(p.Playing&&p.Positions[0]==x,"outfit preserves animation");
            bool rejected=false;try{p.SetWardrobe("Tops","Weapons/Short Sword");}catch(ArgumentException){rejected=true;}Check(rejected,"invalid wardrobe");
            p.SetSlotVisible("Torso",false);for(int i=0;i<d.attachments.Length;i++)if(d.attachments[i].slot==1)Check(!p.Visible[i],"hide torso");
            p.SetSlotVisible("Torso",true);p.SetAttachment("Torso","");for(int i=0;i<d.attachments.Length;i++)if(d.attachments[i].slot==1)Check(!p.Visible[i],"empty attachment");p.SetAttachment("Torso",null);
            p.Playing=false;float at=p.Time;p.Update(1);Check(p.Time==at,"paused");p.Playing=true;p.Update(10);Check(p.Time<1.2f,"loop");
            p.Stop();Check(p.Positions[0]==d.rest.positions[0],"stop rest");
            // Remove only generated atlas fixtures in this isolated validation project.
            foreach(var guid in AssetDatabase.FindAssets("t:SpriteAtlas",new[]{"Assets/Wayfarer"}))AssetDatabase.DeleteAsset(AssetDatabase.GUIDToAssetPath(guid));
            var nativeAtlas=AgeSkeleton.Editor.AgeSkeletonImporter.CreateAtlas(json);
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
            var go=AgeSkeleton.Editor.AgeSkeletonImporter.Create(json);var component=go.GetComponent<AgeSkeletonPlayer>();component.Load();component.Play("Walk");component.Seek(.35f);component.Pause();
            int looseBatches=component.BatchCount;Check(looseBatches>0 && looseBatches<d.attachments.Length,"atlas batches");
            component.SetWardrobe("Tops","Tops/Steel Armor");component.SetSlotVisible("Torso",false);component.SetSlotVisible("Torso",true);
            Debug.Log("ATLAS_BATCHES "+component.BatchCount);
            var camera=new GameObject("Camera").AddComponent<Camera>();camera.orthographic=true;camera.orthographicSize=3.2f;camera.transform.position=new Vector3(0,0,-10);camera.clearFlags=CameraClearFlags.SolidColor;camera.backgroundColor=new Color(.12f,.13f,.15f);
            var rt=new RenderTexture(640,640,24);camera.targetTexture=rt;camera.Render();RenderTexture.active=rt;var image=new Texture2D(640,640,TextureFormat.RGBA32,false);image.ReadPixels(new Rect(0,0,640,640),0,0);image.Apply();
            int distinct=0;Color background=image.GetPixel(0,0);foreach(Color pixel in image.GetPixels())if(Math.Abs(pixel.r-background.r)+Math.Abs(pixel.g-background.g)+Math.Abs(pixel.b-background.b)>.2f)distinct++;
            Check(distinct>500,"mesh did not render");File.WriteAllBytes("runtime-preview.png",image.EncodeToPNG());var expected=image.GetPixels32();
            // The same page data can be repacked by Unity's native Sprite Atlas.
            component.spriteAtlas=nativeAtlas;component.Load();component.Play("Walk");component.Seek(.35f);component.Pause();component.SetWardrobe("Tops","Tops/Steel Armor");
            foreach(string file in d.textures){var sprite=nativeAtlas.GetSprite(Path.GetFileNameWithoutExtension(file));Debug.Log("NATIVE_PAGE "+file+" "+sprite.texture.name+" "+sprite.texture.width+"x"+sprite.texture.height+" id="+sprite.texture.GetInstanceID()+" uv="+sprite.uv[0]);UnityEngine.Object.DestroyImmediate(sprite);}
            Check(component.BatchCount==1,"native SpriteAtlas should merge the two exported pages");camera.Render();image.ReadPixels(new Rect(0,0,640,640),0,0);image.Apply();int nativePixels=0;foreach(Color pixel in image.GetPixels())if(Math.Abs(pixel.r-background.r)+Math.Abs(pixel.g-background.g)+Math.Abs(pixel.b-background.b)>.2f)nativePixels++;Check(nativePixels>500,"native SpriteAtlas rendering");File.WriteAllBytes("native-atlas-preview.png",image.EncodeToPNG());
            var actual=image.GetPixels32();double pixelError=0;for(int i=0;i<actual.Length;i++)pixelError+=Math.Abs(actual[i].r-expected[i].r)+Math.Abs(actual[i].g-expected[i].g)+Math.Abs(actual[i].b-expected[i].b);
            pixelError/=actual.Length*3;Check(pixelError<1.5,"native atlas differs from direct pages: "+pixelError);Debug.Log("NATIVE_ATLAS_PIXEL_ERROR "+pixelError);Debug.Log("NATIVE_ATLAS_BATCHES "+component.BatchCount+" pixels="+nativePixels);
            camera.targetTexture=null;RenderTexture.active=null;UnityEngine.Object.DestroyImmediate(rt);UnityEngine.Object.DestroyImmediate(image);
            UnityEngine.Object.DestroyImmediate(go);var saved=AgeSkeleton.Editor.AgeSkeletonImporter.Create(json);saved.GetComponent<AgeSkeletonPlayer>().initialAnimation="Walk";EditorSceneManager.SaveScene(EditorSceneManager.GetActiveScene(),"Assets/WayfarerDemo.unity");
            Debug.Log("AGESKELETON_UNITY_PASS playback wardrobe slots renderer pixels="+distinct+" version="+Application.unityVersion);EditorApplication.Exit(0);
        }catch(Exception e){Debug.LogException(e);EditorApplication.Exit(1);}
    }
}
