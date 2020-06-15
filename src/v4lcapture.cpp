#include <stdexcept>
#include <vector>
#include <string>
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include <codecvt>
#include <thread>
#include <mutex>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include <glob.h>

#include "json.hpp"
#include "jget.h"

using json = nlohmann::json;

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

#include <ippcc.h>
#include <ippi.h>

#include "RawSample.h"

namespace
{
    int xioctl(int fh, int request, void *arg)
    {
        int r;
    
        do {
            r = ioctl(fh, request, arg);
        } while (-1 == r && EINTR == errno);
    
        return r;
    }
    std::string pf2str(int pixelformat){
        switch (pixelformat) {
        case V4L2_PIX_FMT_BGR24: return "RGB";
            
        //case 0x59455247: return "GREY";
        case V4L2_PIX_FMT_YUYV: return "YUY2";
        case V4L2_PIX_FMT_UYVY: return "UYVY";
        case 0x32595559: return "YUY2";
        case V4L2_PIX_FMT_YUV420M: return "YV12";
        case V4L2_PIX_FMT_YVU410: return "YVU9";
        case 0x56555949: return "IYUV";
        case V4L2_PIX_FMT_M420:
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_YVU420:
        case V4L2_PIX_FMT_YVU420M:
            return "I420";
        case V4L2_PIX_FMT_NV12: return "NV12";
        case V4L2_PIX_FMT_NV21: return "NV21";
        //case 0x47504a4d: return "MJPEG";
        default: 
            char s[16];
            snprintf(s, 16, "0x%08x", pixelformat);
            return s;
        }
    }
    EColorspace string2csp(const std::string &s) {
        if (s == "RGB") return EMF_RGB24;
        //else if (s == "GREY") return 0x59455247;
        else if (s == "YUYV") return EMF_YUY2;
        else if (s == "UYVY") return EMF_UYVY;
        else if (s == "YUY2") return EMF_YUY2;
        else if (s == "YV12") return EMF_YV12;
        else if (s == "YVU9") return EMF_YVU9;
        else if (s == "IYUV") return EMF_I420;
        else if (s == "I420") return EMF_I420;
        else if (s == "NV12") return EMF_NV12;
        else if (s == "NV21") return EMF_NV21;
        else if (s == "RGB") return EMF_RGB24;
        else {
            throw std::runtime_error("cannot parse csp string representation");
        }
    }
    int csp2pf(EColorspace csp){
        switch(csp){
        case EMF_YUY2: return V4L2_PIX_FMT_YUYV;
        case EMF_UYVY: return V4L2_PIX_FMT_UYVY;
        case EMF_YVU9: return V4L2_PIX_FMT_YVU410;
        case EMF_I420: return V4L2_PIX_FMT_YUV420;
        case EMF_NV12: return V4L2_PIX_FMT_NV12;
        case EMF_YV12: return V4L2_PIX_FMT_YUV420M;
        case EMF_NV21: return V4L2_PIX_FMT_NV21;
        case EMF_RGB16: return V4L2_PIX_FMT_RGB565;
        case EMF_RGB24: return V4L2_PIX_FMT_BGR24;
        case EMF_RGB32: return V4L2_PIX_FMT_BGR32;
        }
    }
}

