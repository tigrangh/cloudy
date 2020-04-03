#include "direct_stream.hpp"

#include "common.hpp"

#include <unordered_set>

namespace cloudy
{
using beltpp::stream;
using beltpp::packet;
using beltpp::queue;
using beltpp::event_handler;

/*class direct_stream_event_handler : public event_handler
{
public:
    using wait_result = beltpp::event_handler::wait_result;

    direct_stream_event_handler() {}
    virtual ~direct_stream_event_handler() {}

    wait_result wait(std::unordered_set<beltpp::event_item const*>&) override
    {
        return nothing;
    }
    std::unordered_set<uint64_t> waited(beltpp::event_item&) const override
    {
        return std::unordered_set<uint64_t>();
    }

    void wake() override {};
    void set_timer(std::chrono::steady_clock::duration const&) override {};

    virtual void add(beltpp::event_item&) override {};
    virtual void remove(beltpp::event_item&) override {};
};*/

struct streams
{
    queue<packet>* incoming = nullptr;
    queue<packet>* outgoing = nullptr;
};

inline streams init(direct_channel& channel,
                    event_handler& eh)
{
    if (channel.taker == direct_channel::participant::none)
        throw std::logic_error("channel.taker == direct_channel::participant::none");

    streams result;

    if (channel.taker == direct_channel::participant::one)
    {
        result.incoming = &channel.one;
        result.outgoing = &channel.two;

        channel.eh_one = &eh;
    }
    else
    {
        result.incoming = &channel.two;
        result.outgoing = &channel.one;

        channel.eh_two = &eh;
    }

    switch (channel.taker)
    {
    case direct_channel::participant::one:
        channel.taker = direct_channel::participant::two;
        break;
    default:
        channel.taker = direct_channel::participant::none;
        break;
    }

    return result;
}

class direct_stream : public stream
{
public:
    using peer_id = stream::peer_id;
    using packets = stream::packets;

    event_handler* eh;
    direct_channel* channel;
    streams streams_incoming_outgoing;

    direct_stream(event_handler& _eh,
                  direct_channel& _channel)
        : stream(_eh)
        , eh(&_eh)
        , channel(&_channel)
        , streams_incoming_outgoing(init(_channel, _eh))
    {
    }
    virtual ~direct_stream() {}

    void prepare_wait() override {};
    void timer_action() override {};

    packets receive(peer_id& peer) override
    {
        std::lock_guard<std::mutex> lock(channel->mutex);

        packets result;

        while (false == streams_incoming_outgoing.incoming->empty())
        {
            result.push_back(std::move(streams_incoming_outgoing.incoming->front()));
            streams_incoming_outgoing.incoming->pop();
        }

        return result;
    }

    void send(peer_id const& peer,
              packet&& package) override
    {
        std::lock_guard<std::mutex> lock(channel->mutex);

        streams_incoming_outgoing.outgoing->push(std::move(package));

        (channel->eh_one == eh ? channel->eh_two : channel->eh_one)->wake();
    }
};

stream_ptr construct_direct_stream(beltpp::event_handler& eh,
                                   direct_channel& channel)
{
    beltpp::stream* p = new direct_stream(eh, channel);
    return stream_ptr(p);
}
}
