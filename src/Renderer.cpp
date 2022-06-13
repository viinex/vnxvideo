#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>

#include <ippi.h>

extern "C" {
#include <libswscale/swscale.h>
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"
#include "RawSample.h"

class CRendererImplMixin {
public:
    virtual void InputSetFormat(int input, EColorspace csp, int width, int height) = 0;
    virtual void InputSetSample(int input, VnxVideo::IRawSample* sample, uint64_t timestamp) = 0;
};

class CRendererImplMixinProxy: public CRendererImplMixin {
private:
    std::mutex m_mutex;
    CRendererImplMixin* m_impl;
public:
    CRendererImplMixinProxy(CRendererImplMixin* impl): m_impl(impl){}
    void ResetImpl() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_impl = nullptr;
    }

    virtual void InputSetFormat(int input, EColorspace csp, int width, int height) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (nullptr != m_impl) {
            m_impl->InputSetFormat(input, csp, width, height);
        }
    }
    virtual void InputSetSample(int input, VnxVideo::IRawSample* sample, uint64_t timestamp) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (nullptr != m_impl) {
            m_impl->InputSetSample(input, sample, timestamp);
        }
    }
};

// this class contains no logic, it's just a proxy for binding an "input" argument to produce a RawProc interface
class CRendererInput : public VnxVideo::IRawProc {
public:
    CRendererInput(std::shared_ptr<CRendererImplMixin> renderer, int input, VnxVideo::PRawTransform transform)
        : m_renderer(renderer)
        , m_input(input)
        , m_transform(transform)
    {
        if (m_transform) {
            m_transform->Subscribe([renderer,input](EColorspace csp, int w, int h) {
                renderer->InputSetFormat(input, csp, w, h);
            }, 
            [renderer, input](VnxVideo::IRawSample* s, uint64_t ts) {
                renderer->InputSetSample(input, s, ts);
            });
        }
    }
    virtual void SetFormat(EColorspace csp, int width, int height) {
        if (m_transform)
            m_transform->SetFormat(csp, width, height);
        else
            m_renderer->InputSetFormat(m_input, csp, width, height);
    }
    virtual void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        if (m_transform)
            m_transform->Process(sample, timestamp);
        else
            m_renderer->InputSetSample(m_input, sample, timestamp);
    }
    virtual void Flush() {

    }
    ~CRendererInput() {
        if (m_transform) {
            m_transform->Subscribe([](EColorspace, int, int) {}, [](VnxVideo::IRawSample*, uint64_t) {});
        }
    }
private:
    VnxVideo::PRawTransform m_transform;
    std::shared_ptr<CRendererImplMixin> m_renderer;
    const int m_input;
};

