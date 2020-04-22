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
string response_library(beltpp::detail::session_special_data& ssd,
                        beltpp::packet const& pc)
{
    if (pc.type() == LibraryResponse::rtt)
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
inline string response_index_list(beltpp::detail::session_special_data& ssd,
                                  beltpp::packet const& pc)
{
    if (pc.type() == IndexListResponse::rtt)
        return beltpp::http::http_response(ssd, pc.to_string());
    else
        return beltpp::http::http_internal_server_error(ssd, pc.to_string());
}
inline string response_index(beltpp::detail::session_special_data& ssd,
                             beltpp::packet const& pc)
{
    if (pc.type() == LibraryIndex::rtt)
        return beltpp::http::http_response(ssd, pc.to_string());
    else
        return beltpp::http::http_internal_server_error(ssd, pc.to_string());
}
inline
string response_authorization(beltpp::detail::session_special_data& ssd,
                              beltpp::packet const& pc)
{
    if (pc.type() == SignedStorageAuthorization::rtt)
        return beltpp::http::http_response(ssd, meshpp::to_base64(pc.to_string(), false));
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
                 1 == ss.resource.path.size() &&
                 ss.resource.path.front() == "index")
        {
            ssd.session_specal_handler = &response_index_list;
            auto p = ::beltpp::new_void_unique_ptr<IndexListGet>();
            //IndexListGet& ref = *reinterpret_cast<IndexListGet*>(p.get());

            return ::beltpp::detail::pmsg_all(IndexListGet::rtt,
                                              std::move(p),
                                              &IndexListGet::pvoid_saver);
        }
        else if (ss.type == beltpp::http::detail::scan_status::get &&
                 2 == ss.resource.path.size() &&
                 ss.resource.path.front() == "index")
        {
            ssd.session_specal_handler = &response_index;
            auto p = ::beltpp::new_void_unique_ptr<IndexGet>();
            IndexGet& ref = *reinterpret_cast<IndexGet*>(p.get());
            ref.sha256sum = ss.resource.path.back();

            return ::beltpp::detail::pmsg_all(IndexGet::rtt,
                                              std::move(p),
                                              &IndexGet::pvoid_saver);
        }
        else if (ss.type == beltpp::http::detail::scan_status::del &&
                 2 == ss.resource.path.size() &&
                 ss.resource.path.front() == "index")
        {
            ssd.session_specal_handler = &response_index;
            auto p = ::beltpp::new_void_unique_ptr<IndexDelete>();
            IndexDelete& ref = *reinterpret_cast<IndexDelete*>(p.get());
            ref.sha256sum = ss.resource.path.back();

            return ::beltpp::detail::pmsg_all(IndexDelete::rtt,
                                              std::move(p),
                                              &IndexDelete::pvoid_saver);
        }
        else if (ss.type == beltpp::http::detail::scan_status::get &&
                 false == ss.resource.path.empty() &&
                 ss.resource.path.front() == "library")
        {
            ssd.session_specal_handler = &response_library;
            auto p = ::beltpp::new_void_unique_ptr<LibraryGet>();
            LibraryGet& ref = *reinterpret_cast<LibraryGet*>(p.get());
            for (size_t index = 1; index != ss.resource.path.size(); ++index)
                ref.path.push_back(ss.resource.path[index]);

            return ::beltpp::detail::pmsg_all(LibraryGet::rtt,
                                              std::move(p),
                                              &LibraryGet::pvoid_saver);
        }
        else if (ss.type == beltpp::http::detail::scan_status::put &&
                 false == ss.resource.path.empty() &&
                 false == posted.empty() &&
                 ss.resource.path.front() == "library")
        {
            ssd.session_specal_handler = &response_library;
            auto p = ::beltpp::new_void_unique_ptr<LibraryPut>();
            LibraryPut& ref = *reinterpret_cast<LibraryPut*>(p.get());
            for (size_t index = 1; index != ss.resource.path.size(); ++index)
                ref.path.push_back(ss.resource.path[index]);


            AdminModel::detail::loader(ref.type_descriptions, posted, nullptr);

            return ::beltpp::detail::pmsg_all(LibraryPut::rtt,
                                              std::move(p),
                                              &LibraryPut::pvoid_saver);
        }
        else if (ss.type == beltpp::http::detail::scan_status::del &&
                 false == ss.resource.path.empty() &&
                 ss.resource.path.front() == "library")
        {
            ssd.session_specal_handler = &response_library;
            auto p = ::beltpp::new_void_unique_ptr<LibraryDelete>();
            LibraryDelete& ref = *reinterpret_cast<LibraryDelete*>(p.get());
            for (size_t index = 1; index != ss.resource.path.size(); ++index)
                ref.path.push_back(ss.resource.path[index]);

            return ::beltpp::detail::pmsg_all(LibraryDelete::rtt,
                                              std::move(p),
                                              &LibraryDelete::pvoid_saver);
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
                 ss.resource.path.size() == 2 &&
                 ss.resource.path.front() == "log")
        {
            ssd.session_specal_handler = &response_log;
            auto p = ::beltpp::new_void_unique_ptr<LogDelete>();
            LogDelete& ref = *reinterpret_cast<LogDelete*>(p.get());

            size_t pos;
            ref.count = beltpp::stoui64(ss.resource.path.back(), pos);

            return ::beltpp::detail::pmsg_all(LogDelete::rtt,
                                              std::move(p),
                                              &LogDelete::pvoid_saver);
        }
        else if (ss.type == beltpp::http::detail::scan_status::get &&
                 ss.resource.path.size() == 1 &&
                 ss.resource.path.front() == "authorization")
        {
            ssd.session_specal_handler = &response_authorization;
            auto p = ::beltpp::new_void_unique_ptr<StorageAuthorization>();
            StorageAuthorization& ref = *reinterpret_cast<StorageAuthorization*>(p.get());

            size_t pos = 0;
            ref.seconds = beltpp::stoui64(ss.resource.arguments["seconds"], pos);
            ref.file_uri = ss.resource.arguments["file"];
            ref.session_id = ss.resource.arguments["session_id"];

            return ::beltpp::detail::pmsg_all(StorageAuthorization::rtt,
                                              std::move(p),
                                              &StorageAuthorization::pvoid_saver);
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
