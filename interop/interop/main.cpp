#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <tchar.h>
#include <memory.h>

#include <iostream>
#include <string>
#include <map>
#include <vector>

#include "dxva_data.h"

#include "CL\cl.h"
#include "CL\cl_d3d11.h"

using namespace std;

#define FREE_RESOURCE(res) \
    if(res) {res->Release(); res = NULL;}

#define CHECK_SUCCESS(hr, msg) \
    if (!SUCCEEDED(hr)) { printf("ERROR: Failed to call %s\n", msg); return -1; }

#define CHECK_OCL_ERROR(err, msg) \
    if (err < 0) { printf("ERROR: %s\n", msg); return -1; }

#define PROGRAM_FILE "convert.cl"
#define KERNEL_FUNC "scale"

cl_platform_id platform;
cl_device_id device;
cl_context context;
cl_command_queue queue;
cl_program program;
cl_kernel kernel;

clCreateFromD3D11Texture2DKHR_fn clCreateFromD3D11Texture2DKHR = NULL;
clEnqueueAcquireD3D11ObjectsKHR_fn clEnqueueAcquireD3D11ObjectsKHR = NULL;
clEnqueueReleaseD3D11ObjectsKHR_fn clEnqueueReleaseD3D11ObjectsKHR = NULL;

void queryImageObjectInfo(cl_mem memObj)
{
    size_t size;
    clGetMemObjectInfo(memObj, CL_MEM_SIZE, sizeof(size), &size, NULL);

    cl_mem_object_type type;
    clGetMemObjectInfo(memObj, CL_MEM_TYPE, sizeof(type), &type, NULL);

    cl_mem_flags flags;
    clGetMemObjectInfo(memObj, CL_MEM_FLAGS, sizeof(flags), &flags, NULL);

    cl_mem associateMem;
    clGetMemObjectInfo(memObj, CL_MEM_ASSOCIATED_MEMOBJECT, sizeof(associateMem), &associateMem, NULL);

    cl_image_format format;
    clGetImageInfo(memObj, CL_IMAGE_FORMAT, sizeof(format), &format, NULL);

    size_t width;
    clGetImageInfo(memObj, CL_IMAGE_WIDTH, sizeof(width), &width, NULL);

    size_t height;
    clGetImageInfo(memObj, CL_IMAGE_HEIGHT, sizeof(height), &height, NULL);

    size_t pitch;
    clGetImageInfo(memObj, CL_IMAGE_ROW_PITCH, sizeof(pitch), &pitch, NULL);

    size_t elesize;
    clGetImageInfo(memObj, CL_IMAGE_ELEMENT_SIZE, sizeof(elesize), &elesize, NULL);

    return;
}

int createDevice(cl_platform_id &platform, cl_device_id &dev)
{
    int err;

    // Identify a platform
    err = clGetPlatformIDs(1, &platform, NULL);
    CHECK_OCL_ERROR(err, "Couldn't identify a platform");

    // Access a device
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &dev, NULL);
    if (err == CL_DEVICE_NOT_FOUND) 
    {
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &dev, NULL);
    }
    CHECK_OCL_ERROR(err, "Couldn't access any devices");

    clCreateFromD3D11Texture2DKHR = (clCreateFromD3D11Texture2DKHR_fn)
        clGetExtensionFunctionAddressForPlatform(platform, "clCreateFromD3D11Texture2DKHR");
    clEnqueueAcquireD3D11ObjectsKHR = (clEnqueueAcquireD3D11ObjectsKHR_fn)
        clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueAcquireD3D11ObjectsKHR");
    clEnqueueReleaseD3D11ObjectsKHR = (clEnqueueReleaseD3D11ObjectsKHR_fn)
        clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueReleaseD3D11ObjectsKHR");
    if (clCreateFromD3D11Texture2DKHR == nullptr || 
        clEnqueueAcquireD3D11ObjectsKHR == nullptr ||
        clEnqueueReleaseD3D11ObjectsKHR == nullptr)
    {
        printf("Couldn't get extension functions for D3D11 sharing\n");
        return -1;
    }

    return 0;
}

