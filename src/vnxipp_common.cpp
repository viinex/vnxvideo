// polyfills for deprecated functions
#include <cstdlib>
#include <cstring>
#include <memory>
#include <algorithm>
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
    int swsInterpolation = (interpolation == VNXIPPI_INTER_NN) ? SWS_POINT : SWS_FAST_BILINEAR;
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
    int swsInterpolation = (interpolation == VNXIPPI_INTER_NN) ? SWS_POINT : SWS_FAST_BILINEAR;
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

VnxippApi vnxippStaticInit(void) {
    return 0;
}
