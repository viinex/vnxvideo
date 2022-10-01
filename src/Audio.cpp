#include <stdexcept>
#include <memory>
#include <algorithm>
#include <functional>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"
#include "BufferImpl.h"

#include "FFmpegUtils.h"

using namespace std::placeholders;

const uint64_t ff_vorbis_channel_layouts[9] = {
    0x04,
    0x03,
    0x07,
    0x33,
    0x37,
    0x3f,
};

template <typename A> void setDefaultParams(int channels, EMediaSubtype t, A& av) {
    //const uint64_t layout = (1 << m_channels) - 1; // m_channels lowest bits set to 1
    const uint64_t layout = 0x8000000000000000ULL;  //AV_CH_LAYOUT_NATIVE
    switch (t) {
    case EMST_PCMU:
    case EMST_PCMA:
        av.sample_rate = 8000;
        break;
    case EMST_OPUS:
        av.sample_rate = 48000;
        break;
    case EMST_AAC:
        av.sample_rate = 48000;
        break;
    default:
        return;
    }
    av.channels = channels;
    av.channel_layout = layout;
}

class CFFmpegAudioDecoder : public VnxVideo::IMediaDecoder {
public:
    CFFmpegAudioDecoder(int channels, EMediaSubtype input)
        : m_channels(channels)
        , m_input(input)
        , m_onFrame([](...) {})
        , m_onFormat([](...) {})
        , m_frm(avframeAlloc())
        , m_pkt(avpacketAlloc())
        , m_onFormatCalled(false)
    {
        m_cc = createAvDecoderContext(codecIdFromSubtype(input),
            [&](AVCodecContext& cc) {
            setDefaultParams(channels, m_input, cc);
        });
    }

    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame) {
        m_onFormatCalled = false;
        m_onFormat = onFormat;
        m_onFrame = onFrame;
    }

    virtual void Decode(VnxVideo::IBuffer* nalu, uint64_t timestamp) {
        int ret;
        AVPacket pkt;
        memset(&pkt, 0, sizeof pkt);
        pkt.pts = timestamp;
        nalu->GetData(pkt.data, pkt.size);
        ret = avcodec_send_packet(m_cc.get(), &pkt);
        if (ret < 0) {
            VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioDecoder::Decode: Failed to avcodec_send_packet: "
                << ret << ": " << fferr2str(ret);
            return;
        }
        fetchDecoded();
    }
    void Flush() {
        AVPacket p;
        memset(&p, 0, sizeof p);
        p.pts = AV_NOPTS_VALUE;
        int res = avcodec_send_packet(m_cc.get(), &p);
        if (0 != res)
            VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_send_packet failed: " << res << ": " << fferr2str(res);
        fetchDecoded();

    }
private:
    void fetchDecoded() {
        int ret = 0;
        while (ret >= 0) {
            ret = avcodec_receive_frame(m_cc.get(), m_frm.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return;
            else if (ret < 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioDecoder::fetchDecoded: Failed to avcodec_receive_frame: "
                    << ret << ": " << fferr2str(ret);
                return;
            }
            else {
                if (!m_onFormatCalled) {
                    m_onFormat(EMF_LPCM, m_cc->sample_rate, m_channels);
                    m_onFormatCalled = true;
                }
                CAvcodecRawSample sample(m_frm.get());
                m_onFrame(&sample, m_frm->pts);
            }
        }
    }
private:
    const int m_channels;
    const EMediaSubtype m_input;
    std::shared_ptr<AVCodecContext> m_cc;
    std::shared_ptr<AVFrame> m_frm;
    std::shared_ptr<AVPacket> m_pkt;

    bool m_onFormatCalled;
    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;
};

