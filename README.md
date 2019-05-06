# dxva-opencl-interop
DXVA video acceleration + OpenCL compute interoperation

## 1. set D3D11_RESOURCE_MISC_SHARED flag

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

## 2. set CL_CONTEXT_INTEROP_USER_SYNC as ture to enable user sync

The Direct3D 11 objects are acquired by the OpenCL context associated with command-queue and can therefore be used by all command-queues associated with the OpenCL context.

OpenCL memory objects created from Direct3D 11 resources must be acquired before they can be used by any OpenCL commands queued to a command-queue. If an OpenCL memory object created from a Direct3D 11 resource is used while it is not currently acquired by OpenCL, the call attempting to use that OpenCL memory object will return CL_D3D11_RESOURCE_NOT_ACQUIRED_KHR.

If CL_CONTEXT_INTEROP_USER_SYNC is not specified as CL_TRUE during context creation, clEnqueueAcquireD3D11ObjectsKHR provides the synchronization guarantee that any Direct3D 11 calls involving the interop device(s) used in the OpenCL context made before clEnqueueAcquireD3D11ObjectsKHR is called will complete executing before event reports completion and before the execution of any subsequent OpenCL work issued in command_queue begins. 

***if not use user sync, OpenCL runtime/driver may invoke D3D driver to submit a GPU query command to query execution status***

If the context was created with properties specifying CL_CONTEXT_INTEROP_USER_SYNC as CL_TRUE, the user is responsible for guaranteeing that any Direct3D 11 calls involving the interop device(s) used in the OpenCL context made before clEnqueueAcquireD3D11ObjectsKHR is called have completed before calling clEnqueueAcquireD3D11ObjectsKHR.

https://www.khronos.org/registry/OpenCL/sdk/1.2/docs/man/xhtml/clEnqueueAcquireD3D11ObjectsKHR.html


```c++
    // Create a context
    cl_context_properties contextProperties[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)platform,
        CL_CONTEXT_D3D11_DEVICE_KHR, (cl_context_properties)(pD3D11Device),
        CL_CONTEXT_INTEROP_USER_SYNC, CL_TRUE, // enable user sync
        0
    };
    context = clCreateContextFromType(contextProperties, CL_DEVICE_TYPE_GPU, NULL, NULL, &err);
    CHECK_OCL_ERROR(err, "Couldn't create a context");
```

## 3. execution timing

![timing](https://github.com/mintaka33/vaapi-opencl-interop/blob/master/log/vaocl_full2.png?raw=true)

