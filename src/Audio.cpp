#include <stdexcept>
#include <memory>
#include <algorithm>
#include <functional>

#include <boost/algorithm/hex.hpp>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include <libswresample/swresample.h>
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"
#include "BufferImpl.h"
#include <vnxvideo/jget.h>
#include "RawSample.h"

#include "FFmpegUtils.h"

using namespace std::placeholders;
using json = nlohmann::json;

const uint64_t ff_vorbis_channel_layouts[9] = {
    0x04,
    0x03,
    0x07,
    0x33,
    0x37,
    0x3f,
};

class CFFmpegAudioDecoder : public VnxVideo::IMediaDecoder {
public:
    CFFmpegAudioDecoder(EMediaSubtype input, int channels, const json& extradata)
        : m_input(input)
        , m_onFrame([](...) {})
        , m_onFormat([](...) {})
        , m_frm(avframeAlloc())
        , m_pkt(avpacketAlloc())
        , m_sample_rate(0)
        , m_channels(0)
    {
        m_cc = createAvDecoderContext(codecIdFromSubtype(input), AV_HWDEVICE_TYPE_NONE,
            [&](AVCodecContext& cc) {
            setDefaultParams(channels, m_input, extradata, cc);
        });
    }

    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame) {
        m_sample_rate = 0;
        m_channels = 0;
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
        if (ret != 0) 
            VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CFFmpegAudioDecoder::Decode: Failed to avcodec_send_packet: "
                << ret << ": " << fferr2str(ret);
        else
            fetchDecoded();
    }
    void Flush() {
        AVPacket p;
        memset(&p, 0, sizeof p);
        p.pts = AV_NOPTS_VALUE;
        int res = avcodec_send_packet(m_cc.get(), &p);
        if (0 != res)
            VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_send_packet failed: " << res << ": " << fferr2str(res);
        else
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
                VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CFFmpegAudioDecoder::fetchDecoded: Failed to avcodec_receive_frame: "
                    << ret << ": " << fferr2str(ret);
                return;
            }
            else {
                //VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "fetch decoded audio: fmt: " << m_frm->format << " nbsamples: " << m_frm->nb_samples << " channels: " << m_frm->channels;
                if (m_sample_rate != m_frm->sample_rate || m_channels != m_frm->channels) {
                    m_onFormat(fromAVSampleFormat((AVSampleFormat)m_frm->format), m_frm->sample_rate, m_frm->channels);

                    m_sample_rate = m_frm->sample_rate;
                    m_channels = m_frm->channels;
                }
                CAvcodecRawSample sample(m_frm.get());
                m_onFrame(&sample, m_frm->pts);
            }
        }
    }
    static void setDefaultParams(int channels, EMediaSubtype t, const json& extradata, AVCodecContext& av) {
        //const uint64_t layout = (1 << m_channels) - 1; // m_channels lowest bits set to 1
        const uint64_t layout = 0x8000000000000000ULL;  //AV_CH_LAYOUT_NATIVE
        switch (t) {
        case EMST_PCMU:
        case EMST_PCMA:
            av.sample_rate = 8000;
            break;
        case EMST_G726:
            av.sample_rate = 8000;
            av.bits_per_coded_sample = jget<int>(extradata, "bits_per_sample");
            break;
        case EMST_OPUS:
            av.sample_rate = 48000;
            break;
        case EMST_AAC:
            av.sample_rate = 0;
            av.channels = 0;
            {
                const std::string aaccfg(jget<std::string>(extradata, "config"));
                av.extradata_size = aaccfg.size() / 2;
                av.extradata = (uint8_t*)av_malloc(av.extradata_size);
                boost::algorithm::unhex(aaccfg, av.extradata);

                av.sample_rate = jget<int>(extradata, "sample_rate", 0);
                av.channels = jget<int>(extradata, "channels", 0);
            }
            break;
        default:
            return;
        }
        if (channels) {
            av.channels = channels;
        }
        av.channel_layout = layout;
    }

private:
    const EMediaSubtype m_input;
    std::shared_ptr<AVCodecContext> m_cc;
    std::shared_ptr<AVFrame> m_frm;
    std::shared_ptr<AVPacket> m_pkt;

    int m_sample_rate;
    int m_channels;
    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;
};

const int kAUDIO_FRAMES_PER_SECOND = 50; // 1000 s/ms / default frame length of 20 ms for Opus

