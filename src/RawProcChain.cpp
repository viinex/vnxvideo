#include <vector>

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

class CRawProcChain : public virtual VnxVideo::IRawProcChain {
    virtual void SetFormat(ERawMediaFormat csp, int width, int height) {
        for (auto link : m_links) {
            link->SetFormat(csp, width, height);
        }
    }
    virtual void Process(VnxVideo::IRawSample * sample, uint64_t timestamp) {
        for (auto link : m_links) {
            link->Process(sample, timestamp);
        }
    }
    virtual void Flush() {
        for (auto link : m_links)
            link->Flush();
    }
    virtual void Link(VnxVideo::IRawProc *link) {
        m_links.push_back(link);
    }
private:
    std::vector<VnxVideo::IRawProc*> m_links;
};

namespace VnxVideo {
    VNXVIDEO_DECLSPEC IRawProcChain* CreateRawProcChain() {
        return new CRawProcChain();
    }
}
