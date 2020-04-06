#pragma once

#include "global.hpp"

#include "direct_stream.hpp"

#include <belt.pp/isocket.hpp>
#include <belt.pp/ilog.hpp>
#include <mesh.pp/cryptoutility.hpp>

#include <boost/filesystem/path.hpp>

#include <memory>

namespace cloudy
{
namespace detail
{
    class admin_server_internals;
}

class CLOUDYSERVERSHARED_EXPORT admin_server
{
public:
    admin_server(beltpp::ip_address const& bind_to_address,
                 boost::filesystem::path const& fs_catalogue_tree,
                 boost::filesystem::path const& fs_admin,
                 meshpp::private_key const& pv_key,
                 beltpp::ilog* plogger,
                 direct_channel& channel);
    admin_server(admin_server&& other) noexcept;
    ~admin_server();

    void wake();
    void run(bool& stop);

private:
    std::unique_ptr<detail::admin_server_internals> m_pimpl;
};

}

