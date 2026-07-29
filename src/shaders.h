#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <cstdint>
#pragma comment(lib, "d3dcompiler.lib")

static const char* VS_SRC = R"(
struct VSInput { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOutput { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOutput vs_main(VSInput input) {
    VSOutput o; o.pos = float4(input.pos, 1.0); o.uv = input.uv; return o;
}
)";

static const char* PS_SRC = R"(
struct VSOutput { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);
cbuffer Constants : register(b0) {
    float g_time; float g_hue_amount; float g_hue_speed; float g_contrast;
    float g_saturation; int g_hue_enabled; int g_contrast_enabled;
    int g_saturation_enabled; int g_invert_enabled; int g_grayscale_enabled;
    float g_pixelate_size; int g_blend_enabled; float g_glitch_intensity;
    int g_glitch_enabled; int g_pixelate_enabled; int g_edge_enabled;
    int g_chroma_enabled; float g_chroma_amount; int g_chroma_mode;
    float g_chroma_fade_speed; int g_sharp_enabled; float g_sharp_amount;
    int g_wave_enabled; float g_wave_intensity; float g_wave_speed;
    int g_trail_enabled; float g_trail_decay; float g_trail_opacity;
    int g_pad2;
};
float3 rgb2hsv(float3 c) {
    float4 K=float4(0,-1./3.,2./3.,-1);
    float4 p=lerp(float4(c.bg,K.wz),float4(c.gb,K.xy),step(c.b,c.g));
    float4 q=lerp(float4(p.xyw,c.r),float4(c.r,p.yzx),step(p.x,c.r));
    float d=q.x-min(q.w,q.y); float e=1e-10;
    return float3(abs(q.z+(q.w-q.y)/(6.*d+e)),d/(q.x+e),q.x);
}
float3 hsv2rgb(float3 c) {
    float4 K=float4(1,2./3.,1./3.,3);
    float3 p=abs(frac(c.xxx+K.xyz)*6.-K.www);
    return c.z*lerp(K.xxx,saturate(p-K.xxx),c.y);
}

float4 ps_main(VSOutput input) : SV_TARGET {
    float2 uv = input.uv;
    float3 color = g_texture.Sample(g_sampler, uv).rgb;

    // Screen Wave (apply before other effects for UV distortion)
    if (g_wave_enabled) {
        uv.x += sin(uv.y * 100 + g_time * g_wave_speed) * g_wave_intensity;
        uv.y += sin(uv.x * 100 + g_time * g_wave_speed * 0.7) * g_wave_intensity;
        color = g_texture.Sample(g_sampler, uv).rgb;
    }

    if (g_invert_enabled) color = 1.0 - color;
    if (g_grayscale_enabled) { float l = dot(color, float3(0.299,0.587,0.114)); color = float3(l,l,l); }
    if (g_hue_enabled) { float3 h = rgb2hsv(color); h.x = frac(h.x + g_hue_amount + g_hue_speed * g_time); color = hsv2rgb(h); }
    if (g_saturation_enabled) { float l = dot(color, float3(0.299,0.587,0.114)); color = lerp(float3(l,l,l), color, g_saturation); }
    if (g_contrast_enabled) color = 0.5 + (color - 0.5) * g_contrast;

    // Chromatic Aberration
    if (g_chroma_enabled) {
        float a = g_chroma_amount;
        if (g_chroma_mode == 2) a *= 0.5 + 0.5 * sin(g_time * g_chroma_fade_speed);
        float r = g_texture.Sample(g_sampler, uv + float2(a, 0)).r;
        float b = g_texture.Sample(g_sampler, uv - float2(a, 0)).b;
        color.r = r; color.b = b;
    }

    // Sharpness (unsharp mask)
    if (g_sharp_enabled) {
        float2 px = 1.0 / float2(1920, 1080);
        float3 blur = 0;
        float k[9] = {1,2,1,2,4,2,1,2,1};
        for (int y=-1; y<=1; y++) for (int x=-1; x<=1; x++)
            blur += g_texture.Sample(g_sampler, uv + float2(x,y)*px).rgb * k[(y+1)*3+(x+1)];
        blur /= 16.0;
        color = color + (color - blur) * (g_sharp_amount - 1.0);
    }

    if (g_pixelate_enabled) {
        float2 d = float2(g_pixelate_size, g_pixelate_size) / float2(1920, 1080);
        float2 c = floor(uv / d) * d + d * 0.5;
        color = g_texture.Sample(g_sampler, c).rgb;
    }
    if (g_glitch_enabled) {
        float row = floor(uv.y * 1080);
        float gl = sin(row*3.14159*0.1+g_time*10)*0.5+0.5;
        if (gl > 1-g_glitch_intensity) {
            float off = (sin(row*7+g_time*15)*0.5+0.5)*0.1;
            color = g_texture.Sample(g_sampler, uv+float2(off,0)).rgb;
        }
        if (sin(row*13+g_time*8)*0.5+0.5 > 0.95) color = float3(1,0,0);
    }
    if (g_edge_enabled) {
        float2 px = 1./float2(1920,1080);
        float3 tl=g_texture.Sample(g_sampler,uv+float2(-px.x,-px.y)).rgb;
        float3 tr=g_texture.Sample(g_sampler,uv+float2(px.x,-px.y)).rgb;
        float3 bl=g_texture.Sample(g_sampler,uv+float2(-px.x,px.y)).rgb;
        float3 br=g_texture.Sample(g_sampler,uv+float2(px.x,px.y)).rgb;
        float3 t =g_texture.Sample(g_sampler,uv+float2(0,-px.y)).rgb;
        float3 b =g_texture.Sample(g_sampler,uv+float2(0,px.y)).rgb;
        float3 l =g_texture.Sample(g_sampler,uv+float2(-px.x,0)).rgb;
        float3 r =g_texture.Sample(g_sampler,uv+float2(px.x,0)).rgb;
        float3 gx=tl+2*l+bl-tr-2*r-br;
        float3 gy=tl+2*t+tr-bl-2*b-br;
        color=sqrt(gx*gx+gy*gy);
    }
    if (g_blend_enabled > 0) {
        float3 b = g_texture.Sample(g_sampler, uv).rgb;
        [branch]
        if (g_blend_enabled==1) color=min(color+b,1);
        else if (g_blend_enabled==2) color=1-abs(color*2-1-(b*2-1))*0.5;
        else if (g_blend_enabled==3) color=max(color-b,0);
        else if (g_blend_enabled==4) color=color*b;
        else if (g_blend_enabled==5) color=1-(1-color)*(1-b);
        else if (g_blend_enabled==6) color=abs(color-b);
        else if (g_blend_enabled==7) color=lerp(2*color*b,1-2*(1-color)*(1-b),step(0.5,b));
        else if (g_blend_enabled==8) color = float3((int(color.r*255)&int(b.r*255))/255.0, (int(color.g*255)&int(b.g*255))/255.0, (int(color.b*255)&int(b.b*255))/255.0);
        else if (g_blend_enabled==9) color = float3((int(color.r*255)|int(b.r*255))/255.0, (int(color.g*255)|int(b.g*255))/255.0, (int(color.b*255)|int(b.b*255))/255.0);
    }
    return float4(color, 1.0);
}
)";

