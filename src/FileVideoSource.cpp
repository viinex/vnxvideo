// This is an example of a H264 video source plugin for Viinex. 
// Particularly this plugin uses libavformat (part of ffmpeg) 
// to read out a media file, and streams the data from an H264 
// video track in that file into Viinex -- as if that video is coming
// from an IP camera or some other live video source.

// For convenience and real-life use cases this plugin is compiled into
// the vnxvideo shared library. But it could be built as a separate DLL.

// In Viinex it can be used as:

//  {
//      "type": "h264sourceplugin",
//      "name" : "cam2",
//      "library" : "vnxvideo.dll",
//      "factory" : "create_media_file_live_source",
//      "init" : {
//          "file": "I:\\My Drive\\temp\\64e134fa-f94d-4548-8dea-784f156e04d4.MP4"
//      }
//  }

// This plugin implementation does not work very well with video files containing
// b-frames. So you might need to encode your video files into H264 baseline profile
// using the command like:
// ffmpeg.exe -i INPUT.AVI -f mp4 -vcodec libx264 -preset fast -profile:v baseline OUTPUT.MP4

// In order to produce an MP4 file containing the subtitles track, so that the plugin
// would produce the subtitles as Viinex events synchronized with video, use:
// ffmpeg.exe -i INPUT.avi -i INPUT.srt -f mp4 -vcodec libx264 -preset fast -profile:v baseline -c:s mov_text OUTPUT.mp4

#include <vnxvideo/vnxvideo.h>
#include <vnxvideo/vnxvideoimpl.h>
#include <vnxvideo/vnxvideologimpl.h>
#include <vnxvideo/BufferImpl.h>

#include <vnxvideo/json.hpp>
#include <vnxvideo/jget.h>

using json = nlohmann::json;

#include <thread>
#include <mutex>
#include <condition_variable>
#include <codecvt>

extern "C" {
#include <libavformat/avformat.h>
}

class CMediaFileLiveSource : public VnxVideo::IH264VideoSource {
public:
    CMediaFileLiveSource(const json& config)
        : m_running(false)
        , m_onBuffer([](VnxVideo::IBuffer*, uint64_t) {})
        , m_onJson([](const std::string&, uint64_t) {})
        , m_stream(-1)
        , m_streamText(-1)
        , m_tsDiff(0)
    {
        if (!mjget(config, "file", m_filePath))
            throw std::runtime_error("mandatory `file' parameter is missing from config");
        m_loop = jget(config, "loop", true);
        m_speed = jget(config, "speed", 1.0);

        static int avregistered = avregister();

        reopenFile();
    }
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_onBuffer = onBuffer;
    }
    virtual void Subscribe(VnxVideo::TOnJsonCallback onJson) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_onJson = onJson;
    }
    virtual void Run() {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_running)
            throw std::runtime_error("CMediaFileLiveSource is already running");
        m_prevTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        m_running = true;
        m_thread = std::move(std::thread(&CMediaFileLiveSource::doRun, this));
    }
    virtual void Stop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_running)
            throw std::runtime_error("CMediaFileLiveSource is not running and cannot be stopped");
        m_running = false;
        m_condition.notify_all();
        lock.unlock();
        if (m_thread.get_id() != std::this_thread::get_id())
            m_thread.join();
    }
