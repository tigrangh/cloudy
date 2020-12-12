#pragma once
#include "global.hpp"
#include "admin_model.hpp"
#include "worker.hpp"

#include <belt.pp/packet.hpp>

#include <boost/filesystem/path.hpp>

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <utility>

namespace InternalModel
{
class ProcessMediaCheckResult;
}
namespace libavwrapper
{
class transcoder_detail;

class transcoder
{
private:
    enum e_state {before_init, before_loop, done};

    e_state state = before_init;
    std::unique_ptr<transcoder_detail> pimpl;

    std::unordered_map<size_t, cloudy::work_unit> loop();
    bool clean();
public:
    transcoder();
    ~transcoder();
    boost::filesystem::path input_file;
    boost::filesystem::path output_dir;

    bool init(std::vector<std::pair<AdminModel::MediaTypeDescriptionVariant, size_t>>& options);
    std::unordered_map<size_t, cloudy::work_unit> run();
};
}
