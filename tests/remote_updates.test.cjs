const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),crypto=require('node:crypto');
// Reuse the existing Sheets/journal/lock fault fixture without running its suite twice.
const fixture=fs.readFileSync(path.join(__dirname,'cloud.test.cjs'),'utf8').split("const boot='a'.repeat(32);")[0];
const scope={require,__dirname,module:{exports:{}}};vm.createContext(scope);vm.runInContext(fixture+'\nmodule.exports=environment;',scope);
const base=scope.module.exports,operator='a'.repeat(64),key='b'.repeat(64),a='111111111111',b='222222222222';
const digest=x=>crypto.createHash('sha256').update(x).digest('hex');
const mac=x=>crypto.createHmac('sha256',key).update(x).digest('hex');
let passed=0;function test(name,fn){fn();passed++;console.log('PASS '+name);}
function env(){
 const e=base(),api=e.api;
 api.PropertiesService.getScriptProperties=()=>({getProperty:k=>e.props.get(k)||null,setProperty:(k,v)=>e.props.set(k,v)});
 api.Utilities.Charset={UTF_8:'utf8'};api.Utilities.DigestAlgorithm={SHA_256:'sha256'};
 api.Utilities.computeDigest=(_,value)=>Array.from(crypto.createHash('sha256').update(value).digest());
 api.Utilities.computeHmacSha256Signature=(value,k)=>Array.from(crypto.createHmac('sha256',k).update(value).digest());
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../cloud/apps_script/RemoteUpdates.gs'),'utf8'),api);
 e.props.set('REMOTE_OPERATOR_SHA256',digest(operator));
 e.props.set('REMOTE_KEY_'+a,key);e.props.set('REMOTE_KEY_'+b,'c'.repeat(64));
 e.props.set('REMOTE_DOWNLOAD_HOSTS','updates.example.invalid');
 e.catalog=[{id:'esp-test',kind:'esp32',version:'2.0.22',build:'2026102801',remote_capable:true,url:'https://updates.example.invalid/v22/gateway.ino.bin',bytes:1024,sha256:'d'.repeat(64),manifest_url:'https://updates.example.invalid/v22/gateway.ino.manifest',manifest_bytes:235,manifest_sha256:'e'.repeat(64)},
  {id:'nrf-test',kind:'nrf',version:'2.0.12',url:'https://updates.example.invalid/node.zip',bytes:162102,sha256:'f'.repeat(64)}];
 e.props.set('REMOTE_UPDATE_CATALOG',JSON.stringify(e.catalog));
 for(const id of [a,b])api.ingest({version:3,device_id:id,sensor_id:'0123456789abcdef',samples:[],health:{firmware_version:'2.0.21',remote_capable:true}});
 e.queue=(id=a,release='esp-test',approval='1'.repeat(32))=>api.queueRemoteUpdate(id,release,false,approval,e.now(),operator);
 e.request=(extra={})=>{const r={nonce:'2'.repeat(32),at:Math.floor(e.now()/1000),id:'',seq:0,state:'IDLE',percent:0,error:'',firmware:'2.0.21',build:'2026102706',sensor_version:'2.0.12',...extra};r.mac=mac(api.remoteRequestCanonical_(a,r));return r;};
 e.exchange=r=>api.remoteExchange_({device_id:a,remote:r});
 e.exchange(e.request());
 return e;
}
test('operator authentication, package target/URL bounds and recording guard fail closed',()=>{
 const e=env();assert.throws(()=>e.api.queueRemoteUpdate(a,'esp-test',false,'1'.repeat(32),e.now(),'0'.repeat(64)),/authentication/);
 assert.throws(()=>e.api.queueRemoteUpdate('legacy','esp-test',false,'1'.repeat(32),e.now(),operator),/Invalid/);
 e.api.startRecording('active',a);assert.throws(()=>e.queue(),/STOPPED/);e.api.stopRecording(a);
 assert.throws(()=>e.api.remoteUrl_('http://updates.example.invalid/a',['updates.example.invalid']),/allowlisted/);
 for(const url of ['https://evil.invalid/a','https://updates.example.invalid:443/a','https://updates.example.invalid/a?token=x','https://updates.example.invalid/%2e%2e/a'])assert.throws(()=>e.api.remoteUrl_(url,['updates.example.invalid']));
 assert.equal(e.api.getRemoteUpdateCatalog(operator).length,2);
});
test('signed command is device/nonce-bound and approval replay is idempotent',()=>{
 const e=env(),q=e.queue(),r=e.request(),c=e.exchange(r);
 assert.equal(c.device,a);assert.equal(c.id,q.id);assert.equal(c.nonce,r.nonce);assert.equal(c.seq,1);
 assert.equal(c.mac,mac(e.api.remoteCommandCanonical_(c,r.nonce)));
 assert.equal(e.queue().id,q.id);assert.equal(e.api.remoteLoad_(a,true).command.seq,1);
 assert.equal(e.api.remoteExchange_({device_id:b,remote:r}),null);
 r.percent=1;assert.equal(e.exchange(r),null); // authenticated fields cannot be edited
});
test('cache eviction or stale health cannot release an update reservation',()=>{
 const e=env();e.queue();e.cache.clear();
 e.api.ingest({version:3,device_id:a,samples:[],health:{ota_state:'IDLE'}});
 assert.throws(()=>e.api.startRecording('must not start',a),/Remote firmware/);
 e.api.startRecording('other device',b);assert.equal(e.api.getDashboard(b).recording.state,'RECORDING');
});
test('valid reports advance phases, reject forged success/version mismatch, and never regress terminal state',()=>{
 const e=env(),q=e.queue(),id=q.id;
 e.exchange(e.request({id,seq:1,state:'RECEIVED'}));assert.equal(e.api.remoteSummary_(a).state,'RECEIVED');
 assert.equal(e.exchange(e.request({id,seq:1,state:'DOWNLOADING',percent:40})),null); // command not reissued
 e.exchange(e.request({id,seq:1,state:'PREPARING'}));assert.equal(e.api.remoteSummary_(a).state,'DOWNLOADING');
 e.exchange(e.request({id,seq:1,state:'SUCCESS',percent:100}));assert.equal(e.api.remoteSummary_(a).error,'VERSION_MISMATCH');
 e.exchange(e.request({id,seq:1,state:'UPDATING'}));assert.equal(e.api.remoteSummary_(a).state,'FAILED');
 e.api.startRecording('allowed after terminal failure',a);
});
test('ESP and nRF success require the actual reported target version',()=>{
 for(const kind of ['esp-test','nrf-test']){
  const e=env(),q=e.queue(a,kind);
  e.exchange(e.request({id:q.id,seq:1,state:'SUCCESS',percent:100,firmware:'2.0.22',build:'2026102801',sensor_version:'2.0.12'}));
  assert.equal(e.api.remoteSummary_(a).state,'SUCCESS');assert.equal(e.api.remoteSummary_(a).reserved,false);
 }
});
test('expiry stops delivery, leaves unknown outcomes explicit, and requests have a freshness bound',()=>{
 const e=env();e.queue();e.setTime(e.now()+601000);assert.equal(e.exchange(e.request()),null);
 assert.equal(e.api.remoteSummary_(a).reserved,true); // accepted-but-lost response may still be executing
 e.setTime(e.now()+1201000);assert.equal(e.api.remoteSummary_(a).state,'UNCONFIRMED');assert.equal(e.api.remoteSummary_(a).reserved,false);
 const r=e.request({at:Math.floor(e.now()/1000)-301});assert.equal(e.exchange(r),null);
});
test('lock contention/malformed remote auth does not discard valid data ACKs or grow history on heartbeats',()=>{
 const e=env(),before=e.all.get('EnvironmentHistory').getLastRow();e.queue();e.setLockBusy(true);
 assert.equal(e.exchange(e.request()),null); // bounded tryLock, no wait/sleep storm
 const payload={version:3,device_id:a,samples:[],health:{ota_state:'IDLE'},remote:e.request()};
 const response=JSON.parse(e.api.doPost({postData:{contents:JSON.stringify(payload)}}).text);
 assert.equal(response.success,true);assert.deepEqual(response.acks,[]);assert.equal(response.remote,null);
 assert.equal(e.all.get('EnvironmentHistory').getLastRow(),before);
});
test('new approval after success gets a fresh sequence; old approval cannot execute twice',()=>{
 const e=env(),q=e.queue();e.exchange(e.request({id:q.id,seq:1,state:'FAILED',error:'BOOT_INTERRUPTED'}));
 e.setTime(e.now()+31000);const second=e.queue(a,'nrf-test','3'.repeat(32));assert.notEqual(second.id,q.id);
 assert.equal(e.api.remoteLoad_(a,true).command.seq,2);assert.throws(()=>e.queue(),/already processed/);
 assert.equal(e.api.remoteSummary_(a).history.length,1);
});
test('downgrades and legacy targets require explicit approval; same ESP version is a no-op',()=>{
 const e=env();e.catalog[0].version='2.0.20';e.props.set('REMOTE_UPDATE_CATALOG',JSON.stringify(e.catalog));e.cache.delete('RU_PROP:REMOTE_UPDATE_CATALOG');
 assert.throws(()=>e.queue(),/Explicit downgrade/);
 const q=e.api.queueRemoteUpdate(a,'esp-test',true,'4'.repeat(32),e.now(),operator);assert.equal(q.version,'2.0.20');
 const same=env();same.catalog[0].version='2.0.21';same.props.set('REMOTE_UPDATE_CATALOG',JSON.stringify(same.catalog));same.cache.delete('RU_PROP:REMOTE_UPDATE_CATALOG');assert.throws(()=>same.queue(),/Same ESP/);
 const legacy=env();legacy.catalog[0].remote_capable=false;legacy.props.set('REMOTE_UPDATE_CATALOG',JSON.stringify(legacy.catalog));legacy.cache.delete('RU_PROP:REMOTE_UPDATE_CATALOG');assert.throws(()=>legacy.queue(),/Explicit downgrade/);
});
test('real dashboard requires approval, scopes the selected device and preserves command state after refresh',()=>{
 const e=env();let approvals=false,queued=0;
 const saved=new Map(),localStorage={getItem:key=>saved.get(key)||null,setItem:(key,value)=>saved.set(key,value),removeItem:key=>saved.delete(key)};
 function browser(initialToken=operator){
  const elements=new Map(),element=id=>{if(!elements.has(id))elements.set(id,{value:id==='remoteToken'?initialToken:'',textContent:'',innerHTML:'',disabled:false,hidden:false,options:[],appendChild(option){this.options.push(option);if(!this.value)this.value=option.value;},remove(){elements.delete(id);}});return elements.get(id);};
  element('devices').insertAdjacentHTML=(_,html)=>{for(const m of html.matchAll(/id="([^"]+)"/g))element(m[1]);};
  let success,failure;const runner={withSuccessHandler(fn){success=fn;return this;},withFailureHandler(fn){failure=fn;return this;}};
  for(const name of ['getDashboardDevices','getDashboard','getBatteryHistory','getRemoteUpdateCatalog','queueRemoteUpdate'])runner[name]=(...args)=>{try{if(name==='queueRemoteUpdate')queued++;success(e.api[name](...args));}catch(err){failure(err);}};
  const html=fs.readFileSync(path.join(__dirname,'../cloud/apps_script/Dashboard.html'),'utf8'),script=html.match(/<script>([\s\S]*?)<\/script>/)[1].replace('<?= refreshMs ?>','5000').replace('<?= webAppUrl ?>','https://example.invalid/exec');
  const ctx={document:{getElementById:element,createElement:()=>({})},google:{script:{run:runner}},localStorage,Date:e.api.Date,crypto:crypto.webcrypto,confirm:()=>approvals,setInterval(){},setTimeout(){},console};
  vm.createContext(ctx);vm.runInContext(script,ctx);return {ctx,element};
 }
 const first=browser();first.ctx.loadRemoteCatalog(a);first.element('remoteRelease-'+a).value='esp-test';
 first.ctx.approveRemoteUpdate(a);assert.equal(queued,0);
 approvals=true;first.ctx.approveRemoteUpdate(a);assert.equal(queued,1);assert.equal(e.api.remoteLoad_(a,true).command.device,a);assert.equal(e.api.remoteSummary_(b).state,'IDLE');
 first.element('remoteToken').value='bad';first.ctx.saveRemoteToken();assert.equal(saved.size,0);
 first.element('remoteToken').value=operator;first.ctx.saveRemoteToken();assert.equal(saved.get('foodmonitor.remote.operator.v1'),operator);
 const second=browser('');assert.equal(second.element('remoteToken').value,operator);assert.equal(second.element('remoteStart-'+a).disabled,true);assert.equal(queued,1);assert.match(second.element('remoteState-'+a).textContent,/명령 전달 대기/);
 second.ctx.clearRemoteToken();assert.equal(saved.size,0);assert.equal(second.element('remoteToken').value,'');
});
// Public, deterministic host-test vectors; these are NOT deployment secrets or live URLs.
const vector=env();vector.queue();const command=vector.exchange(vector.request());
const target=path.join(__dirname,'../data/remote-test-vectors');fs.mkdirSync(target,{recursive:true});
fs.writeFileSync(path.join(target,'command.json'),JSON.stringify(command));
fs.writeFileSync(path.join(target,'canonical.txt'),vector.api.remoteCommandCanonical_(command,command.nonce));
fs.writeFileSync(path.join(target,'mac.txt'),command.mac);
console.log(`${passed} remote cloud tests passed; in-memory services only`);
