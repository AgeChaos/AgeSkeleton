// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
using System;
using System.Collections.Generic;

namespace AgeSkeleton
{
    [Serializable] public sealed class Attachment { public string name; public int texture, offset, count, slot; public int[] triangles; public float[] uv, color; }
    [Serializable] public sealed class Binding { public int slot, attachment; public string key; }
    [Serializable] public sealed class Skin { public string name; public Binding[] bindings; }
    [Serializable] public sealed class Frame { public float time; public float[] positions, colors, matrices, influencePositions; public string[] keys; public int[] orders; }
    [Serializable] public sealed class Clip { public string name; public float duration; public bool loop; public Frame[] frames; public AnimationEvent[] events; }
    [Serializable] public sealed class MeshClip
    {
        public string format, name;
        public int version, fps, vertexCount;
        public RuntimeRig rig;
        public string[] textures, slots, defaultSkins;
        public Attachment[] attachments;
        public Skin[] skins;
        public Frame rest;
        public Clip[] clips;
        static void Require(bool value, string reason) { if (!value) throw new ArgumentException("Invalid AgeSkeleton data: " + reason); }
        static bool Finite(float n) { return !float.IsNaN(n) && !float.IsInfinity(n); }
        static void Numbers(float[] values, int count) { Require(values != null && values.Length == count, "array size"); foreach (float v in values) Require(Finite(v), "non-finite number"); }
        public void Validate()
        {
            Require(format == "ageskeleton.meshclip" && (version == 1 || version == 2), "unsupported format/version");
            Require(vertexCount > 0 && vertexCount <= 100000 && fps >= 1 && fps <= 120, "limits");
            Require(textures != null && slots != null && attachments != null && skins != null && clips != null && defaultSkins != null, "missing arrays");
            foreach (string path in textures) Require(!string.IsNullOrEmpty(path) && path.IndexOfAny(new[]{'/', '\\', ':'}) < 0 && !path.Contains(".."), "texture filename");
            var names = new HashSet<string>(); foreach (string slot in slots) Require(!string.IsNullOrEmpty(slot) && names.Add(slot), "duplicate slot");
            if(version==2)MotionPose.Validate(rig,vertexCount);
            int end = 0;
            foreach (Attachment a in attachments)
            {
                Require(a != null && a.offset == end && a.count >= 3 && a.count <= vertexCount - end, "vertex range"); end += a.count;
                Require(a.texture >= 0 && a.texture < textures.Length && a.slot >= -1 && a.slot < slots.Length, "attachment reference");
                Numbers(a.uv, a.count * 2); Numbers(a.color, 4);
                Require(a.triangles != null && a.triangles.Length > 0 && a.triangles.Length % 3 == 0, "triangles");
                foreach (int i in a.triangles) Require(i >= 0 && i < a.count, "triangle index");
            }
            Require(end == vertexCount, "vertex count"); names.Clear();
            foreach (Skin skin in skins)
            {
                Require(skin != null && !string.IsNullOrEmpty(skin.name) && names.Add(skin.name) && skin.bindings != null, "skin");
                foreach (Binding b in skin.bindings) Require(b != null && b.slot >= 0 && b.slot < slots.Length && !string.IsNullOrEmpty(b.key) && b.attachment >= 0 && b.attachment < attachments.Length && attachments[b.attachment].slot == b.slot, "binding");
            }
            foreach (string skin in defaultSkins) Require(names.Contains(skin), "default skin");
            ValidateFrame(rest); names.Clear();
            foreach (Clip clip in clips)
            {
                Require(clip != null && !string.IsNullOrEmpty(clip.name) && names.Add(clip.name) && Finite(clip.duration) && clip.duration > 0 && clip.frames != null && clip.frames.Length >= 2, "clip");
                if(version==2){Require(clip.events!=null&&clip.events.Length<=100000,"events");float prior=-1;foreach(var e in clip.events){Require(e!=null&&!string.IsNullOrEmpty(e.name)&&Finite(e.time)&&e.time>=prior&&e.time>=0&&e.time<=clip.duration&&Finite(e.floatValue)&&e.stringValue!=null,"event");prior=e.time;}}
                float last = -1;
                foreach (Frame f in clip.frames) { ValidateFrame(f); Require(f.time > last, "frame order"); last = f.time; }
                Require(clip.frames[0].time == 0 && Math.Abs(last - clip.duration) < .0001f, "clip endpoints");
            }
        }
        void ValidateFrame(Frame f)
        {
            Require(f != null && Finite(f.time) && f.time >= 0, "frame time");
            if(version==2){Numbers(f.matrices,rig.names.Length*6);Numbers(f.influencePositions,vertexCount*8);}
            Numbers(f.positions, vertexCount * 2); Numbers(f.colors, slots.Length * 4);
            Require(f.keys != null && f.keys.Length == slots.Length && f.orders != null && f.orders.Length == slots.Length, "slot frame");
            foreach (string key in f.keys) Require(key != null, "null slot key");
        }
    }

