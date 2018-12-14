#pragma once
#include <stdint.h>
#include <windows.h>
#include <D3D11.h>
#include <dxva.h>

typedef struct DXVADecBuf {
    D3D11_VIDEO_DECODER_BUFFER_TYPE bufType;
    uint8_t* pBufData;
    uint32_t bufSize;
} DXVADecBuf;

typedef struct _DXVAData {
    GUID guidDecoder;
    uint32_t picWidth;
    uint32_t picHeight;
    uint32_t isShortFormat;
    uint32_t dxvaBufNum;
    DXVADecBuf dxvaDecBuffers[10];
} DXVAData;

// AVC decode DXVA data
extern DXVAData g_dxvaDataAVC_Short;

