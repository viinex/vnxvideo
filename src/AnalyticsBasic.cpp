#ifndef __aarch64__
#include <algorithm>
#include <boost/lexical_cast.hpp>

#include "json.hpp"
#include "jget.h"

using json = nlohmann::json;

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"
#include "GrayAnalyticsBase.h"

#include <ipp.h>
#include <ippi.h>
#include <ippcv.h>

extern "C" {
#include <libswscale/swscale.h>
}

void vnxHistogramBasic_8u(const uint8_t* data, int stride, int width, int height, int* histogram) {
    memset(histogram, 0, 256*sizeof(int));
    for (int y = 0; y < height; ++y) {
        const uint8_t* ptr = data + stride*y;
        for (int x = 0; x < width; ++x)
            ++histogram[ptr[x]];
    }
}
void vnxMeanVariance_8u(const uint8_t* data, int stride, int width, int height, double& mean, double& variance) {
    uint64_t vari = 0;
    uint32_t meani = 0;
    for (int y = 0; y < height; ++y) {
        const uint8_t* ptr = data + stride*y;
        for (int x = 0; x < width; ++x) {
            int val = ptr[x];
            meani += val;
            vari += val*val;
        }
    }
    mean = double(meani)/double(width*height);
    variance = double(vari) / double(width*height);
    variance -= mean*mean;
}


const int motionCellsH = 8;
const int motionCellsV = 6;

struct SBasicAnalyticsStatus {
    bool alarmTooDark;
    bool alarmTooBright;
    bool alarmTooBlurry;
    uint64_t motionMask;
    bool alarmMotion;
    bool alarmGlobalChange;
    uint64_t timestamp;
    SBasicAnalyticsStatus() {
        memset(this, 0, sizeof *this);
    }
    int alertsMask() {
        return (alarmTooDark << 0) + (alarmTooBright << 1) + (alarmTooBlurry << 2) + (alarmMotion << 3) + (alarmGlobalChange << 4);
    }
};

class CBasicAnalytics : public CGrayAnalyticsBase {
public:
    CBasicAnalytics(const std::vector<float>& roi, float framerate, bool too_bright, bool too_dark, bool too_blurry, float motion, bool scene_change)
        : CGrayAnalyticsBase(roi) 
        , detect_too_bright(too_bright)
        , detect_too_dark(too_dark)
        , detect_too_blurry(too_blurry)
        , detect_motion(motion)
        , detect_scene_change(scene_change)
        , skip_rate(framerate_to_skip_rate(framerate))
    {
        m_histogram.resize(256);
        m_histogramSum.resize(256);
        m_motionCells.resize(motionCellsH * motionCellsV);
    }
protected:
    virtual void reset(int width, int height) {
        m_ratio = std::max(1, std::min(width / 320, height/200));
        m_width = width / m_ratio;
        m_height = height / m_ratio;

        m_bufferLaplace.reset();
        m_bufferMorph.reset();
        m_morphSpec.reset();

        m_frameNumber = 0;
        m_stride = (m_width % 16) ? ((m_width / 16 + 1) * 16) : m_width;
        for (auto b : { &m_data, &m_buffer0, &m_buffer1, &m_motionBackground, &m_motionVariance, &m_motionLabel, &m_motionDelta }) {
            b->reset((uint8_t*)ippMalloc(m_stride*height), ippFree);
            memset(b->get(), 0, m_stride*height);
        }
        m_resizeCtx.reset(sws_getContext(width, height, AV_PIX_FMT_GRAY8,
            m_width, m_height, AV_PIX_FMT_GRAY8, SWS_POINT, nullptr, nullptr, nullptr), sws_freeContext);
    }
    virtual void process(uint8_t* data, int width, int stride, int height, uint64_t timestamp) {
        //auto b = ippGetCpuClocks();
        
        //VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "timestamp diff: " << timestamp - m_status.timestamp;
        if (m_frameNumber != 0 && (skip_rate > 0) && (timestamp - m_status.timestamp) < 40 * (1 << skip_rate)) {
            // uncomment to show result (on each frame)
            //ippiCopy_8u_C1R(m_motionLabel.get(), m_stride, data + width / 2 + height*stride / 2, stride, { m_width, m_height });
            return;
        }
        uint8_t* dst = m_data.get();
        sws_scale(m_resizeCtx.get(), &data, &stride, 0, height, &dst, &m_stride);

        if (detect_too_bright || detect_too_dark)
            detectTooBrightDark(m_data.get(), m_width, m_stride, m_height);
        if (detect_too_blurry)
            detectTooBlurry(m_data.get(), m_width, m_stride, m_height);
        if (detect_motion)
            detectMotion(m_data.get(), m_width, m_stride, m_height);
        //auto e = ippGetCpuClocks();
        //VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "Clocks elapsed: " << e-b;

        // uncomment this to show the resulting motion labels right on the image.
        //ippiCopy_8u_C1R(m_motionLabel.get(), m_stride, data + width / 2 + height*stride / 2, stride, { m_width, m_height });
        m_status.timestamp = timestamp;
        sendEvents();
    }
    void sendEvents() {
        json j(json::object());
        if (detect_motion && (m_status.alarmMotion || m_lastSentStatus.alarmMotion)) {
            j["motion"] = m_status.alarmMotion;
            j["motion_mask"] = m_status.motionMask;
        }
        if(detect_scene_change && (m_status.alarmGlobalChange || m_lastSentStatus.alarmGlobalChange))
            j["scene_changed"] = m_status.alarmGlobalChange;
        if (detect_too_bright && (m_status.alarmTooBright || m_lastSentStatus.alarmTooBright))
            j["too_bright"] = m_status.alarmTooBright;
        if(detect_too_dark && (m_status.alarmTooDark || m_lastSentStatus.alarmTooDark))
            j["too_dark"] = m_status.alarmTooDark;
        if(detect_too_blurry && (m_status.alarmTooBlurry || m_lastSentStatus.alarmTooBlurry))
            j["too_blurry"] = m_status.alarmTooBlurry;

        if ((m_status.alertsMask() != m_lastSentStatus.alertsMask()) ||
            ((m_status.alertsMask() != 0) && (m_status.timestamp - m_lastSentStatus.timestamp) >= 1000)) {
            sendJson(j, m_status.timestamp);
            m_lastSentStatus = m_status;
        }
    }
private:
    const bool detect_too_bright;
    const bool detect_too_dark;
    const bool detect_too_blurry;
    const float detect_motion;
    const bool detect_scene_change;
    const int skip_rate; // corresponds to 0 -> 25-30, 1 -> 10, 2 -> 5, 3 -> 2 or less FPS
                        // frames are skipped IF actual framerate exceeds that values. Otherwise, nothing is actually skipped.
                        // That is, if FPS is limited to 5 at source, and skip_rate=1, - each frame will be processed.