class CVideoDevice : public VnxVideo::IVideoSource
{
public:
    CVideoDevice(const std::string& path, const json& mode)
        : m_fd(-1)
        , m_callFormat(false)
        , m_path(path)
        , m_width(0)
        , m_height(0)
        , m_csp(EMF_NONE)
        , m_pin(0)
    {
        m_fd = open(path.c_str(), O_RDWR);
        if(-1==m_fd){
            throw std::runtime_error("Could not open "+path+": "+strerror(errno));
        }
        m_closeFd.reset((int*)&m_fd, [](int*p){close(*p);});

        m_pin=atoi(jget<std::string>(mode, "pin", "0").c_str());
        std::vector<int> s=jget<std::vector<int> >(mode, "size");
        m_width=s[0];
        m_height=s[1];
        m_framerate=jget<float>(mode, "framerate", 0);
        m_csp=string2csp(jget<std::string>(mode, "colorspace"));

        // select an input (pin)
        if(-1==xioctl(m_fd, VIDIOC_S_INPUT, &m_pin)){
            throw std::runtime_error("Could not select input "+std::to_string(m_pin)+" for device "+path);
        }
        // set the framerate
        if(0!=m_framerate){
            v4l2_streamparm sp;
            memset(&sp, 0, sizeof sp);
            sp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            sp.parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
            v4l2_fract f;
            f.numerator = 1000;
            f.denominator = (int)round(m_framerate*1000.0);
            sp.parm.capture.timeperframe = f;
            if(-1==xioctl(m_fd, VIDIOC_S_PARM, &sp)){
                VNXVIDEO_LOG(VNXLOG_WARNING, "v4lcapture") << "Could not set requested framerate for device " << path << ": " << strerror(errno);
            }
        }

        CRawSample::FillStridesOffsets(m_csp, m_width, m_height, m_nplanes, m_strides, m_offsets, false);
        
        VNXVIDEO_LOG(VNXLOG_DEBUG, "v4lcapture") << "Created video capture device " << path;                
    }
    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_onFormat = onFormat;
        m_onFrame = onFrame;
        m_callFormat = true;
    }
    virtual void Run()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        if(m_runThread.get_id()!=std::thread().get_id())
            throw std::logic_error("the video source is already running");

        m_continue = true;
        startCapturing();        
        m_runThread=std::move(std::thread([this](){ this->captureLoop(); }));        
    }
    virtual void Stop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_continue = false;
        lock.unlock();
        if(std::thread().get_id()!=m_runThread.get_id())
            m_runThread.join();
        stopCapturing();
    }
    ~CVideoDevice()
    {
        Stop();
    }
private:
    const std::string m_path;
    int m_fd;
    std::shared_ptr<void> m_closeFd;

    int m_pin;    
    int m_width;
    int m_height;
    EColorspace m_csp;
    float m_framerate;

    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;
    bool m_callFormat;
    std::thread m_runThread;
    bool m_continue;
    
    std::vector<std::shared_ptr<uint8_t> > m_buffers;

    std::mutex m_mutex;

    // computed strides and offsets of color planes as we expect them within captured samples
    int m_nplanes; // first of all, the number of planes
    int m_strides[4];
    ptrdiff_t m_offsets[4];