class CFFmpegAudioEncoder : public VnxVideo::IMediaEncoder {
public:
    CFFmpegAudioEncoder(EMediaSubtype output) 
        : m_output(output) 
        , m_onBuffer([](...) {})
        , m_frm(avframeAlloc())
        , m_pkt(avpacketAlloc())
    {

    }
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        m_onBuffer = onBuffer;
    }
    virtual void SetFormat(ERawMediaFormat emf, int sample_rate, int channels) {
        if (!vnxvideo_emf_is_audio(emf))
            return;
        if (m_output == EMST_LPCM)
            return;
        VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CFFmpegAudioEncoder::SetFormat: about to create audio encoder, output=" 
            << m_output
            << ", input emf=" << emf << ", sample_rate=" << sample_rate << ", channels=" << channels;
        m_cc = createAvEncoderContext(codecIdFromSubtype(m_output),
            [&](AVCodecContext& cc) {
            cc.sample_rate = sample_rate;
            cc.sample_fmt = toAVSampleFormat(emf);
            if (m_output == EMST_OPUS) {
                cc.bit_rate = 64000 * channels;
                if (sample_rate >= 32000) { // 8000,16000 and 24000 are okay for opus but 32000 and 44100 aren't; last two are upsampled to 48000.
                    cc.sample_rate = 48000;
                    VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CFFmpegAudioEncoder::SetFormat: target sample rate for opus set to " << cc.sample_rate;
                }
                if (emf == EMF_LPCMFP) {
                    cc.sample_fmt = toAVSampleFormat(EMF_LPCMF);
                    int layout = (channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
                    m_resample.reset(swr_alloc_set_opts(nullptr,
                        layout, AV_SAMPLE_FMT_FLT, cc.sample_rate,
                        layout, toAVSampleFormat(emf), sample_rate, 0, nullptr),
                        [](SwrContext* p) { swr_free(&p); });
                    int res = swr_init(m_resample.get());
                    if (res < 0) {
                        VNXVIDEO_LOG(VNXLOG_DEBUG, "renderer") << "CFFmpegAudioEncoder::SetFormat(): failed to swr_init: " << res;
                        m_resample.reset();
                    }
                }
            }
            cc.channels = channels;
            cc.time_base = {1, 1000};
            if (m_output == EMST_OPUS) {
                cc.channel_layout = ff_vorbis_channel_layouts[cc.channels - 1];
                int ret = av_opt_set_double(&cc, "frame_duration", 1000.0 / double(kAUDIO_FRAMES_PER_SECOND), AV_OPT_SEARCH_CHILDREN);
                if (ret < 0) {
                    VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegAudioEncoder::SetFormat: Failed to set opus frame duration: "
                        << ret << ": " << fferr2str(ret);
                }
            }
            else {
                cc.channel_layout = 0x8000000000000000ULL; // m_ccInput->channel_layout;
            }
        });

        m_channels = channels;
        m_sampleRateIn = sample_rate;
        m_sampleRateOut = m_cc->sample_rate;
        m_samplesPerFrameOut = m_sampleRateOut / kAUDIO_FRAMES_PER_SECOND;
        m_formatIn = toAVSampleFormat(emf);
        if (m_output == EMST_OPUS && emf == EMF_LPCMFP) {
            m_formatOut = toAVSampleFormat(EMF_LPCMF);
        }
        else
            m_formatOut = m_formatIn;
        m_bytesPerFrameOut = m_samplesPerFrameOut * channels * bitsPerSampleByAVSampleFormat(m_formatOut) / 8;
        m_ticksPerFrameOut = 1000 / kAUDIO_FRAMES_PER_SECOND;
        m_buffer.reserve(m_bytesPerFrameOut);

        m_frm->nb_samples = m_samplesPerFrameOut;
        m_frm->channels = m_channels;
        m_frm->channel_layout = m_cc->channel_layout;
        m_frm->sample_rate = m_sampleRateOut;
        m_frm->format = m_formatOut;
        m_frm->linesize[0] = m_bytesPerFrameOut;
    }
    virtual void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        ERawMediaFormat format;
        int sampleCount, channels;
        sample->GetFormat(format, sampleCount, channels);
        if (!vnxvideo_emf_is_audio(format))
            return;
        if (!m_cc)
            return;

        static uint64_t prevts;
        //VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "timestamp: " << timestamp << '\t' << timestamp - prevts;
        prevts = timestamp;


        int strides[4] = { 0,0,0,0 };
        uint8_t* data[4] = { nullptr,nullptr,nullptr,nullptr };
        sample->GetData(strides, data);

        if(!m_resample)
            encode(sampleCount, channels, strides, data, timestamp);
        else {
            int sampleCountOutMax = swr_get_out_samples(m_resample.get(), sampleCount);
            std::shared_ptr<VnxVideo::IRawSample> resampled(new CRawSample(EMF_LPCMF, sampleCountOutMax, channels));
            int rstrides[4] = {};
            uint8_t* rdata[4] = {};
            resampled->GetData(rstrides, rdata);
            int res = swr_convert(m_resample.get(), rdata, sampleCountOutMax, (const uint8_t**)data, sampleCount);
            if (res < 0) {
                VNXVIDEO_LOG(VNXLOG_DEBUG, "renderer") << "CRenderer::InputSetSample(): failed to swr_convert: " << res;
            }
            else
                encode(res, channels, rstrides, rdata, timestamp);
        }
    }
    void encode(int sampleCount, int channels, int* strides, uint8_t** data, uint64_t timestamp){
        uint8_t* cur = data[0];
        int bytesPerSample = sampleCount * channels * bitsPerSampleByAVSampleFormat(m_formatOut) / 8;
        uint8_t* end = cur + bytesPerSample;

        // if there was a leftover -- fill it up to full frame size and process
        if (!m_buffer.empty()) {
            int bytesMissing = m_bytesPerFrameOut - (int)m_buffer.size();
            m_buffer.insert(m_buffer.end(), cur, std::min(end, cur + bytesMissing));

            if (m_buffer.size() != m_bytesPerFrameOut) {
                return; // nothing to process yet, whole input saved as leftover
            }
            //VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "delta: " << m_bufferTimestamp - (timestamp - m_buffer.size() * m_ticksPerFrameOut / m_bytesPerFrameOut);
            //VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "m_bufferTimestamp: " << m_bufferTimestamp << '\t' << timestamp - m_bufferTimestamp;

            m_frm->data[0] = &m_buffer[0];
            m_frm->pts = m_bufferTimestamp;

            cur += bytesMissing;
            uint64_t dt = bytesMissing * m_ticksPerFrameOut / m_bytesPerFrameOut;
            timestamp += dt;
            m_bufferTimestamp += m_ticksPerFrameOut;


            sendFrame(*m_frm.get());

            m_buffer.clear();
        }

        if (abs((int64_t)m_bufferTimestamp - (int64_t)timestamp) < 100) { // allowed jitter
            timestamp = m_bufferTimestamp;
        }
        else {
            m_bufferTimestamp = timestamp;
        }

        // process full audio frames w/o copying while possible
        while (cur + m_bytesPerFrameOut <= end) {
            m_frm->data[0] = cur;
            m_frm->pts = timestamp;

            cur += m_bytesPerFrameOut;
            timestamp += m_ticksPerFrameOut;
            m_bufferTimestamp += m_ticksPerFrameOut;

            sendFrame(*m_frm.get());
        }

        // save leftover, if any, for further processing
        if (cur != end) {
            m_buffer.insert(m_buffer.end(), cur, end);
            m_bufferTimestamp = timestamp;
        }

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
    int m_sampleRateIn;
    int m_sampleRateOut;
    std::shared_ptr<SwrContext> m_resample;
    std::shared_ptr<AVCodecContext> m_cc;
    std::shared_ptr<AVFrame> m_frm;
    std::shared_ptr<AVPacket> m_pkt;
    VnxVideo::TOnBufferCallback m_onBuffer;

    AVSampleFormat m_formatIn;
    AVSampleFormat m_formatOut;
    int m_samplesPerFrameOut;
    int m_bytesPerFrameOut;
    uint64_t m_ticksPerFrameOut;
    std::vector<uint8_t> m_buffer;
    uint64_t m_bufferTimestamp;
};

