#pragma once

#include "global.hpp"

#include "direct_stream.hpp"

#include <belt.pp/ilog.hpp>

#include <boost/filesystem/path.hpp>

#include <memory>

namespace cloudy
{
namespace detail
{
    class worker_internals;
}

class CLOUDYSERVERSHARED_EXPORT worker
{
public:
    worker(beltpp::ilog* plogger,
           boost::filesystem::path const& fs,
           direct_channel& channel);
    worker(worker&& other) noexcept;
    ~worker();

    void wake();
    void run(bool& stop);

private:
    std::unique_ptr<detail::worker_internals> m_pimpl;
};

}

