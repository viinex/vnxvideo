#include <cstdlib>
#include <cstring>
#include <memory>
#include <algorithm>
#include <ippi.h>
#include "vnxipp.h"

extern "C"{
#include <libswscale/swscale.h>
}

int vnxippiBGR565ToYCbCr420_16u8u_C3P3R(const uint16_t* pSrc, int srcStep, uint8_t* pDst[3], int dstStep[3], VnxIppiSize roiSize)
{
    std::shared_ptr<SwsContext> ctx(sws_getContext(roiSize.width, roiSize.height, AV_PIX_FMT_BGR565LE,
        roiSize.width, roiSize.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    sws_scale(ctx.get(), (const uint8_t**)&pSrc, &srcStep, 0, roiSize.height, pDst, dstStep);
    return 0;
}

int vnxippiResize_8u_C1R(const uint8_t* pSrc, VnxIppiSize srcSize, int srcStep, VnxIppiRect srcRoi,
    uint8_t* pDst, int dstStep, VnxIppiSize dstRoiSize,
    double xFactor, double yFactor, int interpolation)
{
    int swsInterpolation = (interpolation == IPPI_INTER_NN) ? SWS_POINT : SWS_FAST_BILINEAR;
    std::shared_ptr<SwsContext> ctx(sws_getContext(srcRoi.width, srcRoi.height, AV_PIX_FMT_GRAY8,
        dstRoiSize.width, dstRoiSize.height, AV_PIX_FMT_GRAY8, swsInterpolation, nullptr, nullptr, nullptr), sws_freeContext);
    const uint8_t* srcs = pSrc + srcRoi.y*srcStep + srcRoi.x;
    sws_scale(ctx.get(), &srcs, &srcStep, 0, srcRoi.height, &pDst, &dstStep);
    return 0;
}

// YUV 2-plane NV12 to 3-plane 4:2:0
int vnxippiResize_8u_P2P3R(const uint8_t* const* pSrc, VnxIppiSize srcSize, const int* srcStep, VnxIppiRect srcRoi,
    uint8_t** pDst, int* dstStep, VnxIppiRect dstRoi,
    double xFactor, double yFactor, int interpolation)
{
    int swsInterpolation = (interpolation == IPPI_INTER_NN) ? SWS_POINT : SWS_FAST_BILINEAR;
    std::shared_ptr<SwsContext> ctx(sws_getContext(srcRoi.width, srcRoi.height, AV_PIX_FMT_NV12,
        dstRoi.width, dstRoi.height, AV_PIX_FMT_YUV420P, swsInterpolation, nullptr, nullptr, nullptr), sws_freeContext);

    const uint8_t* src_planes_roi[2] = {
        pSrc[0] + srcRoi.y*srcStep[0] + srcRoi.x,
        pSrc[1] + srcRoi.y*srcStep[1] / 2 + srcRoi.x / 2,
    };
    uint8_t* planes_roi[3] = {
        pDst[0] + dstRoi.y*dstStep[0] + dstRoi.x,
        pDst[1] + dstRoi.y*dstStep[1] / 2 + dstRoi.x / 2,
        pDst[2] + dstRoi.y*dstStep[2] / 2 + dstRoi.x / 2
    };
    int res = sws_scale(ctx.get(), src_planes_roi, srcStep, 0, srcRoi.height, planes_roi, dstStep);
    if (res == dstRoi.height)
        return 0;
    else
        return res;
}

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

#if defined(__aarch64__)

#define IppApi extern "C" IppStatus __STDCALL

IppApi ippStaticInit(void) {
    return ippStsNoErr;
}

IppApi ippiCopy_8u_C1R(const Ipp8u* pSrc, int srcStep,
    Ipp8u* pDst, int dstStep, IppiSize roiSize)
{
    auto src = pSrc;
    auto dst = pDst;
    auto srcEnd = pSrc + srcStep*roiSize.height;
    while (src < srcEnd) {
        memcpy(dst, src, roiSize.width);
        src += srcStep;
        dst += dstStep;
    }
    return ippStsNoErr;
}
IppApi ippiSet_8u_C1R(Ipp8u value, Ipp8u* pDst, int dstStep,
    IppiSize roiSize)
{
    auto dst = pDst;
    auto dstEnd = pDst + dstStep*roiSize.height;
    while (dst < dstEnd) {
        memset(dst, value, roiSize.width);
        dst += dstStep;
    }
    return ippStsNoErr;
}
extern "C" void ippFree(void* ptr)
{
    free(ptr);
}
extern "C" void* ippMalloc(int length)
{
    return malloc(length);
}

IppApi ippiCopy_8u_C1MR(const Ipp8u* pSrc, int srcStep,
    Ipp8u* pDst, int dstStep, IppiSize roiSize,
    const Ipp8u* pMask, int maskStep)
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
    return ippStsNoErr;
}

IppApi ippiCopyWrapBorder_32s_C1R(const Ipp32s* pSrc, int srcStep, IppiSize srcRoiSize,
    Ipp32s* pDst, int dstStep, IppiSize dstRoiSize,
    int topBorderHeight, int leftBorderWidth)
{
    const int xc = (int)ceil(double(dstRoiSize.width) / double(srcRoiSize.width));
    const int yc = (int)ceil(double(dstRoiSize.height) / double(srcRoiSize.height));
    for (int x = 0; x < xc; ++x) {
        for (int y = 0; y < yc; ++y) {
            ippiCopy_8u_C1R((uint8_t*)pSrc, srcStep, (uint8_t*)pDst + dstStep*y*srcRoiSize.height + x*srcRoiSize.width*4, dstStep,
                { std::min(srcRoiSize.width * 4, (dstRoiSize.width-x*srcRoiSize.width)*4), 
                  std::min(srcRoiSize.height, dstRoiSize.height - y*srcRoiSize.height) });
        }
    }
    return ippStsNoErr;
}

IppApi ippiBGRToYCbCr420_8u_C3P3R(const Ipp8u*  pSrc, int srcStep, Ipp8u* pDst[3], int dstStep[3], IppiSize roiSize)
{
    std::shared_ptr<SwsContext> ctx(sws_getContext(roiSize.width, roiSize.height, AV_PIX_FMT_BGR24,
        roiSize.width, roiSize.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    sws_scale(ctx.get(), &pSrc, &srcStep, 0, roiSize.height, pDst, dstStep);
    return ippStsNoErr;
}

IppApi ippiYCbCr422ToYCbCr420_8u_C2P3R(const Ipp8u* pSrc, int srcStep, Ipp8u* pDst[3], int dstStep[3], IppiSize roiSize)
{
    std::shared_ptr<SwsContext> ctx(sws_getContext(roiSize.width, roiSize.height, AV_PIX_FMT_YUYV422,
        roiSize.width, roiSize.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    sws_scale(ctx.get(), &pSrc, &srcStep, 0, roiSize.height, pDst, dstStep);
    return ippStsNoErr;
}

IppApi ippiCbYCr422ToYCrCb420_8u_C2P3R(const Ipp8u* pSrc, int srcStep, Ipp8u* pDst[3], int dstStep[3], IppiSize roiSize)
{
    std::shared_ptr<SwsContext> ctx(sws_getContext(roiSize.width, roiSize.height, AV_PIX_FMT_UYVY422,
        roiSize.width, roiSize.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    sws_scale(ctx.get(), &pSrc, &srcStep, 0, roiSize.height, pDst, dstStep);
    return ippStsNoErr;
}

IppApi ippiBGRToYCbCr420_8u_AC4P3R(const Ipp8u*  pSrc, int srcStep, Ipp8u* pDst[3], int dstStep[3], IppiSize roiSize)
{
    std::shared_ptr<SwsContext> ctx(sws_getContext(roiSize.width, roiSize.height, AV_PIX_FMT_BGRA,
        roiSize.width, roiSize.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
    sws_scale(ctx.get(), &pSrc, &srcStep, 0, roiSize.height, pDst, dstStep);
    return ippStsNoErr;
}

IppApi ippiWarpPerspective_8u_C1R(const Ipp8u* pSrc, IppiSize srcSize, int srcStep, IppiRect srcRoi, Ipp8u* pDst, int dstStep, IppiRect dstRoi, const double coeffs[3][3], int interpolation)
{
    return ippStsErr;
}

IppApi ippiHistogramRange_8u_C1R(const Ipp8u* pSrc, int srcStep, IppiSize roiSize, Ipp32s* pHist, const Ipp32s* pLevels, int nLevels)
{
    return ippStsErr;
}


IppApi ippiFilterLaplace_8u_C1R(const Ipp8u* pSrc, int srcStep,
    Ipp8u* pDst, int dstStep, IppiSize roiSize, IppiMaskSize mask)
{
    return ippStsErr;
}

IppApi ippiThreshold_LTVal_8u_C1R(const Ipp8u* pSrc, int srcStep,
    Ipp8u* pDst, int dstStep, IppiSize roiSize, Ipp8u threshold,
    Ipp8u value)
{
    return ippStsErr;
}
IppApi ippiThreshold_LTVal_8u_C1IR(Ipp8u* pSrcDst, int srcDstStep,
    IppiSize roiSize, Ipp8u threshold, Ipp8u value)
{
    return ippStsErr;
}

IppApi ippiThreshold_GTVal_8u_C1IR(Ipp8u* pSrcDst, int srcDstStep,
    IppiSize roiSize, Ipp8u threshold, Ipp8u value)
{
    return ippStsErr;
}

IppApi ippiCompare_8u_C1R(const Ipp8u* pSrc1, int src1Step,
    const Ipp8u* pSrc2, int src2Step,
    Ipp8u* pDst, int dstStep,
    IppiSize roiSize, IppCmpOp ippCmpOp)
{
    return ippStsErr;
}

IppApi ippiAnd_8u_C1IR(const Ipp8u* pSrc, int srcStep, Ipp8u* pSrcDst, int srcDstStep, IppiSize roiSize)
{
    return ippStsErr;
}

IppApi ippiAndC_8u_C1IR(Ipp8u value, Ipp8u* pSrcDst, int srcDstStep, IppiSize roiSize)
{
    return ippStsErr;
}

IppApi ippiAdd_8u_C1IRSfs(const Ipp8u* pSrc, int srcStep, Ipp8u* pSrcDst,
    int srcDstStep, IppiSize roiSize, int scaleFactor)
{
    return ippStsErr;
}

IppApi ippiSub_8u_C1IRSfs(const Ipp8u* pSrc, int srcStep, Ipp8u* pSrcDst,
    int srcDstStep, IppiSize roiSize, int scaleFactor)
{
    return ippStsErr;
}

IppApi ippiAbsDiff_8u_C1R(const Ipp8u* pSrc1, int src1Step,
    const Ipp8u* pSrc2, int src2Step,
    Ipp8u* pDst, int dstStep, IppiSize roiSize)
{
    return ippStsErr;
}

IppApi ippiErode3x3_8u_C1IR(Ipp8u*  pSrcDst, int srcDstStep, IppiSize roiSize)
{
    return ippStsErr;
}

IppApi ippiDilate3x3_8u_C1IR(Ipp8u*  pSrcDst, int srcDstStep, IppiSize roiSize)
{
    return ippStsErr;
}

IppApi ippiCountInRange_8u_C1R(const Ipp8u* pSrc, int srcStep, IppiSize roiSize,
    int* counts, Ipp8u lowerBound, Ipp8u upperBound)
{
    return ippStsErr;
}

IppApi ippiMulC_8u_C1RSfs(const Ipp8u* pSrc, int srcStep, Ipp8u value, Ipp8u* pDst,
    int dstStep, IppiSize roiSize, int scaleFactor)
{
    return ippStsErr;
}

#endif
