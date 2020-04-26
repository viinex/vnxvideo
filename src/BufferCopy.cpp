#include <cstring>
#include "vnxvideoimpl.h"

namespace {

    class CBuffer : public VnxVideo::IBuffer {
    public:
        CBuffer(const uint8_t* data, int size, bool copy)
            :m_size(size)
        {
            if (copy) {
                m_data.reset((uint8_t*)malloc(size), free);
                if (nullptr == m_data)
                    throw std::runtime_error("CBuffer ctor: Cannot allocate data buffer");
                memcpy(m_data.get(), data, size);
            }
            else {
                m_data.reset(const_cast<uint8_t*>(data), [](uint8_t*) {});
            }
        }
        void GetData(uint8_t* &data, int& size) {
            data = m_data.get();
            size = m_size;
        }
        IBuffer* Dup() {
            return new CBuffer(*this);
        }
    private:
        std::shared_ptr<uint8_t> m_data;
        const int m_size;
    };
}

int vnxvideo_buffer_wrap(const uint8_t *data, int size, vnxvideo_buffer_t* res) {
    res->ptr = static_cast<VnxVideo::IBuffer*>(new CBuffer(data, size, false));
    return vnxvideo_err_ok;
}

int vnxvideo_buffer_copy_wrap(const uint8_t *data, int size, vnxvideo_buffer_t* res) {
    res->ptr = static_cast<VnxVideo::IBuffer*>(new CBuffer(data, size, true));
    return vnxvideo_err_ok;
}

int vnxvideo_buffer_copy(vnxvideo_buffer_t src, vnxvideo_buffer_t* dst) {
    auto s = reinterpret_cast<VnxVideo::IBuffer*>(src.ptr);
    uint8_t* data;
    int size;
    s->GetData(data, size);
    dst->ptr= static_cast<VnxVideo::IBuffer*>(new CBuffer(data, size, true));
    return vnxvideo_err_ok;
}
