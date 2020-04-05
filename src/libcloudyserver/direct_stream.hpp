#pragma once

#include "global.hpp"

#include <belt.pp/ievent.hpp>
#include <belt.pp/stream.hpp>
#include <belt.pp/queue.hpp>
#include <belt.pp/packet.hpp>

#include <mutex>
#include <unordered_map>
#include <utility>

namespace cloudy
{

class direct_channel
{
public:
    using peer_incoming_streams =
        std::unordered_map<
            beltpp::stream::peer_id,
            beltpp::queue<beltpp::packet>
        >;
    using peer_eh_incoming_streams = std::pair<
        beltpp::event_handler*,
        peer_incoming_streams
        >;
    std::unordered_map<
        beltpp::stream::peer_id,
        peer_eh_incoming_streams
        > streams;
    std::mutex mutex;
};

using stream_ptr = beltpp::stream_ptr;
using event_handler_ptr = beltpp::event_handler_ptr;

CLOUDYSERVERSHARED_EXPORT stream_ptr construct_direct_stream(beltpp::stream::peer_id const& peerid,
                                                             beltpp::event_handler& eh,
                                                             direct_channel& channel);
}