class CRenderer : public VnxVideo::IRenderer, public CRendererImplMixin {
public:
    CRenderer(int refresh_rate, PShmAllocator allocator) 
        : m_refresh_rate(refresh_rate)
        , m_dirty(false)
        , m_formatDirty(false)
        , m_width(0)
        , m_height(0)
        , m_run(false)
        , m_backgroundColor({0,0,0})
        , m_onFormat([](...) {})
        , m_onFrame([](...) {})
        , m_rendererImplMixinProxy(new CRendererImplMixinProxy(this))
        , m_allocator(allocator)
    {

    }
    ~CRenderer() {
        m_rendererImplMixinProxy->ResetImpl();
    }
    void UpdateLayout(int width, int height, 
        uint8_t* backgroundColor, VnxVideo::IRawSample* backgroundImage, 
        VnxVideo::IRawSample* nosignalImage,
        const VnxVideo::TLayout& layout) {
        if (width < 0 || height < 0 || width > 4096 || height > 4096)
            throw std::runtime_error("CRenderer::UpdateLayout(): invalid target image size requested");
        std::unique_lock<std::mutex> lock(m_mutex);
        m_layout = std::shared_ptr<VnxVideo::TLayout>(new VnxVideo::TLayout(layout));
        m_swsContexts = std::shared_ptr<TSwsContexts>(new TSwsContexts(layout.size()));
        if (nullptr != backgroundImage) {
            m_backgroundImage = std::shared_ptr<VnxVideo::IRawSample>(backgroundImage->Dup());
            EColorspace csp;
            int w, h;
            m_backgroundImage->GetFormat(csp, w, h);
            if (EMF_I420 != csp) {
                int s[3];
                uint8_t* p[3];
                m_backgroundImage->GetData(s, p);
                m_backgroundImage.reset(new CRawSample(csp, w, h, s, p, true));
            }
        }
        else if (nullptr != backgroundColor) {
            m_backgroundColor = { backgroundColor[0],backgroundColor[1],backgroundColor[2] };
            m_backgroundImage.reset();
        }
        if (nullptr != nosignalImage) {
            m_nosignalImage = std::shared_ptr<VnxVideo::IRawSample>(nosignalImage->Dup());
            EColorspace csp;
            int w, h;
            m_nosignalImage->GetFormat(csp, w, h);
            if (EMF_I420 != csp) {
                int s[3];
                uint8_t* p[3];
                m_nosignalImage->GetData(s, p);
                m_nosignalImage.reset(new CRawSample(csp, w, h, s, p, true));
            }
        }
        m_dirty = true;
        if (m_width != width || m_height != height) {
            m_width = width;
            m_height = height;
            m_formatDirty = true;
        }
        m_cond.notify_all();
    }
    void SetBackground(uint8_t* backgroundColor, VnxVideo::IRawSample* backgroundImage) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if(nullptr != backgroundImage){
            m_backgroundImage = std::shared_ptr<VnxVideo::IRawSample>(backgroundImage->Dup());
            EColorspace csp;
            int w, h;
            m_backgroundImage->GetFormat(csp, w, h);
            if (EMF_I420 != csp) {
                int s[3];
                uint8_t* p[3];
                m_backgroundImage->GetData(s, p);
                m_backgroundImage.reset(new CRawSample(csp, w, h, s, p, true));
            }
        }
        else if (nullptr != backgroundColor) {
            m_backgroundColor = { backgroundColor[0],backgroundColor[1],backgroundColor[2] };
            m_backgroundImage.reset();
        }
        m_dirty = true;
        m_cond.notify_all();
    }
    void SetNosignal(VnxVideo::IRawSample* nosignalImage) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (nullptr != nosignalImage) {
            m_nosignalImage = std::shared_ptr<VnxVideo::IRawSample>(nosignalImage->Dup());
            EColorspace csp;
            int w, h;
            m_nosignalImage->GetFormat(csp, w, h);
            if (EMF_I420 != csp) {
                int s[3];
                uint8_t* p[3];
                m_nosignalImage->GetData(s, p);
                m_nosignalImage.reset(new CRawSample(csp, w, h, s, p, true));
            }
        }
        else
            m_nosignalImage.reset();
        m_dirty = true;
        m_cond.notify_all();
    }
    VnxVideo::IRawProc* CreateInput(int index, VnxVideo::PRawTransform transform) {
        if (index < 0)
            throw std::runtime_error("CRenderer::CreateInput(): invalid index");
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_samples.size() < index+1)
            m_samples.resize(index+1);
        m_samples[index]={ VnxVideo::PRawSample(), 0 };
        return new CRendererInput(m_rendererImplMixinProxy, index, transform);
    }
    virtual void InputSetFormat(int input, EColorspace csp, int width, int height) {

    }
    virtual void InputSetSample(int input, VnxVideo::IRawSample* sample, uint64_t timestamp) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if ((input < 0) || (input >= m_samples.size()))
            throw std::logic_error("CRenderer::InputSetSample() input index out of range");
        m_samples[input] = { VnxVideo::PRawSample(sample->Dup()), timestamp };
        m_dirty = true;
        m_cond.notify_all();
    }

    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_onFormat = onFormat;
        m_onFrame = onFrame;
        int w = m_width;
        int h = m_height;
        m_dirty = true;
        m_formatDirty = true;
    }

    virtual void Run() {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_run)
            throw std::runtime_error("CRenderer::Run(): already running");
        m_run = true;
        m_thread=std::move(std::thread(std::bind(&CRenderer::doRun, this)));
    }
    virtual void Stop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_run = false;
        m_cond.notify_all();
        lock.unlock();
        if(m_thread.get_id()!=std::this_thread::get_id())
            m_thread.join();
    }

