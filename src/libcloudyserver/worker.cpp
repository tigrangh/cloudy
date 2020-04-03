#include "worker.hpp"

#include "common.hpp"
#include "internal_model.hpp"

#include <belt.pp/packet.hpp>
#include <belt.pp/processor.hpp>

#include <string>
#include <memory>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <exception>
#include <mutex>
#include <thread>
#include <iostream>

using namespace InternalModel;

using beltpp::packet;
using peer_id = beltpp::stream::peer_id;

namespace chrono = std::chrono;
using chrono::system_clock;

using std::string;
using std::unordered_set;
using std::unique_ptr;

namespace cloudy
{

namespace detail
{

packet processor_worker(packet&& package)
{
    std::cout << "processing ...\n\n";
    std::this_thread::sleep_for(std::chrono::seconds(10));
    std::cout << "...processed\n\n";
    return packet();
}

class worker_internals
{
public:
    beltpp::ilog* plogger;
    event_handler_ptr ptr_eh;
    stream_ptr ptr_stream;
    stream_ptr ptr_stream_admin;
    wait_result wait_result_info;

    worker_internals(beltpp::ilog* _plogger,
                     direct_channel& channel)
        : plogger(_plogger)
        , ptr_eh(beltpp::libprocessor::construct_event_handler())
        , ptr_stream(beltpp::libprocessor::construct_processor(*ptr_eh, 2, &processor_worker))
        , ptr_stream_admin(cloudy::construct_direct_stream(*ptr_eh, channel))
    {
        //ptr_eh->set_timer(event_timer_period);

        ptr_eh->add(*ptr_stream);
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
                                                    m_pimpl->ptr_stream_admin.get());

    if (wait_result.et == detail::wait_result_item::event)
    {
        auto peerid = wait_result.peerid;
        auto received_packet = std::move(wait_result.packet);

        beltpp::stream* psk = m_pimpl->ptr_stream.get();
        B_UNUSED(psk);

        try
        {
            switch (received_packet.type())
            {
            default:
            {
                m_pimpl->writeln_node("peer: " + peerid);
                m_pimpl->writeln_node("worker can't handle: " + received_packet.to_string());

                //psk->send(peerid, beltpp::packet(beltpp::stream_drop()));
                break;
            }
            }   // switch ref_packet.type()

            m_pimpl->ptr_stream_admin->send("", packet());
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
    else if (m_pimpl->ptr_stream_admin && wait_result.et == detail::wait_result_item::on_demand)
    {
        auto received_packet = std::move(wait_result.packet);

        auto& stream = *m_pimpl->ptr_stream_admin;
        peer_id peerid;

        B_UNUSED(stream);

        try
        {
            m_pimpl->ptr_stream->send("", std::move(received_packet));
            m_pimpl->writeln_node("got message from admin");
//            switch (received_packet.type())
//            {
//            }
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



