#include <stdexcept>
#include <memory>
#include <algorithm>

extern "C" {
#include "libavcodec/avcodec.h"
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"
#include "BufferImpl.h"

#include "FFmpegUtils.h"

class CFFmpegAudioTranscoder: public VnxVideo::ITranscoder {
public:
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        m_onBuffer = onBuffer;
    }
    virtual void Process(VnxVideo::IBuffer* nalu, uint64_t timestamp) {
        int ret;
        if (m_ccInput) {
            AVPacket pkt;
            memset(&pkt, 0, sizeof pkt);
            pkt.pts = timestamp;
            nalu->GetData(pkt.data, pkt.size);
            ret = avcodec_send_packet(m_ccInput.get(), &pkt);
            if (ret < 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioTranscoder::Process: Failed to avcodec_send_packet: " << ret;
                return;
            }
            AVFrame frm;
            while (ret >= 0) {
                ret = avcodec_receive_frame(m_ccInput.get(), &frm);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    return;
                else if (ret < 0) {
                    VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioTranscoder::Process: Failed to avcodec_receive_frame: " << ret;
                    return;
                }
                else {
                    sendFrame(frm);
                }
            }
        }
        else {
            AVFrame frm;
            memset(&frm, 0, sizeof frm);
            setDefaultParams(m_input, frm);
            nalu->GetData(frm.data[0], frm.linesize[0]);
            frm.pts = timestamp;
            sendFrame(frm);
        }
    }
    CFFmpegAudioTranscoder(int channels, EMediaSubtype input, EMediaSubtype output)
        : m_channels(channels)
        , m_input(input)
        , m_output(output)
        , m_onBuffer([](...) {})
    {
        if (input == output) {
            throw std::runtime_error("CFFmpegAudioTranscoderCFFmpegAudioTranscoder: cannot keep same encoding");
        }
        if (input != EMST_WAV) {
            m_ccInput = createAvDecoderContext(codecIdFromSubtype(input), 
                [&](AVCodecContext& cc) {
                setDefaultParams(m_input, cc);
            });
        }
        if (output != EMST_WAV) {
            if (!avcodec_find_encoder(codecIdFromSubtype(output))) {
                throw std::runtime_error("CFFmpegAudioTranscoder: encoder not supported: " + std::to_string(output));
            }
        }
    }
    void checkEncoderContext() {
        if (m_ccOutput.get())
            return;
        m_ccOutput = createAvEncoderContext(codecIdFromSubtype(m_output),
            [&](AVCodecContext& cc) {
            if (m_ccInput.get()) {
                cc.sample_rate = m_ccInput->sample_rate;
                cc.channels = m_ccInput->channels;
                cc.channel_layout = m_ccInput->channel_layout;
            }
            else
                setDefaultParams(m_output, cc);
        });
    }
    template <typename A> void setDefaultParams(EMediaSubtype t, A& av) {
        //const uint64_t layout = (1 << m_channels) - 1; // m_channels lowest bits set to 1
        const uint64_t layout = 0x8000000000000000ULL; // AV_CH_LAYOUT_NATIVE
        switch (t) {
        case EMST_PCMU: 
        case EMST_PCMA:
            av.channels = m_channels;
            av.sample_rate = 8000;
            av.channel_layout = layout;
            return;
        case EMST_OPUS:
            av.channels = m_channels;
            av.sample_rate = 48000;
            av.channel_layout = layout;
            return;
        case EMST_WAV:
            av.channels = m_channels;
            av.sample_rate = 48000;
            av.channel_layout = layout;
            return;
        }
    }
    void sendFrame(const AVFrame& frm) {
        if (m_ccOutput.get() == nullptr) {
            m_onBuffer(&CNoOwnershipNalBuffer(frm.data[0], frm.linesize[0]), frm.pts);
        }
        else {
            int ret = avcodec_send_frame(m_ccOutput.get(), &frm);
            if (ret < 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioTranscoder::sendFrame: Failed to avcodec_send_frame: " << ret;
                return;
            }
            while (ret >= 0) {
                AVPacket pkt;
                ret = avcodec_receive_packet(m_ccOutput.get(), &pkt);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    return;
                else if (ret < 0) {
                    VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioTranscoder::sendFrame: Failed to avcodec_send_frame: " << ret;
                    return;
                }
                else {
                    m_onBuffer(&CNoOwnershipNalBuffer(pkt.data, pkt.size), pkt.pts);
                }
            }

        }
    }
private:
    const int m_channels;
    const EMediaSubtype m_input;
    const EMediaSubtype m_output;
    std::shared_ptr<AVCodecContext> m_ccInput;
    std::shared_ptr<AVCodecContext> m_ccOutput;

    VnxVideo::TOnBufferCallback m_onBuffer;
};

namespace VnxVideo {
    VNXVIDEO_DECLSPEC ITranscoder* CreateAudioTranscoder(int channels,
        EMediaSubtype input, const char* inputDetails,
        EMediaSubtype output, const char* outputDetails) {
        return new CFFmpegAudioTranscoder(channels, input, output);
    }
}