    std::vector<int> m_histogram;
    std::vector<int> m_histogramSum;
    int m_stride;
    int m_ratio;
    int m_width;
    int m_height;
    std::shared_ptr<SwsContext> m_resizeCtx;
    std::shared_ptr<uint8_t> m_data;
    std::shared_ptr<uint8_t> m_buffer0;
    std::shared_ptr<uint8_t> m_buffer1;
    std::shared_ptr<uint8_t> m_bufferLaplace;
    std::shared_ptr<uint8_t> m_bufferMorph;
    std::shared_ptr<IppiMorphState> m_morphSpec;

    std::shared_ptr<uint8_t> m_motionBackground;
    std::shared_ptr<uint8_t> m_motionDelta;
    std::shared_ptr<uint8_t> m_motionVariance;
    std::shared_ptr<uint8_t> m_motionLabel;

    std::vector<int> m_motionCells;

    SBasicAnalyticsStatus m_status;
    SBasicAnalyticsStatus m_lastSentStatus;

    int m_frameNumber;
private:
    void detectTooBrightDark(uint8_t* data, int width, int stride, int height) {
        vnxHistogramBasic_8u(data, stride, width, height, &m_histogram[0]);
        int sum = 0;
        for (int k = 0; k < 256; ++k) {
            sum += m_histogram[k];
            m_histogramSum[k] = sum;
        }
        const int histogramTotal = m_histogramSum[255];
        const int histogram10perc = histogramTotal * 1 / 10;
        const int histogram90perc = histogramTotal * 9 / 10;
        if (m_histogramSum[256 * 1 / 3] > histogram90perc) {
            //VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "Image too dark";
            if(detect_too_dark)
                m_status.alarmTooDark = true;
            m_status.alarmTooBright = false;
        }
        else if (m_histogramSum[256 * 2 / 3] < histogram10perc) {
            //VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "Image too bright";
            if(detect_too_bright)
                m_status.alarmTooBright = true;
            m_status.alarmTooDark = false;
        }
        else
            m_status.alarmTooBright = m_status.alarmTooDark = false;
    }
    void detectTooBlurry(uint8_t* data, int width, int stride, int height) {
        IppStatus s;
        if (m_bufferLaplace.get() == nullptr) {
            int sz;
            s=ippiFilterLaplaceBorderGetBufferSize({ width,height }, ippMskSize3x3, ipp8u, ipp8u, 1, &sz);
            if(s!=ippStsNoErr)
                throw std::runtime_error("ippiFilterLaplaceBorderGetBufferSize failed: "+boost::lexical_cast<std::string>(s));
            m_bufferLaplace.reset(reinterpret_cast<Ipp8u*>(ippMalloc(sz)), ippFree);
        }
        s = ippiFilterLaplaceBorder_8u_C1R(data, stride, m_buffer0.get(), m_stride, { width, height }, ippMskSize3x3,
            ippBorderConst, 0, m_bufferLaplace.get());
        if (s != ippStsNoErr)
            throw std::runtime_error("Could not perform ippiFilterLaplace_8uC1R");

        vnxHistogramBasic_8u(m_buffer0.get(), m_stride, width, height, &m_histogram[0]);
        if (s != ippStsNoErr)
            throw std::runtime_error("Could not perform ippiHistogramRange_8u_C1R");
        int sum = 0;
        for (int k = 0; k < 256; ++k) {
            sum += m_histogram[k];
            m_histogramSum[k] = sum;
        }
        int laplace90 = 0;
        int laplace95 = 0;
        for (int k = 0; k < 256; ++k) {
            if (!laplace90 && (m_histogramSum[k] >= (m_histogramSum[255] * 90) / 100))
                laplace90 = k;
            if (!laplace95 && (m_histogramSum[k] >= (m_histogramSum[255] * 95) / 100))
                laplace95 = k;
        }
        double d = double(laplace95 - laplace90) / double(laplace90 + 1);
        if (d<0.45) {
            m_status.alarmTooBlurry = true;
        }
        else
            m_status.alarmTooBlurry = false;
    }

