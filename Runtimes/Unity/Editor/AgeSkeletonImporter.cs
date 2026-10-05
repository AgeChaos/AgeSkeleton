// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
using System;
using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.U2D;
using UnityEditor.U2D;
namespace AgeSkeleton.Editor
{
    public static class AgeSkeletonImporter
    {
        [MenuItem("Tools/AgeSkeleton/Create Player From Selected JSON")]
        public static void CreateSelected()
        {
            var json = Selection.activeObject as TextAsset;
            if (!json) { EditorUtility.DisplayDialog("AgeSkeleton", "Select skeleton.ageskel.json in the Project window. / 请在项目中选择 JSON 资源。", "OK"); return; }
            Selection.activeGameObject = Create(json);
        }
        [MenuItem("Tools/AgeSkeleton/Create Player With Native Sprite Atlas")]
        public static void CreateSelectedWithAtlas() {
            var json=Selection.activeObject as TextAsset;
            if(!json){EditorUtility.DisplayDialog("AgeSkeleton","Select skeleton.ageskel.json. / 请选择骨骼 JSON。","OK");return;}
            var atlas=CreateAtlas(json);var go=Create(json);go.GetComponent<AgeSkeletonPlayer>().spriteAtlas=atlas;Selection.activeGameObject=go;
        }
        public static SpriteAtlas CreateAtlas(TextAsset json) {
            EditorSettings.spritePackerMode=SpritePackerMode.SpriteAtlasV2;
            var data=JsonUtility.FromJson<MeshClip>(json.text);data.Validate();string directory=Path.GetDirectoryName(AssetDatabase.GetAssetPath(json)).Replace('\\','/');
            var sources=new UnityEngine.Object[data.textures.Length];
            for(int i=0;i<sources.Length;i++) {
                string path=directory+"/"+data.textures[i];var importer=(TextureImporter)AssetImporter.GetAtPath(path);
                importer.textureCompression=TextureImporterCompression.Uncompressed;importer.textureType=TextureImporterType.Sprite;importer.spriteImportMode=SpriteImportMode.Single;importer.alphaIsTransparency=true;importer.mipmapEnabled=false;importer.wrapMode=TextureWrapMode.Clamp;
                var settings=new TextureImporterSettings();importer.ReadTextureSettings(settings);settings.spriteMeshType=SpriteMeshType.FullRect;importer.SetTextureSettings(settings);importer.SaveAndReimport();
                sources[i]=AssetDatabase.LoadAssetAtPath<Texture2D>(path);
            }
            string atlasPath=AssetDatabase.GenerateUniqueAssetPath(directory+"/AgeSkeleton.spriteatlasv2");
            var asset=new SpriteAtlasAsset();SpriteAtlasAsset.Save(asset,atlasPath);AssetDatabase.ImportAsset(atlasPath,ImportAssetOptions.ForceSynchronousImport);
            var importerAtlas=(SpriteAtlasImporter)AssetImporter.GetAtPath(atlasPath);
            var packing=importerAtlas.packingSettings;packing.enableRotation=false;packing.enableTightPacking=false;packing.padding=4;importerAtlas.packingSettings=packing;
            var platform=importerAtlas.GetPlatformSettings("DefaultTexturePlatform");platform.maxTextureSize=4096;platform.format=TextureImporterFormat.Automatic;platform.textureCompression=TextureImporterCompression.Uncompressed;importerAtlas.SetPlatformSettings(platform);
            var textureSettings=importerAtlas.textureSettings;textureSettings.generateMipMaps=false;textureSettings.readable=false;textureSettings.sRGB=true;textureSettings.filterMode=FilterMode.Bilinear;importerAtlas.textureSettings=textureSettings;
            importerAtlas.includeInBuild=true;importerAtlas.SaveAndReimport();
            asset.Add(sources);SpriteAtlasAsset.Save(asset,atlasPath);UnityEngine.Object.DestroyImmediate(asset);
            AssetDatabase.ImportAsset(atlasPath,ImportAssetOptions.ForceSynchronousImport|ImportAssetOptions.ForceUpdate);
            return AssetDatabase.LoadAssetAtPath<SpriteAtlas>(atlasPath);
        }
        public static GameObject Create(TextAsset json)
        {
            var data = JsonUtility.FromJson<MeshClip>(json.text); data.Validate();
            var path = AssetDatabase.GetAssetPath(json); string directory = Path.GetDirectoryName(path).Replace('\\','/');
            var pages = new Texture2D[data.textures.Length];
            for (int i=0;i<pages.Length;i++)
            {
                string pagePath=directory+"/"+data.textures[i];
                if (AssetImporter.GetAtPath(pagePath) is TextureImporter importer && (!importer.alphaIsTransparency || importer.mipmapEnabled || importer.wrapMode!=TextureWrapMode.Clamp)) { importer.alphaIsTransparency=true; importer.mipmapEnabled=false; importer.wrapMode=TextureWrapMode.Clamp; importer.SaveAndReimport(); }
                pages[i]=AssetDatabase.LoadAssetAtPath<Texture2D>(pagePath); if(!pages[i])throw new ArgumentException("Missing texture: "+pagePath);
            }
            Shader shader=Shader.Find("AgeSkeleton/Unlit"); if(!shader)throw new InvalidOperationException("AgeSkeleton shader is missing");
            string materialPath=AssetDatabase.GenerateUniqueAssetPath(directory+"/AgeSkeleton.mat");var mat=new Material(shader);AssetDatabase.CreateAsset(mat,materialPath);
            var go=new GameObject(data.name);Undo.RegisterCreatedObjectUndo(go,"Create AgeSkeleton player");var player=go.AddComponent<AgeSkeletonPlayer>();player.animationData=json;player.textures=pages;player.material=mat;player.initialAnimation=data.clips.Length>0?data.clips[0].name:"";
            return go;
        }
    }
}
