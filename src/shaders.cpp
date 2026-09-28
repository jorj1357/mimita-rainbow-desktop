#include "shaders.h"
#include "log.h"

bool CompileShaderFromString(const char* src, const char* entry, const char* target,
                              ID3DBlob** blob) {
    ID3DBlob* error = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                             entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, &error);
    if (FAILED(hr)) {
        if (error) {
            Log::Write("HLSL %s %s error:\n%s", entry, target, (const char*)error->GetBufferPointer());
            error->Release();
        } else Log::HR("D3DCompile", hr);
        return false;
    }
    if (error) error->Release();
    return true;
}

bool CompileShaders(ID3D11Device* device, CompiledShaders& out) {
    ID3DBlob* vs_blob = nullptr;
    if (!CompileShaderFromString(VS_SRC, "vs_main", "vs_5_0", &vs_blob)) return false;

    HRESULT hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &out.vs);
    if (FAILED(hr)) { Log::HR("CreateVS", hr); vs_blob->Release(); return false; }

    D3D11_INPUT_ELEMENT_DESC lay[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    hr = device->CreateInputLayout(lay, 2, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &out.input_layout);
    if (FAILED(hr)) { Log::HR("CreateIL", hr); vs_blob->Release(); return false; }
    vs_blob->Release();

    // Main pixel shader
    ID3DBlob* ps_blob = nullptr;
    if (!CompileShaderFromString(PS_SRC, "ps_main", "ps_5_0", &ps_blob)) return false;
    hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &out.ps);
    ps_blob->Release();
    if (FAILED(hr)) { Log::HR("CreatePS", hr); return false; }

    // Trail pixel shader
    ID3DBlob* trail_blob = nullptr;
    if (!CompileShaderFromString(TRAIL_PS_SRC, "ps_main", "ps_5_0", &trail_blob)) return false;
    hr = device->CreatePixelShader(trail_blob->GetBufferPointer(), trail_blob->GetBufferSize(), nullptr, &out.trailPs);
    trail_blob->Release();
    if (FAILED(hr)) { Log::HR("CreateTrailPS", hr); return false; }

    // Temporal composite pixel shader
    ID3DBlob* temporal_blob = nullptr;
    if (!CompileShaderFromString(TEMPORAL_PS_SRC, "ps_main", "ps_5_0", &temporal_blob)) return false;
    hr = device->CreatePixelShader(temporal_blob->GetBufferPointer(), temporal_blob->GetBufferSize(), nullptr, &out.temporalPs);
    temporal_blob->Release();
    if (FAILED(hr)) { Log::HR("CreateTemporalPS", hr); return false; }

    // Constant buffer (main) — must match HLSL cbuffer size (13 × 16 = 208)
    D3D11_BUFFER_DESC cb = {};
    cb.ByteWidth = 512; cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cb, nullptr, &out.constant_buffer);
    if (FAILED(hr)) { Log::HR("CreateCB", hr); return false; }

    // Trail constant buffer
    cb.ByteWidth = 16; // 4 floats
    hr = device->CreateBuffer(&cb, nullptr, &out.trail_buffer);
    if (FAILED(hr)) { Log::HR("CreateTrailCB", hr); return false; }

    // Temporal constant buffer (opacity + debug mode + 3 debug colors + 2 padding = 32 bytes)
    cb.ByteWidth = 32;
    hr = device->CreateBuffer(&cb, nullptr, &out.temporal_buffer);
    if (FAILED(hr)) { Log::HR("CreateTemporalCB", hr); return false; }

    // Sampler
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sd, &out.sampler_state);
    if (FAILED(hr)) { Log::HR("CreateSS", hr); return false; }

    Log::Write("shaders compiled OK");
    return true;
}

void ReleaseShaders(CompiledShaders& s) {
    if (s.vs) s.vs->Release();
    if (s.ps) s.ps->Release();
    if (s.trailPs) s.trailPs->Release();
    if (s.temporalPs) s.temporalPs->Release();
    if (s.input_layout) s.input_layout->Release();
    if (s.constant_buffer) s.constant_buffer->Release();
    if (s.trail_buffer) s.trail_buffer->Release();
    if (s.temporal_buffer) s.temporal_buffer->Release();
    if (s.sampler_state) s.sampler_state->Release();
    s = {};
}