private:
    void captureLoop(){
        for (;;) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(m_fd, &fds);
            
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200*1000; // 200 ms

            int r = select(m_fd + 1, &fds, NULL, NULL, &tv);
            int err = errno;

            std::unique_lock<std::mutex> lock(m_mutex);
            if(!m_continue)
                break;

            if (-1 == r) {
                if (EINTR == errno)
                    continue;

                VNXVIDEO_LOG(VNXLOG_WARNING, "v4lcapture") << "Error on select(): " << strerror(err);
                break;
            }
            if (0 == r) { // timeout
                continue;
            }


            bool callFormat(m_callFormat);
            VnxVideo::TOnFormatCallback onFormat(m_onFormat);
            VnxVideo::TOnFrameCallback onFrame(m_onFrame);
            lock.unlock();
            

            v4l2_buffer buf;
            r = readFrame(buf);
            if(r<0)
                continue;

            // compute the relative timestamp so that it matches the viinex units (milliseconds)
            uint64_t timestamp=buf.timestamp.tv_usec/1000+buf.timestamp.tv_sec*1000;

            if(callFormat){
                onFormat(EMF_I420, m_width, m_height); // not m_csp, because we convert to I420 upon receiving
                std::unique_lock<std::mutex> lock(m_mutex);
                m_callFormat = false;
            }

            // compute color planes' pointers, according to actual buffer pointer
            // and the offsets computed and stored in the ctor
            uint8_t* planes[4];
            for(int k=0; k<m_nplanes; ++k)
                planes[k]=m_buffers[buf.index].get()+m_offsets[k];

            // resulting sample, that'll be I420.
            // todo: avoid copying when possible. (i.e. if m_csp==EMF_I420).
            if(m_csp!=EMF_I420){
                CRawSample sample(m_width, m_height);
                int targetStrides[4];
                uint8_t* targetPlanes[4];
                sample.GetData(targetStrides, targetPlanes);
                CRawSample::CopyRawToI420(m_width, m_height, m_csp, planes, m_strides, targetPlanes, targetStrides);
                if (-1 == xioctl(m_fd, VIDIOC_QBUF, &buf)){
                    int err=errno;
                    VNXVIDEO_LOG(VNXLOG_WARNING, "v4lcapture") << "Error on ioctl VIDIOC_QBUF: " << strerror(err);
                }
                onFrame(&sample, timestamp);
            }
            else{
                int fd=m_fd; // we don't want to capture this into underlying dtor.
                auto dtor=[buf, fd](void*){
                    if (-1 == xioctl(fd, VIDIOC_QBUF, const_cast<v4l2_buffer*>(&buf))){
                        int err=errno;
                        VNXVIDEO_LOG(VNXLOG_WARNING, "v4lcapture") << "Error on ioctl VIDIOC_QBUF: " << strerror(err);
                    }
                };
                CRawSample sample(m_csp, m_width, m_height, m_strides, planes, std::shared_ptr<void>(planes, dtor));
                onFrame(&sample, timestamp);
            }
        }        
    }
    void startCapturing()
    {
        // set device format
        v4l2_format fmt;
        memset(&fmt, 0, sizeof fmt);
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = m_width;
        fmt.fmt.pix.height = m_height;
        fmt.fmt.pix.pixelformat = csp2pf(m_csp);
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        
        if(-1 == xioctl(m_fd, VIDIOC_S_FMT, &fmt)){
            throw std::runtime_error("Failed to ioctl VIDIOC_S_FMT: "+std::string(strerror(errno)));
        }
        VNXVIDEO_LOG(VNXLOG_DEBUG, "v4lcapture") << "Capture format successfully set for " << m_path;                

        // allocate buffers and get their addresses
        v4l2_requestbuffers reqb;
        memset(&reqb, 0, sizeof reqb);
        reqb.count = 4;
        reqb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        reqb.memory = V4L2_MEMORY_MMAP;
        
        if(-1 == xioctl(m_fd, VIDIOC_REQBUFS, &reqb)){
            throw std::runtime_error("Failed to ioctl VIDIOC_REQBUFS: "+std::string(strerror(errno)));            
        }

        if(reqb.count<2){
            throw std::runtime_error("Too few buffers returned to VIDIOC_REQBUFS");
        }
        VNXVIDEO_LOG(VNXLOG_DEBUG, "v4lcapture") << "Successfully requested " << reqb.count << " buffers";                
        
        for (int k = 0; k < reqb.count; ++k) {
            v4l2_buffer buf;
            memset(&buf, 0, sizeof buf);

            buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory      = V4L2_MEMORY_MMAP;
            buf.index       = k;

            if (-1 == xioctl(m_fd, VIDIOC_QUERYBUF, &buf))
                throw std::runtime_error("Failed to ioctl VIDIOC_QUERYBUF: "+std::string(strerror(errno)));

            const size_t length=buf.length;
            void* start = mmap(NULL /* start anywhere */,
                               length,
                               PROT_READ | PROT_WRITE /* required */,
                               MAP_SHARED /* recommended */,
                               m_fd, buf.m.offset);
            
            if (MAP_FAILED == start)
                throw std::runtime_error("Failed to mmap: "+std::string(strerror(errno)));
            m_buffers.push_back(std::shared_ptr<uint8_t>((uint8_t*)start, [length](uint8_t* start){ munmap(start, length); }));
        }
        VNXVIDEO_LOG(VNXLOG_DEBUG, "v4lcapture") << "Successfully allocated and mmapped "
                                                 << m_buffers.size() << " buffers for " << m_path;

        // enqueue buffers
        for(int k=0; k<m_buffers.size(); ++k){
            v4l2_buffer buf;
            memset(&buf, 0, sizeof buf);
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = k;

            if (-1 == xioctl(m_fd, VIDIOC_QBUF, &buf))
                throw std::runtime_error("Failed to ioctl VIDIOC_QBUF: "+std::string(strerror(errno)));
        }
        // start capturing
        v4l2_buf_type type(V4L2_BUF_TYPE_VIDEO_CAPTURE);
        if (-1 == xioctl(m_fd, VIDIOC_STREAMON, &type))
            throw std::runtime_error("Failed to ioctl VIDIOC_STREAMON: "+std::string(strerror(errno)));
        VNXVIDEO_LOG(VNXLOG_DEBUG, "v4lcapture") << "Video capturing started for " << m_path;
    }
    void stopCapturing(void)
    {
        v4l2_buf_type type(V4L2_BUF_TYPE_VIDEO_CAPTURE);
        if (-1 == xioctl(m_fd, VIDIOC_STREAMOFF, &type)){
            VNXVIDEO_LOG(VNXLOG_WARNING, "v4lcapture") << "Error on ioctl VIDIOC_STREAMOFF: " << strerror(errno);
        }
        m_buffers.clear();
    }
    int readFrame(v4l2_buffer &buf){
        memset(&buf, 0, sizeof buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (-1 == xioctl(m_fd, VIDIOC_DQBUF, &buf)) {
            switch (errno) {
            case EAGAIN:
                return -1;

            case EIO:
                VNXVIDEO_LOG(VNXLOG_DEBUG, "v4lcapture") << "Got a EIO error on ioctl VIDIOC_DQBUF (temporary problems like signal loss)";
                return -1;

            default:
                VNXVIDEO_LOG(VNXLOG_WARNING, "v4lcapture") << "Error on ioctl VIDIOC_DQBUF: " << strerror(errno);
                return -1;
            }
        }
        return 0;
    }
        
};

