#include <fstream>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <ipp/ippcc.h>
#include <ipp/ippi.h>

#include "vnxipp.h"
#include "vnxvideoimpl.h"

class CComposer : public VnxVideo::IComposer {
public:
    CComposer(uint8_t colorkey[4], int left, int top) 
        : m_overlayLeft(left)
        , m_overlayTop(top)
        , m_width(0)
        , m_height(0)
        , m_csp(EMF_NONE)
    {
        memcpy(m_colorkey, colorkey, 4);
    }
    virtual void Flush(){}
    virtual void SetFormat(EColorspace csp, int w, int h) {
        if (csp != EMF_I420)
            throw std::runtime_error("composer is not implemented for target format other than I420");
        std::lock_guard<std::mutex> lock(m_mutex);
        m_csp = csp;
        m_width = w;
        m_height = h;
        prepare();
    }
    virtual void SetOverlay(VnxVideo::IRawSample* overlay) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (nullptr != overlay) {
            EColorspace csp;
            int w, h;
            overlay->GetFormat(csp, w, h);
            if (csp != EMF_RGB16 && csp != EMF_RGB24 && csp != EMF_RGB32)
                throw std::runtime_error("unsupported overlay image format");
            m_overlay.reset(overlay->Dup());
        }
        else
            m_overlay.reset();
        prepare();
    }
    virtual void Process(VnxVideo::IRawSample *s, uint64_t) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_overlay.get() == nullptr)
            return;
        IppiSize roi = { std::min(m_overlayWidth, m_width-m_overlayLeft), std::min(m_overlayHeight, m_height-m_overlayTop) };
        IppiSize roi2 = { roi.width/2, roi.height/2 };
        int strides[4];
        uint8_t *planes[4];
        s->GetData(strides, planes);

        planes[0] += m_overlayTop*strides[0] + m_overlayLeft;
        planes[1] += (m_overlayTop/2)*strides[1] + m_overlayLeft / 2;
        planes[2] += (m_overlayTop/2)*strides[2] + m_overlayLeft / 2;

        ippiCopy_8u_C1MR(m_overlayPrepared.get(), 
            m_overlayWidth,
            planes[0],strides[0],
            roi,
            m_mask.get(), m_overlayWidth);
        ippiCopy_8u_C1MR(m_overlayPrepared.get() + (m_overlayHeight*m_overlayWidth * 4) / 4, m_overlayWidth / 2,
            planes[1], strides[1],
            roi2,
            m_mask2.get(), m_overlayWidth / 2);
        ippiCopy_8u_C1MR(m_overlayPrepared.get() + (m_overlayHeight*m_overlayWidth * 5) / 4, m_overlayWidth / 2,
            planes[2], strides[2],
            roi2,
            m_mask2.get(), m_overlayWidth / 2);
    }
