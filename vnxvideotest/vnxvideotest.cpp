#include <iostream>
#include <functional>
#include <fstream>
#include <cstdlib>

#include <vnxvideo/vnxvideo.h>
#include <vnxvideo/vnxvideoimpl.h>
#include <vnxvideo/vnxvideologimpl.h>

using namespace std::placeholders;

void log_handler(void* usrptr, ELogLevel level, const char* subsystem, const char* message) {
    std::cerr << level << " " << subsystem << " " << message << std::endl;
}

class CMultiRawProc : public VnxVideo::IRawProc {
public:
    void AddProcessor(VnxVideo::IRawProc* p) {
        m_processors.push_back(p);
    }
    void SetFormat(EColorspace csp, int width, int height) {
        for (auto p : m_processors)
            p->SetFormat(csp, width, height);
    }
    virtual void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        for (auto p : m_processors)
            p->Process(sample, timestamp);
    }
    virtual void Flush() {
        for (auto p : m_processors)
            p->Flush();
    }
private:
    std::vector<VnxVideo::IRawProc*> m_processors;
};

//extern "C" __declspec(dllimport) 
int create_geutebrueck_live_source(const char* json_config, vnxvideo_h264_source_t* source) {
    return -1;
}

int on_handle_data(void* opaque, vnxvideo_buffer_t buffer, uint64_t ts) {
    std::cout << ts << std::endl;
    return 0;
}

int main1(int argc, char** argv) {
    vnxvideo_init(log_handler, 0, VNXLOG_DEBUG);
    vnxvideo_h264_source_t src;
    int res = create_geutebrueck_live_source("{\"address\":\"192.168.120.131\",\"username\":\"sysadmin\",\"password\":\"masterkey\",\"channel\":1}", &src);
    if (0 != res)
        return 1;
    vnxvideo_h264_source_subscribe(src, on_handle_data, nullptr);
    vnxvideo_h264_source_start(src);
    std::getline(std::cin, std::string());
    vnxvideo_h264_source_stop(src);
    vnxvideo_h264_source_free(src);
    return 0;
}

int main2(int argc, char** argv) {
    vnxvideo_init(log_handler, 0, VNXLOG_DEBUG);

    std::ifstream ifs(argv[1]);
    std::string path, mode;
    ifs >> path >> mode;
    VnxVideo::PVideoDeviceManager man(VnxVideo::CreateVideoDeviceManager_DirectShow());
    VnxVideo::TDevices dev;
    man->EnumerateDevices(false, dev);
    VnxVideo::PVideoSource src;
    VnxVideo::PRawProc lprov;
    VnxVideo::WithPreferredShmAllocator("qweqwe", [&]() {
        src.reset(man->CreateVideoSource(dev.begin()->first, mode));
        lprov.reset(VnxVideo::CreateLocalVideoProvider("qweqwe"));
    });

    src->Subscribe(std::bind(&VnxVideo::IRawProc::SetFormat, lprov.get(), _1, _2, _3),
        std::bind(&VnxVideo::IRawProc::Process, lprov.get(), _1, _2));
    src->Run();


    VnxVideo::PRawProc disp;
    VnxVideo::PVideoSource lsrc;
    disp.reset(VnxVideo::CreateDisplay(1600, 1200, "qweqwe", [=]() {
        std::cin.putback('\r');
        std::cin.putback('\n');
    }));
    lsrc.reset(VnxVideo::CreateLocalVideoClient("qweqwe"));
    lsrc->Subscribe(
        [&](EColorspace csp, int width, int height) {
        disp->SetFormat(csp, width, height);
    },
        [&](VnxVideo::IRawSample* f, uint64_t ts) {
        disp->Process(f, ts);
    });
    lsrc->Run();


    std::string s;
    std::getline(std::cin, s);

    //src->Stop();
    lsrc->Stop();
    return 0;
}



int main(int argc, char** argv)
{
    std::shared_ptr<VnxVideo::IRawSample> overlay(VnxVideo::LoadBMP("c:\\temp\\qq.bmp"));
    vnxvideo_init(log_handler, 0, VNXLOG_DEBUG);
    if (argc >= 2) {
        try
        {
            std::ifstream ifs(argv[1]);
            std::string path, mode;
            ifs >> path >> mode;
            VnxVideo::PVideoDeviceManager man(VnxVideo::CreateVideoDeviceManager_DirectShow());
            VnxVideo::PVideoSource src(man->CreateVideoSource(path, mode));
            VnxVideo::PVideoEncoder enc(VnxVideo::CreateVideoEncoder_OpenH264("baseline", "veryfast", 25, "small_size"));
            VnxVideo::PVideoDecoder dec(VnxVideo::CreateVideoDecoder_FFmpegH264());
            uint8_t colorkey[4] = { 0,255,0, 0 };
            VnxVideo::PComposer comp(VnxVideo::CreateComposer(colorkey, 20, 100));
            comp->SetOverlay(overlay.get());

            dec->Subscribe([](EColorspace csp, int width, int height) {
                std::cout << "Decoded into format: " << csp << ", width: " << width << ", height: " << height << std::endl;
            }, [](VnxVideo::IRawSample* s, uint64_t ts) {
                std::cout << "Decoded: " << s << ", ts: " << ts << std::endl;
            });

            CMultiRawProc m;
            m.AddProcessor(comp.get());
            m.AddProcessor(enc.get());

            src->Subscribe(std::bind(&VnxVideo::IRawProc::SetFormat, &m, _1, _2, _3),
                std::bind(&VnxVideo::IRawProc::Process, &m, _1, _2));
            std::ofstream ofs("c:\\temp\\cap.h264", std::ios_base::binary);
            enc->Subscribe([&](VnxVideo::IBuffer* buf, uint64_t ts) {
                uint8_t *d;
                int sz;
                buf->GetData(d, sz);
                ofs.write((char*)d, sz);
                dec->Decode(buf, ts);
                std::cout << ts << '\t' << sz << std::endl;
            });
            src->Run();
            getchar();
            src->Stop();
            m.Flush();
        }
        catch (const std::exception& e) {
            std::cerr << "EXCEPTION: " << e.what() << std::endl;
        }

        
        VnxVideo::TDevices dev;
        VnxVideo::PVideoDeviceManager man(VnxVideo::CreateVideoDeviceManager_DirectShow());
        man->EnumerateDevices(true,dev);

        for (auto& d : dev) {
            std::cout << "Display name=" << d.second.first << std::endl;
            std::cout << "Unique  name=" << d.first << std::endl;
            for (auto& c : d.second.second) {
                std::cout << "\t" << c << std::endl;
            }
        }
        
    }
    else {
        char* buf=new char[65536];
        vnxvideo_manager_t mgr;
        std::cerr << 1 << std::endl;
        vnxvideo_manager_dshow_create(&mgr);
        std::cerr << 2 << std::endl;
        vnxvideo_enumerate_video_sources(mgr, true, buf, 65536);
        std::cerr << 3 << std::endl;
        std::cout << buf << std::endl;
        delete[] buf;
    }

    return 0;
}