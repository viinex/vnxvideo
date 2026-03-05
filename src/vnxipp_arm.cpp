#if defined(__aarch64__)

#include <cstdlib>
#include <cstring>
#include <memory>
#include <algorithm>
#include "vnxipp.h"
#include <arm_neon.h>

extern "C"{
#include <libswscale/swscale.h>
}

VnxippApi vnxippInit(void) {
    return vnxippStsNoErr;
}

VnxippApi vnxippiCopy_8u_C1R(const uint8_t* pSrc, int srcStep,
    uint8_t* pDst, int dstStep, VnxIppiSize roiSize)
{
    auto src = pSrc;
    auto dst = pDst;
    auto srcEnd = pSrc + srcStep*roiSize.height;
    while (src < srcEnd) {
        memcpy(dst, src, roiSize.width);
        src += srcStep;
        dst += dstStep;
    }
    return vnxippStsNoErr;
}
VnxippApi vnxippiSet_8u_C1R(uint8_t value, uint8_t* pDst, int dstStep,
    VnxIppiSize roiSize)
{
    auto dst = pDst;
    auto dstEnd = pDst + dstStep*roiSize.height;
    while (dst < dstEnd) {
        memset(dst, value, roiSize.width);
        dst += dstStep;
    }
    return vnxippStsNoErr;
}
extern "C" void vnxippFree(void* ptr)
{
    free(ptr);
}
extern "C" void* vnxippMalloc(int length)
{
    return malloc(length);
}

VnxippApi vnxippiCopy_8u_C1MR(const uint8_t* pSrc, int srcStep,
    uint8_t* pDst, int dstStep, VnxIppiSize roiSize,
    const uint8_t* pMask, int maskStep)
{
    auto src = pSrc;
    auto dst = pDst;
    auto msk = pMask;
    auto srcEnd = pSrc + srcStep*roiSize.height;
    int w8 = roiSize.width / 8;
    while (src < srcEnd) {
#if defined(__aarch64__)
        auto src1 = src;
        auto dst1 = dst;
        auto msk1 = msk;
        for (int k = 0; k < w8; ++k) {
            uint8x8_t s = vld1_u8(src1);
            uint8x8_t d = vld1_u8(dst1);
            uint8x8_t m = vld1_u8(msk1);
            uint8x8_t md = vbic_u8(d, m);
            uint8x8_t ms = vand_u8(s, m);
            uint8x8_t r = vorr_u8(md, ms);
            vst1_u8(dst1, r);
            src1 += 8;
            dst1 += 8;
            msk1 += 8;
}
#else
        for (int k = 0; k < roiSize.width; ++k)
            if (msk[k])
                dst[k] = src[k];
#endif
        src += srcStep;
        dst += dstStep;
        msk += maskStep;
    }
    return vnxippStsNoErr;
}

VnxippApi vnxippiCopyWrapBorder_32s_C1R(const int32_t* pSrc, int srcStep, VnxIppiSize srcRoiSize,
    int32_t* pDst, int dstStep, VnxIppiSize dstRoiSize,
    int topBorderHeight, int leftBorderWidth)
{
    const int xc = (int)ceil(double(dstRoiSize.width) / double(srcRoiSize.width));
    const int yc = (int)ceil(double(dstRoiSize.height) / double(srcRoiSize.height));
    for (int x = 0; x < xc; ++x) {
        for (int y = 0; y < yc; ++y) {
            vnxippiCopy_8u_C1R((uint8_t*)pSrc, srcStep, (uint8_t*)pDst + dstStep*y*srcRoiSize.height + x*srcRoiSize.width*4, dstStep,
                { std::min(srcRoiSize.width * 4, (dstRoiSize.width-x*srcRoiSize.width)*4), 
                  std::min(srcRoiSize.height, dstRoiSize.height - y*srcRoiSize.height) });
        }
    }
    return vnxippStsNoErr;
}