private:
    uint8_t m_colorkey[4];
    int m_overlayLeft;
    int m_overlayTop;
    int m_overlayWidth;
    int m_overlayHeight;

    std::shared_ptr<VnxVideo::IRawSample> m_overlay;
    std::shared_ptr<uint8_t> m_mask;
    std::shared_ptr<uint8_t> m_mask2;
    std::shared_ptr<uint8_t> m_overlayPrepared;

    int m_width;
    int m_height;
    EColorspace m_csp;

    std::mutex m_mutex;

    void prepare() {
        if (nullptr == m_overlay) {
            m_mask.reset();
            m_overlayPrepared.reset();
            return;
        }
        if (m_csp == EMF_NONE)
            return;
        EColorspace csp;
        m_overlay->GetFormat(csp, m_overlayWidth, m_overlayHeight);
        m_overlayWidth -= m_overlayWidth % 4;
        m_overlayHeight -= m_overlayHeight % 2;
        m_mask.reset((uint8_t*)malloc(m_overlayWidth*m_overlayHeight), free);
        m_mask2.reset((uint8_t*)malloc(m_overlayWidth*m_overlayHeight/4), free);
        m_overlayPrepared.reset((uint8_t*)malloc(m_overlayWidth*m_overlayHeight*3/2), free);

        if (nullptr == m_mask.get() || nullptr == m_mask2 || nullptr==m_overlayPrepared)
            throw std::runtime_error("CComposer::prepare(): Cannot allocate data buffer");

        int offsetY = 0;
        int pitchY = m_overlayWidth;
        int offsetU = m_overlayWidth*m_overlayHeight;
        int pitchU = m_overlayWidth / 2;
        int offsetV = m_overlayWidth*m_overlayHeight * 5 / 4;
        int pitchV = m_overlayWidth / 2;
        IppiSize roi = { m_overlayWidth, m_overlayHeight };
        Ipp8u* dst[3] = {
            m_overlayPrepared.get() + offsetY,
            m_overlayPrepared.get() + offsetU,
            m_overlayPrepared.get() + offsetV
        };
        int steps[3] = { pitchY, pitchU, pitchV };

        int strides[4];
        uint8_t* planes[4];
        memset(planes, 0, sizeof planes);
        m_overlay->GetData(strides, planes);
        if (planes[1] != nullptr)
            throw std::logic_error("planar rgb not supported here");
        switch (csp) {
        case EMF_RGB32: {
            uint32_t key = (m_colorkey[0] << 24) + (m_colorkey[1] << 16) + (m_colorkey[2] << 8) + (m_colorkey[3] << 0);
            for (int y = 0; y < m_overlayHeight; ++y) {
                uint32_t* p = (uint32_t*)(planes[0] + strides[0] * y);
                for (int x = 0; x < m_overlayWidth; ++x)
                    m_mask.get()[x + y*m_overlayWidth] = p[x] == key? 0 : 1;
            }
            if (m_csp == EMF_I420) {
                ippiBGRToYCbCr420_8u_AC4P3R(planes[0], strides[0], dst, steps, roi);
            }
            break;
        }
        case EMF_RGB24: {
            uint32_t key = (m_colorkey[0] << 16) + (m_colorkey[1] << 8) + (m_colorkey[2] << 0);
            for (int y = 0; y < m_overlayHeight; ++y) {
                uint8_t* p = (uint8_t*)(planes[0] + strides[0] * y);
                for (int x = 0; x < m_overlayWidth; ++x) {
                    uint32_t v = (p[x * 3 + 0] << 16) + (p[x * 3 + 1] << 8) + (p[x * 3 + 2] << 0);
                    m_mask.get()[x + y*m_overlayWidth] = v == key ? 0 : 0xff;
                }
            }
            if (m_csp == EMF_I420) {
                ippiBGRToYCbCr420_8u_C3P3R(planes[0], strides[0], dst, steps, roi);
            }
            break;
        }
        case EMF_RGB16: {
            uint16_t key = ((m_colorkey[0] >> 3) << 11) + ((m_colorkey[1] >> 2) << 5) + ((m_colorkey[2] >> 3) << 0);
            for (int y = 0; y < m_overlayHeight; ++y) {
                uint16_t* p = (uint16_t*)(planes[0] + strides[0] * y);
                for (int x = 0; x < m_overlayWidth; ++x)
                    m_mask.get()[x + y*m_overlayWidth] = p[x] == key ? 0 : 1;
            }
            if (m_csp == EMF_I420) {
                vnxippiBGR565ToYCbCr420_16u8u_C3P3R((uint16_t*)planes[0], strides[0], dst, steps, 
                { roi.width,roi.height });
            }
            break;
        }
        default:
            throw std::logic_error("unsupported format, and this should not happen (should have been thrown earlier)");
        }
        { // prepare subsampled mask for U and V planes
            IppiSize sz = { m_overlayWidth, m_overlayHeight };
            IppiRect roi = { 0,0,m_overlayWidth, m_overlayHeight };
            IppiSize sz2 = { m_overlayWidth / 2, m_overlayHeight / 2 };
            vnxippiResize_8u_C1R(m_mask.get(), { sz.width, sz.height }, m_overlayWidth, 
            {roi.x,roi.y,roi.width, roi.height}, m_mask2.get(), m_overlayWidth / 2, { sz2.width, sz2.height },
                0.5, 0.5, IPPI_INTER_NN);
        }
    }
};

