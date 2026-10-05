// Validate the engine-independent TypeScript evaluator compiled with tsc.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const {Player}=require(process.argv[2]);
const data=JSON.parse(fs.readFileSync(process.argv[3],'utf8'));
const p=new Player(data);p.play('Walk');p.seek(.35);const positions=Array.from(p.positions);
p.setWardrobe('Tops','Tops/Steel Armor');assert.deepEqual(Array.from(p.positions),positions);assert(p.playing);
assert.throws(()=>p.setWardrobe('Tops','Weapons/Short Sword'));
p.setSlotVisible('Torso',false);data.attachments.forEach((a,i)=>{if(a.slot===1)assert(!p.visible[i]);});
p.setSlotVisible('Torso',true);p.setAttachment('Torso','');data.attachments.forEach((a,i)=>{if(a.slot===1)assert(!p.visible[i]);});p.setAttachment('Torso',null);
p.playing=false;const t=p.time;p.update(1);assert.equal(p.time,t);p.playing=true;p.update(10);assert(p.time>=0&&p.time<1.2);
assert.throws(()=>p.seek(NaN));assert.throws(()=>p.update(-1));
p.stop();assert.deepEqual(Array.from(p.positions),Array.from(new Float32Array(data.rest.positions)));
const bad=JSON.parse(JSON.stringify(data));bad.attachments[0].triangles[0]=999999;assert.throws(()=>new Player(bad));
console.log('AGESKELETON_TYPESCRIPT_PASS playback wardrobe slots loop pause invalid stop');

const {Batcher}=require(require('node:path').join(require('node:path').dirname(process.argv[2]),'Batcher.js'));
const synthetic=JSON.parse(JSON.stringify(data));synthetic.textures=['a.png','b.png'];synthetic.attachments.forEach(a=>a.texture=0);synthetic.attachments[1].texture=1;
const q=new Player(synthetic);q.visible.fill(false);for(let i=0;i<3;i++){q.visible[i]=true;q.orders[i]=i;}
const batches=new Batcher();assert(batches.update(q));assert.equal(batches.batches.length,3);
const first=batches.batches[0].positions,revision=batches.revision;q.positions[0]+=1;assert(!batches.update(q));assert.equal(batches.revision,revision);assert.strictEqual(first,batches.batches[0].positions);
q.visible[1]=false;assert(batches.update(q));assert.equal(batches.batches.length,1);
for(const i of batches.batches[0].indices)assert(i<batches.batches[0].positions.length/2);
q.visible.fill(false);assert(batches.update(q));assert.equal(batches.batches.length,0);
console.log('AGESKELETON_BATCHING_PASS A_B_A hidden_order topology_reuse indices all_hidden');
