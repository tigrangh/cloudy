#include "worker.hpp"

#include "common.hpp"
#include "internal_model.hpp"
#include "admin_model.hpp"

#include "libavwrapper.hpp"

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

using namespace InternalModel;

using beltpp::packet;
using peer_id = beltpp::stream::peer_id;

namespace chrono = std::chrono;
using chrono::system_clock;

using std::string;
using std::unordered_set;
using std::unordered_map;
using std::unique_ptr;
using std::vector;
using std::pair;
using beltpp::stream_ptr;
using beltpp::event_handler_ptr;

namespace filesystem = boost::filesystem;

namespace cloudy
{

namespace detail
{

void processor_worker(packet&& package, beltpp::libprocessor::async_result& stream)
{
    switch(package.type())
    {
    case InternalModel::ProcessIndexRequest::rtt:
    {
        InternalModel::ProcessIndexRequest request;
        std::move(package).get(request);

        packet result;

        try
        {
            std::istreambuf_iterator<char> end, begin;
            filesystem::ifstream fl;
            filesystem::path path(check_path(request.path).first);

            meshpp::load_file(path, fl, begin, end);
            if (begin == end)
                throw std::runtime_error(path.string() + ": empty or does not exist, cannot index");

            InternalModel::ProcessIndexResult response;
            response.path = request.path;
            response.sha256sum = meshpp::hash(begin, end);
            response.type_descriptions = request.type_descriptions;

            result.set(std::move(response));
        }
        catch (std::exception const& ex)
        {
            InternalModel::ProcessIndexError response;
            response.path = request.path;
            response.type_descriptions = request.type_descriptions;
            response.reason = ex.what();

            result.set(std::move(response));
        }

        stream.send(std::move(result));

        break;
    }
    case InternalModel::ProcessMediaCheckRequest::rtt:
    {
        InternalModel::ProcessMediaCheckRequest request;
        try
        {
            std::move(package).get(request);

            vector<pair<packet, size_t>> full_progress;
            full_progress.resize(request.type_descriptions.size());

            size_t index = 0;
            for (auto const& type_description_str : request.type_descriptions)
            {
                auto& progress_item = full_progress[index];
                AdminModel::detail::loader(progress_item.first, type_description_str, nullptr);
                progress_item.second = 0;
                ++index;
            }

            libavwrapper::transcoder transcoder;
            transcoder.input_file = check_path(request.path).first;
            transcoder.output_dir = request.output_dir;
            transcoder.init(std::move(full_progress));

            while (true)
            {
                auto transcoder_progress = transcoder.run();

                bool no_progress = true;

                size_t option_index = 0;
                for (auto& progress_item : full_progress)
                {
                    if (progress_item.first.type() == AdminModel::MediaTypeDescriptionRaw::rtt)
                    {
                        InternalModel::ProcessMediaCheckResult raw_progress_item;

                        auto src_location = join_path(request.path).first;
                        filesystem::path copy_location = request.output_dir;
                        copy_location /= std::to_string(option_index);
                        boost::system::error_code ec;
                        filesystem::copy(src_location,
                                         copy_location,
                                         ec);
                        if (ec)
                            throw std::runtime_error("processor_worker: filesystem::copy(" +
                                                     src_location + ", " +
                                                     copy_location.string() + ")");

                        raw_progress_item.count = 1;
                        raw_progress_item.path = request.path;
                        raw_progress_item.type_description = std::move(progress_item.first);
                        raw_progress_item.result_type = InternalModel::ResultType::file;
                        raw_progress_item.data_or_file = copy_location.string();

                        stream.send(packet(std::move(raw_progress_item)));
                        no_progress = false;
                    }
                    else if (transcoder_progress.count(option_index))
                    {
                        auto& transcoder_progress_item = transcoder_progress[option_index];

                        if (transcoder_progress_item.count == 0)
                        {   //  happens when processing image instead of video
                            filesystem::path path(transcoder_progress_item.data_or_file);
                            boost::system::error_code ec;
                            if (false == filesystem::remove(path, ec) || ec)
                                throw std::logic_error("processor_worker: filesystem::remove(path, ec)");
                        }
                        else
                        {
                            transcoder_progress_item.path = request.path;
                            transcoder_progress_item.accumulated = progress_item.second;

                            progress_item.second += transcoder_progress_item.count;

                            stream.send(packet(std::move(transcoder_progress_item)));
                            no_progress = false;
                        }
                    }

                    ++option_index;
                }

                if (no_progress)
                {
                    InternalModel::ProcessMediaCheckResult response;
                    response.path = request.path;

                    stream.send(packet(std::move(response)));
                    break;
                }
            }
        }
        catch (...)
        {
            InternalModel::ProcessMediaCheckResult response;
            response.path = request.path;

            stream.send(packet(std::move(response)));
        }

        break;
    }
    }
}

stream_ptr construct_processor_wrap(beltpp::event_handler& eh,
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
    filesystem::path fs;
    wait_result wait_result_info;

    worker_internals(beltpp::ilog* _plogger,
                     filesystem::path const& _fs,
                     beltpp::direct_channel& channel)
        : plogger(_plogger)
        , ptr_eh(beltpp::libprocessor::construct_event_handler())
        , ptr_stream(construct_processor_wrap(*ptr_eh, 2, &processor_worker))
        , ptr_direct_stream(beltpp::construct_direct_stream(worker_peerid, *ptr_eh, channel))
        , fs(_fs)
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
               filesystem::path const& fs,
               beltpp::direct_channel& channel)
    : m_pimpl(new detail::worker_internals(plogger,
                                           fs,
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
                if (received_packet.type() == InternalModel::ProcessMediaCheckRequest::rtt)
                {
                    InternalModel::ProcessMediaCheckRequest* p;
                    received_packet.get(p);
                    p->output_dir = m_pimpl->fs.string();
                }
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



