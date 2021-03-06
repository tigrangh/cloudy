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
        vector<pair<AdminModel::MediaTypeDescriptionVariant, size_t>> all_options;
        try
        {
            std::move(package).get(request);

            vector<AdminModel::MediaTypeDescriptionVariant> unchanged_options;
            unchanged_options.resize(request.type_descriptions.size());
            all_options.resize(request.type_descriptions.size());

            size_t index = 0;
            for (auto type_description : request.type_descriptions)
            {
                auto& option_item = all_options[index];
                option_item.first = std::move(type_description);
                option_item.second = 0;

                unchanged_options[index] = option_item.first;

                ++index;
            }

            libavwrapper::transcoder transcoder;
            transcoder.input_file = check_path(request.path).first;
            transcoder.output_dir = request.output_dir;
            
            transcoder.init(all_options);
            bool raw_done = false;

            while (true)
            {
                auto progress = transcoder.run();

                if (false == raw_done)
                {
                    raw_done = true;
                    for (size_t option_index = 0; option_index != all_options.size(); ++option_index)
                    {
                        auto& option_item = all_options[option_index];

                        if (option_item.first->type() == AdminModel::MediaTypeDescriptionRaw::rtt)
                        {
                            auto& progress_item = progress[option_index];
                            InternalModel::ProcessMediaCheckResult raw_progress_item;

                            auto src_location = join_path(request.path).first;
                            filesystem::path copy_location = request.output_dir;
                            copy_location /= std::to_string(option_index);// + "_" + std::to_string(raw_done));
                            boost::system::error_code ec;
                            filesystem::copy(src_location,
                                             copy_location,
                                             ec);
                            if (ec)
                                throw std::runtime_error("processor_worker: filesystem::copy(" +
                                                         src_location + ", " +
                                                         copy_location.string() + ")");

                            progress_item.duration = 1;
                            progress_item.data_or_file = copy_location.string();
                            progress_item.result_type = InternalModel::ResultType::file;
                        }
                    }
                }

                auto it = progress.begin();
                while (it != progress.end())
                {
                    bool empty_transcoder_progress = false;

                    if (it->second.duration == 0)
                    {   //  happens when processing image instead of video
                        empty_transcoder_progress = true;
                    }
                    else if (it->second.result_type == InternalModel::ResultType::file)
                    {
                        filesystem::path path(it->second.data_or_file);
                        filesystem::fstream fl;
                        fl.open(path, std::ios_base::binary |
                                      std::ios_base::out |
                                      std::ios_base::in);

                        if (!fl)
                            empty_transcoder_progress = true;
                        else
                        {
                            fl.seekg(0, std::ios_base::end);
                            size_t size_when_opened = size_t(fl.tellg());

                            if (0 == size_when_opened)
                                empty_transcoder_progress = true;
                        }
                    }
                    else if (it->second.data_or_file.empty())
                        empty_transcoder_progress = true;
                    
                    if (empty_transcoder_progress)
                    {
                        filesystem::path path(it->second.data_or_file);
                        filesystem::remove(path);

                        it = progress.erase(it);
                    }
                    else
                        ++it;
                }

                if (progress.empty())
                {
                    InternalModel::ProcessMediaCheckResult response;
                    response.path = request.path;
                    response.count = 0;
                    response.accumulated = 0;

                    for (auto& option : all_options)
                        response.accumulated += option.second;

                    stream.send(packet(std::move(response)));
                    break;
                }
                else
                {
                    for (auto& progress_item : progress)
                    {
                        InternalModel::ProcessMediaCheckResult response;

                        response.path = request.path;
                        response.result_type = progress_item.second.result_type;
                        response.type_description = unchanged_options[progress_item.first];
                        response.type_description_refined = all_options[progress_item.first].first;
                        response.accumulated = all_options[progress_item.first].second;
                        response.count = progress_item.second.duration;
                        response.data_or_file = progress_item.second.data_or_file;

                        all_options[progress_item.first].second += response.count;

                        stream.send(packet(std::move(response)));
                    }
                }
            }
        }
        catch (...)
        {
            InternalModel::ProcessMediaCheckResult response;
            response.path = request.path;
            response.count = 0;
            response.accumulated = 0;

            for (auto& option : all_options)
                response.accumulated += option.second;

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



