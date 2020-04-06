#pragma once

#include "admin_model.hpp"
#include "common.hpp"

#include <belt.pp/parser.hpp>
#include <belt.pp/http.hpp>

#include <mesh.pp/cryptoutility.hpp>

#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <chrono>

using std::string;
using std::vector;
using std::pair;
using std::unordered_map;

namespace cloudy
{
namespace http
{
using namespace AdminModel;

inline
string response(beltpp::detail::session_special_data& ssd,
                beltpp::packet const& pc)
{
    return beltpp::http::http_response(ssd, pc.to_string());
}
inline
string response_tree(beltpp::detail::session_special_data& ssd,
                     beltpp::packet const& pc)
{
    if (pc.type() == CatalogueTreeResponse::rtt)
        return beltpp::http::http_response(ssd, pc.to_string());
    else
        return beltpp::http::http_internal_server_error(ssd, pc.to_string());
}
inline
string response_log(beltpp::detail::session_special_data& ssd,
                    beltpp::packet const& pc)
{
    if (pc.type() == Log::rtt)
        return beltpp::http::http_response(ssd, pc.to_string());
    else
        return beltpp::http::http_internal_server_error(ssd, pc.to_string());
}

template <beltpp::detail::pmsg_all (*fallback_message_list_load)(
        std::string::const_iterator&,
        std::string::const_iterator const&,
        beltpp::detail::session_special_data&,
        void*)>
beltpp::detail::pmsg_all message_list_load(
        std::string::const_iterator& iter_scan_begin,
        std::string::const_iterator const& iter_scan_end,
        beltpp::detail::session_special_data& ssd,
        void* putl)
{
    auto it_fallback = iter_scan_begin;

    ssd.session_specal_handler = nullptr;
    ssd.autoreply.clear();

    auto protocol_error = [&iter_scan_begin, &iter_scan_end, &ssd]()
    {
        ssd.session_specal_handler = nullptr;
        ssd.autoreply.clear();
        iter_scan_begin = iter_scan_end;
        return ::beltpp::detail::pmsg_all(size_t(-2),
                                          ::beltpp::void_unique_nullptr(),
                                          nullptr);
    };

    string posted;
    auto result = beltpp::http::protocol(ssd,
                                         iter_scan_begin,
                                         iter_scan_end,
                                         it_fallback,
                                         cloudy::http_enough_length,
                                         cloudy::http_header_max_size,
                                         cloudy::http_content_max_size,
                                         posted);
    auto code = result.first;
    auto& ss = result.second;

    if (code == beltpp::e_three_state_result::error &&
        ss.status == beltpp::http::detail::scan_status::clean)
    {
        return fallback_message_list_load(iter_scan_begin, iter_scan_end, ssd, putl);
    }
    else if (code == beltpp::e_three_state_result::error)
        return protocol_error();
    else if (code == beltpp::e_three_state_result::attempt)
    {
        iter_scan_begin = it_fallback;
        return ::beltpp::detail::pmsg_all(size_t(-1),
                                          ::beltpp::void_unique_nullptr(),
                                          nullptr);
    }
    else// if (code == beltpp::e_three_state_result::success)
    {
        ssd.session_specal_handler = &response;
        ssd.autoreply.clear();

        if (ss.type == beltpp::http::detail::scan_status::post &&
                 ss.resource.path.size() == 1 &&
                 ss.resource.path.front() == "api")
        {
            std::string::const_iterator iter_scan_begin_temp = posted.cbegin();
            std::string::const_iterator const iter_scan_end_temp = posted.cend();

            auto parser_unrecognized_limit_backup = ssd.parser_unrecognized_limit;
            ssd.parser_unrecognized_limit = 0;

            auto pmsgall = fallback_message_list_load(iter_scan_begin_temp, iter_scan_end_temp, ssd, putl);

            ssd.parser_unrecognized_limit = parser_unrecognized_limit_backup;

            if (pmsgall.pmsg)
                return pmsgall;

            return protocol_error();
        }
        else if (ss.type == beltpp::http::detail::scan_status::get &&
                 false == ss.resource.path.empty() &&
                 ss.resource.path.front() == "tree")
        {
            ssd.session_specal_handler = &response_tree;
            auto p = ::beltpp::new_void_unique_ptr<CatalogueTreeGet>();
            CatalogueTreeGet& ref = *reinterpret_cast<CatalogueTreeGet*>(p.get());
            for (size_t index = 1; index != ss.resource.path.size(); ++index)
                ref.path.push_back(ss.resource.path[index]);

            return ::beltpp::detail::pmsg_all(CatalogueTreeGet::rtt,
                                              std::move(p),
                                              &CatalogueTreeGet::pvoid_saver);
        }
        else if (ss.type == beltpp::http::detail::scan_status::put &&
                 false == ss.resource.path.empty() &&
                 ss.resource.path.front() == "tree")
        {
            ssd.session_specal_handler = &response_tree;
            auto p = ::beltpp::new_void_unique_ptr<CatalogueTreePut>();
            CatalogueTreePut& ref = *reinterpret_cast<CatalogueTreePut*>(p.get());
            for (size_t index = 1; index != ss.resource.path.size(); ++index)
                ref.path.push_back(ss.resource.path[index]);

            return ::beltpp::detail::pmsg_all(CatalogueTreePut::rtt,
                                              std::move(p),
                                              &CatalogueTreePut::pvoid_saver);
        }
        else if (ss.type == beltpp::http::detail::scan_status::get &&
                 ss.resource.path.size() == 1 &&
                 ss.resource.path.front() == "log")
        {
            ssd.session_specal_handler = &response_log;
            auto p = ::beltpp::new_void_unique_ptr<LogGet>();
            //LogGet& ref = *reinterpret_cast<LogGet*>(p.get());

            return ::beltpp::detail::pmsg_all(LogGet::rtt,
                                              std::move(p),
                                              &LogGet::pvoid_saver);
        }
        else if (ss.type == beltpp::http::detail::scan_status::del &&
                 ss.resource.path.size() == 1 &&
                 ss.resource.path.front() == "log")
        {
            ssd.session_specal_handler = &response_log;
            auto p = ::beltpp::new_void_unique_ptr<LogDelete>();
            //LogDelete& ref = *reinterpret_cast<LogDelete*>(p.get());

            return ::beltpp::detail::pmsg_all(LogDelete::rtt,
                                              std::move(p),
                                              &LogDelete::pvoid_saver);
        }
        else if (ss.type == beltpp::http::detail::scan_status::get &&
                 ss.resource.path.size() == 1 &&
                 ss.resource.path.front() == "protocol")
        {
            ssd.session_specal_handler = nullptr;

            ssd.autoreply = beltpp::http::http_response(ssd, AdminModel::detail::storage_json_schema());

            return ::beltpp::detail::pmsg_all(size_t(-1),
                                              ::beltpp::void_unique_nullptr(),
                                              nullptr);
        }
        else
        {
            ssd.session_specal_handler = nullptr;

            string message("noo! \r\n");

            for (auto const& dir : ss.resource.path)
                message += "/" + dir;
            message += "\r\n";
            for (auto const& arg : ss.resource.arguments)
                message += (arg.first + ": " + arg.second + "\r\n");
            message += "\r\n";
            message += "\r\n";
            for (auto const& prop : ss.resource.properties)
                message += (prop.first + ": " + prop.second + "\r\n");
            message += "that's an error! \r\n";
            message += "here's the protocol, by the way \r\n";

            ssd.autoreply = beltpp::http::http_not_found(ssd,
                                                         message +
                                                         AdminModel::detail::storage_json_schema());

            return ::beltpp::detail::pmsg_all(size_t(-1),
                                              ::beltpp::void_unique_nullptr(),
                                              nullptr);
        }
    }
}
}
}
