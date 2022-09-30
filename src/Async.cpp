#include <mutex>
#include <condition_variable>
#include <thread>

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

class CAsyncProc : public virtual VnxVideo::IRawProc {
public:
    CAsyncProc(VnxVideo::PRawProc proc)
        : m_impl(proc)
        , m_busy(false)
        , m_continue(true)
    {
        m_thread=std::move(std::thread(&CAsyncProc::processThread, this));
    }
    void SetFormat(EColorspace csp, int width, int height) {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_busy)
            m_cond.wait_for(lock, std::chrono::milliseconds(200));
        m_impl->SetFormat(csp, width, height);
    }
    void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_sample.reset(sample->Dup());
        m_timestamp = timestamp;
        m_cond.notify_all();
    }
    void Flush() {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_busy)
            m_cond.wait_for(lock, std::chrono::milliseconds(200));
        m_impl->Flush();
    }
    ~CAsyncProc() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_continue = false;
        m_cond.notify_all();
        lock.unlock();
        if(m_thread.get_id()!=std::this_thread::get_id())
            m_thread.join();
    }
protected:
    void processThread() {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_continue) {
            while (m_continue && m_sample.get() == nullptr)
                m_cond.wait(lock);
            if (!m_continue)
                break;

            m_busy = true;
            VnxVideo::PRawSample sample(m_sample);
            uint64_t timestamp(m_timestamp);
            m_sample.reset();
            
            lock.unlock();
            m_impl->Process(sample.get(), timestamp);
            
            lock.lock();
            m_busy = false;
            m_cond.notify_all();
        }
    }
protected:
    VnxVideo::PRawProc m_impl;
    std::condition_variable m_cond;
    std::mutex m_mutex;
    bool m_busy;
    bool m_continue;
    std::thread m_thread;

    // next sample for processing and its timestamp
    VnxVideo::PRawSample m_sample;
    uint64_t m_timestamp;
};

#pragma warning(push)
#ifdef _MSC_VER
#pragma warning(disable: 4250) // method xxx is inherited via dominance, which is exactly what we want
#endif
class CAsyncTransform: public VnxVideo::IRawTransform, private CAsyncProc {
public:
    CAsyncTransform(VnxVideo::PRawTransform tform) 
        : CAsyncProc(tform)
        , m_impl(tform)
    {
    }
    void SetFormat(EColorspace csp, int width, int height) {
        CAsyncProc::SetFormat(csp, width, height);
    }
    void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        CAsyncProc::Process(sample, timestamp);
    }
    void Flush() {
        CAsyncProc::Flush();
    }
    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame) {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_busy && m_continue)
            m_cond.wait_for(lock, std::chrono::milliseconds(200));
        m_impl->Subscribe(onFormat, onFrame);
    }
private:
    VnxVideo::PRawTransform m_impl;
};

class CAsyncVideoEncoder : public VnxVideo::IMediaEncoder, private CAsyncProc {
public:
    CAsyncVideoEncoder(VnxVideo::PMediaEncoder enc)
        : CAsyncProc(enc)
        , m_impl(enc)
    {
    }
    void SetFormat(EColorspace csp, int width, int height) {
        CAsyncProc::SetFormat(csp, width, height);
    }
    void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        CAsyncProc::Process(sample, timestamp);
    }
    void Flush() {
        CAsyncProc::Flush();
    }
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_busy && m_continue)
            m_cond.wait_for(lock, std::chrono::milliseconds(500));
        m_impl->Subscribe(onBuffer);
    }
private:
    VnxVideo::PMediaEncoder m_impl;
};
#pragma warning(pop)

namespace VnxVideo {
    IRawTransform* CreateAsyncTransform(PRawTransform tform) {
        return new CAsyncTransform(tform);
    }
    IMediaEncoder* CreateAsyncVideoEncoder(PMediaEncoder enc) {
        return new CAsyncVideoEncoder(enc);
    }
}