    // Engine-independent evaluator. Arrays are allocated once; Update performs no per-vertex allocation.
    public sealed class Player
    {
        public MeshClip Data { get; }
        public float[] Positions { get; }
        public float[] Colors { get; }
        public bool[] Visible { get; }
        public int[] Orders { get; }
        public bool Playing { get; set; }
        public float Speed { get; set; } = 1;
        public float Time { get; private set; }
        public string Animation => clip?.name;
        readonly List<string> active = new List<string>();
        readonly string[] slotOverrides;
        readonly bool[] hidden;
        Clip clip;readonly MotionPose motion;bool eventStart;
        public readonly List<AnimationEvent> Events=new List<AnimationEvent>();
        public Player(MeshClip data)
        {
            data.Validate(); Data = data; Positions = new float[data.vertexCount * 2]; Colors = new float[data.attachments.Length * 4];
            Visible = new bool[data.attachments.Length]; Orders = new int[data.attachments.Length]; slotOverrides = new string[data.slots.Length]; hidden = new bool[data.slots.Length];
            active.AddRange(data.defaultSkins);motion=new MotionPose(data.version==2?data.rig:null); Evaluate();
        }
        public void Play(string name, bool restart = true)
        {
            Clip next = Array.Find(Data.clips, x => x.name == name) ?? throw new ArgumentException("Unknown animation: " + name);
            if (restart || clip != next) {Time = 0;eventStart=true;} Events.Clear(); clip = next; Playing = true; Evaluate();
        }
        public void Stop() { Events.Clear();eventStart=false;clip = null; Time = 0; Playing = false; Evaluate(); }
        public void Seek(float time) { if (float.IsNaN(time) || float.IsInfinity(time)) throw new ArgumentException("Invalid time");Events.Clear();eventStart=false; Time = clip == null ? 0 : Math.Max(0, Math.Min(clip.duration, time)); Evaluate(); }
        public void Update(float delta)
        {
            Events.Clear();if (!Playing || clip == null) return;
            if (delta < 0 || float.IsNaN(delta) || float.IsInfinity(delta) || float.IsNaN(Speed) || float.IsInfinity(Speed)) throw new ArgumentException("Invalid playback step");
            double next = Time + (double)delta * Speed;
            if(double.IsInfinity(next)||double.IsNaN(next))throw new ArgumentException("Playback overflow");
            double to=clip.loop?next:Math.Max(0,Math.Min(clip.duration,next));if(Data.version==2)EventTraversal.Collect(clip,Time,to,eventStart,Events);if(to!=Time)eventStart=false;
            if (clip.loop) Time = (float)((next % clip.duration + clip.duration) % clip.duration);
            else { Time = (float)Math.Max(0, Math.Min(clip.duration, next)); if (next >= clip.duration || next < 0) Playing = false; }
            Evaluate();
        }
        public void SetIKTarget(string bone,float x,float y,int chainLength=2,float mix=1,int iterations=24,float tolerance=.1f){motion.SetTarget(bone,x,y,chainLength,mix,iterations,tolerance);Evaluate();}
        public void ClearIKTarget(string bone){motion.ClearTarget(bone);Evaluate();}
        public void GetBoneTip(string bone,out float x,out float y){motion.Tip(bone,out x,out y);}
        public void SetSkin(string name) { FindSkin(name); active.Clear(); active.Add(name); Evaluate(); }
        public void SetWardrobe(string group, string name)
        {
            if (string.IsNullOrEmpty(group) || group.Contains("/") || !Array.Exists(Data.skins, s => s.name.StartsWith(group + "/", StringComparison.Ordinal))) throw new ArgumentException("Unknown group");
            if (!string.IsNullOrEmpty(name)) { FindSkin(name); if (!name.StartsWith(group + "/", StringComparison.Ordinal)) throw new ArgumentException("Skin is outside group"); }
            active.RemoveAll(s => s.StartsWith(group + "/", StringComparison.Ordinal)); if (!string.IsNullOrEmpty(name)) active.Add(name); Evaluate();
        }
        public void SetSlotVisible(string name, bool visible) { hidden[Slot(name)] = !visible; Evaluate(); }
        // null restores the animation's slot key; empty string explicitly hides it.
        public void SetAttachment(string slot, string key)
        {
            int i = Slot(slot);
            if (!string.IsNullOrEmpty(key) && !Array.Exists(Data.skins, s => Array.Exists(s.bindings, b => b.slot == i && b.key == key))) throw new ArgumentException("Unknown attachment key");
            slotOverrides[i] = key; Evaluate();
        }
        int Slot(string name) { int index = Array.IndexOf(Data.slots, name); if (index < 0) throw new ArgumentException("Unknown slot"); return index; }
        Skin FindSkin(string name) => Array.Find(Data.skins, s => s.name == name) ?? throw new ArgumentException("Unknown skin: " + name);
        int Resolve(string skin, int slot, string key, int fallback)
        {
            Skin found = Array.Find(Data.skins, s => s.name == skin); if (found == null) return fallback;
            foreach (Binding b in found.bindings) if (b.slot == slot && b.key == key) fallback = b.attachment; return fallback;
        }
        void Evaluate()
        {
            Frame a = Data.rest, b = a; float t = 0;
            if (clip != null)
            {
                int lo = 0, hi = clip.frames.Length - 1;
                while (lo < hi) { int mid = (lo + hi + 1) / 2; if (clip.frames[mid].time <= Time) lo = mid; else hi = mid - 1; }
                a = clip.frames[lo]; b = clip.frames[Math.Min(lo + 1, clip.frames.Length - 1)];
                t = b.time > a.time ? (Time - a.time) / (b.time - a.time) : 0;
            }
            for (int i = 0; i < Positions.Length; ++i) Positions[i] = a.positions[i] + (b.positions[i] - a.positions[i]) * t;
            motion.Apply(a,b,t,Positions);
            for (int i = 0; i < Data.attachments.Length; ++i)
            {
                Attachment mesh = Data.attachments[i]; int s = mesh.slot; Visible[i] = s < 0; Orders[i] = s < 0 ? i : a.orders[s];
                for (int k = 0; k < 4; ++k) Colors[i * 4 + k] = mesh.color[k] * (s < 0 ? 1 : a.colors[s * 4 + k] + (b.colors[s * 4 + k] - a.colors[s * 4 + k]) * t);
            }
            for (int s = 0; s < Data.slots.Length; ++s)
            {
                if (hidden[s]) continue; string key = slotOverrides[s] ?? a.keys[s]; if (key.Length == 0) continue;
                int index = Resolve("default", s, key, -1); foreach (string skin in active) index = Resolve(skin, s, key, index);
                if (index >= 0) Visible[index] = true;
            }
        }
    }
}
