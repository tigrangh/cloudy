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
#include <vector>
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
using std::vector;

template <typename>
string mime_type();

template <>
string mime_type<AdminModel::MediaTypeDescriptionVideoContainer>()
{
    return "video/mp4";
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

    vector<InternalModel::ProcessMediaCheckResult> pending_for_storage;

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
        , pending_for_storage()
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
        library.commit();
        log.commit();
    }

    void discard() noexcept
    {
        library.discard();
        log.discard();
    }

    void clear()
    {
        library.clear();
        log->log.clear();
    }

    void process_storage(InternalModel::ProcessMediaCheckResult&& pending_data)
    {
        string data = std::move(pending_data.data_or_file);
        pending_data.data_or_file.clear();

        if (pending_data.count &&
            false == data.empty())
        {
            if (pending_data.result_type == InternalModel::ResultType::data)
            {
                StorageModel::StorageFile file;
                file.data = std::move(data);

                file.mime_type = mime_type<AdminModel::MediaTypeDescriptionVideoContainer>();
                ptr_direct_stream->send(storage_peerid, packet(std::move(file)));
            }
            else
            {
                StorageModel::StorageFileAdd file;
                file.file = std::move(data);

                file.mime_type = mime_type<AdminModel::MediaTypeDescriptionVideoContainer>();
                ptr_direct_stream->send(storage_peerid, packet(std::move(file)));
            }
            pending_for_storage.push_back(std::move(pending_data));
        }
        else
        {
            if (pending_data.count != 0)
                throw std::logic_error("process_storage: pending_data.count != 0");
            //pending_data.count = 0;

            bool found = false;
            for (auto const& pending_item : pending_for_storage)
            {
                if (pending_item.path == pending_data.path)
                {
                    found = true;
                    break;
                }
            }

            if (found)
                pending_for_storage.push_back(std::move(pending_data));
            else
            {
                process_check_done_wrapper(std::move(pending_data),
                                           string(),
                                           string());
            }
        }
    }

    void process_storage_done(string const& uri,
                              string const& error_override)
    {
        do
        {
            auto&& progress_info = pending_for_storage.front();
            process_check_done_wrapper(std::move(progress_info),
                                       uri,
                                       error_override);

            pending_for_storage.erase(pending_for_storage.begin());
        }
        while (false == pending_for_storage.empty() &&
               pending_for_storage.front().count == 0);
    }

    void process_check_done_wrapper(InternalModel::ProcessMediaCheckResult&& progress_info,
                                    string const& uri,
                                    string const& error_override)
    {
        auto path = progress_info.path;

        bool final_progress_for_path =
                library.process_check_done(std::move(progress_info),
                                           uri);

        if (final_progress_for_path)
        {
            string sha256sum = library.process_index_retrieve_hash(path);
            if (sha256sum.empty())
                throw std::logic_error("process_check_done_wrapper: sha256sum.empty()");

            if (false == library.indexed(sha256sum))
            {
                AdminModel::CheckMediaProblem problem;
                problem.path = path;
                problem.reason = "was not able to detect any media type";
                if (false == error_override.empty())
                    problem.reason = error_override;
                writeln_node(join_path(path).first + ": " + problem.reason);
                log->log.push_back(packet(std::move(problem)));
            }
            {
                AdminModel::CheckMediaResult done;
                done.path = path;
                writeln_node(join_path(path).first + ": done");
                log->log.push_back(packet(std::move(done)));
                library.process_index_done(path);
            }
        }
        else if (uri.empty())
        {
            AdminModel::CheckMediaProblem problem;
            problem.path = path;
            problem.reason = "problem with storage";
            if (false == error_override.empty())
                problem.reason = error_override;

            writeln_node(join_path(path).first + ": " + problem.reason);
            log->log.push_back(packet(std::move(problem)));
        }
        else
        {
            writeln_node(join_path(path).first + ": added " + uri);
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
        auto items = m_pimpl->library.process_check();
        for (auto&& item : items)
        {
            m_pimpl->writeln_node(join_path(item.path).first + " processing for check");
            m_pimpl->ptr_direct_stream->send(worker_peerid, packet(std::move(item)));
        }
    }
    {
        auto paths = m_pimpl->library.process_index();
        for (auto&& path : paths)
        {
            m_pimpl->writeln_node(join_path(path).first + " processing for index");
            InternalModel::ProcessIndexRequest request;
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

                std::move(received_packet).get(request);

                if (false == request.path.empty())
                {
                    auto path_copy = request.path;

                    m_pimpl->writeln_node(join_path(path_copy).first + " scheduling for index");

                    m_pimpl->library.index(std::move(path_copy));

                    request.path.pop_back();
                }
                stream.send(peerid, packet(m_pimpl->library.list(request.path)));

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

            m_pimpl->library.process_index_store_hash(request.path, request.sha256sum);

            if (m_pimpl->library.indexed(request.sha256sum))
            {
                m_pimpl->writeln_node(join_path(request.path).first + ": with hash "  + request.sha256sum + " is already indexed");
                InternalModel::ProcessMediaCheckResult dummy_progress_item;
                dummy_progress_item.path = request.path;
                m_pimpl->library.add(std::move(dummy_progress_item),
                                     string());
                m_pimpl->library.process_index_done(request.path);
            }
            else
            {
                m_pimpl->writeln_node(join_path(request.path).first + ": with hash "  + request.sha256sum + " scheduling for check");
                m_pimpl->library.process_index_store_hash(request.path, request.sha256sum);
                if (false == m_pimpl->library.check(std::move(request.path)))
                    m_pimpl->writeln_node("\tis already scheduled");
            }

            break;
        }
        case InternalModel::ProcessMediaCheckResult::rtt:
        {
            InternalModel::ProcessMediaCheckResult request;
            std::move(received_packet).get(request);

            if (request.count && request.data_or_file.empty())
                throw std::logic_error("request.count && request.data_or_file.empty()");

            m_pimpl->writeln_node(join_path(request.path).first + " got some checked data");
            m_pimpl->process_storage(std::move(request));

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

                m_pimpl->writeln_node(join_path(request.path).first + ": " + request.reason);
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

            m_pimpl->writeln_node("storage got new data: " + request.uri);

            m_pimpl->process_storage_done(request.uri, string());

            break;
        }
        case StorageModel::UriError::rtt:
        {
            StorageModel::UriError request;
            received_packet.get(request);

            if (request.uri_problem_type != StorageModel::UriProblemType::duplicate)
                throw std::logic_error("request.uri_problem_type != StorageModel::UriProblemType::duplicate");

            m_pimpl->writeln_node("storage found a duplicate: " + request.uri);

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
