#pragma once

#include <cstdint>

typedef struct {
    int width;
    int height;
} VnxIppiSize;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} VnxIppiRect;

int vnxippiResize_8u_C1R(const uint8_t* pSrc, VnxIppiSize srcSize, int srcStep, VnxIppiRect srcRoi,
    uint8_t* pDst, int dstStep, VnxIppiSize dstRoiSize,
    double xFactor, double yFactor, int interpolation);

int vnxippiBGR565ToYCbCr420_16u8u_C3P3R(const uint16_t* pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize);
