#if defined(__x86_64) || defined(_WIN32)
#include <ipp/ipp.h>
#include <ipp/ippi.h>
#include <ipp/ippcc.h>
#include <ipp/ippcv.h>
#include "vnxipp.h"

VnxippApi vnxippInit(void) {
    return ippInit();
}

VnxippApi vnxippiCopy_8u_C1R(const uint8_t* pSrc, int srcStep,
    uint8_t* pDst, int dstStep, VnxIppiSize roiSize)
{
    return ippiCopy_8u_C1R(pSrc, srcStep, pDst, dstStep, *(IppiSize*)&roiSize);
}
VnxippApi vnxippiSet_8u_C1R(uint8_t value, uint8_t* pDst, int dstStep,
                            VnxIppiSize roiSize)
{
    return ippiSet_8u_C1R(value, pDst, dstStep, *(IppiSize*)&roiSize);
}
extern "C" void vnxippFree(void* ptr)
{
    ippFree(ptr);
}
extern "C" void* vnxippMalloc(int length)
{
    return ippMalloc(length);
}

VnxippApi vnxippiCopy_8u_C1MR(const uint8_t* pSrc, int srcStep,
                              uint8_t* pDst, int dstStep, VnxIppiSize roiSize,
                              const uint8_t* pMask, int maskStep)
{
    return ippiCopy_8u_C1MR(pSrc, srcStep, pDst, dstStep, *(IppiSize*)&roiSize, pMask, maskStep);
}

VnxippApi vnxippiCopyWrapBorder_32s_C1R(const int32_t* pSrc, int srcStep, VnxIppiSize srcRoiSize,
                                        int32_t* pDst, int dstStep, VnxIppiSize dstRoiSize,
                                        int topBorderHeight, int leftBorderWidth)
{
    return ippiCopyWrapBorder_32s_C1R(pSrc, srcStep, *(IppiSize*)&srcRoiSize, pDst, dstStep, *(IppiSize*)&dstRoiSize, topBorderHeight, leftBorderWidth);
}

VnxippApi vnxippiBGRToYCbCr420_8u_C3P3R(const uint8_t*  pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize)
{
    return ippiBGRToYCbCr420_8u_C3P3R(pSrc, srcStep, pDst, dstStep, *(IppiSize*)&roiSize);
}

VnxippApi vnxippiYCbCr422ToYCbCr420_8u_C2P3R(const uint8_t* pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize)
{
    return ippiYCbCr422ToYCbCr420_8u_C2P3R(pSrc, srcStep, pDst, dstStep, *(IppiSize*)&roiSize);
}

VnxippApi vnxippiCbYCr422ToYCrCb420_8u_C2P3R(const uint8_t* pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize)
{
    return ippiCbYCr422ToYCrCb420_8u_C2P3R(pSrc, srcStep, pDst, dstStep, *(IppiSize*)&roiSize);
}

VnxippApi vnxippiBGRToYCbCr420_8u_AC4P3R(const uint8_t*  pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize)
{
    return ippiBGRToYCbCr420_8u_AC4P3R(pSrc, srcStep, pDst, dstStep, *(IppiSize*)&roiSize);
}

VnxippApi vnxippiYCbCr420ToBGR_8u_P3C3R(const uint8_t*  pSrc[3], int srcStep[3], uint8_t* pDst, int dstStep, VnxIppiSize roiSize)
{
    return ippiYCbCr420ToBGR_8u_P3C3R(pSrc, srcStep, pDst, dstStep, *(IppiSize*)&roiSize);
}
VnxippApi vnxippiYCbCr420ToBGR_8u_P3C4R(const uint8_t*  pSrc[3], int srcStep[3], uint8_t* pDst, int dstStep, VnxIppiSize roiSize, uint8_t aval)
{
    return ippiYCbCr420ToBGR_8u_P3C4R(pSrc, srcStep, pDst, dstStep, *(IppiSize*)&roiSize, aval);
}

VnxippApi vnxippiWarpPerspective_8u_C1R(const uint8_t* pSrc, VnxIppiSize srcSize, int srcStep, VnxIppiRect srcRoi, uint8_t* pDst, int dstStep, VnxIppiRect dstRoi, const double coeffs[3][3], int interpolation)
{
    //return ippiWarpPerspective_8u_C1R(pSrc, *(IppiSize*)&srcSize, srcStep, *(IppiRect*)&srcRoi, pDst, dstStep, *(IppiRect*)&dstRoi, coeffs, interpolation);
    return vnxippStsErr;
}

