#include "storage_server.hpp"

#include "common.hpp"
#include "storage.hpp"
#include "storage_model.hpp"
#include "internal_model.hpp"
#include "storage_http.hpp"

#include <belt.pp/socket.hpp>

#include <mesh.pp/cryptoutility.hpp>

#include <vector>
#include <list>
#include <string>
#include <memory>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <exception>
#include <mutex>

using namespace StorageModel;

using beltpp::ip_address;
using beltpp::socket;
using beltpp::packet;
using peer_id = socket::peer_id;

namespace chrono = std::chrono;
using chrono::system_clock;

using std::pair;
using std::string;
using std::vector;
using std::list;
using std::unordered_set;
using std::unique_ptr;

namespace filesystem = boost::filesystem;

namespace cloudy
{

namespace detail
{
using rpc_storage_sf = beltpp::socket_family_t<&http::message_list_load<&StorageModel::message_list_load>>;

class storage_server_internals
{
public:
    beltpp::ilog* plogger;
    unique_ptr<beltpp::event_handler> ptr_eh;
    unique_ptr<beltpp::socket> ptr_socket;

    beltpp::stream_ptr ptr_direct_stream;

    cloudy::storage m_storage;

    meshpp::public_key pb_key;
    wait_result wait_result_info;

    storage_server_internals(beltpp::ip_address const& bind_to_address,
                             filesystem::path const& path,
                             filesystem::path const& path_binaries,
                             meshpp::public_key const& _pb_key,
                             beltpp::ilog* _plogger,
                             beltpp::direct_channel& channel)
        : plogger(_plogger)
        , ptr_eh(beltpp::libsocket::construct_event_handler())
        , ptr_socket(beltpp::libsocket::getsocket<rpc_storage_sf>(*ptr_eh))
        , ptr_direct_stream(beltpp::construct_direct_stream(storage_peerid, *ptr_eh, channel))
        , m_storage(path, path_binaries)
        , pb_key(_pb_key)
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
};
}

/*
 * storage_server
 */
storage_server::storage_server(beltpp::ip_address const& bind_to_address,
                               filesystem::path const& path,
                               filesystem::path const& path_binaries,
                               meshpp::public_key const& pb_key,
                               beltpp::ilog* plogger,
                               beltpp::direct_channel& channel)
    : m_pimpl(new detail::storage_server_internals(bind_to_address,
                                                   path,
                                                   path_binaries,
                                                   pb_key,
                                                   plogger,
                                                   channel))
{}
storage_server::storage_server(storage_server&&) noexcept = default;
storage_server::~storage_server() = default;

void storage_server::wake()
{
    m_pimpl->ptr_eh->wake();
}

void storage_server::run(bool& stop)
{
    stop = false;

    unordered_set<beltpp::event_item const*> wait_sockets;

    auto wait_result = detail::wait_and_receive_one(m_pimpl->wait_result_info,
                                                    *m_pimpl->ptr_eh,
                                                    *m_pimpl->ptr_socket,
                                                    m_pimpl->ptr_direct_stream.get());

    if (wait_result.et == detail::wait_result_item::event)
    {
        auto peerid = wait_result.peerid;
        auto received_packet = std::move(wait_result.packet);

        beltpp::stream* psk = m_pimpl->ptr_socket.get();

        try
        {
            switch (received_packet.type())
            {
            case beltpp::stream_join::rtt:
            {
                m_pimpl->writeln_node("storage: joined: " + peerid);
                break;
            }
            case beltpp::stream_drop::rtt:
            {
                m_pimpl->writeln_node("storage: dropped: " + peerid);
                break;
            }
            case beltpp::stream_protocol_error::rtt:
            {
                beltpp::stream_protocol_error msg;
                std::move(received_packet).get(msg);
                m_pimpl->writeln_node("storage: protocol error: " + peerid);
                m_pimpl->writeln_node(msg.buffer);

                break;
            }
            case beltpp::socket_open_refused::rtt: break;
            case beltpp::socket_open_error::rtt: break;
            case StorageFileRequest::rtt:
            {
                StorageFileRequest file_info;
                std::move(received_packet).get(file_info);

                string file_uri;

                if (false)
                {
                    file_uri = file_info.uri;
                }
                else
                {
                    string channel_address;
                    string session_id;
                    uint64_t seconds;
                    system_clock::time_point tp;

                    if (false == verify_storage_order(file_info.authorization,
                                                      channel_address,
                                                      file_uri,
                                                      session_id,
                                                      seconds,
                                                      tp) ||
                        m_pimpl->pb_key.to_string() != channel_address)
                        file_info.uri = std::move(file_uri);
                }

                StorageFile file;
                if (false == file_uri.empty() &&
                    m_pimpl->m_storage.get(file_uri, file))
                {
                    psk->send(peerid, beltpp::packet(std::move(file)));
                }
                else
                {
                    UriError error;
                    error.uri = file_info.uri;
                    error.uri_problem_type = UriProblemType::missing;
                    psk->send(peerid, beltpp::packet(std::move(error)));
                }

                break;
            }
            case StorageFileRangeRequest::rtt:
            {
                StorageFileRangeRequest file_info;
                std::move(received_packet).get(file_info);

                string file_uri;

                if (false)
                {
                    file_uri = file_info.uri;
                }
                else
                {
                    string channel_address;
                    string session_id;
                    uint64_t seconds;
                    system_clock::time_point tp;

                    if (false == verify_storage_order(file_info.authorization,
                                                      channel_address,
                                                      file_uri,
                                                      session_id,
                                                      seconds,
                                                      tp) ||
                        m_pimpl->pb_key.to_string() != channel_address)
                        file_info.uri = std::move(file_uri);
                }

                StorageFile file;
                if (false == file_uri.empty() &&
                    m_pimpl->m_storage.get(file_uri, file))
                {
                    if (file_info.count == 0)
                        file_info.count = 1024 * 1024;

                    file_info.count = std::min(file_info.count, file.data.length() - file_info.start);

                    StorageFileRange fr;
                    fr.start = file_info.start;
                    fr.count = file_info.count;
                    fr.full_size = file.data.length();

                    fr.data = file.data.substr(fr.start, fr.count);
                    fr.mime_type = file.mime_type;

                    psk->send(peerid, beltpp::packet(std::move(fr)));
                }
                else
                {
                    UriError error;
                    error.uri = file_info.uri;
                    error.uri_problem_type = UriProblemType::missing;
                    psk->send(peerid, beltpp::packet(std::move(error)));
                }

                break;
            }
            case StorageFileDetails::rtt:
            {
                StorageFileDetails details_request;
                std::move(received_packet).get(details_request);

                StorageFile file;
                if (m_pimpl->m_storage.get(details_request.uri, file))
                {
                    StorageFileDetailsResponse details_response;
                    details_response.uri = details_request.uri;
                    details_response.size = file.data.length();
                    details_response.mime_type = file.mime_type;

                    psk->send(peerid, beltpp::packet(std::move(details_response)));
                }
                else
                {
                    UriError error;
                    error.uri = details_request.uri;
                    error.uri_problem_type = UriProblemType::missing;
                    psk->send(peerid, beltpp::packet(std::move(error)));
                }

                break;
            }
            default:
            {
                m_pimpl->writeln_node("peer: " + peerid);
                m_pimpl->writeln_node("storage can't handle: " + received_packet.to_string());

                psk->send(peerid, beltpp::packet(beltpp::stream_drop()));
                break;
            }
            }   // switch ref_packet.type()
        }
        catch (std::exception const& e)
        {
            RemoteError msg;
            msg.message = e.what();
            psk->send(peerid, beltpp::packet(std::move(msg)));
            throw;
        }
        catch (...)
        {
            RemoteError msg;
            msg.message = "unknown exception";
            psk->send(peerid, beltpp::packet(std::move(msg)));
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

        auto& stream = *m_pimpl->ptr_direct_stream;

        try
        {
            switch (received_packet.type())
            {
            case StorageFile::rtt:
            {
                StorageFile storage_file;
                std::move(received_packet).get(storage_file);

                string uri;
                uint64_t duplicate_count = m_pimpl->m_storage.put(std::move(storage_file), uri);
                assert(duplicate_count);

                StorageFileAddress file_address;
                file_address.uri = uri;
                file_address.duplicate_count = duplicate_count;

                stream.send(peerid, packet(std::move(file_address)));
                
                break;
            }
            case StorageFileAdd::rtt:
            {
                StorageFileAdd storage_file_add;
                std::move(received_packet).get(storage_file_add);

                StorageFile storage_file;
                storage_file.mime_type = storage_file_add.mime_type;
                storage_file.data = storage_file_add.file;

                string uri;
                uint64_t duplicate_count = m_pimpl->m_storage.put_file(std::move(storage_file), uri);
                assert(duplicate_count);
                
                StorageFileAddress file_address;
                file_address.uri = uri;
                file_address.duplicate_count = duplicate_count;

                stream.send(peerid, packet(std::move(file_address)));

                break;
            }
            case StorageFileDelete::rtt:
            {
                StorageFileDelete storage_file_delete;
                std::move(received_packet).get(storage_file_delete);

                uint64_t existing_count = m_pimpl->m_storage.remove(storage_file_delete.uri);
                if (existing_count)
                {
                    StorageFileDeleted file_deleted;
                    file_deleted.uri = storage_file_delete.uri;
                    file_deleted.remaining_count = existing_count - 1;
                    stream.send(peerid, packet(std::move(file_deleted)));
                }
                else
                {
                    UriError msg;
                    msg.uri = storage_file_delete.uri;
                    msg.uri_problem_type = UriProblemType::missing;

                    stream.send(peerid, packet(std::move(msg)));
                }
                break;
            }
            case FileUrisRequest::rtt:
            {
                FileUris msg;

                auto set_file_uris = m_pimpl->m_storage.get_file_uris();
                msg.file_uris.reserve(set_file_uris.size());
                for (auto& file_uri : set_file_uris)
                    msg.file_uris.push_back(std::move(file_uri));

                stream.send(peerid, packet(std::move(msg)));
                break;
            }
            }
        }
        catch (std::exception const& e)
        {
            RemoteError msg;
            msg.message = e.what();

            stream.send(peerid, packet(std::move(msg)));
            throw;
        }
        catch (...)
        {
            RemoteError msg;
            msg.message = "unknown error";

            stream.send(peerid, packet(std::move(msg)));
            throw;
        }
    }
}

}