class CFFmpegAudioTranscoder: public VnxVideo::ITranscoder {
public:
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        m_encoder->Subscribe(onBuffer);
    }
    virtual void Process(VnxVideo::IBuffer* nalu, uint64_t timestamp) {
        m_decoder->Decode(nalu, timestamp);
    }
    CFFmpegAudioTranscoder(EMediaSubtype output, EMediaSubtype input, int channels, const json& extradata)
        : m_encoder(new CFFmpegAudioEncoder(output))
        , m_decoder(new CFFmpegAudioDecoder(input, channels, extradata))
    {
        m_decoder->Subscribe(
            std::bind(&VnxVideo::IRawProc::SetFormat, m_encoder, _1, _2, _3),
            std::bind(&VnxVideo::IRawProc::Process, m_encoder, _1, _2)
        );
    }
    ~CFFmpegAudioTranscoder() {
        m_decoder->Subscribe([](...) {}, [](...) {});
    }
private:
    std::shared_ptr<VnxVideo::IMediaEncoder> m_encoder;
    std::shared_ptr<VnxVideo::IMediaDecoder> m_decoder;
};

namespace VnxVideo {
    VNXVIDEO_DECLSPEC ITranscoder* CreateAudioTranscoder(EMediaSubtype output, 
        EMediaSubtype input, int channels, const json& extradata) {
        return new CFFmpegAudioTranscoder(output, input, channels, extradata);
    }

    VNXVIDEO_DECLSPEC IMediaEncoder* CreateAudioEncoder(EMediaSubtype output) {
        return new CFFmpegAudioEncoder(output);
    }

    VNXVIDEO_DECLSPEC IMediaDecoder* CreateAudioDecoder(EMediaSubtype input, int channels, const json& extradata) {
        return new CFFmpegAudioDecoder(input, channels, extradata);
    }
}