private:
    void reopenFile() {
        AVFormatContext* ctx(nullptr);
        int res = avformat_open_input(&ctx, m_filePath.c_str(), nullptr, nullptr);
        if (res != 0)
            throw std::runtime_error("CMediaFileLiveSource::reopenFile(): avformat_open_input failed: " + averr2str(res));
        m_ctx.reset(ctx, [](AVFormatContext* ctx) { avformat_close_input(&ctx); });

        res = avformat_find_stream_info(m_ctx.get(), nullptr);
        if (res != 0)
            throw std::runtime_error("CMediaFileLiveSource::reopenFile(): avformat_find_stream_info failed: " + averr2str(res));
        VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CMediaFileLiveSource ctor: file " + m_filePath + " found; " <<
            m_ctx->nb_streams << " stream(s) recognized in it";
        for (unsigned int k = 0; k < m_ctx->nb_streams; ++k) {
            if (AV_CODEC_ID_H264 == m_ctx->streams[k]->codecpar->codec_id && (-1 == m_stream)) {
                m_stream = k;
                VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CMediaFileLiveSource::reopenFile(): stream #" << k << " is H264 video of resolution "
                    << m_ctx->streams[k]->codecpar->width << 'x' << m_ctx->streams[k]->codecpar->height;
            }
            else if (AV_CODEC_ID_MOV_TEXT == m_ctx->streams[k]->codecpar->codec_id && (-1 == m_streamText)) {
                m_streamText = k;
                VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CMediaFileLiveSource::reopenFile(): stream #" << k << " is a MOV_TEXT (subtitles) stream";
            }
            else
                m_ctx->streams[k]->discard = AVDISCARD_ALL;
        }
        if (-1 == m_stream)
            throw std::runtime_error("Could not find appropriate H264 video stream in " + m_filePath);

        extractParamSets();
        if (m_sps.empty() || m_pps.empty()) {
            throw std::runtime_error("CMediaFileLiveSource::extractParamSets(): unable to parse parameter sets from " + m_filePath);
        }
    }
    void doRun() {
        AVRational time_base(m_ctx->streams[m_stream]->time_base);
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_running) {
            int res = av_seek_frame(m_ctx.get(), m_stream, m_ctx->streams[m_stream]->first_dts, AVSEEK_FLAG_FRAME);
            if (0 != res) {
                VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "CMediaFileLiveSource::doRun() failed to seek to the very first frame:"
                    << averr2str(res);
                try {
                    CMediaFileLiveSource::reopenFile();
                }
                catch (const std::exception& e) {
                    VNXVIDEO_LOG(VNXLOG_WARNING, "vnxvideo") << "CMediaFileLiveSource::doRun() failed reopen url: " << e.what();

                    m_condition.wait_for(lock, std::chrono::milliseconds(1000));
                    continue;
                }
            }
            AVPacket p;
            while (m_running) {
                memset(&p, 0, sizeof p);
                if (0 == av_read_frame(m_ctx.get(), &p)) {
                    if (p.stream_index == m_streamText) {
                        if (p.size >= 2) {
                            int len = int(p.data[0])*256+int(p.data[1]);
                            if (p.size == 2 + len) {
                                std::string text(p.data + 2, p.data + 2 + len);
                                json j;
                                j["text"] = text;
                                std::stringstream ss;
                                ss << j;
                                m_onJson(ss.str(), m_prevTs);
                                //VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "Timestamp: " << m_prevTs << ", text: " << text;
                            }
                        }
                        av_packet_unref(&p);
                        continue;
                    }
                    uint64_t ts = m_tsDiff + (p.pts == AV_NOPTS_VALUE)?(m_prevTs+40):(p.pts*time_base.num * 1000 / time_base.den);
                    uint64_t diffTimeMilliseconds = ts - m_prevTs;
                    if (m_prevTs > 0){
                        if (diffTimeMilliseconds > 10 && diffTimeMilliseconds < 1000) {
                            uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                            if (now > ts)
                                diffTimeMilliseconds = 10;
                            else
                                diffTimeMilliseconds = std::min(diffTimeMilliseconds, ts - now);
                            if(m_speed != 0)
                                m_condition.wait_for(lock, std::chrono::milliseconds((long long)round(diffTimeMilliseconds / m_speed)));
                        }
                        else {
                            m_tsDiff = m_prevTs + 40 - ts;
                            ts = m_prevTs + 40;
                        }
                    }
                    if (!m_running)
                        break;
                    auto onBuffer = m_onBuffer;
                    lock.unlock();
                    try {
                        if (p.flags & AV_PKT_FLAG_KEY) {
                            CNoOwnershipNalBuffer sps(&m_sps[0], m_sps.size());
                            onBuffer(&sps, ts);
                            CNoOwnershipNalBuffer pps(&m_pps[0], m_pps.size());
                            onBuffer(&pps, ts);
                        }
                        int pos = 0;
                        while (pos < p.size) {
                            // there'll be either NAL unit separator 0,0,0,1 in case of h264 stream (raw or TS),
                            // or 4 bytes of the length of NAL unit in case of file encoding (mp4/mov)
                            int len = (p.data[pos + 0] << 24) + (p.data[pos + 1] << 16) + (p.data[pos + 2] << 8) + (p.data[pos + 3] << 0);
                            if (1 == len)
                                len = p.size - 4;
                            CNoOwnershipNalBuffer nalu(p.data + pos + 4, len);
                            onBuffer(&nalu, ts);
                            pos += len + 4;
                        }
                    }
                    catch (const std::exception& e) {
                        VNXVIDEO_LOG(VNXLOG_WARNING, "vnxvideo") << "CMediaFileLiveSource::doRun() got an exception from onBuffer callback: " 
                            << e.what();
                    }
                    lock.lock();
                    av_packet_unref(&p);
                    m_prevTs = ts;
                }
                else {
                    break;
                }
            }
            if (!m_loop) {
                auto onBuffer = m_onBuffer;
                lock.unlock();
                try {
                    static uint8_t endOfStreamNal[] = { 11 };
                    CNoOwnershipNalBuffer nalu(endOfStreamNal, 1);
                    m_onBuffer(&nalu, m_prevTs);
                }
                catch (const std::exception& e) {
                    VNXVIDEO_LOG(VNXLOG_WARNING, "vnxvideo") << "CMediaFileLiveSource::doRun() got an exception from onBuffer final callback: "
                        << e.what();
                }
                lock.lock();
                m_running = false;
                break;
            }
        }
    }
    inline static std::string averr2str(int err) {
        char buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(err, buf, AV_ERROR_MAX_STRING_SIZE);
        return buf;
    }
    inline static int avregister() {
        av_register_all();
        return 0;
    }

    // Unfortunatly we'll have to manually extract SPS and PPS from "extradata" which is stored by avformat.
    void extractParamSets() {
        m_sps.clear();
        m_pps.clear();
        const uint8_t* extradata = m_ctx->streams[m_stream]->codecpar->extradata;
        int extradata_size = m_ctx->streams[m_stream]->codecpar->extradata_size;
        if (extradata_size < 8) {
            VNXVIDEO_LOG(VNXLOG_WARNING, "vnxvideo") << "CMediaFileLiveSource::extractParamSets(): unable to parse parameter sets from extradata of length "
                << std::to_string(extradata_size);
            return;
        }
        if (startcode(extradata))
            extractParamSetsStream(extradata, extradata_size, m_sps, m_pps);
        else
            extractParamSetsAvcC(extradata, extradata_size, m_sps, m_pps);
    }
    // There'll be a few ugly utility functions for parsing some of H264 stream
    inline static int startcode(const uint8_t* p) {
        if (p[0] == 0 && p[1] == 0)
            if (p[2] == 1)
                return 3;
            else if (p[2] == 0 && p[3] == 1)
                return 4;
            else
                return 0;
        else
            return 0;
    }
    static int findstartcode(const uint8_t* data, int length) {
        for (int k = 0; k < length - 5; ++k)
            if (startcode(data + k))
                return k;
        return length;
    }
    static void extractParamSetsStream(const uint8_t* extradata, int extradata_size, 
        std::vector<uint8_t>& sps, std::vector<uint8_t>& pps) {
        int d;
        while(d = startcode(extradata)) {
            extradata += d;
            extradata_size -= d;
            int n = findstartcode(extradata, extradata_size);
            if ((extradata[0] & 0x1f) == 7)
                sps.assign(extradata, extradata + n);
            else if ((extradata[0] & 0x1f) == 8)
                pps.assign(extradata, extradata + n);
            if (n < extradata_size - 5) {
                extradata += n;
                extradata_size -= n;
            }
        }
    }
    static void extractParamSetsAvcC(const uint8_t* extradata, int extradata_size,
        std::vector<uint8_t>& sps, std::vector<uint8_t>& pps) {
        if (extradata_size < 8)
            return;
        if (extradata[0] != 1)
            return;
        uint8_t ssz = extradata[4] & 3;
        uint8_t nsps = extradata[5] & 31;
        extradata += 6;
        extradata_size -= 6;

        for (int k = 0; k < nsps; ++k) {
            uint16_t spssz = (((uint16_t)extradata[0]) << 8) + extradata[1];
            if (extradata_size < spssz + 2)
                return;
            sps.assign(extradata + 2, extradata + 2 + spssz);
            extradata += 2 + spssz;
            extradata_size -= 2 + spssz;
        }
        if (extradata_size < 4)
            return;

        uint8_t npps = extradata[0];
        extradata += 1;
        extradata_size -= 1;
        for (int k = 0; k < npps; ++k) {
            uint16_t ppssz = (((uint16_t)extradata[0]) << 8) + extradata[1];
            if (extradata_size < ppssz + 2)
                return;
            pps.assign(extradata + 2, extradata + 2 + ppssz);
            extradata += 2 + ppssz;
            extradata_size -= 2 + ppssz;
        }
    }
private:
    std::string m_filePath;
    bool m_loop;
    double m_speed;

    std::mutex m_mutex;
    std::thread m_thread;
    std::condition_variable m_condition;
    
    VnxVideo::TOnBufferCallback m_onBuffer;
    VnxVideo::TOnJsonCallback m_onJson;
    bool m_running;

    std::shared_ptr<AVFormatContext> m_ctx;
    int m_stream; // index of the appropriate video stream in the file
    int m_streamText; // index of appropriate text stream in the file
    std::vector<uint8_t> m_sps;
    std::vector<uint8_t> m_pps;
    uint64_t m_prevTs;
    uint64_t m_tsDiff;
};

extern "C" VNXVIDEO_DECLSPEC
int create_media_file_live_source(const char* json_config, vnxvideo_h264_source_t* source) {
    try {
        json j;
        std::string s(json_config);
        std::stringstream ss(s);
        ss >> j;
        source->ptr = new CMediaFileLiveSource(j);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on create_media_file_live_source: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
