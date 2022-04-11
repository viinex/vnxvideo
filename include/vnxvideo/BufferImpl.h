#pragma once
#include <memory>

#include "vnxvideoimpl.h"

class CNalBuffer : public VnxVideo::IBuffer {
    std::shared_ptr<uint8_t> m_data;
    size_t m_size;
public:
    CNalBuffer(uint8_t *data, size_t size)
        : m_data((uint8_t*)malloc(size), free)
        , m_size(size)
    {
        if (nullptr == m_data)
            throw std::runtime_error("CNalBuffer ctor: Cannot allocate data buffer");
        memcpy(m_data.get(), data, size);
    }
    void GetData(uint8_t* &data, int& size) {
        data = m_data.get();
        size = (int)m_size;
    }
    VnxVideo::IBuffer* Dup() {
        return new CNalBuffer(*this);
    }
};
class CNoOwnershipNalBuffer : public VnxVideo::IBuffer {
    uint8_t* m_data;
    size_t m_size;
public:
    CNoOwnershipNalBuffer(uint8_t *data, size_t size)
        : m_data(data)
        , m_size(size)
    {
    }
    void GetData(uint8_t* &data, int& size) {
        data = m_data;
        size = (int)m_size;
    }
    VnxVideo::IBuffer* Dup() {
        return new CNalBuffer(m_data, m_size);
    }
};
