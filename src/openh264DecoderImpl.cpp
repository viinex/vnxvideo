#include <stdexcept>
#include <memory>
#include <algorithm>
#include <cstring>

#include "openh264Common.h"
#include "RawSample.h"

class COpenH264Decoder : public VnxVideo::IVideoDecoder
{
    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;
    EColorspace m_csp;
    int m_width, m_height;

    std::shared_ptr<ISVCDecoder> m_decoder;
public:
    COpenH264Decoder() {
        ISVCDecoder* decoder = 0;
        long res = WelsCreateDecoder(&decoder);
        if (res != 0)
            throw std::runtime_error("WelsCreateDecoder failed: " + std::to_string(res));

        SDecodingParam param;
        memset(&param, 0, sizeof param);
        param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
        res = decoder->Initialize(&param);
        if (res != 0) {
            WelsDestroyDecoder(decoder);
            throw std::runtime_error("ISVCDecoder::Initialize failed: " + std::to_string(res));
        }
        m_decoder.reset(decoder, [](ISVCDecoder* d){
            d->Uninitialize();
            WelsDestroyDecoder(d);
        });


        auto logcb = openh264log;
        m_decoder->SetOption(DECODER_OPTION_TRACE_CALLBACK, &logcb);
        auto loglevel = WELS_LOG_WARNING;
        m_decoder->SetOption(DECODER_OPTION_TRACE_LEVEL, &loglevel);
    }
    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame) {
        m_onFormat = onFormat;
        m_onFrame = onFrame;
    }
    virtual void Decode(VnxVideo::IBuffer* nalu, uint64_t timestamp) {
        uint8_t* data;
        int dataSize;
        nalu->GetData(data, dataSize);

        SBufferInfo bufferInfo;
        memset(&bufferInfo, 0, sizeof bufferInfo);
        bufferInfo.uiInBsTimeStamp = timestamp;

        uint8_t* dst[4] = { 0,0,0,0 };
        int res = m_decoder->DecodeFrameNoDelay(data, dataSize, dst, &bufferInfo);
        if (res != 0) {
            VNXVIDEO_LOG(VNXLOG_DEBUG, "openh264decoder") << "ISVCDecoder::DecodeFrameNoDelay returned error " << res;
        }
        else if (bufferInfo.iBufferStatus == 1) {
            pushDecoded(bufferInfo, dst);
        }
    }
    virtual void Flush() {
        SBufferInfo bufferInfo;
        memset(&bufferInfo, 0, sizeof bufferInfo);
        uint8_t* dst[4] = { 0,0,0,0 };
        int res = m_decoder->FlushFrame(dst, &bufferInfo);
        if (res != 0) {
            VNXVIDEO_LOG(VNXLOG_DEBUG, "openh264decoder") << "ISVCDecoder::FlushFrame returned error " << res;
        }
        else if (bufferInfo.iBufferStatus == 1) {
            pushDecoded(bufferInfo, dst);
        }
    }
private:
    void callOnFormat(int width, int height) {
        if (m_width != width || m_height != height || m_csp != EMF_I420) {
            m_width = width;
            m_height = height;
            m_csp = EMF_I420;
            m_onFormat(m_csp, m_width, m_height);
        }
    }
    void pushDecoded(const SBufferInfo& bufferInfo, uint8_t** dst) {
        int width = bufferInfo.UsrData.sSystemBuffer.iWidth;
        int height = bufferInfo.UsrData.sSystemBuffer.iHeight;
        callOnFormat(bufferInfo.UsrData.sSystemBuffer.iWidth, bufferInfo.UsrData.sSystemBuffer.iHeight);
        switch (bufferInfo.UsrData.sSystemBuffer.iFormat) {
        case videoFormatI420: {
            int strides[4] = { bufferInfo.UsrData.sSystemBuffer.iStride[0],
                bufferInfo.UsrData.sSystemBuffer.iStride[1],bufferInfo.UsrData.sSystemBuffer.iStride[1],
                0 };
            CRawSample sample(EMF_I420, width, height, strides, dst, m_decoder); // m_decoder actually holds the memory buffer
            m_onFrame(&sample, bufferInfo.uiOutYuvTimeStamp);
            break;
        }
        default: VNXVIDEO_LOG(VNXLOG_WARNING, "openh264decoder") << "Unsupported format of decoded frame: " << bufferInfo.UsrData.sSystemBuffer.iFormat;
        }
    }
};

namespace VnxVideo {
    VnxVideo::IVideoDecoder* CreateVideoDecoder_OpenH264() {
        return new COpenH264Decoder();
    }
}
