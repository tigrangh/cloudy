#include "worker.hpp"

#include "common.hpp"
#include "internal_model.hpp"
#include "admin_model.hpp"

#include <belt.pp/packet.hpp>
#include <belt.pp/processor.hpp>

#include <mesh.pp/fileutility.hpp>
#include <mesh.pp/cryptoutility.hpp>

#include <boost/filesystem.hpp>

#include <string>
#include <memory>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <exception>
#include <mutex>
#include <thread>

using namespace InternalModel;

using beltpp::packet;
using peer_id = beltpp::stream::peer_id;

namespace chrono = std::chrono;
using chrono::system_clock;

using std::string;
using std::unordered_set;
using std::unique_ptr;
using std::vector;

namespace filesystem = boost::filesystem;

namespace cloudy
{

namespace detail
{
filesystem::path check_path(vector<string> const& path)
{
    filesystem::path fs_path("/");
    for (auto const& name : path)
    {
        if (name == "." || name == "..")
            throw std::runtime_error("self or parent directories are not supported");
        fs_path /= name;
    }

    return fs_path;
}

packet processor_worker(packet&& package)
{
    packet result;

    switch(package.type())
    {
    case InternalModel::ProcessIndexRequest::rtt:
    {
        InternalModel::ProcessIndexRequest request;
        std::move(package).get(request);

        try
        {
            std::istreambuf_iterator<char> end, begin;
            boost::filesystem::ifstream fl;
            boost::filesystem::path path(check_path(request.path));

            meshpp::load_file(path, fl, begin, end);
            if (begin == end)
                throw std::runtime_error("no such file");

            InternalModel::ProcessIndexResult response;
            response.path = request.path;
            response.sha256sum = meshpp::hash(begin, end);

            result.set(std::move(response));
        }
        catch (std::exception const& ex)
        {
            InternalModel::AdminModelWrapper response_wrapper;
            AdminModel::ProcessIndexProblem response;
            response.path = request.path;
            response.reason = ex.what();

            response_wrapper.package.set(std::move(response));

            result.set(std::move(response_wrapper));
        }
        break;
    }
    case InternalModel::ProcessCheckMediaRequest::rtt:
    {
        try
        {
            InternalModel::ProcessCheckMediaRequest request;
            std::move(package).get(request);
            InternalModel::ProcessCheckMediaResult response;

            response.request = std::move(request);
            response.count = 0;
            response.data.clear();

            response.data = InternalModel::to_string(response.request.type) + ", " +
                            std::to_string(response.request.dimension) + ", " +
                            std::to_string(response.request.accumulated);

            response.count = 1;

            if (response.request.accumulated == 2)
            {
                response.count = 0;
                response.data.clear();
            }

            result.set(std::move(response));
        }
        catch (...)
        {
        }

        break;
    }
    }

    return packet(std::move(result));
}

stream_ptr construct_processor(beltpp::event_handler& eh,
                               size_t count,
                               beltpp::libprocessor::fpworker const& worker)
{
    auto result = beltpp::libprocessor::construct_processor(eh, 2, &processor_worker);
    eh.add(*result);

    return result;
}

class worker_internals
{
public:
    beltpp::ilog* plogger;
    event_handler_ptr ptr_eh;
    stream_ptr ptr_stream;
    stream_ptr ptr_direct_stream;
    wait_result wait_result_info;

    worker_internals(beltpp::ilog* _plogger,
                     direct_channel& channel)
        : plogger(_plogger)
        , ptr_eh(beltpp::libprocessor::construct_event_handler())
        , ptr_stream(construct_processor(*ptr_eh, 2, &processor_worker))
        , ptr_direct_stream(cloudy::construct_direct_stream(worker_peerid, *ptr_eh, channel))
    {
        //ptr_eh->set_timer(event_timer_period);
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
 * worker
 */
worker::worker(beltpp::ilog* plogger,
               direct_channel& channel)
    : m_pimpl(new detail::worker_internals(plogger,
                                           channel))
{}
worker::worker(worker&&) noexcept = default;
worker::~worker() = default;

void worker::wake()
{
    m_pimpl->ptr_eh->wake();
}

void worker::run(bool& stop)
{
    stop = false;

    unordered_set<beltpp::event_item const*> wait_sockets;

    auto wait_result = detail::wait_and_receive_one(m_pimpl->wait_result_info,
                                                    *m_pimpl->ptr_eh,
                                                    *m_pimpl->ptr_stream,
                                                    m_pimpl->ptr_direct_stream.get());

    if (wait_result.et == detail::wait_result_item::event)
    {
        auto peerid = wait_result.peerid;
        auto received_packet = std::move(wait_result.packet);

        beltpp::stream* psk = m_pimpl->ptr_stream.get();
        B_UNUSED(psk);

        try
        {
            m_pimpl->ptr_direct_stream->send(admin_peerid, std::move(received_packet));
        }
        catch (std::exception const& e)
        {
//            RemoteError msg;
//            msg.message = e.what();
//            psk->send(peerid, beltpp::packet(std::move(msg)));
            throw;
        }
        catch (...)
        {
//            RemoteError msg;
//            msg.message = "unknown exception";
//            psk->send(peerid, beltpp::packet(std::move(msg)));
            throw;
        }
    }
    else if (wait_result.et == detail::wait_result_item::timer)
    {
        m_pimpl->ptr_stream->timer_action();
    }
    else if (m_pimpl->ptr_direct_stream && wait_result.et == detail::wait_result_item::on_demand)
    {
        auto peerid = wait_result.peerid;
        auto received_packet = std::move(wait_result.packet);

        auto& stream = *m_pimpl->ptr_direct_stream;

        B_UNUSED(stream);

        try
        {
            if (peerid == admin_peerid &&
                (received_packet.type() != beltpp::stream_join::rtt &&
                 received_packet.type() != beltpp::stream_drop::rtt)
                )
            {
                m_pimpl->ptr_stream->send(string(), std::move(received_packet));
            }
        }
        catch (std::exception const& e)
        {
//            RemoteError msg;
//            msg.message = e.what();

//            stream.send(peerid, packet(std::move(msg)));
            throw;
        }
        catch (...)
        {
//            RemoteError msg;
//            msg.message = "unknown error";

//            stream.send(peerid, packet(std::move(msg)));
            throw;
        }
    }
}

}



