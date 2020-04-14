#pragma once
#include "global.hpp"

#include <belt.pp/packet.hpp>

#include <string>
#include <vector>
#include <memory>

namespace libavwrapper
{
class transcoder_detail;

class transcoder
{
private:
    enum e_state {before_init, before_loop, done};

    e_state state = before_init;
    std::unique_ptr<transcoder_detail> pimpl;

    bool loop(size_t& count);
    bool clean();
public:
    transcoder();
    ~transcoder();
    std::string from;
    std::string to;

    bool init(std::vector<beltpp::packet>&& options);
    size_t run();
};
}