class CVideoDeviceManager : public VnxVideo::IVideoDeviceManager
{
public:
    virtual void EnumerateDevices(bool details, VnxVideo::TDevices& dev)
    {
        std::vector<std::string> paths(listFiles("/dev/video*"));
        for(const auto& path: paths){
            VnxVideo::TCapabilities caps;

            int fd = open(path.c_str(), O_RDWR);
            if(-1==fd){
                VNXVIDEO_LOG(VNXLOG_DEBUG, "v4lcapture") << "Could not open v4l device " << path;
                continue;
            }
            std::shared_ptr<void> closeFd(nullptr, [fd](void*){close(fd);});

            int res;

            v4l2_capability qcap;
            memset(&qcap, 0, sizeof qcap);
            res = xioctl(fd, VIDIOC_QUERYCAP, &qcap);

            if(-1==res){
                VNXVIDEO_LOG(VNXLOG_DEBUG, "v4lcapture") << "Could not VIDIOC_QUERYCAP at " << path;
                continue;
            }
            const int requiredCaps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
            if(requiredCaps != (qcap.device_caps & requiredCaps)){
                // capture is because we want to capture
                // streaming is because we want mmap
                VNXVIDEO_LOG(VNXLOG_INFO, "v4lcapture") << "Skipping " << path << " because it does not support both V4L2_CAP_VIDEO_CAPTURE and V4L2_CAP_STREAMING";
                continue;
            }
            std::string deviceName = (const char*)qcap.card;
            
            v4l2_input inp;
            memset(&inp, 0, sizeof inp);
            inp.index=0;
            for(;;++inp.index){
                res = xioctl(fd, VIDIOC_ENUMINPUT, &inp);
                if(res!=0)
                    break;

                if(inp.type!=V4L2_INPUT_TYPE_CAMERA && inp.type!=V4L2_INPUT_TYPE_TUNER)
                    continue;

                /*
                // not sure whether S_INPUT is really required during discovery
                // to obtain true values for format, frame sizes, framerate and so on
                // BUT for sure this call fails when the cature is already in progress
                // so let's comment it out.
                res = xioctl(fd, VIDIOC_S_INPUT, &inp.index);
                if(-1==res){
                    VNXVIDEO_LOG(VNXLOG_DEBUG, "v4lcapture") << "Could not select input " << inp.index << " for device " << path;
                    continue;
                }
                */

                v4l2_fmtdesc fmtd;
                memset(&fmtd, 0, sizeof fmtd);
                fmtd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                for(;;++fmtd.index){
                    res = xioctl(fd, VIDIOC_ENUM_FMT, &fmtd);
                    if(res!=0)
                        break;
                    if(fmtd.flags!=0)
                        continue; // we do not want compressed or emulated formats
                    
                    v4l2_frmsizeenum fsze;
                    memset(&fsze, 0, sizeof fsze);
                    fsze.pixel_format = fmtd.pixelformat;
                    for(;;++fsze.index){
                        res = xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsze);
                        
                        if(res!=0){
                            // a tuner with saa7134 returns errno=inappropriate ioctl for device.
                            if(errno==ENOTTY && fsze.index==0){
                                v4l2_format fmt;
                                memset(&fmt,0,sizeof fmt);
                                fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                                res = xioctl(fd, VIDIOC_G_FMT, &fmt);
                                if(res!=0)
                                    break;
                                // so let's pretend we got a possible resolution, taking a current format as one
                                fsze.type=V4L2_FRMSIZE_TYPE_DISCRETE;
                                fsze.discrete.width=fmt.fmt.pix.width;
                                fsze.discrete.height=fmt.fmt.pix.height;
                            }
                            else{
                                break;
                            }
                        }

                        v4l2_frmivalenum fivl;
                        memset(&fivl, 0, sizeof fivl);
                        fivl.pixel_format = fmtd.pixelformat;
                        if(fsze.type==V4L2_FRMSIZE_TYPE_DISCRETE){
                            fivl.width=fsze.discrete.width;
                            fivl.height=fsze.discrete.height;
                        }
                        else{
                            // to obtain maximal framerate, request the minimal framerate
                            fivl.width=fsze.stepwise.min_width;
                            fivl.height=fsze.stepwise.min_height;
                        }
                        for(;;++fivl.index){
                            res = xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fivl);
                            if(-1==res){
                                if(!(errno==ENOTTY && fivl.index==0))
                                    break;
                            }

                            json j(mkcaps(inp, fmtd, fsze, fivl));
                            std::stringstream ss;
                            ss << j;
                            caps.push_back(ss.str());
                        } // frame intervals
                    } // frame sizes
                } // formats
            } // input pins
            dev[path]=std::make_pair(deviceName, caps);
        } // devices
    }
    VnxVideo::IVideoSource *CreateVideoSource(const VnxVideo::TUniqueName& path, const std::string& mode)
    {
        json jm;
        std::stringstream ss(mode);
        ss >> jm;        
        return new CVideoDevice(path, jm);
    }