class CFFmpegAudioEncoder : public VnxVideo::IMediaEncoder {
public:
    CFFmpegAudioEncoder(EMediaSubtype output) 
        : m_output(output) 
        , m_onBuffer([](...) {})
    {

    }
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        m_onBuffer = onBuffer;
    }
    virtual void SetFormat(ERawMediaFormat emf, int sample_rate, int channels) {
        m_cc = createAvEncoderContext(codecIdFromSubtype(m_output),
            [&](AVCodecContext& cc) {
            if (m_cc.get()) {

                cc.sample_rate = sample_rate;
                cc.sample_fmt = toAVSampleFormat(emf);
                cc.bit_rate = 8*sample_rate*channels*bitsPerSampleByAVSampleFormat(cc.sample_fmt);
                if (m_output == EMST_OPUS) {
                    cc.bit_rate = 32000 * channels;
                }
                cc.channels = channels;
                cc.time_base = {1, 1000};
                if (m_output == EMST_OPUS) {
                    cc.channel_layout = ff_vorbis_channel_layouts[channels - 1];
                    int ret = av_opt_set_double(&cc, "frame_duration", 0.02, AV_OPT_SEARCH_CHILDREN);
                    if (ret < 0) {
                        VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioTranscoder::SetFormat: Failed to set opus frame duration: "
                            << ret << ": " << fferr2str(ret);
                    }
                }
                else {
                    cc.channel_layout = 0x8000000000000000ULL; // m_ccInput->channel_layout;
                }
            }
            else
                setDefaultParams(m_channels, m_output, cc);
        });
    }
    virtual void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        sample->GetData(m_frm->linesize, m_frm->data);
        m_frm->pts = timestamp;
        sendFrame(*m_frm.get());
    }
    virtual void Flush() {
        m_frm->linesize[0] = 0;
        m_frm->pts = AV_NOPTS_VALUE;
        sendFrame(*m_frm.get());
    }
private:
    void sendFrame(const AVFrame& frm) {
        if (m_output == EMST_LPCM) {
            CNoOwnershipNalBuffer buf(frm.data[0], frm.linesize[0]);
            m_onBuffer(&buf, frm.pts);
        }
        else {
            if (!m_cc.get())
                return;
            int ret = avcodec_send_frame(m_cc.get(), &frm);
            if (ret < 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioEncoder::sendFrame: Failed to avcodec_send_frame: "
                    << ret << ": " << fferr2str(ret);
                return;
            }
            while (ret >= 0) {
                ret = avcodec_receive_packet(m_cc.get(), m_pkt.get());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    return;
                else if (ret < 0) {
                    VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioEncoder::sendFrame: Failed to avcodec_receive_packet: "
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
    const EMediaSubtype m_output;
    int m_channels;
    int m_sampleRate;
    std::shared_ptr<AVCodecContext> m_cc;
    std::shared_ptr<AVFrame> m_frm;
    std::shared_ptr<AVPacket> m_pkt;
    VnxVideo::TOnBufferCallback m_onBuffer;
};

class CFFmpegAudioTranscoder: public VnxVideo::ITranscoder {
public:
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        m_encoder->Subscribe(onBuffer);
    }
    virtual void Process(VnxVideo::IBuffer* nalu, uint64_t timestamp) {
        m_decoder->Decode(nalu, timestamp);
    }
    CFFmpegAudioTranscoder(int channels, EMediaSubtype input, EMediaSubtype output)
        : m_encoder(new CFFmpegAudioEncoder(output))
        , m_decoder(new CFFmpegAudioDecoder(channels, input))
    {
        m_decoder->Subscribe(
            std::bind(&VnxVideo::IRawProc::SetFormat, m_encoder, _1, _2, _3),
            std::bind(&VnxVideo::IRawProc::Process, m_encoder, _1, _2)
        );
    }
    ~CFFmpegAudioTranscoder() {
        m_decoder->Flush();
        m_decoder->Subscribe([](...) {}, [](...) {});
        m_encoder->Flush();
    }
private:
    std::shared_ptr<VnxVideo::IMediaEncoder> m_encoder;
    std::shared_ptr<VnxVideo::IMediaDecoder> m_decoder;
};

namespace VnxVideo {
    VNXVIDEO_DECLSPEC ITranscoder* CreateAudioTranscoder(int channels,
        EMediaSubtype input, const char* inputDetails,
        EMediaSubtype output, const char* outputDetails) {
        return new CFFmpegAudioTranscoder(channels, input, output);
    }
}
