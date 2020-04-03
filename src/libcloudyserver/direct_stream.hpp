#pragma once

#include "global.hpp"

#include <belt.pp/ievent.hpp>
#include <belt.pp/stream.hpp>
#include <belt.pp/queue.hpp>
#include <belt.pp/packet.hpp>

#include <mutex>

namespace cloudy
{

class direct_channel
{
public:
    enum class participant {one, two, none};

    participant taker = participant::one;

    beltpp::event_handler* eh_one = nullptr;
    beltpp::event_handler* eh_two = nullptr;

    beltpp::queue<beltpp::packet> one;
    beltpp::queue<beltpp::packet> two;
    std::mutex mutex;
};

using stream_ptr = beltpp::stream_ptr;
using event_handler_ptr = beltpp::event_handler_ptr;

CLOUDYSERVERSHARED_EXPORT stream_ptr construct_direct_stream(beltpp::event_handler& eh,
                                                             direct_channel& channel);
}
