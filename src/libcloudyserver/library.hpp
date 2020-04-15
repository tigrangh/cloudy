#pragma once

#include "global.hpp"
#include "storage_model.hpp"

#include <belt.pp/packet.hpp>

#include <boost/filesystem/path.hpp>

#include <memory>
#include <unordered_set>
#include <string>
#include <vector>

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
class MediaDefinition;
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
             std::string const& uri);

    void index(std::vector<std::string>&& path);
    std::vector<std::vector<std::string>> process_index();
    void process_index_store_hash(std::vector<std::string> const& path, std::string const& sha256sum);
    std::string process_index_retrieve_hash(std::vector<std::string> const& path) const;
    void process_index_done(std::vector<std::string> const& path);

    bool check(std::vector<std::string>&& path);
    std::vector<InternalModel::ProcessMediaCheckRequest> process_check();

    bool
    process_check_done(InternalModel::ProcessMediaCheckResult&& item,
                       std::string const& uri);

    bool indexed(std::string const& checksum) const;
    AdminModel::IndexListResponse list_index(std::string const& sha256sum) const;
private:
    std::unique_ptr<detail::library_internal> m_pimpl;
};

}