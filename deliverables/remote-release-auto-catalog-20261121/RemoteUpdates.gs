// Remote control only. Firmware bytes live on an external HTTPS object store.
// Secrets are manually provisioned Script Properties, never source literals.
const RU_PREFIX='REMOTE_JOB_';
const RU_STATES=['RECEIVED','PREPARING','DOWNLOADING','VERIFYING','UPDATING','RECONNECTING','SUCCESS','FAILED'];
const RU_RUN_SECONDS=1200;
const RU_RELEASE_REPO='sammik9660/FoodSpoilageMonitor';
let RU_GITHUB_CATALOG_STATUS='NOT_CHECKED';
function remoteProperty_(key) {
  const cache=CacheService.getScriptCache(),cached=cache.get('RU_PROP:'+key);
  if(cached!==null&&cached!==undefined)return cached;
  const value=PropertiesService.getScriptProperties().getProperty(key)||'';
  cache.put('RU_PROP:'+key,value,60);return value;
}
function remoteHex_(bytes){return bytes.map(x=>('0'+((x+256)%256).toString(16)).slice(-2)).join('');}
function remoteMac_(text,key){return remoteHex_(Utilities.computeHmacSha256Signature(text,key,Utilities.Charset.UTF_8));}
function remoteEqual_(a,b){if(typeof a!=='string'||typeof b!=='string'||a.length!==b.length)return false;let d=0;for(let i=0;i<a.length;i++)d|=a.charCodeAt(i)^b.charCodeAt(i);return d===0;}
function remoteOperator_(token) {
  const expected=PropertiesService.getScriptProperties().getProperty('REMOTE_OPERATOR_SHA256')||'';
  if(!/^[a-f0-9]{64}$/.test(expected)||typeof token!=='string'||!/^[a-fA-F0-9]{64}$/.test(token))throw new Error('원격 관리 인증이 필요합니다 / Remote operator token required');
  const digest=remoteHex_(Utilities.computeDigest(Utilities.DigestAlgorithm.SHA_256,token,Utilities.Charset.UTF_8));
  if(!remoteEqual_(digest,expected))throw new Error('Remote operator authentication failed');
}
function remoteUrl_(url,hosts) {
  if(typeof url!=='string'||url.length>384||!/^[\x21-\x7e]+$/.test(url))throw new Error('Invalid download URL');
  const m=/^https:\/\/([a-z0-9.-]+)(\/[^?#]*)$/.exec(url);
  if(!m||!hosts.includes(m[1])||url.includes('..')||url.includes('%')||url.includes('\\'))throw new Error('Only allowlisted immutable HTTPS paths are accepted (no redirects or signed-query URLs)');
  return url;
}
function remoteVersionOrder_(a,b){const x=a.split('.').map(Number),y=b.split('.').map(Number);for(let i=0;i<3;i++){if(x[i]<y[i])return -1;if(x[i]>y[i])return 1;}return 0;}
function remoteGithubCatalog_() {
  const cache=CacheService.getScriptCache(),key='RU_GITHUB_CATALOG_V2';
  const cached=cache.get(key);
  if(cached){try{const rows=JSON.parse(cached);RU_GITHUB_CATALOG_STATUS=rows.length?'CACHED':'NO_MATCH';return rows;}catch(_){}}
  const fallback=reason=>{
    let rows=[];
    try{rows=JSON.parse(cache.get(key+'_LAST')||'[]');}catch(_){}
    RU_GITHUB_CATALOG_STATUS=rows.length?'STALE_'+reason:reason;
    return rows;
  };
  if(typeof UrlFetchApp==='undefined')return fallback('URLFETCH_UNAVAILABLE');
  try{
    const response=UrlFetchApp.fetch('https://api.github.com/repos/'+RU_RELEASE_REPO+'/releases?per_page=30',{
      muteHttpExceptions:true,followRedirects:false,
      headers:{Accept:'application/vnd.github+json','User-Agent':'FoodSpoilageMonitor-AppsScript'}});
    if(response.getResponseCode()!==200)return fallback('GITHUB_HTTP_'+response.getResponseCode());
    const body=response.getContentText();
    if(body.length>1000000)return fallback('GITHUB_RESPONSE_TOO_LARGE');
    const releases=JSON.parse(body);
    if(!Array.isArray(releases))return fallback('GITHUB_RESPONSE_INVALID');
    const rows=[];
    for(const release of releases){
      if(!release||release.draft!==false||release.prerelease!==false||!Array.isArray(release.assets)||
         release.assets.some(a=>/^HOLD_.*\.txt$/i.test(String(a&&a.name||''))))continue;
      const tag=String(release.tag_name||'');
      if(tag.length>40)continue;
      const esp=/^esp-v(\d+\.\d+\.\d+)-(\d{10})$/.exec(tag);
      const nrf=/^nrf-v(\d+\.\d+\.\d+)$/.exec(tag);
      if(!esp&&!nrf)continue;
      if((esp?esp[1]:nrf[1]).length>23)continue;
      const base='https://github.com/'+RU_RELEASE_REPO+'/releases/download/'+tag+'/';
      const asset=(name,max)=>{
        const matches=release.assets.filter(a=>a&&a.name===name);
        if(matches.length!==1)return null;
        const a=matches[0];
        if(a.state!=='uploaded'||!Number.isInteger(a.size)||a.size<1||a.size>max||
           !/^sha256:[a-f0-9]{64}$/.test(String(a.digest||''))||a.browser_download_url!==base+name)return null;
        return {bytes:a.size,sha256:a.digest.slice(7)};
      };
      if(esp){
        if(remoteVersionOrder_(esp[1],'2.0.28')<0)continue;
        const bin=asset('gateway.ino.bin',3145728),manifest=asset('gateway.ino.manifest',511);
        if(!bin||!manifest)continue;
        rows.push({id:'esp32-'+tag.replace(/\./g,'_'),kind:'esp32',version:esp[1],build:esp[2],
          remote_capable:true,url:base+'gateway.ino.bin',bytes:bin.bytes,sha256:bin.sha256,
          manifest_url:base+'gateway.ino.manifest',manifest_bytes:manifest.bytes,manifest_sha256:manifest.sha256});
      }else{
        const name='bme688-node-'+nrf[1]+'-legacy-dfu.zip',zip=asset(name,831488);
        if(!zip)continue;
        rows.push({id:'nrf-'+tag.replace(/\./g,'_'),kind:'nrf',version:nrf[1],
          url:base+name,bytes:zip.bytes,sha256:zip.sha256});
      }
    }
    const text=JSON.stringify(rows.slice(0,8));
    try{cache.put(key,text,600);cache.put(key+'_LAST',text,21600);}catch(_){}
    RU_GITHUB_CATALOG_STATUS=rows.length?'LIVE':'NO_MATCH';
    return rows;
  }catch(e){return fallback(/authoriz|permission|scope/i.test(String(e))?'URLFETCH_AUTH_REQUIRED':'GITHUB_REQUEST_FAILED');}
}
function diagnosePublicReleaseCatalog() {
  const probe=UrlFetchApp.fetch('https://api.github.com/repos/'+RU_RELEASE_REPO+'/releases?per_page=1',{
    muteHttpExceptions:true,followRedirects:false,headers:{Accept:'application/vnd.github+json'}});
  if(probe.getResponseCode()!==200)throw new Error('GitHub API HTTP '+probe.getResponseCode()+': '+probe.getContentText().slice(0,300));
  const releases=remoteGithubCatalog_();
  const result={status:RU_GITHUB_CATALOG_STATUS,releases:releases.map(r=>r.id)};
  console.log(JSON.stringify(result));
  return result;
}
function remoteCatalog_() {
  const hosts=(remoteProperty_('REMOTE_DOWNLOAD_HOSTS')||'').split(',').map(x=>x.trim()).filter(Boolean);
  RU_GITHUB_CATALOG_STATUS=hosts.includes('github.com')?'NOT_CHECKED':'GITHUB_HOST_NOT_ALLOWED';
  const configured=JSON.parse(remoteProperty_('REMOTE_UPDATE_CATALOG')||'[]');
  if(!Array.isArray(configured)||configured.length>8)throw new Error('Remote catalog limit is eight releases');
  const configuredIds=new Set();
  for(const release of configured){
    if(release&&configuredIds.has(release.id))throw new Error('Invalid/duplicate release ID');
    configuredIds.add(release&&release.id);
  }
  const list=[],known=new Set();
  const available=(hosts.includes('github.com')?remoteGithubCatalog_():[]).concat(configured);
  for(const release of available){
    if(list.length>=8)break;
    if(!release||!known.has(release.id)){list.push(release);known.add(release&&release.id);}
  }
  const ids=new Set();
  return list.map(item=>{
    const r=Object.assign({},item);
    if(!/^[a-zA-Z0-9_-]{1,48}$/.test(r.id)||ids.has(r.id))throw new Error('Invalid/duplicate release ID');ids.add(r.id);
    if(!['esp32','nrf'].includes(r.kind)||!/^\d+\.\d+\.\d+$/.test(r.version)||r.version.length>23)throw new Error('Invalid release target/version');
    if(!Number.isInteger(r.bytes)||r.bytes<1||r.bytes>(r.kind==='esp32'?3145728:831488)||!/^[a-f0-9]{64}$/.test(r.sha256))throw new Error('Invalid release size/hash');
    remoteUrl_(r.url,hosts);
    if(r.kind==='esp32'){
      if(typeof r.remote_capable!=='boolean')throw new Error('ESP catalog must declare remote_capable');
      if(!/^\d{10}$/.test(String(r.build))||!Number.isInteger(r.manifest_bytes)||r.manifest_bytes<1||r.manifest_bytes>511||!/^[a-f0-9]{64}$/.test(r.manifest_sha256))throw new Error('Invalid ESP manifest metadata');
      remoteUrl_(r.manifest_url,hosts);
    }else{r.build=0;r.manifest_url='';r.manifest_sha256='';r.manifest_bytes=0;}
    return r;
  });
}
function remoteLoad_(device,fresh) {const cache=CacheService.getScriptCache(),hit=!fresh&&cachedObject(cache,RU_PREFIX+device);if(hit)return hit;const text=PropertiesService.getScriptProperties().getProperty(RU_PREFIX+device)||'{}';try{cache.put(RU_PREFIX+device,text,600);}catch(_){}return JSON.parse(text);}
function remoteSave_(device,value) {
  const text=JSON.stringify(value);if(text.length>8000)throw new Error('Remote record is too large');
  PropertiesService.getScriptProperties().setProperty(RU_PREFIX+device,text);
  try{CacheService.getScriptCache().put(RU_PREFIX+device,text,600);}catch(_){try{CacheService.getScriptCache().remove(RU_PREFIX+device);}catch(_){}}
}
function remoteBusy_(job,now){return job&&job.command&&!['SUCCESS','FAILED'].includes(job.state)&&now<job.command.expires+RU_RUN_SECONDS;}
function remoteReservationActive_(device){return remoteBusy_(remoteLoad_(device,true),Math.floor(Date.now()/1000));}
function remoteSummary_(device) {
  try {
  const j=remoteLoad_(device),peer=cachedObject(CacheService.getScriptCache(),'RU_PEER_'+device);
  const authenticated=!!peer&&Date.now()-peer.at<90000;
  if(!j.command)return {state:'IDLE',authenticated,peer};
  const expired=!['SUCCESS','FAILED'].includes(j.state)&&Date.now()/1000>=j.command.expires+RU_RUN_SECONDS;
  return {authenticated,peer,id:j.command.id,kind:j.command.kind,version:j.command.version,state:expired?'UNCONFIRMED':j.state,
    percent:j.percent||0,error:j.error||'',updated_at:j.updated_at||null,
    firmware:j.firmware||'',build:j.build||'',sensor_version:j.sensor_version||'',history:j.history||[],reserved:remoteBusy_(j,Math.floor(Date.now()/1000)),expires_at:j.command.expires*1000};
  }catch(_){return {state:'UNKNOWN',reserved:true,error:'REMOTE_STATE_UNAVAILABLE'};}
}
function getRemoteUpdateCatalog(token) {
  remoteOperator_(token);
  const releases=remoteCatalog_();
  if(!['LIVE','CACHED','GITHUB_HOST_NOT_ALLOWED'].includes(RU_GITHUB_CATALOG_STATUS)&&
     !RU_GITHUB_CATALOG_STATUS.startsWith('STALE_')){
    throw new Error('GitHub 자동 패키지 조회 실패 ('+RU_GITHUB_CATALOG_STATUS+'). 기존 등록 목록은 보존됩니다.');
  }
  return releases.map(r=>({id:r.id,kind:r.kind,version:r.version,build:r.build,bytes:r.bytes,sha256:r.sha256,remote_capable:r.kind==='esp32'?r.remote_capable:true}));
}
function queueRemoteUpdate(device,releaseId,allowDowngrade,approvalId,approvedAt,token) {
  remoteOperator_(token);device=validDeviceId(device);
  if(device==='legacy'||!/^[a-f0-9]{32}$/.test(approvalId)||!Number.isFinite(approvedAt)||Math.abs(Date.now()-approvedAt)>120000)throw new Error('Invalid or expired approval');
  const key=remoteProperty_('REMOTE_KEY_'+device);if(!/^[a-fA-F0-9]{64}$/.test(key))throw new Error('Device command key is not provisioned');
  const release=remoteCatalog_().find(r=>r.id===releaseId);if(!release)throw new Error('Release not found');
  const attempt=tryLocked(()=>{
    const old=remoteLoad_(device,true),now=Math.floor(Date.now()/1000);
    if(old.approval_id===approvalId)return remoteSummary_(device);
    if((old.approvals||[]).includes(approvalId))throw new Error('Approval was already processed');
    if(remoteBusy_(old,now))throw new Error('An update is already queued or running');
    if(old.approved_at&&Date.now()-old.approved_at<30000)throw new Error('Wait 30 seconds before another update approval');
    const ctx=sheets();recoverTransactions(ctx);
    if(activeSession(sessionsList(ctx.sessions),device))throw new Error('실험 종료 후 업데이트하세요 / Recording must be STOPPED');
    const row=dataRows(ctx.devices).find(r=>r[0]===device);
    if(!row)throw new Error('Device is not registered');
    const h=cachedObject(CacheService.getScriptCache(),'LIVE_V2:'+device)||{};
    const peer=cachedObject(CacheService.getScriptCache(),'RU_PEER_'+device);
    if(!peer||Date.now()-peer.at>=90000)throw new Error('Wait for an authenticated remote-capable device heartbeat');
    if(release.kind==='esp32'){
      const order=remoteVersionOrder_(release.version,peer.firmware);
      if(order===0)throw new Error('Same ESP firmware version; running firmware will be kept');
      if((order<0||Number(release.build)<=Number(peer.build)||!release.remote_capable)&&allowDowngrade!==true)throw new Error('Explicit downgrade/legacy-target approval required');
    }
    if(h.ota_state==='UPDATING'||h.ota_state==='VERIFYING')throw new Error('A local update is active');
    if(release.kind==='nrf'&&!/^[a-f0-9]{16}$/.test(String(row[4])))throw new Error('No known target sensor ID');
    const seq=(old.command?old.command.seq:0)+1;if(seq>4294967295)throw new Error('Remote sequence exhausted');
    const command={device,id:Utilities.getUuid().replace(/-/g,''),seq,issued:now,expires:now+600,
      kind:release.kind,sensor:release.kind==='nrf'?row[4]:'',version:release.version,build:String(release.build),
      url:release.url,sha256:release.sha256,bytes:release.bytes,manifest_url:release.manifest_url||'',
      manifest_sha256:release.manifest_sha256||'',manifest_bytes:release.manifest_bytes||0,allow_downgrade:allowDowngrade===true};
    remoteSave_(device,{command,state:'QUEUED',percent:0,error:'',updated_at:Date.now(),approval_id:approvalId,approved_at:Date.now(),approvals:(old.approvals||[]).concat(old.approval_id?[old.approval_id]:[]).slice(-5),history:(old.history||[]).concat(old.command?[{id:old.command.id,kind:old.command.kind,version:old.command.version,state:['SUCCESS','FAILED'].includes(old.state)?old.state:'UNCONFIRMED',error:old.error,at:old.updated_at}]:[]).slice(-5)});
    return remoteSummary_(device);
  });
  if(!attempt.acquired)throw new Error('Cloud is busy; retry approval later');return attempt.value;
}
function remoteRequestCanonical_(device,r){return ['FSGWREQUEST1',device,r.nonce,r.at,r.id,r.seq,r.state,r.percent,r.error,r.firmware,r.build,r.sensor_version,''].join('\n');}
function remoteCommandCanonical_(c,nonce){return ['FSGWREMOTE1',c.device,c.id,c.seq,nonce,c.issued,c.expires,c.kind,c.sensor,c.version,c.build,c.url,c.sha256,c.bytes,c.manifest_url,c.manifest_sha256,c.manifest_bytes,c.allow_downgrade?'1':'0',''].join('\n');}
function remoteExchange_(payload) {
  const device=validDeviceId(payload.device_id),r=payload.remote;
  if(!r||device==='legacy')return null;
  const key=remoteProperty_('REMOTE_KEY_'+device);
  if(!/^[a-fA-F0-9]{64}$/.test(key))return null;
  if(!/^[a-f0-9]{32}$/.test(r.nonce)||!Number.isInteger(r.at)||Math.abs(Date.now()/1000-r.at)>300||
     !/^(|[a-f0-9]{32})$/.test(r.id)||!Number.isInteger(r.seq)||r.seq<0||r.seq>4294967295||
     !['IDLE'].concat(RU_STATES).includes(r.state)||!Number.isInteger(r.percent)||r.percent<0||r.percent>100||
     !/^[A-Z0-9_]{0,48}$/.test(r.error)||!/^\d+\.\d+\.\d+$/.test(r.firmware)||r.firmware.length>23||
     !/^\d{10}$/.test(r.build)||!/^([0-9]+\.[0-9]+\.[0-9]+)?$/.test(r.sensor_version)||r.sensor_version.length>23||
     !remoteEqual_(remoteMac_(remoteRequestCanonical_(device,r),key),r.mac))return null;
  CacheService.getScriptCache().put('RU_PEER_'+device,JSON.stringify({at:r.at*1000,firmware:r.firmware,build:r.build,sensor_version:r.sensor_version}),120);
  const attempt=tryLocked(()=>{
    const j=remoteLoad_(device,true),c=j.command;if(!c)return null;
    if(r.state==='SUCCESS'&&c.id===r.id&&c.seq===r.seq&&(c.kind==='esp32'?(r.firmware!==c.version||r.build!==c.build):r.sensor_version!==c.version)){r.state='FAILED';r.error='VERSION_MISMATCH';}
    if(c.id===r.id&&c.seq===r.seq&&!['SUCCESS','FAILED'].includes(j.state)&&RU_STATES.includes(r.state)) {
      // Never regress an accepted phase or mistake stale/unsigned health for a result.
      if(RU_STATES.indexOf(r.state)>=RU_STATES.indexOf(j.state)||j.state==='QUEUED') {
        if(j.state!==r.state||j.error!==r.error||r.percent>= (j.percent||0)+5) {
          j.state=r.state;j.percent=r.percent;j.error=r.error;j.updated_at=Date.now();j.firmware=r.firmware;j.build=r.build;j.sensor_version=r.sensor_version;remoteSave_(device,j);
        }
      }
    }
    if(c.seq<=r.seq||Date.now()/1000>c.expires||['SUCCESS','FAILED'].includes(j.state))return null;
    const command=Object.assign({},c);command.nonce=r.nonce;command.mac=remoteMac_(remoteCommandCanonical_(command,r.nonce),key);
    return command;
  });
  return attempt.acquired?attempt.value:null; // No waitLock/sleep/retry inside a cloud execution.
}
