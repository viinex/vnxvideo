#pragma once

#include <cstddef>
#include <cstdlib>
#include <ippi.h>
#include <ippcc.h>
#include "vnxipp.h"
#include "vnxvideoimpl.h"

class IAllocator {
public:
    ~IAllocator() {}
    virtual std::shared_ptr<uint8_t> Alloc(int size) = 0;
};
typedef std::shared_ptr<IAllocator> PAllocator;

class IShmMapping {
public:
    ~IShmMapping() {}
    virtual uint64_t FromPointer(void* ptr) = 0;
    virtual void* ToPointer(uint64_t offset) = 0;
};

class IShmAllocator : public virtual IAllocator, public virtual IShmMapping {
public:
    virtual IShmAllocator* Dup() = 0;
};
typedef std::shared_ptr<IShmAllocator> PShmAllocator;

extern IAllocator* const g_privateAllocator;

IShmAllocator *CreateShmAllocator(const char* name, int maxSizeMB);
IShmMapping* CreateShmMapping(const char* name);

void WithPreferredShmAllocator(IShmAllocator* allocator, std::function<void(void)> action);
IShmAllocator* GetPreferredShmAllocator();
PShmAllocator DupPreferredShmAllocator();


class CRawSample : public VnxVideo::IRawSample
{
private:
    const EColorspace m_csp;
    union {
        const int m_width;
        const int m_nsamples;
    };
    union {
        const int m_height;
        const int m_nchannels;
    };
    std::shared_ptr<uint8_t> m_data;

    int m_nplanes;
    int m_strides[4];
    ptrdiff_t m_offsets[4];

    std::shared_ptr<void> m_underlying;

    static int ceil16(int v) {
        return (v & 0x0000000f) ? ((v & 0x0ffffff0) + 0x10) : v;
    }
public:
private:
    void Init(ERawMediaFormat csp, int width, int height, int* strides, uint8_t **planes, IAllocator* allocate)
    {
        // NB: for video formats, it could be that csp!=m_csp inside of this call.
        // which means that format conversion should be performed.
        // m_csp would typically be EMF_I420 in such cases.

        if (csp != EMF_I420 && csp < EMF_AUDIO && planes != nullptr && !allocate)
            throw std::logic_error("Sample format other than I420 is only supported when wrapping or allocating a frame");

        if (allocate) {
            FillStridesOffsets(m_csp, m_width, m_height, m_nplanes, m_strides, m_offsets, true);
            int size=0;
            if (csp < EMF_AUDIO) {
                for (int k = 0; k < m_nplanes; ++k)
                    m_strides[k] = ceil16(m_strides[k]);

                int height2 = m_height;
                if (m_csp == EMF_I420 || m_csp == EMF_P440)
                    height2 /= 2;
                int heights[3] = { m_height, height2, height2 };
                // how did it come to this:
                // see https://github.com/mozilla/mozjpeg/blob/master/turbojpeg.c#L630
                // function tjBufSize. Both width and height are rounded up to MCU size (which is at most 16).
                // 2048 is added then (in advance?).
                size = 2048;
                for (int k = 0; k < m_nplanes; ++k)
                    size += m_strides[k] * ceil16(heights[k]);
            }
            else {
                switch (m_csp) {
                case EMF_LPCM16: 
                case EMF_LPCM16P:
                    size = m_nsamples * m_nchannels * 2;
                    break;
                case EMF_LPCM32:
                case EMF_LPCM32P:
                case EMF_LPCMF:
                case EMF_LPCMFP:
                    size = m_nsamples * m_nchannels * 2;
                    break;
                }
            }
            m_data = allocate->Alloc(size);
            if (nullptr == m_data)
                throw std::runtime_error("CRawSample::Init(): Cannot allocate data buffer");

            if (strides && planes) {
                uint8_t* my_planes[4] = { m_data.get() + m_offsets[0], m_data.get() + m_offsets[1], m_data.get() + m_offsets[2], m_data.get() + m_offsets[3] };
                if(m_csp==EMF_I420) // the only case we can handle when copying video data with format conversion
                    CopyRawToI420(width, height, csp, planes, strides, my_planes, m_strides);
            }
        }
        else {
            m_data.reset(planes[0], [](void*) {});
            m_nplanes = 3;
            for (int k = 0; k < 3; ++k) {
                m_strides[k] = strides[k];
                m_offsets[k] = planes[k] - m_data.get();
            }
        }
    }
public:
    CRawSample(ERawMediaFormat emf, int width, int height, IAllocator* allocator = nullptr)
        : m_csp(emf)
        , m_width(width)
        , m_height(height)
    {
        IAllocator* a = allocator;
        if (a == nullptr)
            a = GetPreferredShmAllocator();
        if (a == nullptr)
            a = g_privateAllocator;
        Init(emf, width, height, nullptr, nullptr, a);
    }
    // for backwards compatibility, deprecated
    CRawSample(int width, int height, IAllocator* allocator = nullptr)
        : m_csp(EMF_I420)
        , m_width(width)
        , m_height(height)
    {
        IAllocator* a = allocator;
        if (a == nullptr)
            a = GetPreferredShmAllocator();
        if (a == nullptr)
            a = g_privateAllocator;

        Init(m_csp, width, height, nullptr, nullptr, a);
    }

