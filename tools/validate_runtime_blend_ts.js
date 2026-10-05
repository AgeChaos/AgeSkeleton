// SPDX-License-Identifier: LicenseRef-AgeSkeleton-Free-RestrictedCommercial-1.0
const assert=require('assert'),fs=require('fs'),{Player}=require(process.argv[2]);
const data=JSON.parse(fs.readFileSync(process.argv[3],'utf8')),p=new Player(data);
const near=(v,e)=>assert(Math.abs(v-e)<.0002,`${v} != ${e}`);
p.play('Source');p.update(.25);p.crossFade('Destination',1);near(p.positions[0],2.5);assert(p.isBlending&&p.blendProgress===0);
p.update(.25);near(p.positions[0],5.25);near(p.colors[1],.75);near(p.colors[3],.875);assert(p.visible[0]&&p.orders[0]===0);assert(p.events.length===2&&p.events.every(e=>e.animation==='Destination'));
let before=Array.from(p.positions),progress=p.blendProgress;p.playing=false;p.update(1);assert.deepStrictEqual(Array.from(p.positions),before);near(p.blendProgress,progress);assert(!p.events.length);p.playing=true;p.speed=0;p.update(1);near(p.blendProgress,progress);p.speed=1;
assert.throws(()=>p.crossFade('Missing',1));assert.throws(()=>p.crossFade('Third',NaN));assert.throws(()=>p.crossFade('Third',-1));assert.deepStrictEqual(Array.from(p.positions),before);
p.update(.25);near(p.positions[0],7.75);assert(!p.visible[0]&&p.orders[0]===5);p.setAttachment('Slot','visible');assert(p.visible[0]);p.setSlotVisible('Slot',false);assert(!p.visible[0]);p.setSlotVisible('Slot',true);p.setAttachment('Slot',null);
p.crossFade('Third',1);near(p.positions[0],7.75);p.update(.5);near(p.positions[0],14.875);p.update(.5);near(p.positions[0],22);assert(!p.isBlending&&p.blendProgress===1);
p.crossFade('Source',1);p.seek(.5);assert(!p.isBlending);near(p.positions[0],3);p.crossFade('Third',1);p.stop();assert(!p.isBlending);assert.deepStrictEqual(Array.from(p.positions),data.rest.positions);
p.crossFade('Destination',0);near(p.positions[0],12);assert(!p.isBlending);
p.play('Source');p.seek(.75);p.crossFade('Source',1,false);near(p.time,.75);p.speed=-1;p.update(.25);near(p.time,.5);near(p.blendProgress,.25);near(p.positions[0],3);
p.speed=1;p.play('Source');p.crossFade('Once',2);p.update(1);assert(p.playing&&p.isBlending);p.update(1);assert(!p.playing&&!p.isBlending);
p.play('Source');p.crossFade('Destination',1);p.update(.25);before=Array.from(p.positions);progress=p.blendProgress;assert.throws(()=>p.update(2000));assert.deepStrictEqual(Array.from(p.positions),before);near(p.blendProgress,progress);assert(!p.events.length);
p.setIKTarget('Tip',4.25,1,2,1,64,.0001);let tip=p.getBoneTip('Tip');near(tip[0],4.25);near(tip[1],1);p.clearIKTarget('Tip');assert.deepStrictEqual(Array.from(p.positions),before);
const legacy=JSON.parse(JSON.stringify(data));legacy.version=1;delete legacy.rig;const old=new Player(legacy);old.play('Source');old.crossFade('Destination',1);old.update(.5);near(old.positions[0],7.5);
console.log('AGESKELETON_BLEND_TS_PASS advancing_source interrupt pause reverse slots colors events IK legacy');
