#include <stdio.h>
#include <stdlib.h>

#include "dxva_dec.h"
#include "ocl.h"

int main(char argc, char** argv)
{
    cl_int error = CL_SUCCESS;
    ocl_args_d_t ocl;
    cl_mem sharedDecodeRT;
    cl_device_type deviceType = CL_DEVICE_TYPE_GPU;

    DXVAData dxvaDecData = g_dxvaDataAVC_Short;
    HRESULT hr = S_OK;
    ID3D11Device *pD3D11Device = NULL;
    ID3D11DeviceContext *pDeviceContext = NULL;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1 };
    D3D_FEATURE_LEVEL fl;
    GUID profile = dxvaDecData.guidDecoder;

    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &pD3D11Device, &fl, &pDeviceContext);

    ID3D11VideoDevice * pD3D11VideoDevice = NULL;
    if (SUCCEEDED(hr))
    {
        hr = pD3D11Device->QueryInterface(&pD3D11VideoDevice);
    }

    //initialize Open CL objects (context, queue, etc.)
    if (CL_SUCCESS != SetupOpenCL(&ocl, deviceType, pD3D11Device))
    {
        return -1;
    }

    ID3D11VideoDecoder *pVideoDecoder = NULL;
    if (SUCCEEDED(hr))
    {
        D3D11_VIDEO_DECODER_DESC desc = { 0 };
        desc.Guid = profile;
        desc.SampleWidth = dxvaDecData.picWidth;
        desc.SampleHeight = dxvaDecData.picHeight;
        desc.OutputFormat = DXGI_FORMAT_NV12;
        D3D11_VIDEO_DECODER_CONFIG config = { 0 };
        config.ConfigBitstreamRaw = dxvaDecData.isShortFormat; // 0: long format; 1: short format
        hr = pD3D11VideoDevice->CreateVideoDecoder(&desc, &config, &pVideoDecoder);
    }

    ID3D11Texture2D *pSurfaceDecodeNV12 = NULL;
    if (SUCCEEDED(hr))
    {
        D3D11_TEXTURE2D_DESC descRT = { 0 };
        descRT.Width = dxvaDecData.picWidth;
        descRT.Height = dxvaDecData.picHeight;
        descRT.MipLevels = 1;
        descRT.ArraySize = 1;
        descRT.Format = DXGI_FORMAT_NV12;
        descRT.SampleDesc = { 1, 0 }; // DXGI_SAMPLE_DESC 
        descRT.Usage = D3D11_USAGE_DEFAULT; // D3D11_USAGE 
        descRT.BindFlags = D3D11_BIND_DECODER;
        descRT.CPUAccessFlags = 0;
        descRT.MiscFlags = 0;
        hr = pD3D11Device->CreateTexture2D(&descRT, NULL, &pSurfaceDecodeNV12);
    }

    if (SUCCEEDED(hr))
    {
        sharedDecodeRT = clCreateFromD3D11Texture2DKHR(ocl.context, CL_MEM_READ_WRITE, pSurfaceDecodeNV12, 0, &error);
        CL_CHECK_AND_RETURN(error);
    }

    ID3D11Texture2D *pSurfaceCopyStaging = NULL;
    if (SUCCEEDED(hr))
    {
        D3D11_TEXTURE2D_DESC descRT = { 0 };
        descRT.Width = dxvaDecData.picWidth;
        descRT.Height = dxvaDecData.picHeight;
        descRT.MipLevels = 1;
        descRT.ArraySize = 1;
        descRT.Format = DXGI_FORMAT_NV12;
        descRT.SampleDesc = { 1, 0 }; // DXGI_SAMPLE_DESC 
        descRT.Usage = D3D11_USAGE_STAGING; // D3D11_USAGE 
        descRT.BindFlags = 0;
        descRT.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        descRT.MiscFlags = 0;
        hr = pD3D11Device->CreateTexture2D(&descRT, NULL, &pSurfaceCopyStaging);
    }

    ID3D11VideoDecoderOutputView *pDecodeOutputView = NULL;
    if (SUCCEEDED(hr))
    {
        D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC desc = { 0 };
        desc.DecodeProfile = profile;
        desc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
        hr = pD3D11VideoDevice->CreateVideoDecoderOutputView(pSurfaceDecodeNV12, &desc, &pDecodeOutputView);
    }

    UINT profileCount = 0;
    GUID decoderGUID = {};
    if (SUCCEEDED(hr))
    {
        profileCount = pD3D11VideoDevice->GetVideoDecoderProfileCount();
        printf("INFO: Decoder Profile Count = %d\n", profileCount);

        for (UINT i = 0; i < profileCount; i++)
        {
            hr = pD3D11VideoDevice->GetVideoDecoderProfile(i, &decoderGUID);
            if (SUCCEEDED(hr))
            {
                OLECHAR sGUID[64] = { 0 };
                StringFromGUID2(decoderGUID, sGUID, 64);
                wprintf(L"INFO: Index %02d - GUID = %s\n", i, sGUID);
            }
        }
    }

    ID3D11VideoContext* pVideoContext = NULL;
    if (SUCCEEDED(hr))
    {
        hr = pDeviceContext->QueryInterface(&pVideoContext);
    }

    if (SUCCEEDED(hr))
    {
        hr = pVideoContext->DecoderBeginFrame(pVideoDecoder, pDecodeOutputView, 0, 0);
    }

    if (SUCCEEDED(hr))
    {
        UINT sizeDesc = sizeof(D3D11_VIDEO_DECODER_BUFFER_DESC) * dxvaDecData.dxvaBufNum;
        D3D11_VIDEO_DECODER_BUFFER_DESC *descDecBuffers = new D3D11_VIDEO_DECODER_BUFFER_DESC[dxvaDecData.dxvaBufNum];
        memset(descDecBuffers, 0, sizeDesc);

        for (UINT i = 0; i < dxvaDecData.dxvaBufNum; i++)
        {
            BYTE* buffer = 0;
            UINT bufferSize = 0;
            descDecBuffers[i].BufferIndex = i;
            descDecBuffers[i].BufferType = dxvaDecData.dxvaDecBuffers[i].bufType;
            descDecBuffers[i].DataSize = dxvaDecData.dxvaDecBuffers[i].bufSize;

            hr = pVideoContext->GetDecoderBuffer(pVideoDecoder, descDecBuffers[i].BufferType, &bufferSize, reinterpret_cast<void**>(&buffer));
            if (SUCCEEDED(hr))
            {
                UINT copySize = min(bufferSize, descDecBuffers[i].DataSize);
                memcpy_s(buffer, copySize, dxvaDecData.dxvaDecBuffers[i].pBufData, copySize);
                hr = pVideoContext->ReleaseDecoderBuffer(pVideoDecoder, descDecBuffers[i].BufferType);
            }
        }

        hr = pVideoContext->SubmitDecoderBuffers(pVideoDecoder, dxvaDecData.dxvaBufNum, descDecBuffers);
        delete[] descDecBuffers;
    }

    if (SUCCEEDED(hr))
    {
        hr = pVideoContext->DecoderEndFrame(pVideoDecoder);
    }

    return 0;
}
