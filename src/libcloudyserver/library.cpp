#include "library.hpp"
#include "common.hpp"
#include "internal_model.hpp"
#include "admin_model.hpp"

#include <mesh.pp/fileutility.hpp>

#include <utility>
#include <unordered_map>
#include <algorithm>

using beltpp::packet;
namespace filesystem = boost::filesystem;
using std::string;
using std::vector;
using std::pair;
using std::unordered_set;
using std::unordered_map;

namespace cloudy
{
using namespace InternalModel;
namespace detail
{
class library_internal
{
public:
    library_internal(filesystem::path const& path)
        : library_tree("library_tree", path, 10000, get_internal_putl())
        , library_index("library_index", path, 10000, get_admin_putl())
        , pending_for_index(path / "pending_for_index.json")
        , pending_for_media_check(path / "pending_for_media_check.json")
    {}

    meshpp::map_loader<LibraryTree> library_tree;
    meshpp::map_loader<AdminModel::LibraryIndex> library_index;
    meshpp::file_loader<PendingForIndex,
                        &PendingForIndex::from_string,
                        &PendingForIndex::to_string> pending_for_index;
    meshpp::file_loader<PendingForMediaCheck,
                        &PendingForMediaCheck::from_string,
                        &PendingForMediaCheck::to_string> pending_for_media_check;
    unordered_set<string> processing_for_index;
    unordered_set<string> processing_for_check;
};
}

library::library(boost::filesystem::path const& path)
    : m_pimpl(new detail::library_internal(path))
{}
library::~library()
{}

void library::save()
{
    m_pimpl->pending_for_index.save();
    m_pimpl->pending_for_media_check.save();
    m_pimpl->library_tree.save();
    m_pimpl->library_index.save();
}
void library::commit() noexcept
{
    m_pimpl->pending_for_index.commit();
    m_pimpl->pending_for_media_check.commit();
    m_pimpl->library_tree.commit();
    m_pimpl->library_index.commit();
}
void library::discard() noexcept
{
    m_pimpl->pending_for_index.discard();
    m_pimpl->pending_for_media_check.commit();
    m_pimpl->library_tree.discard();
    m_pimpl->library_index.discard();
}
void library::clear()
{
    m_pimpl->pending_for_index->items.clear();
    m_pimpl->pending_for_media_check->items.clear();
    m_pimpl->library_tree.clear();
    m_pimpl->library_index.clear();
}

AdminModel::LibraryResponse library::list(vector<string> const& path) const
{
    AdminModel::LibraryResponse result;
    string path_string = join_path(path).first;

    if (m_pimpl->library_tree.contains(path_string))
    {
        auto const& item = m_pimpl->library_tree.as_const().at(path_string);

        for (auto const& name : item.names)
        {
            string path = path_string + "/" + name;

            auto const& child = m_pimpl->library_tree.as_const().at(path);

            if (false == child.checksums.empty())
            {
                AdminModel::LibraryItemFile file;
                file.name = name;

                for (auto const& checksum : child.checksums)
                    file.checksums.push_back(checksum);

                result.files.push_back(std::move(file));
            }

            if (false == child.names.empty())
            {
                AdminModel::LibraryItemDirectory dir;
                dir.name = name;

                result.directories.push_back(std::move(dir));
            }
        }
    }

    return result;
}

void library::add(AdminModel::MediaDescription&& media_description,
                  vector<string>&& path,
                  string const& checksum)
{
    string child;

    while (true)
    {
        auto jp = join_path(path);
        string path_string = jp.first;
        string last_name = jp.second;

        if (child.empty())
        {
            {
                LibraryTree item;
                item.checksums.insert(checksum);
                m_pimpl->library_tree.insert(path_string, item);
            }
            {
                LibraryTree& item = m_pimpl->library_tree.at(path_string);
                item.checksums.insert(checksum);
            }
            {
                AdminModel::LibraryIndex item;
                item.media_description = std::move(media_description);
                item.paths.insert(path_string);
                m_pimpl->library_index.insert(checksum, item);
            }
            {
                AdminModel::LibraryIndex& item = m_pimpl->library_index.at(checksum);
                item.paths.insert(path_string);
                // can also merge old and new media descriptions
            }
        }
        else
        {
            {
                LibraryTree item;
                item.names.insert(child);
                m_pimpl->library_tree.insert(path_string, item);
            }
            {
                LibraryTree& item = m_pimpl->library_tree.at(path_string);
                item.names.insert(child);
            }
        }
        child = last_name;

        if (path.empty())
            break;
        else
            path.pop_back();
    }
}

void library::index(vector<string>&& path)
{
    string path_string = join_path(path).first;

    PendingForIndexItem item;
    item.path = std::move(path);

    m_pimpl->pending_for_index->items.push_back(std::move(item));
}

vector<vector<string>> library::process_index()
{
    vector<vector<string>> result;
    if (false == m_pimpl->processing_for_index.empty())
        return result;

    for (auto const& item : m_pimpl->pending_for_index.as_const()->items)
    {
        if (m_pimpl->processing_for_index.size() == 2)
            break;
        auto insert_res = m_pimpl->processing_for_index.insert(join_path(item.path).first);

        if (insert_res.second)
            result.push_back(item.path);
    }

    return result;
}

void library::process_index_store_hash(std::vector<std::string> const& path, std::string const& sha256sum)
{
    auto& items = m_pimpl->pending_for_index->items;
    for (auto& item : items)
    {
        if (item.path == path)
            item.sha256sum = sha256sum;
    }
}
string library::process_index_retrieve_hash(vector<string> const& path) const
{
    auto& items = m_pimpl->pending_for_index->items;
    for (auto& item : items)
    {
        if (item.path == path)
            return item.sha256sum;
    }
    return string();
}
void library::process_index_done(vector<string> const& path)
{
    m_pimpl->processing_for_index.erase(join_path(path).first);

    auto& items = m_pimpl->pending_for_index->items;
    auto end_it = std::remove_if(items.begin(), items.end(), [&path](PendingForIndexItem const& item)
    {
        return path == item.path;
    });
    items.erase(end_it, items.end());
}

void library::check(vector<string>&& path)
{
    string path_string = join_path(path).first;

    MediaCheckProgress check;
    check.path = std::move(path);
    check.type = MediaType::video;
    check.types[MediaType::video].dimensions = std::vector<uint64_t>{720, 480, 360, 240, 144};
    check.types[MediaType::image].dimensions = std::vector<uint64_t>{720};

    auto& items = m_pimpl->pending_for_media_check->items;
    bool duplicate = false;
    for (auto const& item : items)
    {
        if (item.path == path)
        {
            duplicate = true;
            break;
        }
    }

    if (false == duplicate)
        items.push_back(check);
}

vector<ProcessCheckMediaRequest> library::process_check()
{
    vector<ProcessCheckMediaRequest> result;
    if (false == m_pimpl->processing_for_check.empty())
        return result;

    for (InternalModel::MediaCheckProgress const& item : m_pimpl->pending_for_media_check.as_const()->items)
    {
        if (m_pimpl->processing_for_check.size() == 2)
            break;
        auto insert_res = m_pimpl->processing_for_check.insert(join_path(item.path).first);

        if (insert_res.second)
        {
            ProcessCheckMediaRequest progress_item;

            auto it_type = item.types.find(item.type);
            if (it_type == item.types.end())
                throw std::logic_error("library::process_check: it_type == item.types.end()");
            auto const& dimensions = it_type->second.dimensions;
            if (dimensions.empty())
                throw std::logic_error("library::process_check: dimensions.empty()");
            auto const& overlays_per_type = it_type->second.overlays;

            auto it_dimension = dimensions.cbegin() + it_type->second.dimension_index;
            /*while (true)
            {
                if (overlays_per_type.count(*it_dimension) ||
                    (it_dimension + 1) == dimensions.crend())
                    break;
                ++it_dimension;
            }*/

            auto it_overlay = overlays_per_type.find(*it_dimension);

            progress_item.accumulated = 0;
            if (it_overlay != overlays_per_type.end() &&
                false == it_overlay->second.sequence.empty())
                progress_item.accumulated = it_overlay->second.sequence.back().count;
            progress_item.path = item.path;
            progress_item.dimension = dimensions[it_type->second.dimension_index];
            progress_item.type = item.type;

            result.push_back(progress_item);
        }
    }

    return result;
}

std::unique_ptr<AdminModel::MediaDescription>
library::process_check_done(ProcessCheckMediaRequest const& progress_item,
                            uint64_t count,
                            string const& uri)
{
    std::unique_ptr<AdminModel::MediaDescription> result;
    string string_path = join_path(progress_item.path).first;

    auto& items = m_pimpl->pending_for_media_check->items;

    auto it_item = items.begin();
    for (; it_item != items.end(); ++it_item)
    {
        InternalModel::MediaCheckProgress& item = *it_item;

        if (item.path == progress_item.path &&
            item.type == progress_item.type)
        {
            auto it_type = item.types.find(item.type);
            if (it_type == item.types.end())
                throw std::logic_error("library::process_check_done: it_type == item.types.end()");

            auto& progress_per_type = it_type->second.overlays;

            auto& dimensions = it_type->second.dimensions;
            /*auto it_dimension = dimensions.cbegin();
            for (; it_dimension != dimensions.cend(); ++it_dimension)
            {
                if (*it_dimension == progress_item.dimension)
                    break;
            }
            if (it_dimension == dimensions.cend())
                throw std::logic_error("library::process_check_done: it_dimension == dimensions.cend()");*/
            if (dimensions[it_type->second.dimension_index] != progress_item.dimension)
                throw std::logic_error("library::process_check_done: dimensions[it_type->second.dimension_index] != progress_item.dimension");

            uint64_t accumulated = 0;
            vector<MediaFrame>& sequence_progress = progress_per_type[progress_item.dimension].sequence;

            if (false == sequence_progress.empty())
                accumulated = sequence_progress.back().count;

            if (accumulated != progress_item.accumulated)
                throw std::logic_error("library::process_check_done: accumulated != progress_item.request.accumulated");

            if (false == uri.empty())
            {
                if (0 == count)
                    throw std::logic_error("library::process_check_done: 0 == count");

                MediaFrame frame;
                frame.uri = uri;
                frame.count = progress_item.accumulated + count;
                sequence_progress.push_back(frame);
            }
            //  this now means already that either uri is empty - that is an error already has happened
            //  or 0 == count - in this case it either needs to pass to next check, or there are no more checks
            else
            {
                if (it_type->second.dimension_index + 1 < dimensions.size())
                    ++it_type->second.dimension_index;
                else //if (it_type->second.dimension_index + 1 == dimensions.size())  //  canot switch to next dimension automatically
                {
                    if (uri.empty() && count)
                        item.type = MediaType::end_value; // force to end this media checking task

                    // try to switch to next type
                    while (item.type != MediaType::end_value)
                    {
                        uint64_t current_type = static_cast<uint64_t>(item.type);
                        ++current_type;

                        item.type = static_cast<MediaType>(current_type);

                        if (item.types.count(item.type))
                            break;
                    }

                    if (item.type == MediaType::end_value)
                    {   //  we're done with this item
                        result.reset(new AdminModel::MediaDescription());

                        for (auto& item_type : item.types)
                        {
                            bool type_appended = false;

                            for (auto& type_overlay : item_type.second.overlays)
                            {
                                if (false == type_overlay.second.sequence.empty())
                                {
                                    if (false == type_appended)
                                    {
                                        result->types.push_back(AdminModel::MediaTypeDescription());
                                        beltpp::assign(result->types.back().type, item_type.first);
                                        type_appended = true;
                                    }

                                    uint64_t dimension = type_overlay.first;
                                    for (auto&& frame : type_overlay.second.sequence)
                                    {
                                        AdminModel::MediaFrame new_frame;
                                        beltpp::assign(new_frame, std::move(frame));
                                        result->types.back().overlays[dimension].sequence.push_back(std::move(new_frame));
                                    }
                                }
                            }

                            type_appended = false;
                        }

                        items.erase(it_item);
                    }
                }
            }

            break;
        }
    }

    m_pimpl->processing_for_check.erase(string_path);

    return result;
}

bool library::indexed(string const& checksum) const
{
    return m_pimpl->library_index.contains(checksum);
}
AdminModel::IndexListResponse library::list_index(std::string const& sha256sum) const
{
    AdminModel::IndexListResponse result;

    if (sha256sum.empty())
    {
        auto keys = m_pimpl->library_index.keys();
        for (auto const& key : keys)
        {
            auto const& item = m_pimpl->library_index.as_const().at(key);
            result.list_index.insert(std::make_pair(key, item));
        }
    }
    else if (m_pimpl->library_index.contains(sha256sum))
    {
        result.list_index.insert(
                    std::make_pair(sha256sum,
                                   m_pimpl->library_index.as_const().at(sha256sum)));
    }

    return result;
}

}