private:
    void sanitizeTimestamps() {
        auto now_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        for (auto &s : m_samples) {
            if (s.first.get() != nullptr && abs(int64_t(now_milliseconds - s.second)) > 5000) { // difference from system clock is more than 5 seconds
                s.first.reset();
                s.second = 0;
            }
        }
    }
    void doRun() {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto prev = std::chrono::high_resolution_clock::now();
        const auto period = std::chrono::microseconds(1000000 / m_refresh_rate);
        while (m_run) {
            while (m_run && !m_dirty && !m_formatDirty)
                m_cond.wait(lock);
            if (!m_run)
                break;
            sanitizeTimestamps();
            auto samples = m_samples;
            int width = m_width;
            int height = m_height;
            auto layout = m_layout;
            auto swsContexts = m_swsContexts;
            uint8_t backgroundColor[3] = { m_backgroundColor[0],m_backgroundColor[1],m_backgroundColor[2] };
            auto backgroundImage = m_backgroundImage;
            auto nosignalImage = m_nosignalImage;
            auto onFrame = m_onFrame;
            auto onFormat = m_onFormat;
            auto checkFormat = m_formatDirty;
            m_dirty = false;
            m_formatDirty = false;
            if (m_width == 0 || m_height == 0)
                continue;
            lock.unlock();
            if (checkFormat) {
                onFormat(EMF_I420, width, height);
            }
            if (layout) {
                std::pair<VnxVideo::PRawSample, uint64_t> res = doRender(width, height, backgroundColor, backgroundImage, nosignalImage,
                    *layout.get(), *swsContexts.get(), m_allocator.get(), samples);
                onFrame(res.first.get(), res.second);
            }



            auto now = std::chrono::high_resolution_clock::now();
            if (now < prev + period) {
                const auto left = period - (now - prev);
                std::this_thread::sleep_for(left);
                now = std::chrono::high_resolution_clock::now();
            }

            prev=now;
            lock.lock();

        }
    }

    static void DrawRect_8u_C1(uint8_t* data, IppiSize size, int stride, IppiRect rect, uint8_t val) {
        uint8_t* b = data + rect.y*stride + rect.x;
        memset(b, val, rect.width);
        for (int k = 1; k < rect.height; ++k) {
            b += stride;
            b[0] = val;
            b[rect.width - 1] = val;
        }
        memset(b, val, rect.width);
    }

    static void rgb2yuv(const uint8_t* rgb, uint8_t* yuv) {
        float y = 0.299f*float(rgb[0]) + 0.587f*float(rgb[1]) + 0.114f*float(rgb[2]);
        float u = -0.14713f*float(rgb[0]) - 0.28886f*float(rgb[1]) + 0.436f*float(rgb[2]) + 128;
        float v = 0.615f*float(rgb[0]) - 0.51499f*float(rgb[1]) - 0.10001f*float(rgb[2]) + 128;
        yuv[0] = std::min<int>(255, std::max<int>(0, round(y)));
        yuv[1] = std::min<int>(255, std::max<int>(0, round(u)));
        yuv[2] = std::min<int>(255, std::max<int>(0, round(v)));
    }

    typedef std::vector<std::shared_ptr<SwsContext>> TSwsContexts;

    static std::pair<VnxVideo::PRawSample, uint64_t> doRender(int width, int height,
        uint8_t *backgroundColorRgb,
        VnxVideo::PRawSample backgroundImage,
        VnxVideo::PRawSample nosignalImage,
        const VnxVideo::TLayout& layout,
        TSwsContexts& swsContexts,
        IAllocator *allocator,
        std::vector<std::pair<VnxVideo::PRawSample, uint64_t> > samples) 
    {
        int strides[4];
        uint8_t* planes[4];

        int planeSizeDiv[3] = { 1,2,2 };

        VnxVideo::PRawSample res;
        uint64_t ts=0;
        if (backgroundImage.get() != nullptr) {
            EColorspace csp;
            int w;
            int h;
            backgroundImage->GetFormat(csp, w, h);
            if (csp != EMF_I420)
                throw std::logic_error("CRenderer::doRender(): prepared background sample expected, i.e. resized to target size");
            int stridesBg[4];
            uint8_t* planesBg[4];
            backgroundImage->GetData(stridesBg, planesBg);

            res.reset(new CRawSample(width, height, allocator));
            res->GetData(strides, planes);

            // align width and height to 8 because we'll use 32bpp-oriented function for copying with tiling
            w = (w / 8) * 8;
            h = (h / 8) * 8;

            IppStatus s;
            for (int j = 0; j < 3; ++j) {
                s = ippiCopyWrapBorder_32s_C1R((int32_t*)planesBg[j], stridesBg[j], 
                    { std::min(w,width) / (4 * planeSizeDiv[j]), std::min(h,height) / planeSizeDiv[j] },
                    (int32_t*)planes[j], strides[j], { width / (4 * planeSizeDiv[j]), height / planeSizeDiv[j] }, 0, 0);
            }
        }
        else {
            res.reset(new CRawSample(width, height, allocator));
            res->GetData(strides, planes);
            uint8_t backgroundYuv[3];
            rgb2yuv(backgroundColorRgb, backgroundYuv);
            for (int j = 0; j < 3; ++j){
                ippiSet_8u_C1R(backgroundYuv[j], planes[j], strides[j], { width / planeSizeDiv[j], height / planeSizeDiv[j] });
            }	
        }

        for (size_t k = 0; k < layout.size(); ++k) {
            if (layout[k].input != -1 && (layout[k].input < 0 || layout[k].input >= samples.size())) {
                VNXVIDEO_LOG(VNXLOG_DEBUG, "renderer") << "CRenderer::doRender(): input index out of range: " << layout[k].input;
                continue;
            }

            VnxVideo::PRawSample src;
            if(layout[k].input != -1)
                src = samples[layout[k].input].first;

            if (src.get() == nullptr) {
                if (nosignalImage.get() == nullptr)
                    continue;
                else
                    src = nosignalImage;
            }

            if(layout[k].input != -1)
                ts = std::max(ts, samples[layout[k].input].second);

            EColorspace csp;
            int w;
            int h;
            src->GetFormat(csp, w, h);
            AVPixelFormat avPixFmt = AV_PIX_FMT_NONE;
            if (csp != EMF_I420 && csp != EMF_NV12) {
                VNXVIDEO_LOG(VNXLOG_DEBUG, "renderer") << "CRenderer::doRender(): unsupported input sample format: " << csp;
                continue;
            }
            else {
                if (csp == EMF_I420)
                    avPixFmt = AV_PIX_FMT_YUV420P;
                else if (csp == EMF_NV12)
                    avPixFmt = AV_PIX_FMT_NV12;
            }

            int src_strides[4];
            uint8_t* src_planes[4];
            src->GetData(src_strides, src_planes);

            const int RoundSizeX = 16; // swscale works incorrectly if sizes not rounded to 16
            const int RoundSizeY = 4; // y coordinates should be at lease event as well

            IppiRect srcRoi = {
                (int)round(w*layout[k].src_left),
                (int)round(h*layout[k].src_top),
                (int)round(w*(layout[k].src_right - layout[k].src_left)),
                (int)round(h*(layout[k].src_bottom - layout[k].src_top))
            };
            srcRoi.x = (srcRoi.x / RoundSizeX) * RoundSizeX;
            srcRoi.y = (srcRoi.y / RoundSizeY) * RoundSizeY;
            srcRoi.width = std::min(srcRoi.width, w - srcRoi.x);
            srcRoi.width = (srcRoi.width / RoundSizeX) * RoundSizeX;
            srcRoi.height = std::min(srcRoi.height, h - srcRoi.y);
            if (srcRoi.width < RoundSizeX || srcRoi.height < RoundSizeY)
                continue;

            float dst_center_x = (layout[k].dst_left + layout[k].dst_right) / 2;
            float dst_center_y = (layout[k].dst_top + layout[k].dst_bottom) / 2;
            float dst_width = layout[k].dst_right - layout[k].dst_left;
            float dst_height = layout[k].dst_bottom - layout[k].dst_top;
            float scale_x = dst_width*width / float(srcRoi.width);
            float scale_y = dst_height*height / float(srcRoi.height);
            float scale = std::min(scale_x, scale_y);
            if (scale < scale_x)
                dst_width *= scale / scale_x;
            else if (scale < scale_y)
                dst_height *= scale / scale_y;
            
            IppiRect dstRoi = {
                (int)round((dst_center_x - dst_width*0.5f)*width),
                (int)round((dst_center_y - dst_height*0.5f)*height),
                (int)round(dst_width*width),
                (int)round(dst_height*height)
            };
            dstRoi.x = (dstRoi.x / RoundSizeX) * RoundSizeX;
            dstRoi.y = (dstRoi.y / RoundSizeY) * RoundSizeY;
            dstRoi.width = std::min(dstRoi.width, width - dstRoi.x);
            dstRoi.width = (dstRoi.width / RoundSizeX) * RoundSizeX;
            dstRoi.height = std::min(dstRoi.height, height - dstRoi.y);
            dstRoi.height = (dstRoi.height / RoundSizeY) * RoundSizeY;
            if (dstRoi.width < RoundSizeX || dstRoi.height < RoundSizeY)
                continue;

            uint8_t border_yuv[3];
            if (layout[k].border) {
                rgb2yuv(layout[k].border_rgb, border_yuv);
            }

            std::shared_ptr<SwsContext> sws;
            if (swsContexts[k].get() == nullptr) {
                sws.reset(sws_getContext(srcRoi.width, srcRoi.height, avPixFmt,
                    dstRoi.width, dstRoi.height, AV_PIX_FMT_YUV420P, 
                    SWS_BICUBIC, nullptr, nullptr, nullptr), sws_freeContext);
                swsContexts[k] = sws;
            }
            else
                sws=swsContexts[k];
            const uint8_t* src_planes_roi[3] = {
                src_planes[0] + srcRoi.y*src_strides[0] + srcRoi.x,
                src_planes[1] + srcRoi.y*src_strides[1] / 2 + srcRoi.x / 2,
                src_planes[2] + srcRoi.y*src_strides[2] / 2 + srcRoi.x / 2
            };
            uint8_t* planes_roi[3] = {
                planes[0] + dstRoi.y*strides[0] + dstRoi.x,
                planes[1] + dstRoi.y*strides[1] / 2 + dstRoi.x / 2,
                planes[2] + dstRoi.y*strides[2] / 2 + dstRoi.x / 2
            };
            int res=sws_scale(sws.get(), src_planes_roi, src_strides, 0, srcRoi.height, planes_roi, strides);
            if (res != dstRoi.height)
                VNXVIDEO_LOG(VNXLOG_WARNING, "renderer") << "sws_scale failed";

            if (layout[k].border) {
                for (int j = 0; j < 3; ++j) {
                    IppiRect planeRoiDst = {
                        dstRoi.x / planeSizeDiv[j],
                        dstRoi.y / planeSizeDiv[j],
                        dstRoi.width / planeSizeDiv[j],
                        dstRoi.height / planeSizeDiv[j]
                    };
                    DrawRect_8u_C1(planes[j], { width / planeSizeDiv[j], height / planeSizeDiv[j] }, strides[j],
                        planeRoiDst, border_yuv[j]);
                }
            }
        }
        return{ res,ts };
    }
private:
    const int m_refresh_rate;

    std::mutex m_mutex;
    bool m_dirty;
    bool m_formatDirty;
    std::condition_variable m_cond;

    PShmAllocator m_allocator;

    int m_width;
    int m_height;
    std::vector<uint8_t> m_backgroundColor;
    VnxVideo::PRawSample m_backgroundImage;
    std::vector<uint8_t> m_nosignalColor;
    VnxVideo::PRawSample m_nosignalImage;
    std::shared_ptr<VnxVideo::TLayout> m_layout;
    std::shared_ptr<TSwsContexts> m_swsContexts;
    std::vector<std::pair<VnxVideo::PRawSample, uint64_t> > m_samples;

    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;

    bool m_run;
    std::thread m_thread;

    std::shared_ptr<CRendererImplMixinProxy> m_rendererImplMixinProxy;
};

namespace VnxVideo {
    IRenderer* CreateRenderer(int refresh_rate) {
        return new CRenderer(refresh_rate, DupPreferredShmAllocator());
    }
}