#pragma pack(push, 1)
struct vnxBITMAPFILEHEADER {
    uint16_t  bfType;
    uint32_t bfSize;
    uint16_t  bfReserved1;
    uint16_t  bfReserved2;
    uint32_t bfOffBits;
};
struct vnxBITMAPINFOHEADER {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack(pop)


class CRgbBitmap : public VnxVideo::IRawSample {
public:
    CRgbBitmap(const uint8_t* buffer, int buffer_size) {
        if (buffer_size < sizeof(vnxBITMAPINFOHEADER))
            throw std::runtime_error("buffer size less than BITMAPINFOHEADER");
        const vnxBITMAPFILEHEADER* bfh = reinterpret_cast<const vnxBITMAPFILEHEADER*>(buffer);
        if (bfh->bfType != 'MB') // 'BM' little endian
            throw std::runtime_error("type not recognized");
        if(bfh->bfSize!=buffer_size)
            throw std::runtime_error("buffer size values mismatching");
        const uint8_t* pdata = buffer + bfh->bfOffBits;
        const vnxBITMAPINFOHEADER* bih = reinterpret_cast<const vnxBITMAPINFOHEADER*>(buffer + sizeof(vnxBITMAPFILEHEADER));
        if(bih->biSize<sizeof(vnxBITMAPINFOHEADER))
            throw std::runtime_error("biSize field is less than BITMAPINFOHEADER");
        if(bih->biPlanes!=1)
            throw std::runtime_error("unsupported number of planes");
        if (bih->biBitCount != 16 && bih->biBitCount != 24 && bih->biBitCount != 32)
            throw std::runtime_error("unsupported bpp");
        if(bih->biCompression != 0) // BI_RGB
            throw std::runtime_error("unsupported compression");

        // https://docs.microsoft.com/en-us/windows/desktop/api/wingdi/ns-wingdi-tagbitmapinfoheader#calculating-surface-stride
        m_stride = ((((bih->biWidth * bih->biBitCount) + 31) & ~31) >> 3);
        m_bpp = bih->biBitCount;
        m_width = bih->biWidth;
        m_height = abs(bih->biHeight);
        m_size = m_stride*m_height;
        m_data.reset((uint8_t*)malloc(m_size), free);
        if (nullptr == m_data.get())
            throw std::runtime_error("CRgbBitmap ctor: Cannot allocate data buffer");
        if (bih->biHeight < 0)
            memcpy(m_data.get(), pdata, m_size);
        else {
            for (int y = 0; y < m_height; ++y)
                memcpy(m_data.get() + m_size - (y+1)*m_stride, pdata + y*m_stride, m_stride);
        }
    }
    IRawSample* Dup() {
        return new CRgbBitmap(*this);
    }
    void GetData(uint8_t* &data, int& size) {
        data = m_data.get();
        size = m_size;
    }
    void GetData(int* strides, uint8_t** planes) {
        strides[0] = m_stride;
        planes[0] = m_data.get();
    }
    void GetFormat(EColorspace &csp, int &width, int &height) {
        switch (m_bpp) {
        case 16: csp = EMF_RGB16; break;
        case 24: csp = EMF_RGB24; break;
        case 32: csp = EMF_RGB32; break;
        default: throw std::logic_error("unsupported bpp in already created bitmap?");
        }
        width = m_width;
        height = m_height;
    }
private:
    int m_bpp;
    int m_width;
    int m_height;
    int m_stride;
    int m_size;
    std::shared_ptr<uint8_t> m_data;
};

namespace VnxVideo {
    VNXVIDEO_DECLSPEC IComposer* CreateComposer(uint8_t colorkey[4], int left, int top) {
        return new CComposer(colorkey, left, top);
    }

    VNXVIDEO_DECLSPEC IRawSample* ParseBMP(const uint8_t* buffer, int buffer_size) {
        return new CRgbBitmap(buffer, buffer_size);
    }
    VNXVIDEO_DECLSPEC IRawSample* LoadBMP(const char* filename) {
        std::ifstream ifs(filename, std::ios_base::binary);
        ifs.seekg(0, std::ios_base::end);
        size_t sz=(size_t)ifs.tellg();
        std::shared_ptr<uint8_t> data((uint8_t*)malloc(sz), free);
        ifs.seekg(0, std::ios_base::beg);
        ifs.read((char*)data.get(), sz);
        return ParseBMP(data.get(), (int)sz);
    }
}
