using System;
using System.Collections.Generic;
using Godot;
using D=Godot.Collections.Dictionary;
namespace Recovery;
public sealed class Game:ECSGame {
 ECSNavigation? navigation;
 long ring,player,visual,hud,message,restart,gate,animation; readonly List<long> enemies=new(),crystals=new();
 readonly List<Vector3> homes=new(); readonly HashSet<Key> keys=new();
 int collected,health=5,round,ticks; double clock,hurt,pulseCooldown,dashCooldown,dash,pulseTime; bool ended,won,paused,auto; int autoStage; Vector3 direction=new(0,0,-1);
 static readonly Vector3 Exit=new(0,.6f,7);
 Mesh orb=new SphereMesh { Radius=.46f,Height=.92f }; Mesh gem=new BoxMesh { Size=new Vector3(.6f,.6f,.6f) };
 Material blue=new StandardMaterial3D { AlbedoColor=new Color(.12f,.78f,1),ShadingMode=BaseMaterial3D.ShadingModeEnum.Unshaded };
 Material red=new StandardMaterial3D { AlbedoColor=new Color(1,.2f,.3f),ShadingMode=BaseMaterial3D.ShadingModeEnum.Unshaded };
 Material gold=new StandardMaterial3D { AlbedoColor=new Color(1,.8f,.18f),ShadingMode=BaseMaterial3D.ShadingModeEnum.Unshaded };
 void Require(bool ok,string text) { if(!ok) throw new Exception("RECOVERY_ASSERT "+text); }
 long Entity(Vector3 p) { long id=World.CreateEntity(); World.SetVector(id,"position",p); return id; }
 long Box(Vector3 p,Vector3 size,Color color,bool solid=false) {
  long id=Entity(p); World.SetMesh(id,new BoxMesh { Size=size },new StandardMaterial3D { AlbedoColor=color,ShadingMode=BaseMaterial3D.ShadingModeEnum.Unshaded });
  if(solid) Require(World.SetPhysics(id,new D { ["shape"]=new BoxShape3D { Size=size },["layer"]=1,["mask"]=2 }),"wall"); return id;
 }
 long Label(string text,int y,int size=22) { long id=World.CreateEntity(); World.SetUI(id,new D { ["kind"]="label",["text"]=text,["font_size"]=size,["rect"]=new Rect2(new Vector2(30,y),new Vector2(1190,50)) }); return id; }
 Animation Bob(float height) { var c=new Animation(); c.Set("length",1.0); c.Set("loop_mode",1); long t=c.AddTrack(Animation.TrackType.Position3D); c.PositionTrackInsertKey(t,0,Vector3.Zero); c.PositionTrackInsertKey(t,.5,new Vector3(0,height,0)); c.PositionTrackInsertKey(t,1,Vector3.Zero); return c; }
 protected override void OnInitialize() {
  navigation=new ECSNavigation(World);
  var navMesh=new NavigationMesh();navMesh.SetVertices(new Vector3[]{new(-13.8f,.6f,-8.8f),new(13.8f,.6f,-8.8f),new(13.8f,.6f,8.8f),new(-13.8f,.6f,8.8f)});navMesh.AddPolygon(new int[]{0,1,2,3});
  long region=World.CreateEntity();Require(World.SetNavigation(region,navMesh),"navigation_region");
  foreach(string arg in OS.GetCmdlineUserArgs()) if(arg=="--verify-game") auto=true;
  long camera=Entity(new Vector3(0,26,0)); World.SetVector(camera,"rotation",new Vector3(-(float)Math.PI/2,0,0)); Require(World.SetCamera(camera,new D { ["projection"]="orthogonal",["size"]=29.0 }),"camera");
  Box(new Vector3(0,-.2f,0),new Vector3(29,.3f,19),new Color(.035f,.06f,.11f));
  for(int x=-14;x<=14;x+=2) Box(new Vector3(x,0,0),new Vector3(.025f,.02f,18),new Color(.08f,.13f,.21f));
  for(int z=-8;z<=8;z+=2) Box(new Vector3(0,0,z),new Vector3(28,.02f,.025f),new Color(.08f,.13f,.21f));
  Box(new Vector3(-14.5f,.5f,0),new Vector3(.5f,1,19),new Color(.2f,.35f,.5f),true); Box(new Vector3(14.5f,.5f,0),new Vector3(.5f,1,19),new Color(.2f,.35f,.5f),true);
  Box(new Vector3(0,.5f,-9.5f),new Vector3(29,1,.5f),new Color(.2f,.35f,.5f),true); Box(new Vector3(0,.5f,9.5f),new Vector3(29,1,.5f),new Color(.2f,.35f,.5f),true);
  gate=Box(new Vector3(0,.05f,7),new Vector3(2,.1f,2),new Color(.05f,.45f,.35f));
  ring=Entity(new Vector3(0,-10,0)); World.SetMesh(ring,new TorusMesh { InnerRadius=3.8f,OuterRadius=4.0f },blue);
  player=Entity(Exit); Require(World.SetPhysics(player,new D { ["shape"]=new SphereShape3D { Radius=.45f },["mode"]="kinematic",["layer"]=2,["mask"]=1 }),"player");
  visual=Entity(Vector3.Zero); World.SetParent(visual,player); World.SetMesh(visual,orb,blue);
  var points=new Godot.Collections.Array(); points.Add(new D { ["position"]=0.0,["clip"]=Bob(.02f) }); points.Add(new D { ["position"]=1.0,["clip"]=Bob(.28f) });
  animation=Entity(Vector3.Zero); Require(World.SetAnimation(animation,new D { ["targets"]=new Variant(new long[]{visual}),["blend_space"]=new D { ["points"]=points } }),"blendspace");
  Label("AGECHAOS  /  ENERGY RECOVERY",18,28); hud=Label("",60); message=Label("",625,24);
  Label("WASD / arrows: move   SHIFT: dash   SPACE: pulse   P: pause   R: restart   ESC: exit",675,18);
  restart=World.CreateEntity(); World.SetUI(restart,new D { ["kind"]="button",["text"]="RESTART",["rect"]=new Rect2(new Vector2(1110,25),new Vector2(140,40)) });
  for(int i=0;i<8;i++) { double a=i*Math.PI/4; homes.Add(new Vector3((float)Math.Cos(a)*9,.6f,(float)Math.Sin(a)*5.5f)); }
  Reset(); GD.Print("RECOVERY_READY pure_ecs=true controls=WASD,SHIFT,SPACE,P,R");
 }
 void Reset() {
  foreach(long id in enemies) { navigation!.Remove(id); World.DestroyEntity(id); } enemies.Clear(); foreach(long id in crystals) if(World.IsAlive(id)) World.DestroyEntity(id); crystals.Clear();
  health=5;collected=0;clock=0;pulseTime=0;World.SetVector(ring,"position",new Vector3(0,-10,0));hurt=0;dash=0;dashCooldown=0;pulseCooldown=0;ended=false;won=false;paused=false;keys.Clear(); round++;
  World.SetAnimation(animation,new D { ["playing"]=true });
  World.SetVector(player,"position",Exit); World.SetVector(player,"velocity",Vector3.Zero);
  foreach(Vector3 home in homes) { long id=Entity(home); World.SetMesh(id,gem,gold); crystals.Add(id); }
  for(int i=0;i<6;i++) { long id=Entity(new Vector3(i%2==0?-12:12,.6f,-7+i*2.5f)); World.SetMesh(id,orb,red); enemies.Add(id);Require(World.SetPhysics(id,new D { ["shape"]=new SphereShape3D { Radius=.46f },["mode"]="kinematic",["layer"]=2,["mask"]=1 }),"enemy_physics");Require(navigation!.Add(id,.55,4),"navigation_agent"); }
  UpdateHud();
 }
 void UpdateHud() {
  World.SetUI(hud,new D { ["text"]="ENERGY "+collected+" / 8     SHIELD "+health+" / 5     TIME "+((int)clock)+"s     DASH "+(dashCooldown<=0?"READY":"...")+"     PULSE "+(pulseCooldown<=0?"READY":"...") });
  World.SetUI(message,new D { ["text"]=paused?"PAUSED - press P to resume":ended?(won?"RECOVERY COMPLETE - press R for another run":"SHIELD LOST - press R to retry"):collected==8?"All energy secured. Return to the green extraction pad!":"Collect the eight gold cells. Pulse pushes nearby hunters away." });
 }
 void Damage() { if(hurt>0 || ended) return; health--; hurt=1.5; if(health<=0) { ended=true; won=false; } }
 void Pulse() {
  if(pulseCooldown>0 || ended || paused) return; pulseCooldown=2;pulseTime=.25;
  Vector3 p=World.GetVector(player,"position"); foreach(long id in enemies) { Vector3 q=World.GetVector(id,"position"),d=q-p; float n=d.Length(); if(n<4.5) { if(n<.01f)d=new Vector3(1,0,0); else d=d/n; World.SetVector(id,"position",new Vector3(Math.Clamp(q.X+d.X*4,-13,13),.6f,Math.Clamp(q.Z+d.Z*4,-8,8)));navigation!.NavigateTo(id,p,Math.Min(3.6,1.6+clock*.008),.2); } }
 }
 public override void HandleInput(InputEvent input,bool uiHandled) {
  if(input is not InputEventKey k) return;
  if(!k.Pressed) { keys.Remove(k.Keycode); return; } if(k.Echo) return;
  if(k.Keycode==Key.Escape) { Loop.Quit(); return; }
  if(k.Keycode==Key.R) { Reset(); return; }
  if(k.Keycode==Key.P) { paused=!paused; World.SetAnimation(animation,new D { ["playing"]=!paused }); UpdateHud(); return; }
  if(uiHandled)return; keys.Add(k.Keycode); if(k.Keycode==Key.Space) Pulse();
 }
 protected override void OnUIEvent(long entity,StringName type,Variant value) { if(entity==restart && type.ToString()=="pressed") Reset(); }
 public override void PhysicsUpdate(double dt) {
  ticks++;
  if(auto && ticks==1) {
   var press=new InputEventKey(); press.Keycode=Key.P;press.Pressed=true;HandleInput(press,false);Require(paused,"pause_key");HandleInput(press,false);Require(!paused,"resume_key");
   press.Keycode=Key.W;HandleInput(press,false);Require(keys.Contains(Key.W),"move_key");press.Pressed=false;HandleInput(press,true);Require(!keys.Contains(Key.W),"release_over_ui");
   World.SetVector(enemies[0],"position",Exit+new Vector3(1,0,0));Pulse();Require((World.GetVector(enemies[0],"position")-Exit).Length()>4,"pulse_push");
   World.SetAnimation(animation,new D { ["playing"]=false });Reset();Require((bool)World.GetAnimation(animation)["playing"],"restart_resumes_animation");
  }
  if(auto && ticks==2) { var collision=World.MoveAndCollide(player,new Vector3(100,0,0)); Require((bool)collision["collided"] && World.GetVector(player,"position").X<14,"wall_collision"); Reset(); }
  if(paused || ended) { if(auto && ended) VerifyEnd(); return; }
  pulseTime=Math.Max(0,pulseTime-dt);World.SetVector(ring,"position",pulseTime>0?World.GetVector(player,"position"):new Vector3(0,-10,0));
  clock+=dt; hurt=Math.Max(0,hurt-dt);pulseCooldown=Math.Max(0,pulseCooldown-dt);dashCooldown=Math.Max(0,dashCooldown-dt);dash=Math.Max(0,dash-dt);
  Vector3 input=Vector3.Zero;
  if(auto) {
   Vector3 target=Exit; foreach(long id in crystals) if(World.IsAlive(id)) { target=World.GetVector(id,"position");break; }
   input=target-World.GetVector(player,"position"); input.Y=0;
   // Test mode parks hunters outside the pickup route; damage/death is tested separately.
   foreach(long id in enemies) World.SetVector(id,"position",new Vector3(13,.6f,8));
  } else {
   if(keys.Contains(Key.W)||keys.Contains(Key.Up))input.Z-=1;if(keys.Contains(Key.S)||keys.Contains(Key.Down))input.Z+=1;
   if(keys.Contains(Key.A)||keys.Contains(Key.Left))input.X-=1;if(keys.Contains(Key.D)||keys.Contains(Key.Right))input.X+=1;
  }
  float length=input.Length(); if(length>.01) { input=input/length;direction=input; }
  if(keys.Contains(Key.Shift)&&dashCooldown<=0&&length>.01) { dash=.18;dashCooldown=1.4; }
  Vector3 velocity=(dash>0?direction:input)*(dash>0?17:6);
  if(length<=.01&&dash<=0)velocity=Vector3.Zero;
  World.MoveAndSlide(player,velocity,dt); if(ticks%6==0)World.SetAnimationBlendPosition(animation,length>.01?1:0);
  Vector3 pos=World.GetVector(player,"position");
  foreach(long id in crystals) if(World.IsAlive(id)) { Vector3 q=World.GetVector(id,"position");World.SetVector(id,"rotation",new Vector3(.4f,(float)clock,.4f));if((q-pos).Length()<.9) {World.DestroyEntity(id);collected++;} }
  for(int i=0;i<enemies.Count;i++) {
   long id=enemies[i];
   if(!auto && (ticks%30==0 || navigation!.GetStatus(id)=="idle")) navigation!.NavigateTo(id,pos,Math.Min(3.6,1.6+clock*.008),.2);
   if((World.GetVector(id,"position")-pos).Length()<.85&&dash<=0)Damage();
  }
  navigation!.PhysicsUpdate(dt);
  if(collected==8&&(pos-Exit).Length()<1) {ended=true;won=true;GD.Print("RECOVERY_WIN seconds="+clock);}
  if(ticks%6==0||ended)UpdateHud();
  if(auto&&ticks>5000)throw new Exception("RECOVERY_VERIFY_TIMEOUT");
 }
 void VerifyEnd() {
  Require(won && collected==8,"win"); Reset(); Require(health==5&&collected==0&&!ended,"restart");
  for(int i=0;i<5;i++) {hurt=0;Damage();} Require(ended&&!won&&health==0,"lose");Reset();
  Require(enemies.Count==6&&crystals.Count==8,"reset_counts"); autoStage=1; GD.Print("RECOVERY_GAME_PASS collision pickups win loss restart");Loop.Quit();
 }
 protected override void OnShutdown() { navigation?.Dispose(); if(auto&&autoStage!=1)Loop.Quit(1); }
}
