#include <iostream>
#include <functional>
#include <fstream>
#include <cstdlib>
#include <mutex>

#include <vnxvideo/vnxvideo.h>
#include <vnxvideo/vnxvideoimpl.h>
#include <vnxvideo/vnxvideologimpl.h>

using namespace std::placeholders;

void log_handler(void* usrptr, ELogLevel level, const char* subsystem, const char* message) {
    std::cerr << level << " " << subsystem << " " << message << std::endl;
}

int main(int argc, char** argv) {
    vnxvideo_init(log_handler, 0, VNXLOG_DEBUG);

    try {
        std::string origin("rend0");
        if (argc >= 2)
            origin = argv[1];

        VnxVideo::PRawProc disp;
        VnxVideo::PVideoSource lsrc;
        
        bool stop=false;
        std::mutex mutex;
        std::condition_variable condition;

        disp.reset(VnxVideo::CreateDisplay(1600, 1200, ("Viinex :: " + origin).c_str(), [&]() {
            std::unique_lock<std::mutex> lock(mutex);
            stop = true;
            condition.notify_all();
        }));
        lsrc.reset(VnxVideo::CreateLocalVideoClient(origin.c_str()));
        lsrc->Subscribe(
            [&](EColorspace csp, int width, int height) {
            disp->SetFormat(csp, width, height);
        },
            [&](VnxVideo::IRawSample* f, uint64_t ts) {
            disp->Process(f, ts);
        });

        lsrc->Run();


        std::string s;
        std::cout << "Close display window to quit" << std::endl;

        std::unique_lock<std::mutex> lock(mutex);
        while(!stop)
            condition.wait(lock);

        lsrc->Stop();
        return 0;
    }
    catch (const std::exception& e) {

    }
}