VnxippApi vnxippiThreshold_LTVal_8u_C1R(const uint8_t* pSrc, int srcStep,
                                        uint8_t* pDst, int dstStep, VnxIppiSize roiSize, uint8_t threshold,
                                        uint8_t value)
{
    return ippiThreshold_LTVal_8u_C1R(pSrc, srcStep, pDst, dstStep, *(IppiSize*)&roiSize, threshold, value);
}
VnxippApi vnxippiThreshold_LTVal_8u_C1IR(uint8_t* pSrcDst, int srcDstStep,
                                         VnxIppiSize roiSize, uint8_t threshold, uint8_t value)
{
    return ippiThreshold_LTVal_8u_C1IR(pSrcDst, srcDstStep, *(IppiSize*)&roiSize, threshold, value);
}

VnxippApi vnxippiThreshold_GTVal_8u_C1IR(uint8_t* pSrcDst, int srcDstStep,
                                         VnxIppiSize roiSize, uint8_t threshold, uint8_t value)
{
    return ippiThreshold_GTVal_8u_C1IR(pSrcDst, srcDstStep, *(IppiSize*)&roiSize, threshold, value);
}

VnxippApi vnxippiCompare_8u_C1R(const uint8_t* pSrc1, int src1Step,
                                const uint8_t* pSrc2, int src2Step,
                                uint8_t* pDst, int dstStep,
                                VnxIppiSize roiSize, VnxIppCmpOp ippCmpOp)
{
    return ippiCompare_8u_C1R(pSrc1, src1Step, pSrc2, src2Step, pDst, dstStep, *(IppiSize*)&roiSize, (IppCmpOp)ippCmpOp);
}

VnxippApi vnxippiAnd_8u_C1IR(const uint8_t* pSrc, int srcStep, uint8_t* pSrcDst, int srcDstStep, VnxIppiSize roiSize)
{
    return ippiAnd_8u_C1IR(pSrc, srcStep, pSrcDst, srcDstStep, *(IppiSize*)&roiSize);
}

VnxippApi vnxippiAndC_8u_C1IR(uint8_t value, uint8_t* pSrcDst, int srcDstStep, VnxIppiSize roiSize)
{
    return ippiAndC_8u_C1IR(value, pSrcDst, srcDstStep, *(IppiSize*)&roiSize);
}

VnxippApi vnxippiAdd_8u_C1IRSfs(const uint8_t* pSrc, int srcStep, uint8_t* pSrcDst,
                                int srcDstStep, VnxIppiSize roiSize, int scaleFactor)
{
    return ippiAdd_8u_C1IRSfs(pSrc, srcStep, pSrcDst, srcDstStep, *(IppiSize*)&roiSize, scaleFactor);
}

VnxippApi vnxippiSub_8u_C1IRSfs(const uint8_t* pSrc, int srcStep, uint8_t* pSrcDst,
                                int srcDstStep, VnxIppiSize roiSize, int scaleFactor)
{
    return ippiSub_8u_C1IRSfs(pSrc, srcStep, pSrcDst, srcDstStep, *(IppiSize*)&roiSize, scaleFactor);
}

VnxippApi vnxippiAbsDiff_8u_C1R(const uint8_t* pSrc1, int src1Step,
                                const uint8_t* pSrc2, int src2Step,
                                uint8_t* pDst, int dstStep, VnxIppiSize roiSize)
{
    return ippiAbsDiff_8u_C1R(pSrc1, src1Step, pSrc2, src2Step, pDst, dstStep, *(IppiSize*)&roiSize);
}

VnxippApi vnxippiCountInRange_8u_C1R(const uint8_t* pSrc, int srcStep, VnxIppiSize roiSize,
                                     int* counts, uint8_t lowerBound, uint8_t upperBound)
{
    return ippiCountInRange_8u_C1R(pSrc, srcStep, *(IppiSize*)&roiSize, counts, lowerBound, upperBound);
}

VnxippApi vnxippiMulC_8u_C1RSfs(const uint8_t* pSrc, int srcStep, uint8_t value, uint8_t* pDst,
                                int dstStep, VnxIppiSize roiSize, int scaleFactor)
{
    return ippiMulC_8u_C1RSfs(pSrc, srcStep, value, pDst, dstStep, *(IppiSize*)&roiSize, scaleFactor);
}



#endif
