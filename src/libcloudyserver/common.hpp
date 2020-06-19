#pragma once

#include "global.hpp"

#include <belt.pp/isocket.hpp>
#include <belt.pp/packet.hpp>

#include <boost/filesystem/path.hpp>

#include <string>
#include <chrono>
#include <utility>
#include <vector>

namespace cloudy
{

size_t const http_enough_length = 10 * 1024;
size_t const http_header_max_size = 64 * 1024;
size_t const http_content_max_size = 10 * 1024 * 1024;

size_t const storage_order_sign_instant_precision = 60;

std::string const worker_peerid = "worker";
std::string const admin_peerid = "admin";
std::string const storage_peerid = "storage";

std::chrono::steady_clock::duration const event_timer_period = std::chrono::seconds(15);

beltpp::void_unique_ptr get_admin_putl();
beltpp::void_unique_ptr get_storage_putl();
beltpp::void_unique_ptr get_internal_putl();

bool verify_storage_order(std::string const& storage_order_token,
                          std::string& channel_address,
                          std::string& file_uri,
                          std::string& session_id,
                          uint64_t& seconds,
                          std::chrono::system_clock::time_point& tp);


std::pair<std::string, std::string> join_path(std::vector<std::string> const& path);
std::pair<boost::filesystem::path, std::string> check_path(std::vector<std::string> const& path);

namespace detail
{

class wait_result_item
{
public:
    enum event_type {nothing, event, timer, on_demand};
    event_type et = nothing;
    beltpp::socket::peer_id peerid;
    beltpp::packet packet;

    static wait_result_item event_result(beltpp::socket::peer_id const& peerid,
                                         beltpp::packet&& packet)
    {
        wait_result_item res;
        res.et = event;
        res.peerid = peerid;
        res.packet = std::move(packet);

        return res;
    }

    static wait_result_item on_demand_result(beltpp::socket::peer_id const& peerid,
                                             beltpp::packet&& packet)
    {
        wait_result_item res;
        res.et = on_demand;
        res.peerid = peerid;
        res.packet = std::move(packet);

        return res;
    }

    static wait_result_item timer_result()
    {
        wait_result_item res;
        res.et = timer;

        return res;
    }

    static wait_result_item empty_result()
    {
        wait_result_item res;
        res.et = nothing;

        return res;
    }
};

class wait_result
{
public:
    beltpp::event_handler::wait_result m_wait_result = beltpp::event_handler::wait_result::nothing;
    std::pair<beltpp::socket::peer_id, beltpp::socket::packets> event_packets;
    std::pair<beltpp::socket::peer_id, beltpp::socket::packets> on_demand_packets;
};

wait_result_item wait_and_receive_one(wait_result& wait_result_info,
                                             beltpp::event_handler& eh,
                                             beltpp::stream& event_stream,
                                             beltpp::stream* on_demand_stream);

std::string dashboard();
}
}
