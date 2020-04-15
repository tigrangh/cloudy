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

class transcoder
{
private:
    enum e_state {before_init, before_loop, done};

    e_state state = before_init;
    std::unique_ptr<transcoder_detail> pimpl;

    bool loop(std::unordered_map<std::string, std::string>& filename_to_type_definition);
    bool clean();
public:
    transcoder();
    ~transcoder();
    boost::filesystem::path input_file;
    boost::filesystem::path output_dir;

    bool init(std::vector<beltpp::packet>&& options);
    void run(std::unordered_map<std::string, std::string>& filename_to_type_definition);
};
}
