#include "storage.hpp"
#include "common.hpp"

#include <mesh.pp/fileutility.hpp>
#include <mesh.pp/cryptoutility.hpp>

#include <belt.pp/utility.hpp>

#include <string>

namespace filesystem = boost::filesystem;
using std::string;
using std::unordered_set;

namespace cloudy
{

namespace detail
{
class storage_internals
{
public:
    storage_internals(filesystem::path const& path)
        : map("storage", path, 10000, get_storage_putl())
    {}

    meshpp::map_loader<StorageModel::StorageFile> map;
};
}

storage::storage(boost::filesystem::path const& fs_storage)
    : m_pimpl(new detail::storage_internals(fs_storage))
{}
storage::~storage()
{}

bool storage::put(StorageModel::StorageFile&& file, string& uri)
{
    bool code = false;
    uri = meshpp::hash(file.data);
    file.data = meshpp::to_base64(file.data, true);
    beltpp::on_failure guard([this]
    {
        m_pimpl->map.discard();
    });

    if (m_pimpl->map.insert(uri, file))
        code = true;

    m_pimpl->map.save();

    guard.dismiss();
    m_pimpl->map.commit();
    return code;
}

bool storage::get(string const& uri, StorageModel::StorageFile& file)
{
    if (false == m_pimpl->map.contains(uri))
        return false;

    file = m_pimpl->map.as_const().at(uri);

    file.data = meshpp::from_base64(file.data);

    if (beltpp::chance_one_of(1000))
        m_pimpl->map.discard();

    return true;
}

bool storage::remove(string const& uri)
{
    if (false == m_pimpl->map.contains(uri))
        return false;

    beltpp::on_failure guard([this]
    {
        m_pimpl->map.discard();
    });
    m_pimpl->map.erase(uri);
    m_pimpl->map.save();

    guard.dismiss();
    m_pimpl->map.commit();

    return true;
}

unordered_set<string> storage::get_file_uris() const
{
    return m_pimpl->map.keys();
}

}
