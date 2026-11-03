// Optional private HTTPS download adapter. No deployment or credentials included.
// Cloudflare bindings: RELEASES (R2 bucket), DOWNLOAD_TOKEN (64-hex Worker secret).
function authorized(request, token) {
  if (typeof token !== 'string' || !/^[a-fA-F0-9]{64}$/.test(token)) return false;
  const expected='Bearer '+token, actual=request.headers.get('Authorization')||'';
  if(actual.length!==expected.length)return false;
  let difference=0;for(let i=0;i<actual.length;i++)difference|=actual.charCodeAt(i)^expected.charCodeAt(i);
  return difference===0;
}
export default {
  async fetch(request,env) {
    if(!authorized(request,env.DOWNLOAD_TOKEN))return new Response('Unauthorized',{status:401});
    if(!['GET','HEAD'].includes(request.method))return new Response('Read only',{status:405});
    const url=new URL(request.url),key=url.pathname.slice(1);
    if(url.protocol!=='https:'||url.search||!/^(releases\/)[a-zA-Z0-9_./-]+$/.test(key)||key.includes('..'))return new Response('Invalid path',{status:400});
    const metadata=await env.RELEASES.head(key);
    if(!metadata)return new Response('Not found',{status:404});
    if(metadata.size<1||metadata.size>3145728)return new Response('Invalid size',{status:413});
    let start=0,end=metadata.size-1,partial=false;
    const range=request.headers.get('Range');
    if(range){const match=/^bytes=(\d+)-(\d+)$/.exec(range);
      if(!match)return new Response('Invalid range',{status:416});
      start=Number(match[1]);end=Number(match[2]);partial=true;
      if(!Number.isSafeInteger(start)||!Number.isSafeInteger(end)||start>end||end>=metadata.size||end-start+1>131072)return new Response('Invalid range',{status:416});
    }
    const headers=new Headers({'Content-Type':'application/octet-stream','Content-Length':String(end-start+1),'Accept-Ranges':'bytes','Cache-Control':'private, no-store','ETag':metadata.httpEtag});
    if(partial)headers.set('Content-Range',`bytes ${start}-${end}/${metadata.size}`);
    if(request.method==='HEAD')return new Response(null,{headers,status:partial?206:200});
    const object=await env.RELEASES.get(key,partial?{range:{offset:start,length:end-start+1}}:{});
    if(!object||!object.body||object.httpEtag!==metadata.httpEtag)return new Response('Object changed; use immutable release paths',{status:409});
    return new Response(object.body,{headers,status:partial?206:200});
  }
};
