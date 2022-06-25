#include <stdexcept>
#include <memory>
#include <algorithm>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"
#include "BufferImpl.h"

#include "FFmpegUtils.h"

const uint64_t ff_vorbis_channel_layouts[9] = {
    0x04,
    0x03,
    0x07,
    0x33,
    0x37,
    0x3f,
};

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
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioTranscoder::Process: Failed to avcodec_send_packet: " 
                    << ret << ": " << fferr2str(ret);
                return;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(m_ccInput.get(), m_frm.get());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    return;
                else if (ret < 0) {
                    VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioTranscoder::Process: Failed to avcodec_receive_frame: " 
                        << ret << ": " << fferr2str(ret);
                    return;
                }
                else {
                    sendFrame(*m_frm.get());
                }
            }
        }
        else {
            setDefaultParams(m_input, *m_frm.get());
            nalu->GetData(m_frm->data[0], m_frm->linesize[0]);
            m_frm->pts = timestamp;
            sendFrame(*m_frm.get());
        }
    }
    CFFmpegAudioTranscoder(int channels, EMediaSubtype input, EMediaSubtype output)
        : m_channels(channels)
        , m_input(input)
        , m_output(output)
        , m_onBuffer([](...) {})
        , m_frm(avframeAlloc())
        , m_pkt(avpacketAlloc())
        , m_opusFrameDuration(0)
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
        if (m_ccOutput.get()) {
            if (m_output == EMST_OPUS) {
                checkOpusFrameDuration(*m_ccOutput.get());
            }
            return;
        }
        m_ccOutput = createAvEncoderContext(codecIdFromSubtype(m_output),
            [&](AVCodecContext& cc) {
            if (m_ccInput.get()) {

                cc.bit_rate = m_ccInput->bit_rate; //64000
                cc.sample_rate = m_ccInput->sample_rate;
                cc.sample_fmt = m_ccInput->sample_fmt;
                cc.channels = m_ccInput->channels;
                cc.time_base = m_ccInput->time_base;
                if(m_output == EMST_OPUS){
                    cc.channel_layout = ff_vorbis_channel_layouts[m_ccInput->channels - 1];
                    checkOpusFrameDuration(cc);
                }
                else {
                    cc.channel_layout = 0x8000000000000000ULL; // m_ccInput->channel_layout;
                }
            }
            else
                setDefaultParams(m_output, cc);
        });
    }
    void checkOpusFrameDuration(AVCodecContext& cc) {
        double duration = m_frm->nb_samples * 1000.0 / m_frm->sample_rate;
        if (m_opusFrameDuration == duration)
            return;
        int ret = av_opt_set_double(&cc, "frame_duration", duration, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioTranscoder::checkEncoderContext: Failed to set frame duration: "
                << ret << ": " << fferr2str(ret);
        }
        else
            m_opusFrameDuration = duration;
    }
    template <typename A> void setDefaultParams(EMediaSubtype t, A& av) {
        //const uint64_t layout = (1 << m_channels) - 1; // m_channels lowest bits set to 1
        const uint64_t layout = 0x8000000000000000ULL;  //AV_CH_LAYOUT_NATIVE
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
        if (m_output == EMST_WAV) {
            CNoOwnershipNalBuffer buf(frm.data[0], frm.linesize[0]);
            m_onBuffer(&buf, frm.pts);
        }
        else {
            checkEncoderContext();
            int ret = avcodec_send_frame(m_ccOutput.get(), &frm);
            if (ret < 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioTranscoder::sendFrame: Failed to avcodec_send_frame: " 
                    << ret << ": " << fferr2str(ret);
                return;
            }
            while (ret >= 0) {
                ret = avcodec_receive_packet(m_ccOutput.get(), m_pkt.get());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    return;
                else if (ret < 0) {
                    VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioTranscoder::sendFrame: Failed to avcodec_receive_packet: " 
                        << ret << ": " << fferr2str(ret);
                    return;
                }
                else {
                    CNoOwnershipNalBuffer buf(m_pkt->data, m_pkt->size);
                    m_onBuffer(&buf, m_pkt->pts);
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
    std::shared_ptr<AVFrame> m_frm;
    std::shared_ptr<AVPacket> m_pkt;
    double m_opusFrameDuration;

    VnxVideo::TOnBufferCallback m_onBuffer;
};

namespace VnxVideo {
    VNXVIDEO_DECLSPEC ITranscoder* CreateAudioTranscoder(int channels,
        EMediaSubtype input, const char* inputDetails,
        EMediaSubtype output, const char* outputDetails) {
        return new CFFmpegAudioTranscoder(channels, input, output);
    }
}
