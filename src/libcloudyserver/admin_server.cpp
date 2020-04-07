#include "admin_server.hpp"

#include "common.hpp"
#include "admin_http.hpp"
#include "admin_model.hpp"
#include "internal_model.hpp"
#include "library.hpp"

#include <belt.pp/socket.hpp>
#include <belt.pp/packet.hpp>
#include <belt.pp/timer.hpp>
#include <belt.pp/scope_helper.hpp>

#include <mesh.pp/cryptoutility.hpp>
#include <mesh.pp/fileutility.hpp>

#include <boost/filesystem.hpp>

#include <memory>
#include <chrono>
#include <unordered_set>

namespace cloudy
{
using beltpp::event_handler;
using beltpp::socket;
using beltpp::event_handler_ptr;
using beltpp::socket_ptr;
using beltpp::packet;
using beltpp::ip_address;
using beltpp::ilog;
using beltpp::timer;

namespace filesystem = boost::filesystem;

using std::unique_ptr;
namespace chrono = std::chrono;
using std::unordered_set;

namespace detail
{
using rpc_sf = beltpp::socket_family_t<&http::message_list_load<&AdminModel::message_list_load>>;

class admin_server_internals
{
public:
    ilog* plogger;
    event_handler_ptr ptr_eh;
    socket_ptr ptr_socket;

    stream_ptr ptr_direct_stream;

    cloudy::library library;
    meshpp::file_loader<AdminModel::Log,
                        &AdminModel::Log::from_string,
                        &AdminModel::Log::to_string> log;

    meshpp::private_key pv_key;
    wait_result wait_result_info;

    admin_server_internals(ip_address const& bind_to_address,
                           filesystem::path const& fs_library,
                           filesystem::path const& fs_admin,
                           meshpp::private_key const& _pv_key,
                           ilog* _plogger,
                           direct_channel& channel)
        : plogger(_plogger)
        , ptr_eh(beltpp::libsocket::construct_event_handler())
        , ptr_socket(beltpp::libsocket::getsocket<rpc_sf>(*ptr_eh))
        , ptr_direct_stream(cloudy::construct_direct_stream(admin_peerid, *ptr_eh, channel))
        , library(fs_library)
        , log(fs_admin / "log.json")
        , pv_key(_pv_key)
    {
        ptr_eh->set_timer(event_timer_period);

        if (bind_to_address.local.empty())
            throw std::logic_error("bind_to_address.local.empty()");

        ptr_socket->listen(bind_to_address);

        ptr_eh->add(*ptr_socket);
    }

    void writeln_node(string const& value)
    {
        if (plogger)
            plogger->message(value);
    }

    void writeln_node_warning(string const& value)
    {
        if (plogger)
            plogger->warning(value);
    }

    void save()
    {
        library.save();
        log.save();
    }

    void commit() noexcept
    {
        library.save();
        log.save();
    }

    void discard() noexcept
    {
        library.save();
        log.save();
    }

