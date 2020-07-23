#pragma once

#include "global.hpp"

#include <belt.pp/isocket.hpp>
#include <belt.pp/ilog.hpp>
#include <belt.pp/direct_stream.hpp>
#include <mesh.pp/cryptoutility.hpp>

#include <boost/filesystem/path.hpp>

#include <memory>

namespace cloudy
{
namespace detail
{
    class storage_server_internals;
}

class CLOUDYSERVERSHARED_EXPORT storage_server
{
public:
    storage_server(beltpp::ip_address const& bind_to_address,
                   boost::filesystem::path const& path,
                   boost::filesystem::path const& path_binaries,
                   meshpp::public_key const& pb_key,
                   beltpp::ilog* plogger,
                   beltpp::direct_channel& channel);
    storage_server(storage_server&& other) noexcept;
    ~storage_server();

    void wake();
    void run(bool& stop);

private:
    std::unique_ptr<detail::storage_server_internals> m_pimpl;
};

}

