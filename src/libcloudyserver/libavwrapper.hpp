#pragma once
#include "global.hpp"

#include <belt.pp/packet.hpp>

#include <boost/filesystem/path.hpp>

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace libavwrapper
{
class transcoder_detail;

class media_part
{
public:
    size_t count = 0;
    std::string type_definition;
};

class transcoder
{
private:
    enum e_state {before_init, before_loop, done};

    e_state state = before_init;
    std::unique_ptr<transcoder_detail> pimpl;

    bool loop(std::unordered_map<std::string, media_part>& filename_to_media_part);
    bool clean();
public:
    transcoder();
    ~transcoder();
    boost::filesystem::path input_file;
    boost::filesystem::path output_dir;

    bool init(std::vector<beltpp::packet>&& options);
    void run(std::unordered_map<std::string, media_part>& filename_to_media_part);
};
}
