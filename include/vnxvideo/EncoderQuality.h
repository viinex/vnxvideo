#pragma once

#include <string>
#include "json.hpp"

namespace VnxVideo {

    typedef enum {
        eqp_best_quality = 18,
        eqp_fine_quality = 21,
        eqp_good_quality = 24,
        eqp_normal       = 27,
        eqp_small_size   = 32,
        eqp_tiny_size    = 38,
        eqp_best_size    = 45,
    } qp_quality_t;
    
    struct TEqQp { qp_quality_t quality; };
    struct TEqRc { int rate_target; int rate_max; }; // rate in bits/s

    // Unfortunately we don't have <variant> in C++11.
    // Implement adt as a tagged union with POD types inside.
    struct TEncoderQuality {
        enum { eq_qp, eq_rc } tag;
        union {
            TEqQp qp;
            TEqRc rc;
        };
        TEncoderQuality() : tag(eq_qp) {
            qp.quality = eqp_normal;
        }
    };

    inline void from_json(const nlohmann::json& j, TEncoderQuality& p) {
        if (j.is_string()) {
            p.tag = TEncoderQuality::eq_qp;
            std::string q = j.get<std::string>();
            if (q == "best_quality") {
                p.qp.quality = eqp_best_quality;
            }
            else if (q == "fine_quality") {
                p.qp.quality = eqp_fine_quality;
            }
            else if (q == "good_quality") {
                p.qp.quality = eqp_good_quality;
            }
            else if (q == "normal") {
                p.qp.quality =eqp_normal;
            }
            else if (q == "small_size") {
                p.qp.quality = eqp_small_size;
            }
            else if (q == "tiny_size") {
                p.qp.quality = eqp_tiny_size;
            }
            else if (q == "best_size") {
                p.qp.quality = eqp_best_size;
            }
            else {
                throw std::runtime_error("`quality' enum literal value not recognized");
            }
        } 
        else if (j.is_array() && j.size() == 2) {
            p.tag = TEncoderQuality::eq_rc;
            std::tie(p.rc.rate_target, p.rc.rate_max) = j.get<std::pair<int, int>>();
        } 
        else {
            throw std::runtime_error("Invalid JSON type for TEncoderQuality");
        }
    }
}
