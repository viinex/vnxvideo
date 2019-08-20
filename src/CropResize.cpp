#include <ippi.h>

extern "C" {
#include <libswscale/swscale.h>
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

#include "RawSample.h"

int vnxvideo_raw_sample_crop_resize(vnxvideo_raw_sample_t in,
    int roi_left, int roi_top, int roi_width, int roi_height, 
    int target_width, int target_height, 
    vnxvideo_raw_sample_t* out) 
{
    auto s = reinterpret_cast<VnxVideo::IRawSample*>(in.ptr);
    EColorspace csp;
    int width, height;
    s->GetFormat(csp, width, height);
    if (csp != EMF_I420) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "vnxvideo_raw_sample_crop_resize() does not support image format " << csp;
        return vnxvideo_err_invalid_parameter;
    }
    if (roi_left < 0 || roi_top < 0 || roi_width <= 0 || roi_height <= 0 || target_width <= 0 || target_height <= 0) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "vnxvideo_raw_sample_crop_resize: roi left/top cannot be negative, roi and target size cannot be non-positive";
        return vnxvideo_err_invalid_parameter;
    }

    if ((roi_left + roi_width > width) || (roi_top + roi_height > height)) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "vnxvideo_raw_sample_crop_resize: roi size exceeds image size";
        return vnxvideo_err_invalid_parameter;
    }
    int stridesSrc[4];
    uint8_t* planesSrc[4];
    s->GetData(stridesSrc, planesSrc);

    VnxVideo::IRawSample* res = new CRawSample(target_width, target_height);
    int stridesDst[4];
    uint8_t* planesDst[4];
    res->GetData(stridesDst, planesDst);

    if (roi_width == target_width && roi_height == target_height) { // size matches -- just copy the data taking ROI into account
        for (int k = 0; k < 3; ++k) {
            int div = (k == 0) ? 1 : 2; // plane dimension size divisor wrt to original size
            IppiSize roi = { target_width / div, target_height / div };
            IppStatus st=ippiCopy_8u_C1R(planesSrc[k] + roi_top*stridesSrc[k] / div + roi_left / div,
                stridesSrc[k], planesDst[k], stridesDst[k], roi);
            if (st != ippStsNoErr) {
                VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "vnxvideo_raw_sample_crop_resize: ippiCopy_8u_C1R returned a non-ok code: " << st;
                return vnxvideo_err_external_api;
            }
        }
    }
    else { // size does not match, resize image
        std::shared_ptr<SwsContext> ctx(sws_getContext(roi_width, roi_height, AV_PIX_FMT_YUV420P,
            target_width, target_height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr), sws_freeContext);
        const uint8_t* srcs[3] = {
            planesSrc[0] + roi_left + roi_top*stridesSrc[0],
            planesSrc[1] + roi_left/2 + roi_top*stridesSrc[1]/2,
            planesSrc[2] + roi_left / 2 + roi_top*stridesSrc[2] / 2
        };
        int res=sws_scale(ctx.get(), srcs, stridesSrc, 0, roi_height, planesDst, stridesDst);
        if (res <= 0) {
            VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "vnxvideo_raw_sample_crop_resize: sws_scale  returned a non-ok code: " << res;
            return vnxvideo_err_external_api;
        }
    }
    out->ptr = res;
    return vnxvideo_err_ok;
}


int vnxvideo_raw_sample_select_roi(vnxvideo_raw_sample_t in,
    int roi_left, int roi_top, int roi_width, int roi_height,
    vnxvideo_raw_sample_t* out)
{
    auto s = reinterpret_cast<VnxVideo::IRawSample*>(in.ptr);
    try {
        VnxVideo::IRawSample *res = new CRawSampleRoi(s, roi_left, roi_top, roi_width, roi_height);
        out->ptr = res;
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "vnxvideo_raw_sample_select_roi: exception: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

namespace VnxVideo {
    IRawSample* CopyRawToI420(IRawSample* sample) {
        int strides[4];
        uint8_t* planes[4];
        int width, height;
        EColorspace csp;
        sample->GetFormat(csp, width, height);
        sample->GetData(strides, planes);
        return new CRawSample(csp, width, height, strides, planes, true);
    }
}