    // copyData means copy actually, into I420, only if the source data is specified;
    // otherwise copyData means allocate memory for the sample of specific size and colorspace
    CRawSample(EColorspace csp, int width, int height, int* strides, uint8_t **planes, bool copyData)
        : m_csp((planes != nullptr && copyData && csp < EMF_AUDIO)?EMF_I420:csp)
        , m_width(width)
        , m_height(height)
    {
        IAllocator* a = nullptr;
        if (copyData || (strides == nullptr && planes == nullptr)) {
            a = GetPreferredShmAllocator();
            if (a == nullptr)
                a = g_privateAllocator;
        }

        Init(csp, width, height, strides, planes, a);
    }
    CRawSample(EColorspace csp, int width, int height, int* strides, uint8_t **planes, IAllocator* allocator)
        : m_csp(csp)
        , m_width(width)
        , m_height(height)
    {
        IAllocator* a = allocator;
        if (a == nullptr)
            a = GetPreferredShmAllocator();
        if (a == nullptr)
            a = g_privateAllocator;

        Init(csp, width, height, strides, planes, a);
    }
    CRawSample(EColorspace csp, int width, int height, int* strides, uint8_t **planes,
               std::shared_ptr<void> underlying) // there's an underlying shared object which allows us not to copy data
        : m_csp(csp)
        , m_width(width)
        , m_height(height)
        , m_underlying(underlying)
    {
        Init(csp, width, height, strides, planes, nullptr);
    }
    void GetFormat(EColorspace &csp, int &width, int &height) {
        csp = m_csp;
        width = m_width;
        height = m_height;
    }
    void GetData(unsigned char* &data, int& size) {
        throw std::logic_error("this method should not be called for that class");
    }
    void GetData(int* strides, uint8_t** planes) {
        for (int k = 0; k < m_nplanes; ++k) {
            strides[k] = m_strides[k];
            planes[k] = m_data.get() + m_offsets[k];
        }
    }
    VnxVideo::IRawSample* Dup() {
        return new CRawSample(*this); // copy constructor will do as CComPtr copy ctor provides behaviour we need
    }

public:
    static void FillStridesOffsets(ERawMediaFormat emf, int p1, int p2, int& nplanes, int* strides, ptrdiff_t* offsets, bool alignStridesAndHeights) {
        const int width = p1;
        const int height = p2;
        const int nsamples = p1;
        const int nchannels = p2;
        auto ceilDim = alignStridesAndHeights ? ceil16 : [](int x) {return x; };
        memset(strides, 0, sizeof(int) * 4);
        memset(offsets, 0, sizeof(ptrdiff_t) * 4);
        switch (emf) {
        case EMF_I420:
            nplanes = 3;
            strides[0] = ceilDim(width);
            strides[1] = strides[2] = ceilDim(width / 2);
            offsets[0] = 0;
            offsets[1] = strides[0] * ceilDim(height);
            offsets[2] = offsets[1] + strides[1] * ceilDim(height / 2);
            break;
        case EMF_YV12:
            nplanes = 3;
            strides[0] = ceilDim(width);
            strides[1] = strides[2] = ceilDim(width / 2);
            offsets[0] = 0;
            offsets[2] = strides[0] * ceilDim(height);
            offsets[1] = offsets[1] + strides[1] * ceilDim(height / 2);
            break;
        case EMF_NV12:
        case EMF_NV21:
            nplanes = 2;
            strides[0] = width;
            strides[1] = width;
            offsets[0] = 0;
            offsets[1] = width*height;
            break;
        case EMF_YUY2:
        case EMF_UYVY:
            nplanes = 1;
            strides[0] = width*2;
            offsets[0] = 0;
            break;
        case EMF_I444:
            nplanes = 3;
            strides[0] = strides[1] = strides[2] = ceilDim(width);
            offsets[0] = 0;
            offsets[1] = strides[0] * ceilDim(height);
            offsets[2] = offsets[1] + strides[1] * ceilDim(height);
            break;
        case EMF_P422:
            nplanes = 3;
            strides[0] = ceilDim(width);
            strides[1] = strides[2] = ceilDim(width) / 2;
            offsets[0] = 0;
            offsets[1] = strides[0] * ceilDim(height);
            offsets[2] = offsets[1] + strides[1] * ceilDim(height);
            break;
        case EMF_P440:
            nplanes = 3;
            strides[0] = strides[1] = strides[2] = ceilDim(width);
            offsets[0] = 0;
            offsets[1] = strides[0] * ceilDim(height/2);
            offsets[2] = offsets[1] + strides[1] * ceilDim(height/2);
            break;
        case EMF_GRAY:
            nplanes = 1;
            strides[0] = ceilDim(width);
            strides[1] = strides[2] = 0;
            offsets[0] = 0;
            offsets[1] = strides[0] * ceilDim(height);
            offsets[2] = offsets[1] + strides[1] * ceilDim(height);
            break;
        case EMF_LPCM16:
            nplanes = 1;
            strides[0] = nsamples * nchannels * 2;
            offsets[0] = 0;
            break;
        case EMF_LPCM32:
        case EMF_LPCMF:
            nplanes = 1;
            strides[0] = nsamples * nchannels * 4;
            offsets[0] = 0;
            break;
        case EMF_LPCM16P:
            nplanes = nchannels;
            strides[0] = nsamples * 2; // FFmpeg convention: only first stride is set
            for(int k=0; k < std::min<int>(nchannels, 4); ++k)
                offsets[k] = k * nsamples * 2;
            break;
        case EMF_LPCM32P:
        case EMF_LPCMFP:
            nplanes = nchannels;
            strides[0] = nsamples * 4;
            for (int k = 0; k < std::min<int>(nchannels, 4); ++k)
                offsets[k] = k * nsamples * 4;
            break;
        }
    }
    /*
    never tested
    static void CopyRawToI422(int width, int height,
        EColorspace src_emf, const uint8_t* const* src, int* src_strides,
        uint8_t** dst, int* dst_strides)
    {
        IppiSize roi = { width, height };
        if (src_emf == EMF_I420) {
            ippiYCbCr420ToYCbCr422_8u_P3R((const Ipp8u**)src, src_strides, dst, dst_strides, roi);
        }
        else if (src_emf == EMF_YV12)
        {
            ippiYCrCb420ToYCbCr422_8u_P3R((const Ipp8u**)src, src_strides, dst, dst_strides, roi);
        }
        else if (src_emf == EMF_YUY2)
        {
            // todo - swap dst u and v planes here
            ippiYCrCb422ToYCbCr422_8u_C2P3R(src[0], src_strides[0], dst, dst_strides, roi);
        }
        else if (src_emf == EMF_UYVY)
        {
            ippiCbYCr422ToYCbCr422_8u_C2P3R(src[0], src_strides[0], dst, dst_strides, roi);
        }
    }
    */
    static void CopyRawToI420(int width, int height, 
        EColorspace src_emf, const uint8_t* const* src, int* src_strides, 
        uint8_t** dst, int* dst_strides) 
    {
        IppiSize roi = { width, height };
        if (src_emf == EMF_I420) {
            IppiSize roi2 = { width / 2, height / 2 };
            ippiCopy_8u_C1R(src[0], src_strides[0], dst[0], dst_strides[0], roi);
            ippiCopy_8u_C1R(src[1], src_strides[1], dst[1], dst_strides[1], roi2);
            ippiCopy_8u_C1R(src[2], src_strides[2], dst[2], dst_strides[2], roi2);
        }
        else if (src_emf == EMF_YV12)
        {
            IppiSize roi2 = { width / 2, height / 2 };
            ippiCopy_8u_C1R(src[0], src_strides[0], dst[0], dst_strides[0], roi);
            ippiCopy_8u_C1R(src[2], src_strides[2], dst[1], dst_strides[1], roi2);
            ippiCopy_8u_C1R(src[1], src_strides[1], dst[2], dst_strides[2], roi2);
        }
        else if (src_emf == EMF_NV12)
        {
            vnxippiResize_8u_P2P3R(src, { width, height }, src_strides, { 0,0,width,height }, dst, dst_strides,
                { 0,0,width,height }, 1, 1, IPPI_INTER_NN);
        }
        else if (src_emf == EMF_I444)
        {
            VnxIppiSize roi2 = { width / 2, height / 2 };
            ippiCopy_8u_C1R(src[0], src_strides[0], dst[0], dst_strides[0], roi);
            vnxippiResize_8u_C1R(src[1], { width, height }, src_strides[1], { 0,0,width,height }, dst[1], dst_strides[1], roi2, 0.5, 0.5, IPPI_INTER_NN);
            vnxippiResize_8u_C1R(src[2], { width, height }, src_strides[2], { 0,0,width,height }, dst[2], dst_strides[2], roi2, 0.5, 0.5, IPPI_INTER_NN);
        }
        else if (src_emf == EMF_P422) {
            IppiSize roi2 = { width / 2, height / 2 };
            ippiCopy_8u_C1R(src[0], src_strides[0], dst[0], dst_strides[0], roi);
            ippiCopy_8u_C1R(src[1], src_strides[1] * 2, dst[1], dst_strides[1], roi2);
            ippiCopy_8u_C1R(src[2], src_strides[2] * 2, dst[2], dst_strides[2], roi2);
        }
        else if (src_emf == EMF_P440)
        {
            VnxIppiSize roi2 = { width / 2, height / 2 };
            ippiCopy_8u_C1R(src[0], src_strides[0], dst[0], dst_strides[0], roi);
            vnxippiResize_8u_C1R(src[1], { width, height }, src_strides[1], { 0,0,width,height/2 }, dst[1], dst_strides[1], roi2, 0.5, 1, IPPI_INTER_NN);
            vnxippiResize_8u_C1R(src[2], { width, height }, src_strides[2], { 0,0,width,height/2 }, dst[2], dst_strides[2], roi2, 0.5, 1, IPPI_INTER_NN);
        }
        else if (src_emf == EMF_GRAY)
        {
            IppiSize roi2 = { width / 2, height / 2 };
            ippiCopy_8u_C1R(src[0], src_strides[0], dst[0], dst_strides[0], roi);
            ippiSet_8u_C1R(0x80, dst[1], dst_strides[1], roi2);
            ippiSet_8u_C1R(0x80, dst[2], dst_strides[2], roi2);
        }
        else if (src_emf == EMF_YUY2)
        {
            ippiYCbCr422ToYCbCr420_8u_C2P3R(src[0], src_strides[0], dst, dst_strides, roi);
        }
        else if (src_emf == EMF_UYVY)
        {
            uint8_t* dst1[3] = { dst[0], dst[2], dst[1] };
            int dst_strides1[3] = { dst_strides[0], dst_strides[2], dst_strides[1] };
            ippiCbYCr422ToYCrCb420_8u_C2P3R(src[0], src_strides[0], dst1, dst_strides1, roi);
        }
        else if (src_emf == EMF_YVU9)
        {
            // never ever tested
            ippiCopy_8u_C1R(src[0], src_strides[0], dst[0], dst_strides[0], roi);
            for (int y = 0; y<height / 4; ++y)
            {
                const uint8_t* vs = src[1] + y*width / 4; //src[0] + width*height
                const uint8_t* us = src[2] + y*width / 4; //src[0] + width*height * 17 / 16
                uint8_t* ud = dst[1] + y * 2 * width / 2;
                uint8_t* vd = dst[2] + y * 2 * width / 2;
                for (int x = 0; x<width / 4; ++x)
                {
                    ud[2 * x] = us[x];
                    ud[2 * x + 1] = us[x];
                    vd[2 * x] = vs[x];
                    vd[2 * x + 1] = vs[x];
                    ud[2 * x + width / 2] = us[x];
                    ud[2 * x + 1 + width / 2] = us[x];
                    vd[2 * x + width / 2] = vs[x];
                    vd[2 * x + 1 + width / 2] = vs[x];
                }
            }
        }
        else if (src_emf == EMF_RGB32)
        {
            ippiBGRToYCbCr420_8u_AC4P3R(src[0], src_strides[0], dst, dst_strides, roi);
        }
        else if (src_emf == EMF_RGB24)
        {
            ippiBGRToYCbCr420_8u_C3P3R(src[0], src_strides[0], dst, dst_strides, roi);
        }
        else if (src_emf == EMF_RGB16)
        {
            vnxippiBGR565ToYCbCr420_16u8u_C3P3R((unsigned short*)src[0], src_strides[0], dst, dst_strides, { roi.width, roi.height });
        }
    }
    static void CopyI420ToRGB(int width, int height,
        const uint8_t* const* src, int* src_strides,
        int dst_bpp, uint8_t* dst, int dst_stride) {
        if (dst_bpp == 32) {
            ippiYCbCr420ToBGR_8u_P3C4R((const uint8_t**)src, src_strides, dst, dst_stride, { width, height }, 0);
        }
        else if (dst_bpp == 24) {
            ippiYCbCr420ToBGR_8u_P3C3R((const uint8_t**)src, src_strides, dst, dst_stride, { width, height });
        }
    }
};

