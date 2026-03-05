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

typedef signed int VnxIppStatus;

#define VnxippApi VnxIppStatus


int vnxippiResize_8u_C1R(const uint8_t* pSrc, VnxIppiSize srcSize, int srcStep, VnxIppiRect srcRoi,
    uint8_t* pDst, int dstStep, VnxIppiSize dstRoiSize,
    double xFactor, double yFactor, int interpolation);

int vnxippiBGR565ToYCbCr420_16u8u_C3P3R(const uint16_t* pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize);

int vnxippiResize_8u_P2P3R(const uint8_t* const* pSrc, VnxIppiSize srcSize, const int* srcStep, VnxIppiRect srcRoi,
    uint8_t** pDst, int* dstStep, VnxIppiRect dstRoi,
    double xFactor, double yFactor, int interpolation);

enum {
    VNXIPPI_INTER_NN     = 1,
    VNXIPPI_INTER_LINEAR = 2,
    VNXIPPI_INTER_CUBIC  = 4,
    VNXIPPI_INTER_LANCZOS = 16
};

typedef enum {
    vnxippMskSize1x3 = 13,
    vnxippMskSize1x5 = 15,
    vnxippMskSize3x1 = 31,
    vnxippMskSize3x3 = 33,
    vnxippMskSize5x1 = 51,
    vnxippMskSize5x5 = 55
} VnxIppiMaskSize;

typedef enum {
    vnxippCmpLess,
    vnxippCmpLessEq,
    vnxippCmpEq,
    vnxippCmpGreaterEq,
    vnxippCmpGreater
} VnxIppCmpOp;


#define vnxippStsNoErr 0
#define vnxippStsErr   -2
#define vnxippStsNonIntelCpu 20

VnxippApi vnxippStaticInit(void);
VnxippApi vnxippInit(void);

VnxippApi vnxippiCopy_8u_C1R(const uint8_t* pSrc, int srcStep,
                             uint8_t* pDst, int dstStep, VnxIppiSize roiSize);
VnxippApi vnxippiSet_8u_C1R(uint8_t value, uint8_t* pDst, int dstStep,
                            VnxIppiSize roiSize);
extern "C" void vnxippFree(void* ptr);
extern "C" void* vnxippMalloc(int length);

VnxippApi vnxippiCopy_8u_C1MR(const uint8_t* pSrc, int srcStep,
                              uint8_t* pDst, int dstStep, VnxIppiSize roiSize,
                              const uint8_t* pMask, int maskStep);

VnxippApi vnxippiCopyWrapBorder_32s_C1R(const int32_t* pSrc, int srcStep, VnxIppiSize srcRoiSize,
                                        int32_t* pDst, int dstStep, VnxIppiSize dstRoiSize,
                                        int topBorderHeight, int leftBorderWidth);

VnxippApi vnxippiBGRToYCbCr420_8u_C3P3R(const uint8_t*  pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize);

VnxippApi vnxippiYCbCr422ToYCbCr420_8u_C2P3R(const uint8_t* pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize);

VnxippApi vnxippiCbYCr422ToYCrCb420_8u_C2P3R(const uint8_t* pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize);

VnxippApi vnxippiBGRToYCbCr420_8u_AC4P3R(const uint8_t*  pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize);

VnxippApi vnxippiYCbCr420ToBGR_8u_P3C3R(const uint8_t*  pSrc[3], int srcStep[3], uint8_t* pDst, int dstStep, VnxIppiSize roiSize);
VnxippApi vnxippiYCbCr420ToBGR_8u_P3C4R(const uint8_t*  pSrc[3], int srcStep[3], uint8_t* pDst, int dstStep, VnxIppiSize roiSize, uint8_t aval);

VnxippApi vnxippiWarpPerspective_8u_C1R(const uint8_t* pSrc, VnxIppiSize srcSize, int srcStep, VnxIppiRect srcRoi, uint8_t* pDst, int dstStep, VnxIppiRect dstRoi, const double coeffs[3][3], int interpolation);

VnxippApi vnxippiThreshold_LTVal_8u_C1R(const uint8_t* pSrc, int srcStep,
                                        uint8_t* pDst, int dstStep, VnxIppiSize roiSize, uint8_t threshold,
                                        uint8_t value);
VnxippApi vnxippiThreshold_LTVal_8u_C1IR(uint8_t* pSrcDst, int srcDstStep,
                                         VnxIppiSize roiSize, uint8_t threshold, uint8_t value);

VnxippApi vnxippiThreshold_GTVal_8u_C1IR(uint8_t* pSrcDst, int srcDstStep,
                                         VnxIppiSize roiSize, uint8_t threshold, uint8_t value);

VnxippApi vnxippiCompare_8u_C1R(const uint8_t* pSrc1, int src1Step,
                                const uint8_t* pSrc2, int src2Step,
                                uint8_t* pDst, int dstStep,
                                VnxIppiSize roiSize, VnxIppCmpOp ippCmpOp);

VnxippApi vnxippiAnd_8u_C1IR(const uint8_t* pSrc, int srcStep, uint8_t* pSrcDst, int srcDstStep, VnxIppiSize roiSize);

VnxippApi vnxippiAndC_8u_C1IR(uint8_t value, uint8_t* pSrcDst, int srcDstStep, VnxIppiSize roiSize);

VnxippApi vnxippiAdd_8u_C1IRSfs(const uint8_t* pSrc, int srcStep, uint8_t* pSrcDst,
                                int srcDstStep, VnxIppiSize roiSize, int scaleFactor);

VnxippApi vnxippiSub_8u_C1IRSfs(const uint8_t* pSrc, int srcStep, uint8_t* pSrcDst,
                                int srcDstStep, VnxIppiSize roiSize, int scaleFactor);

VnxippApi vnxippiAbsDiff_8u_C1R(const uint8_t* pSrc1, int src1Step,
                                const uint8_t* pSrc2, int src2Step,
                                uint8_t* pDst, int dstStep, VnxIppiSize roiSize);

VnxippApi vnxippiCountInRange_8u_C1R(const uint8_t* pSrc, int srcStep, VnxIppiSize roiSize,
                                     int* counts, uint8_t lowerBound, uint8_t upperBound);

VnxippApi vnxippiMulC_8u_C1RSfs(const uint8_t* pSrc, int srcStep, uint8_t value, uint8_t* pDst,
                                int dstStep, VnxIppiSize roiSize, int scaleFactor);
