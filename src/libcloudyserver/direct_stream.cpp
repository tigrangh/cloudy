#include "direct_stream.hpp"

#include "common.hpp"

namespace cloudy
{
using beltpp::stream;
using beltpp::packet;
using beltpp::queue;
using beltpp::event_handler;

class direct_stream : public stream
{
public:
    using peer_id = stream::peer_id;
    using packets = stream::packets;

    event_handler* eh;
    direct_channel* channel;
    peer_id peerid;

    direct_stream(peer_id const& _peerid,
                  event_handler& _eh,
                  direct_channel& _channel)
        : stream(_eh)
        , channel(&_channel)
        , peerid(_peerid)
    {
        std::lock_guard<std::mutex> lock(channel->mutex);
        auto insert_res =
                channel->streams.insert(std::make_pair(peerid, direct_channel::peer_eh_incoming_streams()));

        if (false == insert_res.second)
            throw std::logic_error("direct_stream: false == insert_res.second");
        auto& new_item = *insert_res.first;
        new_item.second.first = &_eh;

        for (auto& existing_item : channel->streams)
        {
            if (existing_item.first == peerid)
                continue;   //  this is self

            auto insert_res_existing = existing_item.second.second.insert(
                                            std::make_pair(peerid,
                                                           beltpp::queue<beltpp::packet>()));

            if (false == insert_res_existing.second)
                throw std::logic_error("direct_stream: false == insert_res_existing.second");
            insert_res_existing.first->second.push(beltpp::packet(beltpp::stream_join()));

            auto insert_res_new = new_item.second.second.insert(
                                      std::make_pair(existing_item.first,
                                                     beltpp::queue<beltpp::packet>()));

            if (false == insert_res_new.second)
                throw std::logic_error("direct_stream: false == insert_res_new.second");
            insert_res_new.first->second.push(beltpp::packet(beltpp::stream_join()));

            existing_item.second.first->wake();
            new_item.second.first->wake();
        }
    }
    virtual ~direct_stream()
    {
        std::lock_guard<std::mutex> lock(channel->mutex);
        auto it = channel->streams.begin();
        while (it != channel->streams.end())
        {
            auto& existing_item = *it;

            if (existing_item.first == peerid)
            {
                it = channel->streams.erase(it);    //  this is self
                continue;
            }

            existing_item.second.second[peerid].push(beltpp::packet(beltpp::stream_drop()));
            existing_item.second.first->wake();

            ++it;
        }
    }

    void prepare_wait() override {};
    void timer_action() override {};

    packets receive(peer_id& peer) override
    {
        std::lock_guard<std::mutex> lock(channel->mutex);

        packets result;

        auto it = channel->streams[peerid].second.begin();
        if (it != channel->streams[peerid].second.end())
        {
            peer = it->first;
            while (false == it->second.empty())
            {
                result.push_back(std::move(it->second.front()));
                it->second.pop();
            }

            it = channel->streams[peerid].second.erase(it);
        }

        return result;
    }

    void send(peer_id const& peer,
              packet&& package) override
    {
        std::lock_guard<std::mutex> lock(channel->mutex);

        if (peer == peerid)
            throw std::runtime_error("direct_stream::send: peer == peerid");

        auto it = channel->streams.find(peer);
        if (it == channel->streams.end())
            throw std::runtime_error("direct_stream::send: it = channel->streams.end()");
        it->second.second[peerid].push(std::move(package));
        it->second.first->wake();
    }
};

stream_ptr construct_direct_stream(beltpp::stream::peer_id const& peerid,
                                   beltpp::event_handler& eh,
                                   direct_channel& channel)
{
    beltpp::stream* p = new direct_stream(peerid, eh, channel);
    return stream_ptr(p);
}
}
