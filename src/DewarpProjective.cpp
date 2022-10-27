#ifndef __aarch64__
#include <boost/lexical_cast.hpp>
#include <ippi.h>
#include <ipp.h>

#include "json.hpp"
#include "jget.h"

using json = nlohmann::json;

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

#include "RawSample.h"

class CWarpTransform_8u {
public:
    CWarpTransform_8u(IppiSize size, const double coeffs[3][3], uint8_t border)
    : m_size(size) {
        IppStatus s;
        Ipp64f borderValue = border;

        int specSize;
        int bufSize;
        int initSize;
        s=ippiWarpPerspectiveGetSize(m_size, ippRectInfinite, m_size, ipp8u, coeffs, ippCubic, ippWarpForward, ippBorderConst,
            &specSize, &initSize);
        if (ippStsNoErr != s)
            throw std::runtime_error("ippiWarpPerspectiveGetSize failed, status = " + boost::lexical_cast<std::string>(s));

        m_spec.reset(reinterpret_cast<IppiWarpSpec*>(ippMalloc(specSize)), ippFree);
        std::shared_ptr<Ipp8u> initBuf(reinterpret_cast<Ipp8u*>(ippMalloc(initSize)), ippFree);

        s = ippiWarpPerspectiveCubicInit(m_size, ippRectInfinite, m_size, ipp8u, coeffs, ippWarpForward, 1, 0, 0,
            ippBorderConst, &borderValue, 0, m_spec.get(), initBuf.get());
        if (ippStsNoErr != s)
            throw std::runtime_error("ippiWarpPerspectiveCubicInit failed, status = " + boost::lexical_cast<std::string>(s));

        s = ippiWarpGetBufferSize(m_spec.get(), m_size, &bufSize);
        if (ippStsNoErr != s)
            throw std::runtime_error("ippiWarpGetBufferSize failed, status = " + boost::lexical_cast<std::string>(s));
        m_buf.reset(reinterpret_cast<Ipp8u*>(ippMalloc(bufSize)), ippFree);
    }
    void Apply(const Ipp8u* src, int srcStep, Ipp8u* dst, int dstStep) {
        IppStatus s=ippiWarpPerspectiveCubic_8u_C1R(src, srcStep, dst, dstStep, { 0,0 }, m_size, m_spec.get(), m_buf.get());
        if (ippStsNoErr != s)
            throw std::runtime_error("ippiWarpPerspectiveCubic_8u_C1R failed, status = " + boost::lexical_cast<std::string>(s));
    }
private:
    IppiSize m_size;
    std::shared_ptr<IppiWarpSpec> m_spec;
    std::shared_ptr<Ipp8u> m_buf;
};

class CDewarpProjective : public VnxVideo::IRawTransform {
public:
    CDewarpProjective(const std::vector<double>& tform)
        : m_width(0)
        , m_height(0) {

        if(tform.size()!=9)
            throw std::logic_error("Nine transform coefficients, row-wise, are expected");
        // row-wise means
        // 1 2 3
        // 4 5 6
        // 7 8 9
        // because this way it can be easily formatted in JSON as a single array

        for (int k = 0; k < 9; ++k) {
            m_warpCoeffs[k % 3][k / 3] = tform[k];
        }
    }

    static void scaleGeoTransformMatrix(int width, int height, const double from[3][3], double to[3][3]) {
        double diagF[3] = { double(width), double(height), 1 };
        double diagB[3] = { 1.0 / width, 1.0 / height, 1 };

        for (int k = 0; k < 3; ++k)
            for (int j = 0; j < 3; ++j) {
                to[j][k] = diagB[k] * from[j][k] * diagF[j];
            }
    }

    void SetFormat(EColorspace csp, int width, int height) {
        if(csp!=EMF_I420)
            throw std::logic_error("Sample format other than I420 is not supported");
        m_width = width;
        m_height = height;

        scaleGeoTransformMatrix(width, height, m_warpCoeffs, m_warpCoeffsY);
        scaleGeoTransformMatrix(width/2, height/2, m_warpCoeffs, m_warpCoeffsUV);

        m_warpY.reset();
        m_warpUV.reset();

        m_onFormat(csp, width, height);
    }
    void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        if (0 == m_width || 0 == m_height)
            return;

        int stridesIn[4];
        uint8_t* planesIn[4];
        sample->GetData(stridesIn, planesIn);

        CRawSample out(EMF_I420, m_width, m_height);
        int stridesOut[4];
        uint8_t* planesOut[4];
        out.GetData(stridesOut, planesOut);

        const int sizeDivisor[3] = { 1, 2, 2 };
        double(*warpCoeffs[3])[3][3]  = { &m_warpCoeffsY, &m_warpCoeffsUV, &m_warpCoeffsUV };

        for (int k = 0; k < 3; ++k) {
            std::unique_ptr<CWarpTransform_8u>& warp = (k == 0) ? m_warpY : m_warpUV;

            IppiSize size = { m_width / sizeDivisor[k], m_height / sizeDivisor[k] };

            const uint8_t border = (k == 0 ? 0 : 0x80);
            if (warp.get() == nullptr) {
                warp.reset(new CWarpTransform_8u(size, *warpCoeffs[k], border));
            }

            warp->Apply(planesIn[k], stridesIn[k], planesOut[k], stridesOut[k]);
        }
        m_onFrame(&out, timestamp);
    }
    void Flush() {
    }
    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame) {
        m_onFormat = onFormat;
        m_onFrame = onFrame;
    }
private:
    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;
    int m_width;
    int m_height;

    double m_warpCoeffs[3][3];
    double m_warpCoeffsY[3][3];
    double m_warpCoeffsUV[3][3];
    std::unique_ptr<CWarpTransform_8u> m_warpY;
    std::unique_ptr<CWarpTransform_8u> m_warpUV;
};

namespace VnxVideo {
    IRawTransform* CreateRawTransform_DewarpProjective(const std::vector<double>& tform) {
        return new CDewarpProjective(tform);
    }
    IRawTransform* CreateRawTransform(const nlohmann::json& config) {
        std::string type(jget<std::string>(config, "type"));
        if (type == "projective") {
            std::vector<double> tform(jget<std::vector<double> >(config, "matrix"));
            if (tform.size() != 9) {
                throw std::runtime_error("incorrect number of transform coefficiens, 3x3 matrix unwarped in a single array, row-wise, expected");
            }
            return CreateAsyncTransform(
                PRawTransform(VnxVideo::CreateRawTransform_DewarpProjective(tform)));
        }
        else
            throw std::runtime_error("unknown raw video transform type: " + type);
    }
}
#endif // aarch64