    void clear()
    {
        library.clear();
        log->log.clear();
    }
};
}

using namespace AdminModel;

admin_server::admin_server(ip_address const& bind_to_address,
                           filesystem::path const& fs_library,
                           filesystem::path const& fs_admin,
                           meshpp::private_key const& pv_key,
                           ilog* plogger,
                           direct_channel& channel)
    : m_pimpl(new detail::admin_server_internals(bind_to_address,
                                                 fs_library,
                                                 fs_admin,
                                                 pv_key,
                                                 plogger,
                                                 channel))
{

}
admin_server::admin_server(admin_server&& other) noexcept = default;
admin_server::~admin_server() = default;

void admin_server::wake()
{
    m_pimpl->ptr_eh->wake();
}

void admin_server::run(bool& stop_check)
{
    stop_check = false;


    {
        auto paths = m_pimpl->library.process_index();
        for (auto&& path : paths)
        {
            ProcessIndexRequest request;
            request.path = std::move(path);
            m_pimpl->ptr_direct_stream->send(worker_peerid, packet(request));
        }
    }

    auto wait_result = detail::wait_and_receive_one(m_pimpl->wait_result_info,
                                                    *m_pimpl->ptr_eh,
                                                    *m_pimpl->ptr_socket,
                                                    m_pimpl->ptr_direct_stream.get());

    if (wait_result.et == detail::wait_result_item::event)
    {
        auto peerid = wait_result.peerid;
        auto received_packet = std::move(wait_result.packet);

        auto& stream = *m_pimpl->ptr_socket;

        try
        {
            if (/*false == m_pimpl->m_nodeid_sessions.process(peerid, std::move(received_packet)) &&
                false == m_pimpl->m_sync_sessions.process(peerid, std::move(received_packet))*/
                true)
            {
                beltpp::on_failure guard([this]{ m_pimpl->discard(); });

                switch (received_packet.type())
                {
                case beltpp::stream_join::rtt:
                {
                    m_pimpl->writeln_node("admin: joined: " + peerid);
                    break;
                }
                case beltpp::stream_drop::rtt:
                {
                    m_pimpl->writeln_node("admin: dropped: " + peerid);
                    break;
                }
                case beltpp::stream_protocol_error::rtt:
                {
                    beltpp::stream_protocol_error msg;
                    m_pimpl->writeln_node("admin: protocol error: " + peerid);
                    m_pimpl->writeln_node(msg.buffer);
                    stream.send(peerid, beltpp::packet(beltpp::stream_drop()));

                    break;
                }
                case beltpp::socket_open_refused::rtt:
                {
                    beltpp::socket_open_refused msg;
                    std::move(received_packet).get(msg);
                    m_pimpl->writeln_node_warning(msg.reason + ", " + peerid);
                    break;
                }
                case beltpp::socket_open_error::rtt:
                {
                    beltpp::socket_open_error msg;
                    std::move(received_packet).get(msg);
                    m_pimpl->writeln_node_warning(msg.reason + ", " + peerid);
                    break;
                }
                case LibraryGet::rtt:
                {
                    LibraryGet request;
                    std::move(received_packet).get(request);

                    stream.send(peerid, packet(m_pimpl->library.list(request.path)));
                    break;
                }
                case LibraryPut::rtt:
                {
                    LibraryPut request;
                    LibraryResponse response;

                    std::move(received_packet).get(request);

                    if (false == request.path.empty())
                    {
                        ProcessIndexRequest process_index_request;
                        process_index_request.path = request.path;

                        m_pimpl->library.index(std::move(process_index_request.path));

                        request.path.pop_back();
                    }
                    stream.send(peerid, packet(m_pimpl->library.list(request.path)));

                    stream.send(peerid, packet(response));
                    break;
                }
                case LogGet::rtt:
                {
                    stream.send(peerid, packet(*m_pimpl->log));

                    break;
                }
                case LogDelete::rtt:
                {
                    LogDelete request;
                    std::move(received_packet).get(request);

                    auto& log = m_pimpl->log->log;
                    if (request.count > log.size())
                        request.count = log.size();

                    log.erase(log.begin(), log.begin() + request.count);
                    stream.send(peerid, packet(*m_pimpl->log));

                    break;
                }
                default:
                {
                    m_pimpl->writeln_node("peer: " + peerid);
                    m_pimpl->writeln_node("admin can't handle: " + received_packet.to_string());

                    break;
                }
                }   // switch received_packet.type()

                m_pimpl->save();
                guard.dismiss();
                m_pimpl->commit();
            }   // if not processed by sessions
        }
        catch (std::exception const& e)
        {
            RemoteError msg;
            msg.message = e.what();
            stream.send(peerid, beltpp::packet(msg));
            throw;
        }
        catch (...)
        {
            RemoteError msg;
            msg.message = "unknown exception";
            stream.send(peerid, beltpp::packet(msg));
            throw;
        }
    }
    else if (wait_result.et == detail::wait_result_item::timer)
    {
        m_pimpl->ptr_socket->timer_action();
    }
    else if (m_pimpl->ptr_direct_stream && wait_result.et == detail::wait_result_item::on_demand)
    {
        auto peerid = wait_result.peerid;
        auto received_packet = std::move(wait_result.packet);

        beltpp::on_failure guard([this]{ m_pimpl->discard(); });

        if (peerid == worker_peerid)
        switch (received_packet.type())
        {
        case beltpp::stream_join::rtt:
        {
            m_pimpl->writeln_node("admin: joined: " + peerid);
            break;
        }
        case beltpp::stream_drop::rtt:
        {
            m_pimpl->writeln_node("admin: dropped: " + peerid);
            break;
        }
        case ProcessIndexResult::rtt:
        {
            ProcessIndexResult request;
            received_packet.get(request);

            m_pimpl->library.process_index_done(request.path);
            m_pimpl->library.add(std::move(request.path), request.sha256sum);

            m_pimpl->log->log.push_back(std::move(received_packet));

            break;
        }
        case ProcessIndexProblem::rtt:
        {
            ProcessIndexProblem request;
            received_packet.get(request);

            m_pimpl->library.process_index_done(request.path);

            m_pimpl->log->log.push_back(std::move(received_packet));
            break;
        }
        }

        m_pimpl->save();
        guard.dismiss();
        m_pimpl->commit();
        /*if (false == m_pimpl->m_sessions.process("slave", std::move(ref_packet)))
        {
            switch (received_packet.type())
            {

            }
        }*/   // if not processed by sessions
    }

//    m_pimpl->m_sessions.erase_all_pending();
//    m_pimpl->m_sync_sessions.erase_all_pending();
//    m_pimpl->m_nodeid_sessions.erase_all_pending();
}

}
