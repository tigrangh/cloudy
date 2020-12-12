#pragma once

#include "global.hpp"
#include "internal_model.hpp"

#include <belt.pp/ilog.hpp>
#include <belt.pp/direct_stream.hpp>

#include <boost/filesystem/path.hpp>

#include <memory>
#include <string>

namespace cloudy
{
namespace detail
{
    class worker_internals;
}

class work_unit
{
public:
    uint64_t duration = 0;
    std::string data_or_file;
    InternalModel::ResultType result_type;
};

class CLOUDYSERVERSHARED_EXPORT worker
{
public:
    worker(beltpp::ilog* plogger,
           boost::filesystem::path const& fs,
           beltpp::direct_channel& channel);
    worker(worker&& other) noexcept;
    ~worker();

    void wake();
    void run(bool& stop);

private:
    std::unique_ptr<detail::worker_internals> m_pimpl;
};

}

