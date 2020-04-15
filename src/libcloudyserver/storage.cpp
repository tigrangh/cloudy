#include "storage.hpp"
#include "common.hpp"

#include <mesh.pp/fileutility.hpp>
#include <mesh.pp/cryptoutility.hpp>

#include <belt.pp/utility.hpp>

#include <boost/filesystem.hpp>

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
    storage_internals(filesystem::path const& path,
                      filesystem::path const& _path_binaries)
        : map("storage", path, 10000, get_storage_putl())
        , path_binaries(_path_binaries)
    {}

    meshpp::map_loader<StorageModel::StorageFile> map;
    filesystem::path path_binaries;
};
}

storage::storage(filesystem::path const& path,
                 filesystem::path const& path_binaries)
    : m_pimpl(new detail::storage_internals(path, path_binaries))
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

bool storage::put_file(StorageModel::StorageFile&& file, string& uri)
{
    bool code = false;

    std::istreambuf_iterator<char> end, begin;
    filesystem::ifstream fl;
    filesystem::path path(file.data);

    meshpp::load_file(path, fl, begin, end);
    if (begin == end)
        throw std::runtime_error(file.data + ": empty or does not exist, cannot store");

    string file_contents(begin, end);

    if (file_contents.size() < 10 * 1024 * 1024)
    {
        file.data = std::move(file_contents);
        code = put(std::move(file), uri);

        boost::system::error_code ec;
        if (false == filesystem::remove(path, ec) || ec)
            throw std::logic_error("storage::put_file: filesystem::remove(path, ec)");

        return code;
    }

    uri = meshpp::hash(file_contents);

    if (false == m_pimpl->map.contains(uri))
    {
        code = true;

        filesystem::path new_location = m_pimpl->path_binaries / uri;

        boost::system::error_code ec;
        filesystem::rename(path, new_location, ec);
        if (ec)
            throw std::logic_error("storage::put_file: filesystem::rename(path, new_location, ec)");

        file.data = ":PATH_URI:";

        beltpp::on_failure guard([this]
        {
            m_pimpl->map.discard();
        });

        m_pimpl->map.insert(uri, file);

        m_pimpl->map.save();

        guard.dismiss();
        m_pimpl->map.commit();
    }

    return code;
}

bool storage::get(string const& uri, StorageModel::StorageFile& file)
{
    if (false == m_pimpl->map.contains(uri))
        return false;

    file = m_pimpl->map.as_const().at(uri);

    if (file.data == ":PATH_URI:")
    {
        std::istreambuf_iterator<char> end, begin;
        filesystem::ifstream fl;
        filesystem::path path(m_pimpl->path_binaries / uri);

        meshpp::load_file(path, fl, begin, end);
        if (begin == end)
            throw std::logic_error(file.data + ": storage::get: empty or does not exist");

        file.data.assign(begin, end);
    }
    else
    {
        file.data = meshpp::from_base64(file.data);
    }

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

    filesystem::path path(m_pimpl->path_binaries / uri);
    filesystem::remove(path);

    guard.dismiss();
    m_pimpl->map.commit();

    return true;
}

unordered_set<string> storage::get_file_uris() const
{
    return m_pimpl->map.keys();
}

}
