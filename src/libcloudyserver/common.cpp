#include "common.hpp"

#include "admin_model.hpp"
#include "storage_model.hpp"
#include "internal_model.hpp"

#include <mesh.pp/cryptoutility.hpp>

#include <unordered_set>

using std::string;
using std::pair;
using std::vector;
namespace chrono = std::chrono;

namespace cloudy
{
beltpp::void_unique_ptr get_admin_putl()
{
    beltpp::message_loader_utility utl;
    AdminModel::detail::extension_helper(utl);

    auto ptr_utl =
        beltpp::new_void_unique_ptr<beltpp::message_loader_utility>(std::move(utl));

    return ptr_utl;
}

beltpp::void_unique_ptr get_storage_putl()
{
    beltpp::message_loader_utility utl;
    StorageModel::detail::extension_helper(utl);

    auto ptr_utl =
        beltpp::new_void_unique_ptr<beltpp::message_loader_utility>(std::move(utl));

    return ptr_utl;
}

beltpp::void_unique_ptr get_internal_putl()
{
    beltpp::message_loader_utility utl;
    InternalModel::detail::extension_helper(utl);

    auto ptr_utl =
        beltpp::new_void_unique_ptr<beltpp::message_loader_utility>(std::move(utl));

    return ptr_utl;
}
bool verify_storage_order(string const& storage_order_token,
                          string& channel_address,
                          string& file_uri,
                          string& session_id,
                          uint64_t& seconds,
                          chrono::system_clock::time_point& tp)
{
    AdminModel::SignedStorageAuthorization msg_verfy_sig_storage_order;

    msg_verfy_sig_storage_order.from_string(meshpp::from_base64(storage_order_token), nullptr);

    channel_address = msg_verfy_sig_storage_order.authorization.address;
    file_uri = msg_verfy_sig_storage_order.token.file_uri;
    session_id = msg_verfy_sig_storage_order.token.session_id;
    seconds = msg_verfy_sig_storage_order.token.seconds;
    tp = chrono::system_clock::from_time_t(msg_verfy_sig_storage_order.token.time_point.tm);

    auto now = chrono::system_clock::now();
    auto signed_time_point = chrono::system_clock::from_time_t(msg_verfy_sig_storage_order.token.time_point.tm);
    auto expiring_time_point = signed_time_point + chrono::seconds(msg_verfy_sig_storage_order.token.seconds);

    if (signed_time_point > now + chrono::seconds(storage_order_sign_instant_precision) ||
        expiring_time_point <= now - chrono::seconds(storage_order_sign_instant_precision))
        return false;

    bool correct = meshpp::verify_signature(
                       msg_verfy_sig_storage_order.authorization.address,
                       msg_verfy_sig_storage_order.token.to_string(),
                       msg_verfy_sig_storage_order.authorization.signature);

    if (false == correct)
        return false;

    return true;
}

pair<string, string> join_path(vector<string> const& path)
{
    string path_string;
    string last_name;
    for (auto const& name : path)
    {
        if (name == "." || name == "..")
            throw std::runtime_error("self or parent directories are not supported");
        path_string += "/" + name;
        last_name = name;
    }

    return std::make_pair(path_string, last_name);
}
pair<boost::filesystem::path, string> check_path(vector<string> const& path)
{
    boost::filesystem::path fs_path("/");
    string last_name;
    for (auto const& name : path)
    {
        if (name == "." || name == "..")
            throw std::runtime_error("self or parent directories are not supported");
        fs_path /= name;
        last_name = name;
    }

    return std::make_pair(fs_path, last_name);
}

namespace detail
{
wait_result_item wait_and_receive_one(wait_result& wait_result_info,
                                      beltpp::event_handler& eh,
                                      beltpp::stream& event_stream,
                                      beltpp::stream* on_demand_stream)
{
    auto& info = wait_result_info.m_wait_result;

    if (info == beltpp::event_handler::wait_result::nothing)
    {
        assert(wait_result_info.on_demand_packets.second.empty());
        if (false == wait_result_info.on_demand_packets.second.empty())
            throw std::logic_error("false == wait_result_info.on_demand_packets.second.empty()");
        assert(wait_result_info.event_packets.second.empty());
        if (false == wait_result_info.event_packets.second.empty())
            throw std::logic_error("false == wait_result_info.event_packets.second.empty()");

        std::unordered_set<beltpp::event_item const*> wait_streams;

        info = eh.wait(wait_streams);

        if (info & beltpp::event_handler::event)
        {
            for (auto& pevent_item : wait_streams)
            {
                B_UNUSED(pevent_item);

                beltpp::socket::packets received_packets;
                beltpp::socket::peer_id peerid;
                received_packets = event_stream.receive(peerid);

                wait_result_info.event_packets = std::make_pair(peerid,
                                                                std::move(received_packets));
            }
        }

        /*if (wait_result & beltpp::event_handler::timer_out)
        {
        }*/

        if (on_demand_stream && (info & beltpp::event_handler::on_demand))
        {
            beltpp::socket::packets received_packets;
            beltpp::socket::peer_id peerid;
            received_packets = on_demand_stream->receive(peerid);

            wait_result_info.on_demand_packets = std::make_pair(peerid,
                                                                std::move(received_packets));
        }
    }

    auto result = wait_result_item::empty_result();

    if (info & beltpp::event_handler::event)
    {
        if (false == wait_result_info.event_packets.second.empty())
        {
            auto packet = std::move(wait_result_info.event_packets.second.front());
            auto peerid = wait_result_info.event_packets.first;

            wait_result_info.event_packets.second.pop_front();

            result = wait_result_item::event_result(peerid, std::move(packet));
        }

        if (wait_result_info.event_packets.second.empty())
            info = beltpp::event_handler::wait_result(info & ~beltpp::event_handler::event);

        return result;
    }

    if (info & beltpp::event_handler::timer_out)
    {
        info = beltpp::event_handler::wait_result(info & ~beltpp::event_handler::timer_out);
        result = wait_result_item::timer_result();
        return result;
    }

    if (info & beltpp::event_handler::on_demand)
    {
        if (false == wait_result_info.on_demand_packets.second.empty())
        {
            auto packet = std::move(wait_result_info.on_demand_packets.second.front());
            auto peerid = wait_result_info.on_demand_packets.first;

            wait_result_info.on_demand_packets.second.pop_front();

            result = wait_result_item::on_demand_result(peerid, std::move(packet));
        }

        if (wait_result_info.on_demand_packets.second.empty())
            info = beltpp::event_handler::wait_result(info & ~beltpp::event_handler::on_demand);

        return result;
    }

    return result;
}
}
}


