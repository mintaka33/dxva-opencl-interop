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

using namespace std;

#define _CRT_SECURE_NO_WARNINGS
#define PROGRAM_FILE "convert.cl"
#define KERNEL_FUNC "scale"

clCreateFromD3D11Texture2DKHR_fn clCreateFromD3D11Texture2DKHR = NULL;
clEnqueueAcquireD3D11ObjectsKHR_fn clEnqueueAcquireD3D11ObjectsKHR = NULL;
clEnqueueReleaseD3D11ObjectsKHR_fn clEnqueueReleaseD3D11ObjectsKHR = NULL;

/* Find a GPU or CPU associated with the first available platform */
cl_device_id createDevice()
{
    cl_platform_id platform;
    cl_device_id dev;
    int err;

    /* Identify a platform */
    err = clGetPlatformIDs(1, &platform, NULL);
    if (err < 0) {
        perror("Couldn't identify a platform");
        exit(1);
    }

    /* Access a device */
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &dev, NULL);
    if (err == CL_DEVICE_NOT_FOUND) {
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &dev, NULL);
    }
    if (err < 0) {
        perror("Couldn't access any devices");
        exit(1);
    }

    clCreateFromD3D11Texture2DKHR = (clCreateFromD3D11Texture2DKHR_fn)
        clGetExtensionFunctionAddressForPlatform(platform, "clCreateFromD3D11Texture2DKHR");
    clEnqueueAcquireD3D11ObjectsKHR = (clEnqueueAcquireD3D11ObjectsKHR_fn)
        clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueAcquireD3D11ObjectsKHR");
    clEnqueueReleaseD3D11ObjectsKHR = (clEnqueueReleaseD3D11ObjectsKHR_fn)
        clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueReleaseD3D11ObjectsKHR");

    return dev;
}

/* Create program from a file and compile it */
cl_program buildProgram(cl_context ctx, cl_device_id dev, const char* filename)
{
    cl_program program;
    FILE *program_handle;
    char *program_buffer, *program_log;
    size_t program_size, log_size;
    int err;

    /* Read program file and place content into buffer */
    fopen_s(&program_handle, filename, "r");
    if (program_handle == NULL) {
        perror("Couldn't find the program file");
        exit(1);
    }
    fseek(program_handle, 0, SEEK_END);
    program_size = ftell(program_handle);
    rewind(program_handle);
    program_buffer = (char*)malloc(program_size + 1);
    memset(program_buffer, 0, program_size + 1);
    fread(program_buffer, sizeof(char), program_size, program_handle);
    fclose(program_handle);

    /* Create program from file */
    program = clCreateProgramWithSource(ctx, 1,
        (const char**)&program_buffer, &program_size, &err);
    if (err < 0) {
        perror("Couldn't create the program");
        exit(1);
    }
    free(program_buffer);

    /* Build program */
    err = clBuildProgram(program, 0, NULL, "-cl-std=CL2.0", NULL, NULL);
    if (err < 0) {

        /* Find size of log and print to std output */
        clGetProgramBuildInfo(program, dev, CL_PROGRAM_BUILD_LOG,
            0, NULL, &log_size);
        program_log = (char*)malloc(log_size + 1);
        program_log[log_size] = '\0';
        clGetProgramBuildInfo(program, dev, CL_PROGRAM_BUILD_LOG,
            log_size + 1, program_log, NULL);
        printf("%s\n", program_log);
        free(program_log);
        exit(1);
    }

    return program;
}

int oclConvertInPlace(ID3D11Device *pD3D11Device, size_t width, size_t height)
{
    /* Host/device data structures */
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_int err;
    size_t global_size[2];

    /* Image data */
    cl_mem inOutImage;
    size_t origin[3], region[3];

    /* Create a device and context */
    device = createDevice();
    context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (err < 0) {
        perror("Couldn't create a context");
        exit(1);
    }

    /* Build the program and create a kernel */
    program = buildProgram(context, device, PROGRAM_FILE);
    kernel = clCreateKernel(program, KERNEL_FUNC, &err);
    if (err < 0) {
        printf("Couldn't create a kernel: %d", err);
        exit(1);
    };

    vector<uint8_t> pixels(width*height);
    pixels[0] = 10;
    pixels[1] = 20;
    pixels[2] = 30;
    pixels[3] = 40;

    cl_image_format format = {};
    format.image_channel_data_type = CL_UNSIGNED_INT8;
    format.image_channel_order = CL_R;
    cl_image_desc desc = {};
    desc.image_type = CL_MEM_OBJECT_IMAGE2D;
    desc.image_width = width;
    desc.image_height = height;
    desc.image_depth = 0;
    desc.image_array_size = 1;
    desc.image_row_pitch = 0;
    desc.image_slice_pitch = 0;
    desc.num_mip_levels = 0;
    desc.num_samples = 0;
    desc.mem_object = NULL;

    /* Create image object */
    inOutImage = clCreateImage(context,
        CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        &format, &desc, (void*)&pixels[0], &err);
    if (err < 0) {
        perror("Couldn't create the image object");
        exit(1);
    };

    /* Create kernel arguments */
    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &inOutImage);
    if (err < 0) {
        printf("Couldn't set a kernel argument");
        exit(1);
    };

    /* Create a command queue */
    const cl_command_queue_properties properties[] = { CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0 };
    queue = clCreateCommandQueueWithProperties(context, device, properties, &err);
    if (err < 0) {
        perror("Couldn't create a command queue");
        exit(1);
    };

    /* Enqueue kernel */
    global_size[0] = height; global_size[1] = width;
    err = clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_size,
        NULL, 0, NULL, NULL);
    if (err < 0) {
        perror("Couldn't enqueue the kernel");
        exit(1);
    }

    /* Read the image object */
    vector<uint8_t> hostMem(width*height);
    origin[0] = 0; origin[1] = 0; origin[2] = 0;
    region[0] = width; region[1] = height; region[2] = 1;
    err = clEnqueueReadImage(queue, inOutImage, CL_TRUE, origin,
        region, 0, 0, (void*)&hostMem[0], 0, NULL, NULL);
    if (err < 0) {
        perror("Couldn't read from the image object");
        exit(1);
    }

    /* Deallocate resources */
    clReleaseMemObject(inOutImage);
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

    ID3D11VideoDevice * pD3D11VideoDevice = NULL;
    if (SUCCEEDED(hr))
    {
        hr = pD3D11Device->QueryInterface(&pD3D11VideoDevice);
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

    oclConvertInPlace(pD3D11Device, dxvaDecData.picWidth, dxvaDecData.picHeight);

    if (SUCCEEDED(hr))
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

        if (SUCCEEDED(hr))
        {
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
    }

    FREE_RESOURCE(pDeviceContext);
    FREE_RESOURCE(pSurfaceDecodeNV12);
    FREE_RESOURCE(pSurfaceCopyStaging);
    FREE_RESOURCE(pD3D11VideoDevice);
    FREE_RESOURCE(pVideoDecoder);
    FREE_RESOURCE(pDecodeOutputView);
    FREE_RESOURCE(pVideoContext);
    FREE_RESOURCE(pD3D11Device);

    printf("Execution done. \n");

    return 0;
}
