#pragma once

#include <algorithm>
#include"vnxvideoimpl.h"
#include "vnxvideologimpl.h"

class CGrayAnalyticsBase : public VnxVideo::IAnalytics {
public:
    CGrayAnalyticsBase(const std::vector<float>& roi)
        : m_roi(roi)
        , m_width(0)
        , m_height(0)
        , m_onJson([](const std::string&, uint64_t) {})
        , m_onBuffer([](VnxVideo::IBuffer*, uint64_t) {})
    {
    }
    void SetFormat(EColorspace csp, int w, int h) {
        if (csp == EMF_I420 || csp == EMF_YV12 || csp == EMF_NV12 || csp == EMF_NV21) {
            m_width = w;
            m_height = h;

            m_roiLeft = (int)floorf(float(m_width)*m_roi[0]);
            m_roiTop = (int)floorf(float(m_height)*m_roi[1]);
            m_roiWidth = (int)ceilf(float(m_width)*(m_roi[2] - m_roi[0]));
            m_roiHeight = (int)ceilf(float(m_height)*(m_roi[3] - m_roi[1]));

            m_roiLeft = std::max(0, std::min(m_roiLeft, m_width - 2));
            m_roiTop = std::max(0, std::min(m_roiTop, m_height - 2));
            m_roiWidth = std::max(1, std::min(m_roiWidth, m_width - m_roiLeft));
            m_roiHeight = std::max(1, std::min(m_roiHeight, m_height - m_roiTop));
            VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CGrayAnalyticsBase::SetFormat(): accepted format; ROI set to (L=" <<
                m_roiLeft << ",T=" << m_roiTop << ",W=" << m_roiWidth << ",H=" << m_roiHeight << ")";
            reset(m_roiWidth, m_roiHeight);
        }
        else {
            m_width = m_height = 0;
            VNXVIDEO_LOG(VNXLOG_WARNING, "vnxvideo") << "CGrayAnalyticsBase::SetFormat(): unacceptable format csp=" << csp;
        }
    }
    void Process(VnxVideo::IRawSample *sample, uint64_t timestamp) {
        if (!checkFormat(sample))
            return;
        int strides[4];
        uint8_t* planes[4];
        sample->GetData(strides, planes);
        process(planes[0] + m_roiTop*strides[0] + m_roiLeft, m_roiWidth, strides[0], m_roiHeight, timestamp);
    }
    void Flush(void) {

    }
    void Subscribe(VnxVideo::TOnJsonCallback onJson, VnxVideo::TOnBufferCallback onBuffer) {
        m_onJson = onJson;
        m_onBuffer = onBuffer;
    }
private:
    const std::vector<float> m_roi;
    VnxVideo::TOnJsonCallback m_onJson;
    VnxVideo::TOnBufferCallback m_onBuffer;
    int m_width;
    int m_height;

    int m_roiLeft;
    int m_roiTop;
    int m_roiWidth;
    int m_roiHeight;
private:
    bool checkFormat(VnxVideo::IRawSample *sample) const {
        EColorspace csp;
        int w, h;
        sample->GetFormat(csp, w, h);
        if (csp == EMF_I420 || csp == EMF_YV12 || csp == EMF_NV12 || csp == EMF_NV21)
            if (w == m_width && h == m_height)
                return true;
        return false;
    }
protected:
    virtual void process(uint8_t* data, int width, int stride, int height, uint64_t timestamp) = 0;
    virtual void reset(int width, int height) = 0;

    void sendJson(const json& j, uint64_t ts) {
        std::stringstream ss;
        ss << j;
        const std::string& s(ss.str());
        m_onJson(s, ts);
    }
};