VnxippApi vnxippiBGRToYCbCr420_8u_C3P3R(const uint8_t*  pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize)
{
    std::shared_ptr<SwsContext> ctx(sws_getContext(roiSize.width, roiSize.height, AV_PIX_FMT_BGR24,
        roiSize.width, roiSize.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    sws_scale(ctx.get(), &pSrc, &srcStep, 0, roiSize.height, pDst, dstStep);
    return vnxippStsNoErr;
}

VnxippApi vnxippiYCbCr422ToYCbCr420_8u_C2P3R(const uint8_t* pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize)
{
    std::shared_ptr<SwsContext> ctx(sws_getContext(roiSize.width, roiSize.height, AV_PIX_FMT_YUYV422,
        roiSize.width, roiSize.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    sws_scale(ctx.get(), &pSrc, &srcStep, 0, roiSize.height, pDst, dstStep);
    return vnxippStsNoErr;
}

VnxippApi vnxippiCbYCr422ToYCrCb420_8u_C2P3R(const uint8_t* pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize)
{
    std::shared_ptr<SwsContext> ctx(sws_getContext(roiSize.width, roiSize.height, AV_PIX_FMT_UYVY422,
        roiSize.width, roiSize.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    sws_scale(ctx.get(), &pSrc, &srcStep, 0, roiSize.height, pDst, dstStep);
    return vnxippStsNoErr;
}

VnxippApi vnxippiBGRToYCbCr420_8u_AC4P3R(const uint8_t*  pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize)
{
    std::shared_ptr<SwsContext> ctx(sws_getContext(roiSize.width, roiSize.height, AV_PIX_FMT_BGRA,
        roiSize.width, roiSize.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    sws_scale(ctx.get(), &pSrc, &srcStep, 0, roiSize.height, pDst, dstStep);
    return vnxippStsNoErr;
}

VnxippApi vnxippiWarpPerspective_8u_C1R(const uint8_t* pSrc, VnxIppiSize srcSize, int srcStep, VnxIppiRect srcRoi, uint8_t* pDst, int dstStep, VnxIppiRect dstRoi, const double coeffs[3][3], int interpolation)
{
    return vnxippStsErr;
}

VnxippApi vnxippiThreshold_LTVal_8u_C1R(const uint8_t* pSrc, int srcStep,
    uint8_t* pDst, int dstStep, VnxIppiSize roiSize, uint8_t threshold,
    uint8_t value)
{
    return vnxippStsErr;
}
VnxippApi vnxippiThreshold_LTVal_8u_C1IR(uint8_t* pSrcDst, int srcDstStep,
    VnxIppiSize roiSize, uint8_t threshold, uint8_t value)
{
    return vnxippStsErr;
}

VnxippApi vnxippiThreshold_GTVal_8u_C1IR(uint8_t* pSrcDst, int srcDstStep,
    VnxIppiSize roiSize, uint8_t threshold, uint8_t value)
{
    return vnxippStsErr;
}

VnxippApi vnxippiCompare_8u_C1R(const uint8_t* pSrc1, int src1Step,
    const uint8_t* pSrc2, int src2Step,
    uint8_t* pDst, int dstStep,
    VnxIppiSize roiSize, VnxIppCmpOp ippCmpOp)
{
    return vnxippStsErr;
}

VnxippApi vnxippiAnd_8u_C1IR(const uint8_t* pSrc, int srcStep, uint8_t* pSrcDst, int srcDstStep, VnxIppiSize roiSize)
{
    return vnxippStsErr;
}

VnxippApi vnxippiAndC_8u_C1IR(uint8_t value, uint8_t* pSrcDst, int srcDstStep, VnxIppiSize roiSize)
{
    return vnxippStsErr;
}

VnxippApi vnxippiAdd_8u_C1IRSfs(const uint8_t* pSrc, int srcStep, uint8_t* pSrcDst,
    int srcDstStep, VnxIppiSize roiSize, int scaleFactor)
{
    return vnxippStsErr;
}

VnxippApi vnxippiSub_8u_C1IRSfs(const uint8_t* pSrc, int srcStep, uint8_t* pSrcDst,
    int srcDstStep, VnxIppiSize roiSize, int scaleFactor)
{
    return vnxippStsErr;
}

VnxippApi vnxippiAbsDiff_8u_C1R(const uint8_t* pSrc1, int src1Step,
    const uint8_t* pSrc2, int src2Step,
    uint8_t* pDst, int dstStep, VnxIppiSize roiSize)
{
    return vnxippStsErr;
}

VnxippApi vnxippiCountInRange_8u_C1R(const uint8_t* pSrc, int srcStep, VnxIppiSize roiSize,
    int* counts, uint8_t lowerBound, uint8_t upperBound)
{
    return vnxippStsErr;
}

VnxippApi vnxippiMulC_8u_C1RSfs(const uint8_t* pSrc, int srcStep, uint8_t value, uint8_t* pDst,
    int dstStep, VnxIppiSize roiSize, int scaleFactor)
{
    return vnxippStsErr;
}

#endif
