#pragma once

#include <functional>
#include "json.hpp"
#include "jget.h"

namespace VnxVideo {

    // Unfortunately we don't have <variant> in C++11.

    struct CS_Id { std::string value; };
    struct CS_GlobalNumber { long long value; };
    struct CS_Name { std::string value; };
    typedef enum { ESS_Main, ESS_Sub } EStreamSelector;

    class CVmsChannelSelector {
        enum { CST_Id, CST_GlobalNumber, CST_Name } tag;
        union {
            CS_Id m_id;
            CS_GlobalNumber m_globalNumber;
            CS_Name m_name;
        };
        EStreamSelector m_StreamSelector;
    public:
        ~CVmsChannelSelector() {
            switch (tag) {
            case CST_Id: m_id.~CS_Id(); break;
            case CST_GlobalNumber: m_globalNumber.~CS_GlobalNumber(); break;
            case CST_Name: m_name.~CS_Name(); break;
            }
        }
        CVmsChannelSelector(const CVmsChannelSelector& sel) {
            tag = sel.tag;
            switch (sel.tag) {
            case CST_Id: new (&m_id) CS_Id(sel.m_id); break;
            case CST_GlobalNumber: new (&m_globalNumber) CS_GlobalNumber(sel.m_globalNumber); break;
            case CST_Name: new (&m_name) CS_Name(sel.m_name); break;
            default: throw std::logic_error("unhandled case");
            }
            m_StreamSelector = sel.m_StreamSelector;
        }
        CVmsChannelSelector(const nlohmann::json& j) {
            long long l;
            std::string s;
            if (mjget(j, "stream", s)) {
                if (s == "main") {
                    m_StreamSelector = ESS_Main;
                }
                else if (s == "sub") {
                    m_StreamSelector = ESS_Sub;
                }
                else {
                    throw std::runtime_error("unrecognized value for stream type tag: " + s);
                }
            }
            else
            {
                m_StreamSelector = ESS_Main;
            }
            if (mjget(j, "id", s)) {
                tag = CST_Id;
                new (&m_id) CS_Id({ s });
            }
            else if (mjget(j, "global_number", l)) {
                tag = CST_GlobalNumber;
                new(&m_globalNumber) CS_GlobalNumber({ l });
            }
            else if (mjget(j, "name", s)) {
                tag = CST_Name;
                new(&m_name) CS_Name({ s });
            }
            else if (mjget(j, s)) {
                tag = CST_Id;
                new (&m_id) CS_Id({ s });
            }
            else if (mjget(j, l)) {
                tag = CST_GlobalNumber;
                new (&m_globalNumber) CS_GlobalNumber({ l });
            }
            else {
                throw std::runtime_error("could not parse stream selector");
            }
        }
        template<typename a> a Visit(std::function<a(CS_Id)> onId,
            std::function<a(CS_GlobalNumber)> onGlobalNumber,
            std::function<a(CS_Name)> onName) const {
            switch (tag) {
            case CST_Id: return onId(m_id);
            case CST_GlobalNumber: return onGlobalNumber(m_globalNumber);
            case CST_Name: return onName(m_name);
            default: throw std::logic_error("unhandled case");
            }
        }
        EStreamSelector Stream() const {
            return m_StreamSelector;
        }
    };

}