// Motion trail: exponential accumulation of frames
// accum = current + accum * decay
// output = lerp(current, accum, opacity)
// With decay=0.5, opacity=1.0:
//   Frame 1: cur1
//   Frame 2: cur2 + cur1*0.5
//   Frame 3: cur3 + cur2*0.5 + cur1*0.25
static const char* TRAIL_PS_SRC = R"(
struct VSOutput { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
Texture2D g_current : register(t0);
Texture2D g_accum   : register(t1);
SamplerState g_sampler : register(s0);
cbuffer Constants : register(b0) { float g_decay; float g_opacity; float2 g_pad; };
float4 ps_main(VSOutput input) : SV_TARGET {
    float3 cur = g_current.Sample(g_sampler, input.uv).rgb;
    float3 acc = g_accum.Sample(g_sampler, input.uv).rgb;
    float3 accum = cur + acc * g_decay;
    float3 result = lerp(cur, accum, g_opacity);
    return float4(result, 1.0);
}
)";

struct ShaderConstants {
    float  g_time;
    float  g_hue_amount;
    float  g_hue_speed;
    float  g_contrast;
    float  g_saturation;
    int    g_hue_enabled;
    int    g_contrast_enabled;
    int    g_saturation_enabled;
    int    g_invert_enabled;
    int    g_grayscale_enabled;
    float  g_pixelate_size;
    int    g_blend_enabled;
    float  g_glitch_intensity;
    int    g_glitch_enabled;
    int    g_pixelate_enabled;
    int    g_edge_enabled;
    int    g_chroma_enabled;
    float  g_chroma_amount;
    int    g_chroma_mode;
    float  g_chroma_fade_speed;
    int    g_sharp_enabled;
    float  g_sharp_amount;
    int    g_wave_enabled;
    float  g_wave_intensity;
    float  g_wave_speed;
    int    g_trail_enabled;
    float  g_trail_decay;
    float  g_trail_opacity;
    int    g_pad2;
};

static_assert(sizeof(ShaderConstants) <= 128, "ShaderConstants too large for HLSL cbuffer");

struct TrailConstants {
    float g_decay, g_opacity, g_pad1, g_pad2;
};

struct CompiledShaders {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11PixelShader* trailPs = nullptr;
    ID3D11InputLayout* input_layout = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11Buffer* trail_buffer = nullptr;
    ID3D11SamplerState* sampler_state = nullptr;
};

bool CompileShaders(ID3D11Device* device, CompiledShaders& out);
void ReleaseShaders(CompiledShaders& s);
