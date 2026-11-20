// Source food_model.json SHA-256: 81894648c5b8f15a07e41b4675f98b2103843de62e68dc28036de146739ae224
#pragma once
#include <cmath>
// Experimental food identity only. Not a spoilage or safety probability.
namespace ai_food {
static constexpr int N=13,K=6;
static constexpr const char* LABELS[K]={"banana","chicken","egg","rice","sprouts","tofu"};
static constexpr unsigned SUPPORT[K]={2,2,1,2,1,2};
static constexpr float MEAN[N]={-0.63369045f,0.964185587f,0.892855474f,0.750926478f,-0.163962818f,-0.187633921f,-0.207193207f,-0.551543013f,-0.456034153f,-0.407909978f,18.3636895f,95.604108f,1013.42636f};
static constexpr float STD[N]={0.220368965f,0.290698211f,0.282422447f,0.279971646f,0.0785968522f,0.0869549893f,0.0962780012f,0.216288814f,0.21413351f,0.216902135f,9.70408746f,4.27809389f,6.45893189f};
static constexpr float CENTER[K][N]={
  {0.619472357f,-0.0998588703f,-0.180825757f,-0.0771646834f,0.609153379f,0.341525838f,0.169684451f,0.0881702034f,-0.248696386f,-0.43585695f,0.672934498f,-1.31575709f,-0.551263925f},
  {1.22946487f,-1.44181722f,-1.63773884f,-1.59629976f,0.348345842f,0.716386054f,0.935326495f,1.36219078f,1.35884127f,1.34773684f,-0.47828089f,0.168093981f,0.0218551328f},
  {0.672729996f,-0.59786932f,-0.167917787f,-0.309716513f,-1.56508855f,-1.39238398f,-1.21256519f,0.760551154f,0.819488568f,0.832342077f,1.03356095f,0.725041871f,-0.760563413f},
  {-1.01905584f,0.814781647f,0.788096087f,0.849779032f,0.560348097f,0.362822281f,0.20926919f,-0.951317532f,-0.873452298f,-0.810141514f,-0.0271337121f,-0.229845453f,0.802642295f},
  {-0.914901365f,1.12433826f,1.10210864f,1.01338637f,0.00552215688f,-0.100506887f,-0.193398907f,-1.04520802f,-1.07433484f,-1.09341211f,-1.09938322f,0.931637803f,-0.320168237f},
  {0.16021337f,-0.0883024746f,-0.34716979f,-0.375123697f,-0.520135241f,-0.470983569f,-0.396591499f,0.384897878f,0.504459135f,0.563311874f,-0.119621779f,-1.00926866f,-0.20158929f}
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
