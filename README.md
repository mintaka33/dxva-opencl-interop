# dxva-opencl-interop
DXVA video acceleration + OpenCL compute interoperation

## note

To make sure real resource sharing (without additional copy) between d3d11 and opencl, we need to set **MiscFlags** as **D3D11_RESOURCE_MISC_SHARED** when create ID3D11Texture2D for decode RT, or OpenCL runtime/driver will invoke D3D11 CopySubresourceRegion API to copy the decode RT into an intermediate surface in **clEnqueueAcquireD3D11ObjectsKHR** call and copy back to decode RT surface in **clEnqueueReleaseD3D11ObjectsKHR** call. The two copy operations fianlly trigger D3D driver to submit two GPU commands in Render engine.

```c++
    ID3D11Texture2D *pSurfaceDecodeNV12 = NULL;
    D3D11_TEXTURE2D_DESC descRT = { 0 };
    descRT.Width = dxvaDecData.picWidth;
    descRT.Height = dxvaDecData.picHeight;
    descRT.MipLevels = 1;
    descRT.ArraySize = 1;
    descRT.Format = DXGI_FORMAT_NV12;
    descRT.SampleDesc = { 1, 0 }; 
    descRT.Usage = D3D11_USAGE_DEFAULT; 
    descRT.BindFlags = D3D11_BIND_DECODER;
    descRT.CPUAccessFlags = 0;
    // set MiscFlags as shared to avoid additional copy
    descRT.MiscFlags = D3D11_RESOURCE_MISC_SHARED; 
    hr = pD3D11Device->CreateTexture2D(&descRT, NULL, &pSurfaceDecodeNV12);
    CHECK_SUCCESS(hr, "CreateTexture2D");
```
