#include "Render/FixedVrs.hpp"
#include <algorithm>
#include <cmath>

namespace cvr::vrs {
Settings Normalize(Settings s) {
    const auto percent=[](float x,float fallback){return std::isfinite(x)?std::clamp(x,20.f,100.f):fallback;};
    s.mode=std::clamp(s.mode,0,2);s.width=percent(s.width,60);s.height=percent(s.height,60);
    const float x=(100-s.width)*.5f,y=(100-s.height)*.5f;
    s.offsetX=std::clamp(std::isfinite(s.offsetX)?s.offsetX:0.f,-x,x);
    s.offsetY=std::clamp(std::isfinite(s.offsetY)?s.offsetY:0.f,-y,y);
    return s;
}
bool ParseSetting(const char* line,Settings& s) {
    if(sscanf_s(line,"xr_vrs = %d",&s.mode)==1)return true;
    if(sscanf_s(line,"xr_vrs_width = %f",&s.width)==1)return true;
    if(sscanf_s(line,"xr_vrs_height = %f",&s.height)==1)return true;
    if(sscanf_s(line,"xr_vrs_offset_x = %f",&s.offsetX)==1)return true;
    return sscanf_s(line,"xr_vrs_offset_y = %f",&s.offsetY)==1;
}
void WriteSettings(FILE* file,Settings s) {
    s=Normalize(s);
    std::fprintf(file,"xr_vrs=%d\nxr_vrs_width=%.2f\nxr_vrs_height=%.2f\nxr_vrs_offset_x=%.2f\nxr_vrs_offset_y=%.2f\n",
        s.mode,s.width,s.height,s.offsetX,s.offsetY);
}
std::vector<uint8_t> BuildRates(Settings s,unsigned w,unsigned h,unsigned tile,bool additional) {
    if(!w || !h || w>16384 || h>16384 || tile<8 || tile>32 || (tile&(tile-1)))return {};
    s=Normalize(s);const unsigned columns=(w+tile-1)/tile,rows=(h+tile-1)/tile;
    std::vector<uint8_t> result(size_t(columns)*rows,D3D12_SHADING_RATE_1X1);
    if(!s.mode)return result;
    const bool isStereo=(w>=(h*3u)/2u);
    const float eyeW=isStereo?(float(w)*.5f):float(w);
    const float eyeH=float(h);
    const float rx_inner=eyeW*(s.width*.5f)/100.f;
    const float ry_inner=eyeH*(s.height*.5f)/100.f;
    const float rx_outer=rx_inner*1.30f;
    const float ry_outer=ry_inner*1.30f;
    const float cY=eyeH*(.5f+s.offsetY/100.f);
    const float cL_x=eyeW*(.5f+s.offsetX/100.f);
    const float cR_x=isStereo?(eyeW+cL_x):cL_x;

    auto getEyeRate=[&](float centerX,float centerY,float x0,float y0,float x1,float y1) -> uint8_t {
        const float qx=std::clamp(centerX,x0,x1);
        const float qy=std::clamp(centerY,y0,y1);
        const float dx=(qx-centerX)/rx_inner;
        const float dy=(qy-centerY)/ry_inner;
        if(dx*dx+dy*dy<=1.0f)return D3D12_SHADING_RATE_1X1;
        const float dx_out=(qx-centerX)/rx_outer;
        const float dy_out=(qy-centerY)/ry_outer;
        if(dx_out*dx_out+dy_out*dy_out<=1.0f)return D3D12_SHADING_RATE_2X2;
        return uint8_t(s.mode==2 && additional ? D3D12_SHADING_RATE_4X4 : D3D12_SHADING_RATE_2X2);
    };

    for(unsigned y=0;y<rows;++y) {
        for(unsigned x=0;x<columns;++x) {
            const float x0=float(x*tile),y0=float(y*tile);
            const float x1=float(std::min(w,(x+1)*tile)),y1=float(std::min(h,(y+1)*tile));
            if(isStereo) {
                result[size_t(y)*columns+x]=std::min(
                    getEyeRate(cL_x,cY,x0,y0,x1,y1),
                    getEyeRate(cR_x,cY,x0,y0,x1,y1)
                );
            } else {
                result[size_t(y)*columns+x]=getEyeRate(cL_x,cY,x0,y0,x1,y1);
            }
        }
    }
    return result;
}
bool EligiblePipeline(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d) {
    if(d.NumRenderTargets<3 || d.NumRenderTargets>4 || !d.PS.BytecodeLength || !d.VS.BytecodeLength ||
       !d.DepthStencilState.DepthEnable || d.SampleDesc.Count!=1 || d.SampleDesc.Quality ||
       d.BlendState.AlphaToCoverageEnable || d.RasterizerState.ForcedSampleCount || d.GS.BytecodeLength || d.HS.BytecodeLength || d.DS.BytecodeLength)return false;
    for(UINT i=0;i<d.NumRenderTargets;++i) {
        const auto& b=d.BlendState.RenderTarget[d.BlendState.IndependentBlendEnable?i:0];
        if(b.BlendEnable || b.LogicOpEnable)return false;
    }
    return true;
}
}
