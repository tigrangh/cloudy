#pragma once

#include "global.hpp"
#include "storage_model.hpp"

#include <belt.pp/packet.hpp>

#include <boost/filesystem/path.hpp>

#include <memory>
#include <unordered_set>
#include <string>
#include <vector>
#include <utility>

namespace AdminModel
{
class LibraryResponse;
}
namespace InternalModel
{
class ProcessMediaCheckRequest;
class ProcessMediaCheckResult;
}
namespace AdminModel
{
class IndexListResponse;
}

namespace cloudy
{

namespace detail
{
class library_internal;
}

class library
{
public:
    library(boost::filesystem::path const& path);
    ~library();

    void save();
    void commit() noexcept;
    void discard() noexcept;
    void clear();

    AdminModel::LibraryResponse list(std::vector<std::string> const& path) const;
    void add(InternalModel::ProcessMediaCheckResult&& progress_item,
             std::string const& uri,
             std::string const& sha256sum);
    std::unordered_set<std::string> delete_library(std::vector<std::string> const& path);

    bool index(std::vector<std::string>&& path, std::unordered_set<std::string>&& type_descriptions);
    std::vector<std::pair<std::vector<std::string>, std::unordered_set<std::string>>> process_index();
    std::unordered_set<std::string>
    process_index_store_hash(std::vector<std::string> const& path,
                             std::unordered_set<std::string> const& type_descriptions,
                             std::string const& sha256sum);
    std::string process_index_retrieve_hash(std::vector<std::string> const& path) const;
    void process_index_update(std::vector<std::string> const& path,
                              std::unordered_set<std::string> const& type_descriptions_find,
                              std::unordered_set<std::string> const& type_descriptions_replace);
    void process_index_done(std::vector<std::string> const& path,
                            std::unordered_set<std::string> const& type_descriptions);

    bool check(std::vector<std::string>&& path, std::unordered_set<std::string>&& type_descriptions);
    std::vector<InternalModel::ProcessMediaCheckRequest> process_check();

    std::unordered_set<std::string>
    process_check_done(InternalModel::ProcessMediaCheckResult&& item,
                       std::string const& uri);

    AdminModel::IndexListResponse list_index(std::string const& sha256sum) const;
    std::unordered_set<std::string> delete_index(std::string const& sha256sum,
                                                 std::vector<std::string> const& only_path = std::vector<std::string>());
private:
    std::unique_ptr<detail::library_internal> m_pimpl;
};

}
