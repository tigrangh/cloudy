#pragma once

#include "storage_model.hpp"
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
inline
string response(beltpp::detail::session_special_data& ssd,
                beltpp::packet const& pc)
{
    return beltpp::http::http_response(ssd, pc.to_string());
}
inline
string file_response(beltpp::detail::session_special_data& ssd,
                     beltpp::packet const& pc)
{
    ssd.session_specal_handler = nullptr;

    if (pc.type() == StorageModel::StorageFile::rtt)
    {
        string str_result;
        StorageModel::StorageFile const* pFile = nullptr;
        pc.get(pFile);

        str_result += "HTTP/1.1 200 OK\r\n";
        if (false == pFile->mime_type.empty())
            str_result += "Content-Type: " + pFile->mime_type + "\r\n";
        str_result += "Access-Control-Allow-Origin: *\r\n";
        str_result += "Content-Length: ";
        str_result += std::to_string(pFile->data.length());
        str_result += "\r\n\r\n";
        str_result += pFile->data;

        return str_result;
    }
    else
    {
        string str_result;
        string message;
        if (pc.type() == StorageModel::UriError::rtt)
        {
            StorageModel::UriError const* pError = nullptr;
            pc.get(pError);
            if (pError->uri_problem_type == StorageModel::UriProblemType::missing)
                message = "404 Not Found\r\n"
                          "requested file: " + pError->uri;
        }

        if (message.empty())
            message = "internal error";

        str_result += "HTTP/1.1 404 Not Found\r\n";
        str_result += "Content-Type: text/plain\r\n";
        str_result += "Access-Control-Allow-Origin: *\r\n";
        str_result += "Content-Length: " + std::to_string(message.length()) + "\r\n\r\n";
        str_result += message;
        return str_result;
    }
}
inline
string file_range_response(beltpp::detail::session_special_data& ssd,
                           beltpp::packet const& pc)
{
    ssd.session_specal_handler = nullptr;

    if (pc.type() == StorageModel::StorageFileRange::rtt)
    {
        string str_result;
        StorageModel::StorageFileRange const* pFile = nullptr;
        pc.get(pFile);

        str_result += "HTTP/1.1 206 OK\r\n";
        if (false == pFile->mime_type.empty())
            str_result += "Content-Type: " + pFile->mime_type + "\r\n";
        str_result += "Access-Control-Allow-Origin: *\r\n";
        str_result += "Content-Range: bytes " + std::to_string(pFile->start) + "-" +
                      std::to_string(pFile->start + pFile->count - 1) + "/" + std::to_string(pFile->full_size) + "\r\n";
        str_result += "Content-Length: ";
        str_result += std::to_string(pFile->data.length());
        str_result += "\r\n\r\n";
        str_result += pFile->data;

        return str_result;
    }
    else
        return file_response(ssd, pc);
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

        if (ss.type == beltpp::http::detail::scan_status::get &&
            ss.resource.path.size() == 1 &&
            ss.resource.path.front() == "storage")
        {
            bool range_request = false;
            uint64_t range_start, range_count;

            auto it_range = ss.resource.properties.find("Range");
            if (it_range != ss.resource.properties.end())
            {
                string property = it_range->second;

                vector<string> parts = beltpp::http::request::split(property, "=", false, 2, true);
                if (parts.size() == 2 &&
                    parts.front() == "bytes")
                {
                    parts = beltpp::http::request::split(parts.back(), "-", false, 2, true);
                    string str_start, str_end;
                    if (parts.size() == 2)
                    {
                        str_start = parts.front();
                        str_end = parts.back();
                    }
                    else if (parts.size() == 1)
                        str_start = parts.front();

                    if (parts.size() == 1 || parts.size() == 2)
                    {
                        size_t pos;
                        uint64_t start = beltpp::stoui64(str_start, pos);
                        if (pos == str_start.size())
                        {
                            auto temp_end = beltpp::stoui64(str_end, pos);
                            if (str_end.empty())
                            {
                                range_start = start;
                                range_count = 0;
                                range_request = true;
                            }
                            else if (pos == str_end.size() &&
                                     temp_end >= start)
                            {
                                range_start = start;
                                range_count = temp_end - start + 1;
                                range_request = true;
                            }
                        }
                    }
                }
            }

            if (range_request)
            {
                ssd.session_specal_handler = &file_range_response;

                auto p = ::beltpp::new_void_unique_ptr<StorageModel::StorageFileRangeRequest>();
                StorageModel::StorageFileRangeRequest& ref = *reinterpret_cast<StorageModel::StorageFileRangeRequest*>(p.get());
                ref.uri = ss.resource.arguments["file"];
                ref.storage_order_token = ss.resource.arguments["storage_order_token"];
                ref.start = range_start;
                ref.count = range_count;
                return ::beltpp::detail::pmsg_all(StorageModel::StorageFileRangeRequest::rtt,
                                                  std::move(p),
                                                  &StorageModel::StorageFileRangeRequest::pvoid_saver);
            }
            else
            {
                ssd.session_specal_handler = &file_response;

                auto p = ::beltpp::new_void_unique_ptr<StorageModel::StorageFileRequest>();
                StorageModel::StorageFileRequest& ref = *reinterpret_cast<StorageModel::StorageFileRequest*>(p.get());
                ref.uri = ss.resource.arguments["file"];
                ref.storage_order_token = ss.resource.arguments["storage_order_token"];
                return ::beltpp::detail::pmsg_all(StorageModel::StorageFileRequest::rtt,
                                                  std::move(p),
                                                  &StorageModel::StorageFileRequest::pvoid_saver);
            }
        }
        else if (ss.type == beltpp::http::detail::scan_status::post &&
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
                 ss.resource.path.size() == 1 &&
                 ss.resource.path.front() == "protocol")
        {
            ssd.session_specal_handler = nullptr;

            ssd.autoreply = beltpp::http::http_response(ssd, StorageModel::detail::storage_json_schema());

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
                                                         StorageModel::detail::storage_json_schema());

            return ::beltpp::detail::pmsg_all(size_t(-1),
                                              ::beltpp::void_unique_nullptr(),
                                              nullptr);
        }
    }
}
}
}
