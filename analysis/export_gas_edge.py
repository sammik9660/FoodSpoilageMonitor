"""Export the already trained centroid model to a tiny, heap-free C++ header."""
import argparse,hashlib,json
from pathlib import Path
def main():
    p=argparse.ArgumentParser();p.add_argument('model',type=Path);p.add_argument('header',type=Path);a=p.parse_args()
    m=json.loads(a.model.read_text(encoding='utf8'))
    assert m['model']=='centroid' and len(m['classes'])==6 and len(m['mean'])==len(m['std'])==13
    assert all(len(row)==13 for row in m['weights']['centers']) and not m['release_approved']
    def floats(v):return ','.join(format(float(x),'.9g')+'f' for x in v)
    center=',\n'.join('  {'+floats(row)+'}' for row in m['weights']['centers'])
    support=','.join(str(m['exposure_support'][c]) for c in m['classes'])
    labels=','.join('"'+c+'"' for c in m['classes'])
    text='''#pragma once
#include <cmath>
// Experimental food identity only. Not a spoilage or safety probability.
namespace ai_food {
static constexpr int N=13,K=6;
static constexpr const char* LABELS[K]={'''+labels+'''};
static constexpr unsigned SUPPORT[K]={'''+support+'''};
static constexpr float MEAN[N]={'''+floats(m['mean'])+'''};
static constexpr float STD[N]={'''+floats(m['std'])+'''};
static constexpr float CENTER[K][N]={
'''+center+'''
};
struct Result {bool measured=false,usable=false;int candidate=-1;float distance=0,margin=0;const char* reason="INVALID_INPUT";};
inline Result predict(const float gas[10],float t,float h,float p){
  Result out;float logGas[10],mean=0,low=1e30f,high=-1e30f;unsigned floor=0;
  for(int i=0;i<10;++i){
    if(!std::isfinite(gas[i])||gas[i]<=0)return out;
    if(std::fabs(gas[i]-5.68f)<=0.01f)++floor;
    logGas[i]=std::log10(gas[i]);mean+=logGas[i]/10.f;
    if(logGas[i]<low)low=logGas[i];if(logGas[i]>high)high=logGas[i];
  }
  if(!std::isfinite(t)||!std::isfinite(h)||!std::isfinite(p)||t < -40||t>85||h<0||h>100||p<300||p>1100)return out;
  if(floor>=8||high-low<0.05f){out.reason="LOW_INFORMATION";return out;}
  float z[N];for(int i=0;i<10;++i)z[i]=(logGas[i]-mean-MEAN[i])/STD[i];
  z[10]=(t-MEAN[10])/STD[10];z[11]=(h-MEAN[11])/STD[11];z[12]=(p-MEAN[12])/STD[12];
  float best=1e30f,second=1e30f;
  for(int c=0;c<K;++c){float distance=0;
    for(int i=0;i<N;++i){const float d=z[i]-CENTER[c][i];distance+=d*d;}
    if(distance<best){second=best;best=distance;out.candidate=c;}
    else if(distance<second)second=distance;
  }
  out.measured=true;out.distance=best;out.margin=second-best;
  out.usable=SUPPORT[out.candidate]>=2;out.reason=out.usable?"EXPERIMENTAL_ONLY":"ABSTAIN_ONE_EXPOSURE";
  return out;
}
} // namespace ai_food
'''
    text='// Source food_model.json SHA-256: '+hashlib.sha256(a.model.read_bytes()).hexdigest()+'\n'+text
    a.header.write_text(text,encoding='utf8')
    print('header sha256',hashlib.sha256(text.encode()).hexdigest(),'bytes',len(text.encode()))
if __name__=='__main__':main()