    static void sigmaDeltaAdjust(uint8_t* data, int dstride, // data, reference to update to
        uint8_t* result, int rstride, // result - inout buffer to be updated/adjusted
        uint8_t* mask, int mstride, // mask - where to update. optional, may be 0
        uint8_t* buffer, int bstride, // temporary buffer
        IppiSize size, uint8_t learningRate) 
    {
        ippiCompare_8u_C1R(data, dstride, result, rstride, buffer, bstride, size, ippCmpGreater);
        if (nullptr != mask)
            ippiAnd_8u_C1IR(mask, mstride, buffer, bstride, size);
        ippiAndC_8u_C1IR(learningRate, buffer, bstride, size);
        ippiAdd_8u_C1IRSfs(buffer, bstride, result, rstride, size, 0);
        ippiCompare_8u_C1R(data, dstride, result, rstride, buffer, bstride, size, ippCmpLess);
        if (nullptr != mask)
            ippiAnd_8u_C1IR(mask, mstride, buffer, bstride, size);
        ippiAndC_8u_C1IR(learningRate, buffer, bstride, size);
        ippiSub_8u_C1IRSfs(buffer, bstride, result, rstride, size, 0);
    }
    void detectMotion(uint8_t* data, int width, int stride, int height) {
        //"Zipfian estimation"
        //http://perso.ensta-paristech.fr/~manzaner/Publis/icip09.pdf
        //MOTION DETECTION: FAST AND ROBUST ALGORITHMS FOR EMBEDDED SYSTEMS
        //L. Lacassagne A.Manzanera
        const IppiSize size = { width, height };
        if(0 == m_frameNumber)
            ippiCopy_8u_C1R(data, stride, m_motionBackground.get(), m_stride, size);
        else {
            int sigma=1;
            int t = m_frameNumber % (64 >> skip_rate);
            while (t / (sigma * 2) > 0)
                sigma *= 2;
            // mask - where background should be updated
            IppStatus st;
            st=ippiThreshold_LTVal_8u_C1R(m_motionVariance.get(), m_stride,
                m_buffer1.get(), m_stride, size,
                sigma, 0);
            st = ippiThreshold_GTVal_8u_C1IR(m_buffer1.get(), m_stride, size,
                sigma-1, 255); 
            sigmaDeltaAdjust(data, stride, m_motionBackground.get(), m_stride,
                m_buffer1.get(), m_stride, // mask
                m_buffer0.get(), m_stride, // temp buffer
                size, skip_rate + 1);
        }

        ippiAbsDiff_8u_C1R(m_motionBackground.get(), m_stride, data, stride, m_motionDelta.get(), m_stride, size);

        if (0 == (m_frameNumber % 4)) { // T_V
            ippiMulC_8u_C1RSfs(m_motionDelta.get(), m_stride, 4, m_buffer1.get(), m_stride, size, 0);

            if (0 == m_frameNumber)
                ippiCopy_8u_C1R(m_buffer1.get(), m_stride, m_motionVariance.get(), m_stride, size);
            else
                sigmaDeltaAdjust(m_buffer1.get(), m_stride, m_motionVariance.get(), m_stride,
                    0, 0, // no mask
                    m_buffer0.get(), m_stride, size, skip_rate + 1);
            ippiThreshold_LTVal_8u_C1IR(m_motionVariance.get(), m_stride, size, 2, 2);
            ippiThreshold_GTVal_8u_C1IR(m_motionVariance.get(), m_stride, size, 64, 64);
        }

        ippiCompare_8u_C1R(m_motionDelta.get(), m_stride, m_motionVariance.get(), m_stride, m_motionLabel.get(), m_stride, size, ippCmpGreater);
        
        // spatial postprocessing
        IppStatus s;
        if (m_morphSpec.get() == nullptr || m_bufferMorph.get() == nullptr) {
            int specSize, bufferSize;
            s = ippiMorphologyBorderGetSize_8u_C1R(size, { 3,3 }, &specSize, &bufferSize);
            if (ippStsNoErr != s)
                std::runtime_error("ippiMorphologyBorderGetSize_8u_C1R failed: "+boost::lexical_cast<std::string>(s));
            m_morphSpec.reset(reinterpret_cast<IppiMorphState*>(ippMalloc(specSize)), ippFree);
            m_bufferMorph.reset(reinterpret_cast<Ipp8u*>(ippMalloc(bufferSize)), ippFree);

            static uint8_t strel[9] = { 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, };
            ippiMorphologyBorderInit_8u_C1R(size, strel, {3,3}, m_morphSpec.get(), m_bufferMorph.get());

        }

        ippiErodeBorder_8u_C1R(m_motionLabel.get(), m_stride, m_buffer0.get(), m_stride, size,
            ippBorderConst, 0, m_morphSpec.get(), m_bufferMorph.get());
        ippiDilateBorder_8u_C1R(m_buffer0.get(), m_stride, m_motionLabel.get(), m_stride, size,
            ippBorderConst, 0, m_morphSpec.get(), m_bufferMorph.get());

        motionProcessFinal();

        ++m_frameNumber;
    }
    void motionProcessFinal() {
        int cellW = m_width / motionCellsH;
        int cellH = m_height / motionCellsV;
        int cellSize = cellW*cellH;
        m_status.motionMask = 0;
        int motionCellsActive = 0;
        for (int y = 0; y < motionCellsV; ++y) {
            for (int x = 0; x < motionCellsH; ++x) {
                int count = 0;
                ippiCountInRange_8u_C1R(m_motionLabel.get() + x*cellW + y*m_stride*cellH, m_stride, { cellW, cellH },
                    &count, 1, 255);
                m_motionCells[motionCellsH*y + x] = count;
                const int cellCountThreshold = std::min<int>(cellSize/2, std::max<int>(1, int(ceil(cellSize) * (1.0 - detect_motion) * 0.2)));
                if (count >= cellCountThreshold) {
                    m_status.motionMask |= (1UL << x) << (y*motionCellsH);
                    ++motionCellsActive;
                }
            }
        }
        if (m_status.motionMask > 0) {
            //VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "Motion detector active, mask=" << m_status.motionMask;
            if (motionCellsActive < 2 * motionCellsH*motionCellsV / 3) {
                if(detect_motion)
                    m_status.alarmMotion = true;
                m_status.alarmGlobalChange = false;
            }
            else {
                if(detect_motion)
                    m_status.alarmMotion = true;
                if(detect_scene_change)
                    m_status.alarmGlobalChange = true;
                // todo: could it be just a lighting conditions change or camera exposure adjusted?
                //VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "Global scene change detected";
            }
        }
        else {
            m_status.alarmMotion = false;
            m_status.alarmGlobalChange = false;
        }
    }
    static int framerate_to_skip_rate(float fps) {
        int skip_rate = 0; // corresponds to 0 -> 25-30, 1 -> 10, 2 -> 5, 3 -> 2 or less FPS
        if (fps > 0) {
            if (fps <= 2)
                skip_rate = 3;
            else if (fps <= 5)
                skip_rate = 2;
            else if (fps <= 10)
                skip_rate = 1;
        }
        return skip_rate;
    }
};

namespace VnxVideo {
    IAnalytics* CreateAnalytics_Basic(const std::vector<float>& roi, float framerate, bool too_bright, bool too_dark, bool too_blurry, float motion, bool scene_change) {
        return new CBasicAnalytics(roi, framerate, too_bright, too_dark, too_blurry, motion, scene_change);
    }

}
#endif // aarch64