private:
    static std::vector<std::string> listFiles(const char* pattern)
    {
        glob_t g;
        memset(&g, 0, sizeof g);
        int r=glob(pattern, 0, nullptr, &g);
        std::vector<std::string> res;
        if(r==GLOB_NOMATCH){
            return res;
        }
        if(r!=0){
            throw std::runtime_error("glob failed for "+std::string(pattern));
        }
        std::shared_ptr<glob_t> p(&g, globfree); // that's to call globfree on exit
        for(int k=0; k<g.gl_pathc; ++k){
            res.push_back(g.gl_pathv[k]);
        }
        return res;
    }

    static json mkcaps(const v4l2_input& inp,
                       const v4l2_fmtdesc& fmtd,
                       const v4l2_frmsizeenum& fsze,
                       const v4l2_frmivalenum& fivl)
    {
        json j;
        j["pin"] = std::to_string(inp.index);
        j["colorspace"] = pf2str(fmtd.pixelformat);
        if("RGB"==pf2str(fmtd.pixelformat)){
            j["bpp"] = 24;
            j["planes"] = 1;
        }
        
        if(fsze.type==V4L2_FRMSIZE_TYPE_DISCRETE){
            j["size"] = {fsze.discrete.width, fsze.discrete.height};
        }
        else if(fsze.type==V4L2_FRMSIZE_TYPE_STEPWISE || fsze.type==V4L2_FRMSIZE_TYPE_CONTINUOUS){
            j["size"]["min"] = {fsze.stepwise.min_width,fsze.stepwise.min_height};
            j["size"]["max"] = {fsze.stepwise.max_width,fsze.stepwise.max_height};
            j["size"]["step"] = {fsze.stepwise.step_width,fsze.stepwise.step_height};
        }

        if(fivl.type==V4L2_FRMIVAL_TYPE_DISCRETE){
            j["framerate"] = round(fivl.discrete.denominator/fivl.discrete.numerator);
        }
        else if(fivl.type==V4L2_FRMIVAL_TYPE_STEPWISE || fivl.type==V4L2_FRMIVAL_TYPE_CONTINUOUS){
            j["framerate"]["max"] = round(fivl.stepwise.min.denominator/fivl.stepwise.min.numerator);
            j["framerate"]["min"] = round(fivl.stepwise.max.denominator/fivl.stepwise.max.numerator);
            j["framerate"]["step"] = round(fivl.stepwise.step.denominator/fivl.stepwise.step.numerator);
        }
        else{
            j["framerate"]=0;
        }
        return j;
    }

};

namespace VnxVideo
{
    IVideoDeviceManager *CreateVideoDeviceManager_V4L()
    {
        return new CVideoDeviceManager();
    }
}