int buildProgram(cl_context ctx, cl_device_id dev, const char* filename, cl_program &program)
{
    FILE *program_handle;
    char *program_buffer, *program_log;
    size_t program_size, log_size;
    int err;

    fopen_s(&program_handle, filename, "r");
    if (program_handle == NULL) 
    {
        printf("Couldn't find the program file\n");
        return -1;
    }
    fseek(program_handle, 0, SEEK_END);
    program_size = ftell(program_handle);
    rewind(program_handle);
    program_buffer = (char*)malloc(program_size + 1);
    memset(program_buffer, 0, program_size + 1);
    fread(program_buffer, sizeof(char), program_size, program_handle);
    fclose(program_handle);

    // Create program from file
    program = clCreateProgramWithSource(ctx, 1, (const char**)&program_buffer, &program_size, &err);
    CHECK_OCL_ERROR(err, "Couldn't create the program");
    free(program_buffer);

    // Build program
    err = clBuildProgram(program, 0, NULL, "-cl-std=CL2.0", NULL, NULL);
    if (err < 0) 
    {
        // Find size of log and print to std output
        clGetProgramBuildInfo(program, dev, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        program_log = (char*)malloc(log_size + 1);
        memset(program_log, 0, log_size + 1);
        clGetProgramBuildInfo(program, dev, CL_PROGRAM_BUILD_LOG, log_size + 1, program_log, NULL);
        printf("%s\n", program_log);
        free(program_log);
        return -1;
    }

    return 0;
}

int oclInitialize(ID3D11Device *pD3D11Device)
{
    cl_int err;

    // Create a device
    err = createDevice(platform, device);
    CHECK_OCL_ERROR(err, "Couldn't create a OpenCL device");

    // Create a context
    cl_context_properties contextProperties[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)platform,
        CL_CONTEXT_D3D11_DEVICE_KHR, (cl_context_properties)(pD3D11Device),
        CL_CONTEXT_INTEROP_USER_SYNC, CL_TRUE,
        0
    };
    context = clCreateContextFromType(contextProperties, CL_DEVICE_TYPE_GPU, NULL, NULL, &err);
    CHECK_OCL_ERROR(err, "Couldn't create a context");

    /* Build the program and create a kernel */
    err = buildProgram(context, device, PROGRAM_FILE, program);
    CHECK_OCL_ERROR(err, "Couldn't build program");

    kernel = clCreateKernel(program, KERNEL_FUNC, &err);
    CHECK_OCL_ERROR(err, "Couldn't create a kernel");

    // Create a command queue
    const cl_command_queue_properties properties[] = { CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0 };
    queue = clCreateCommandQueueWithProperties(context, device, properties, &err);
    CHECK_OCL_ERROR(err, "Couldn't create a command queue");

    return 0;
}

int oclProcessDecodeRT(size_t width, size_t height, ID3D11Texture2D *pDecodeNV12)
{
    cl_int err;
    size_t global_size[2];
    size_t origin[3], region[3];

    // Note: the image format of sharedImageY created from NV12 d3d11 texture is 
    // image_channel_data_type = CL_UNORM_INT8, image_channel_order = CL_R;
    cl_mem sharedImageY;
    sharedImageY = clCreateFromD3D11Texture2DKHR(context, CL_MEM_READ_WRITE, pDecodeNV12, 0, &err);
    CHECK_OCL_ERROR(err, "Failed to call clCreateFromD3D11Texture2DKHR");

    err = clEnqueueAcquireD3D11ObjectsKHR(queue, 1, &sharedImageY, 0, NULL, NULL);
    CHECK_OCL_ERROR(err, "Failed to call clEnqueueAcquireD3D11ObjectsKHR");

    // Query image info
    queryImageObjectInfo(sharedImageY);

    // Create kernel arguments
    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &sharedImageY);
    CHECK_OCL_ERROR(err, "Couldn't set a kernel argument");

    // Enqueue kernel
    global_size[0] = height; global_size[1] = width;
    err = clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_size, NULL, 0, NULL, NULL);
    CHECK_OCL_ERROR(err, "Couldn't enqueue the kernel");

    // Wait until the queued kernel is completed by the device
    err = clFinish(queue);
    CHECK_OCL_ERROR(err, "Failed to call clFinish");

    // Read the image object
    vector<uint8_t> hostMem(width*height);
    origin[0] = 0; origin[1] = 0; origin[2] = 0;
    region[0] = width; region[1] = height; region[2] = 1;
    err = clEnqueueReadImage(queue, sharedImageY, CL_TRUE, origin, region, 0, 0, (void*)&hostMem[0], 0, NULL, NULL);
    CHECK_OCL_ERROR(err, "Couldn't read from the image object");

    err = clEnqueueReleaseD3D11ObjectsKHR(queue, 1, &sharedImageY, 0, NULL, NULL);
    CHECK_OCL_ERROR(err, "Failed to call clEnqueueReleaseD3D11ObjectsKHR");

    // Deallocate resources
    clReleaseKernel(kernel);
    clReleaseCommandQueue(queue);
    clReleaseProgram(program);
    clReleaseContext(context);

    return 0;
}

