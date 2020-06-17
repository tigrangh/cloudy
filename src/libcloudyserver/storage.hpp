#pragma once

#include "global.hpp"
#include "storage_model.hpp"

#include <boost/filesystem/path.hpp>

#include <memory>
#include <unordered_set>

namespace cloudy
{

namespace detail
{
class storage_internals;
}

class storage
{
public:
    storage(boost::filesystem::path const& path,
            boost::filesystem::path const& path_binaries);
    ~storage();

    uint64_t put(StorageModel::StorageFile&& file, std::string& uri);
    uint64_t put_file(StorageModel::StorageFile&& file, std::string& uri);
    bool get(std::string const& uri, StorageModel::StorageFile& file);
    uint64_t remove(std::string const& uri);
    std::unordered_set<std::string> get_file_uris() const;
private:
    std::unique_ptr<detail::storage_internals> m_pimpl;
};

}