// A sample created from another sample by selecting a specific ROI. Shares same underlying memory with original sample.
class CRawSampleRoi : public VnxVideo::IRawSample {
public:
    CRawSampleRoi(VnxVideo::IRawSample* sample, int left, int top, int width, int height) 
        : m_sample(sample->Dup())
        , m_left(left)
        , m_top(top)
        , m_width(width)
        , m_height(height)
    {
        EColorspace csp;
        int w;
        int h;
        sample->GetFormat(csp, w, h);
        if (csp != EMF_I420 && csp != EMF_NV12) {
            throw std::logic_error("CRawSampleRoi ctor: colorspace format not supported for selecting ROI");
        }
        if (left < 0 || top < 0 || width <= 0 || height <= 0) {
            throw std::logic_error("CRawSampleRoi ctor: roi left/top cannot be negative, roi size cannot be non-positive");
        }
        if (left + width > w || top + height > h) {
            throw std::logic_error("CRawSampleRoi ctor: roi exceeds original sample size");
        }
        m_csp = csp;
    }
    VnxVideo::IRawSample* Dup() {
        return new CRawSampleRoi(*this); // copy constructor will do as underlying shared_ptr provides behaviour we need
    }
    void GetFormat(EColorspace &csp, int &width, int &height) {
        csp = m_csp;
        width = m_width;
        height = m_height;
    }
    void GetData(int* strides, uint8_t** planes) {
        m_sample->GetData(strides, planes);
        if (m_csp == EMF_I420) {
            planes[0] += m_top*strides[0] + m_left;
            planes[1] += (m_top / 2)*strides[1] + m_left / 2;
            planes[2] += (m_top / 2)*strides[2] + m_left / 2;
        }
        else if(m_csp==EMF_NV12) {
            planes[0] += m_top*strides[0] + m_left;
            planes[1] += m_top*strides[1] + m_left;
        }
    }
private:
    std::shared_ptr<VnxVideo::IRawSample> m_sample;
    EColorspace m_csp;
    const int m_left;
    const int m_top;
    const int m_width;
    const int m_height;
};