int main(char argc, char** argv)
{
    DXVAData dxvaDecData = g_dxvaDataAVC_Short;
    HRESULT hr = S_OK;
    ID3D11Device *pD3D11Device = NULL;
    ID3D11DeviceContext *pDeviceContext = NULL;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1 };
    D3D_FEATURE_LEVEL fl;
    GUID profile = dxvaDecData.guidDecoder;

    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &pD3D11Device, &fl, &pDeviceContext);
    CHECK_SUCCESS(hr, "D3D11CreateDevice");

    if (oclInitialize(pD3D11Device) != 0)
    {
        printf("ERROR: failed to initialize OCL\n");
        return -1;
    }

    ID3D11VideoDevice * pD3D11VideoDevice = NULL;
    hr = pD3D11Device->QueryInterface(&pD3D11VideoDevice);
    CHECK_SUCCESS(hr, "QueryInterface");

    ID3D11VideoDecoder *pVideoDecoder = NULL;
    D3D11_VIDEO_DECODER_DESC decoderDesc = { 0 };
    decoderDesc.Guid = profile;
    decoderDesc.SampleWidth = dxvaDecData.picWidth;
    decoderDesc.SampleHeight = dxvaDecData.picHeight;
    decoderDesc.OutputFormat = DXGI_FORMAT_NV12;
    D3D11_VIDEO_DECODER_CONFIG config = { 0 };
    config.ConfigBitstreamRaw = dxvaDecData.isShortFormat; // 0: long format; 1: short format
    hr = pD3D11VideoDevice->CreateVideoDecoder(&decoderDesc, &config, &pVideoDecoder);
    CHECK_SUCCESS(hr, "CreateVideoDecoder");

    ID3D11Texture2D *pSurfaceDecodeNV12 = NULL;
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
    descRT.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = pD3D11Device->CreateTexture2D(&descRT, NULL, &pSurfaceDecodeNV12);
    CHECK_SUCCESS(hr, "CreateTexture2D");

    ID3D11Texture2D *pSurfaceCopyStaging = NULL;
    D3D11_TEXTURE2D_DESC descStaging = { 0 };
    descStaging.Width = dxvaDecData.picWidth;
    descStaging.Height = dxvaDecData.picHeight;
    descStaging.MipLevels = 1;
    descStaging.ArraySize = 1;
    descStaging.Format = DXGI_FORMAT_NV12;
    descStaging.SampleDesc = { 1, 0 }; // DXGI_SAMPLE_DESC 
    descStaging.Usage = D3D11_USAGE_STAGING; // D3D11_USAGE 
    descStaging.BindFlags = 0;
    descStaging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    descStaging.MiscFlags = 0;
    hr = pD3D11Device->CreateTexture2D(&descStaging, NULL, &pSurfaceCopyStaging);
    CHECK_SUCCESS(hr, "CreateTexture2D");

    ID3D11VideoDecoderOutputView *pDecodeOutputView = NULL;
    D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC viewDesc = { 0 };
    viewDesc.DecodeProfile = profile;
    viewDesc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
    hr = pD3D11VideoDevice->CreateVideoDecoderOutputView(pSurfaceDecodeNV12, &viewDesc, &pDecodeOutputView);
    CHECK_SUCCESS(hr, "CreateVideoDecoderOutputView");

    UINT profileCount = 0;
    GUID decoderGUID = {};
    profileCount = pD3D11VideoDevice->GetVideoDecoderProfileCount();
    printf("INFO: Decoder Profile Count = %d\n", profileCount);

    for (UINT i = 0; i < profileCount; i++)
    {
        hr = pD3D11VideoDevice->GetVideoDecoderProfile(i, &decoderGUID);
        CHECK_SUCCESS(hr, "GetVideoDecoderProfile");
        OLECHAR sGUID[64] = { 0 };
        StringFromGUID2(decoderGUID, sGUID, 64);
        //wprintf(L"INFO: Index %02d - GUID = %s\n", i, sGUID);
    }

    ID3D11VideoContext* pVideoContext = NULL;
    hr = pDeviceContext->QueryInterface(&pVideoContext);
    CHECK_SUCCESS(hr, "QueryInterface");

    // Decode begin frame
    hr = pVideoContext->DecoderBeginFrame(pVideoDecoder, pDecodeOutputView, 0, 0);
    CHECK_SUCCESS(hr, "DecoderBeginFrame");

    // Prepare DXVA buffers for decoding
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
        CHECK_SUCCESS(hr, "GetDecoderBuffer");
        UINT copySize = min(bufferSize, descDecBuffers[i].DataSize);
        memcpy_s(buffer, copySize, dxvaDecData.dxvaDecBuffers[i].pBufData, copySize);
        hr = pVideoContext->ReleaseDecoderBuffer(pVideoDecoder, descDecBuffers[i].BufferType);
        CHECK_SUCCESS(hr, "ReleaseDecoderBuffer");
    }

    // Submit decode workload to GPU
    hr = pVideoContext->SubmitDecoderBuffers(pVideoDecoder, dxvaDecData.dxvaBufNum, descDecBuffers);
    CHECK_SUCCESS(hr, "SubmitDecoderBuffers");
    delete[] descDecBuffers;

    // Decode end frame
    hr = pVideoContext->DecoderEndFrame(pVideoDecoder);
    CHECK_SUCCESS(hr, "DecoderEndFrame");

    printf("INFO: decode success\n");

    // Invoke OpenCL kernel to modify decode output surface in place
    int ret = oclProcessDecodeRT(dxvaDecData.picWidth, dxvaDecData.picHeight, pSurfaceDecodeNV12);
    if (ret != 0)
    {
        printf("ERROR: OpenCL process decode RT failed!\n");
        return -1;
    }

    printf("INFO: OCL process success\n");

    // Map decode surface and dump NV12 to file
    if (1)
    {
        D3D11_BOX box;
        box.left = 0,
            box.right = dxvaDecData.picWidth,
            box.top = 0,
            box.bottom = dxvaDecData.picHeight,
            box.front = 0,
            box.back = 1;
        pDeviceContext->CopySubresourceRegion(pSurfaceCopyStaging, 0, 0, 0, 0, pSurfaceDecodeNV12, 0, &box);
        D3D11_MAPPED_SUBRESOURCE subRes;
        ZeroMemory(&subRes, sizeof(subRes));
        hr = pDeviceContext->Map(pSurfaceCopyStaging, 0, D3D11_MAP_READ, 0, &subRes);
        CHECK_SUCCESS(hr, "Map");

        UINT height = dxvaDecData.picHeight;
        BYTE *pData = (BYTE*)malloc(subRes.RowPitch * (height + height / 2));
        if (pData)
        {
            CopyMemory(pData, subRes.pData, subRes.RowPitch * (height + height / 2));
            FILE *fp;
            char fileName[256] = {};
            sprintf_s(fileName, 256, "out_%d_%d_nv12.yuv", subRes.RowPitch, height);
            fopen_s(&fp, fileName, "wb");
            fwrite(pData, subRes.RowPitch * (height + height / 2), 1, fp);
            fclose(fp);
            free(pData);
        }
        pDeviceContext->Unmap(pSurfaceCopyStaging, 0);
    }

    FREE_RESOURCE(pDeviceContext);
    FREE_RESOURCE(pSurfaceDecodeNV12);
    FREE_RESOURCE(pSurfaceCopyStaging);
    FREE_RESOURCE(pD3D11VideoDevice);
    FREE_RESOURCE(pVideoDecoder);
    FREE_RESOURCE(pDecodeOutputView);
    FREE_RESOURCE(pVideoContext);
    FREE_RESOURCE(pD3D11Device);

    printf("INFO: execution done. \n");

    return 0;
}
