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
#include <utility>

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

string mime_type(AdminModel::MediaType type)
{
    string result;
    if (type == AdminModel::MediaType::video)
        result = "video/mp4";
    else if (type == AdminModel::MediaType::image)
        result = "image/jpeg";
    else
        throw std::logic_error("mime type handling");

    return result;
}
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

    meshpp::map_loader<InternalModel::ProcessCheckMediaResult> pending_for_storage;
    string processing_for_storage;

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
        , pending_for_storage("pending_for_storage", fs_admin, 10000, get_internal_putl())
        , processing_for_storage()
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
        pending_for_storage.save();
    }

    void commit() noexcept
    {
        library.commit();
        log.commit();
        pending_for_storage.commit();
    }

    void discard() noexcept
    {
        library.discard();
        log.discard();
        pending_for_storage.discard();
    }

    void clear()
    {
        library.clear();
        log->log.clear();
    }

    void store(InternalModel::ProcessCheckMediaResult&& pending_data)
    {
        string path_string = join_path(pending_data.request.path).first;

        if (path_string.empty())
            throw std::logic_error("admin_server_internals::store: path_string.empty()");

        bool inserted = pending_for_storage.insert(path_string, std::move(pending_data));
        if (false == inserted)
            throw std::logic_error("admin_server_internals::store: false == inserted");
    }
    pair<string, string> process_storage()
    {
        pair<string, string> result;
        if (false == processing_for_storage.empty())
            return result;

        if (false == pending_for_storage.keys().empty())
        {
            auto processing = *pending_for_storage.keys().begin();

            auto const& file = pending_for_storage.as_const().at(processing);

            result = std::make_pair(file.data, mime_type(static_cast<AdminModel::MediaType>(file.request.type)));
            processing_for_storage = processing;
        }

        return result;
    }

    void process_storage_done(string const& uri,
                              string const& error_override)
    {
        InternalModel::ProcessCheckMediaResult& result =
                pending_for_storage.at(processing_for_storage);

        process_check_done(std::move(result.request),
                           result.count,
                           uri,
                           error_override);

        pending_for_storage.erase(processing_for_storage);
        processing_for_storage.clear();
    }

    void process_check_done(InternalModel::ProcessCheckMediaRequest&& progress_info,
                            uint64_t count,
                            string const& uri,
                            string const& error_override)
    {
        auto ptr_description =
                library.process_check_done(progress_info,
                                           count,
                                           uri);

        string sha256sum = library.process_index_retrieve_hash(progress_info.path);
        if (sha256sum.empty())
            throw std::logic_error("process_check_done: sha256sum.empty()");

        if (uri.empty() && count)
        {
            AdminModel::CheckMediaProblem problem;
            problem.path = progress_info.path;
            problem.reason = "problem with storage";
            if (false == error_override.empty())
                problem.reason = error_override;

            log->log.push_back(packet(std::move(problem)));

            for (auto const& type_item : ptr_description->types)
            {
                for (auto const& overlay : type_item.overlays)
                {
                    for (auto const& frame : overlay.second.sequence)
                    {
                        StorageModel::StorageFileDelete request;
                        request.uri = frame.uri;
                        ptr_direct_stream->send(storage_peerid, packet(std::move(request)));

                        writeln_node("asking storage to delete: " + frame.uri);
                    }
                }
            }
            library.process_index_done(progress_info.path);
        }
        else if (ptr_description &&
                 ptr_description->types.empty())
        {
            AdminModel::CheckMediaProblem problem;
            problem.path = progress_info.path;
            problem.reason = "was not able to detect any media type";
            if (false == error_override.empty())
                problem.reason = error_override;
            log->log.push_back(packet(std::move(problem)));
            library.process_index_done(progress_info.path);
        }
        else if (ptr_description &&
                 false == ptr_description->types.empty())
        {
            AdminModel::CheckMediaResult success;
            success.path = progress_info.path;
            for (auto const& type_item : ptr_description->types)
                success.mime_type.push_back(mime_type(type_item.type));
            log->log.push_back(packet(std::move(success)));
            library.add(std::move(*ptr_description), std::move(progress_info.path), sha256sum);
            library.process_index_done(progress_info.path);
        }
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
            InternalModel::ProcessIndexRequest request;
            request.path = std::move(path);
            m_pimpl->ptr_direct_stream->send(worker_peerid, packet(request));
        }
    }
    {
        auto items = m_pimpl->library.process_check();
        for (auto&& item : items)
            m_pimpl->ptr_direct_stream->send(worker_peerid, packet(std::move(item)));
    }
    {
        auto data = m_pimpl->process_storage();
        if (false == data.first.empty())
        {
            StorageModel::StorageFile file;
            file.data = std::move(data.first);
            file.mime_type = std::move(data.second);
            m_pimpl->ptr_direct_stream->send(storage_peerid, packet(std::move(file)));
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
            case IndexListGet::rtt:
            {
                stream.send(peerid, packet(m_pimpl->library.list_index(string())));
                break;
            }
            case IndexGet::rtt:
            {
                IndexGet request;
                std::move(received_packet).get(request);

                LibraryIndex response;
                auto temp = m_pimpl->library.list_index(request.sha256sum);

                if (temp.list_index.empty())
                    throw std::runtime_error("index entry not found: " + request.sha256sum);

                response = temp.list_index.begin()->second;

                stream.send(peerid, packet(std::move(response)));
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
                    auto path_copy = request.path;

                    m_pimpl->library.index(std::move(path_copy));

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
        case InternalModel::ProcessIndexResult::rtt:
        {
            InternalModel::ProcessIndexResult request;
            received_packet.get(request);

            if (m_pimpl->library.indexed(request.sha256sum))
            {
                m_pimpl->library.add(MediaDescription(),
                                     std::move(request.path),
                                     request.sha256sum);
                m_pimpl->library.process_index_done(request.path);
            }
            else
            {
                m_pimpl->library.process_index_store_hash(request.path, request.sha256sum);
                m_pimpl->library.check(std::move(request.path));
            }

            break;
        }
        case InternalModel::ProcessCheckMediaResult::rtt:
        {
            InternalModel::ProcessCheckMediaResult request;
            received_packet.get(request);

            if (request.count && request.data.empty())
                throw std::logic_error("request.count && request.data.empty()");

            if (request.count)
                m_pimpl->store(std::move(request));
            else
                m_pimpl->process_check_done(std::move(request.request),
                                            0,
                                            string(),
                                            string());

            break;
        }
        case InternalModel::AdminModelWrapper::rtt:
        {
            InternalModel::AdminModelWrapper request_wrapper;
            std::move(received_packet).get(request_wrapper);

            if (request_wrapper.package.type() == ProcessIndexProblem::rtt)
            {
                ProcessIndexProblem request;
                request_wrapper.package.get(request);

                m_pimpl->library.process_index_done(request.path);

                m_pimpl->log->log.push_back(std::move(received_packet));
            }
            break;
        }
        }
        else if (peerid == storage_peerid)
        switch (received_packet.type())
        {
        case StorageModel::StorageFileAddress::rtt:
        {
            StorageModel::StorageFileAddress request;
            received_packet.get(request);

            m_pimpl->process_storage_done(request.uri, string());

            break;
        }
        case StorageModel::UriError::rtt:
        {
            StorageModel::UriError request;
            received_packet.get(request);

            if (request.uri_problem_type != StorageModel::UriProblemType::duplicate)
                throw std::logic_error("request.uri_problem_type != StorageModel::UriProblemType::duplicate");

            m_pimpl->process_storage_done(request.uri, string());

            break;
        }
        case StorageModel::RemoteError::rtt:
        {
            StorageModel::RemoteError request;
            received_packet.get(request);

            m_pimpl->process_storage_done(string(), request.message);

            break;
        }
        }

        m_pimpl->save();
        guard.dismiss();
        m_pimpl->commit();
    }
}

}
