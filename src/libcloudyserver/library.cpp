#include "library.hpp"
#include "common.hpp"
#include "internal_model.hpp"

#include <mesh.pp/fileutility.hpp>
#include <mesh.pp/cryptoutility.hpp>

#include <unordered_map>
#include <algorithm>

using beltpp::packet;
namespace filesystem = boost::filesystem;
using std::string;
using std::vector;
using std::pair;
using std::unordered_set;
using std::unordered_map;
using boost::optional;

namespace cloudy
{
using namespace InternalModel;
namespace detail
{
class library_internal
{
public:
    library_internal(filesystem::path const& path)
        : processing_for_check(false)
        , processing_for_index(0)
        , library_tree("library_tree", path, 10000, get_internal_putl())
        , library_index("library_index", path, 10000, get_admin_putl())
        , pending_for_index(path / "pending_for_index.json")
        , pending_for_media_check(path / "pending_for_media_check.json")
    {}

    bool processing_for_check;
    uint64_t processing_for_index;
    meshpp::map_loader<LibraryTree> library_tree;
    meshpp::map_loader<AdminModel::LibraryIndex> library_index;
    meshpp::file_loader<PendingForIndex,
                        &PendingForIndex::from_string,
                        &PendingForIndex::to_string> pending_for_index;
    meshpp::file_loader<PendingForMediaCheck,
                        &PendingForMediaCheck::from_string,
                        &PendingForMediaCheck::to_string> pending_for_media_check;
};
}

library::library(boost::filesystem::path const& path)
    : m_pimpl(new detail::library_internal(path))
{
    for (auto const& item : m_pimpl->pending_for_index.as_const()->items)
    {
        if (false == item.sha256sum.empty())
            ++m_pimpl->processing_for_index;
        else
            break;
    }
}
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

        if (item.names)
        for (auto const& name : *item.names)
        {
            string path = path_string + "/" + name;

            auto const& child = m_pimpl->library_tree.as_const().at(path);

            if (child.checksum)
            {
                AdminModel::FileItem file;
                file.name = name;
                file.checksum = *child.checksum;

                result.lib_files.push_back(std::move(file));
            }
            else if (child.names)
            {
                AdminModel::DirectoryItem dir;
                dir.name = name;

                result.lib_directories.push_back(std::move(dir));
            }
            else
                throw std::logic_error("library::list: empty library tree child");
            
        }
    }

    return result;
}

packet library::info(vector<string> const& path) const
{
    string path_string = join_path(path).first;

    if (false == m_pimpl->library_tree.contains(path_string))
        return packet();

    auto const& item = m_pimpl->library_tree.as_const().at(path_string);
    if (item.checksum)
    {
        AdminModel::FileItem file;
        file.checksum = *item.checksum;

        return packet(std::move(file));
    }
    
    if (item.names)
        return packet(AdminModel::DirectoryItem());
        
    throw std::logic_error("library::info: empty library tree child");
}

void library::add(ProcessMediaCheckResult&& progress_item,
                  string const& uri,
                  string const& sha256sum)
{
    string child;

    if (sha256sum.empty())
        throw std::logic_error("library::add: sha256sum.empty()");

    while (true)
    {
        auto jp = join_path(progress_item.path);
        string path_string = jp.first;
        string last_name = jp.second;

        if (child.empty())
        {
            m_pimpl->library_tree.insert(path_string, LibraryTree());

            LibraryTree& tree_item = m_pimpl->library_tree.at(path_string);
            if (tree_item.checksum && *tree_item.checksum != sha256sum)
                throw std::logic_error("library::add: tree_item.checksum && *tree_item.checksum != sha256sum");
            tree_item.checksum = sha256sum;

            m_pimpl->library_index.insert(sha256sum, AdminModel::LibraryIndex());

            AdminModel::LibraryIndex& index_item = m_pimpl->library_index.at(sha256sum);
            unordered_set<string> set_existing_paths;
            for (auto const& existing_path_item : index_item.paths)
            {
                string str_path_item = join_path(existing_path_item).first;
                set_existing_paths.insert(str_path_item);
            }

            if (0 == set_existing_paths.count(path_string))
                index_item.paths.push_back(progress_item.path);

            if (progress_item.type_description)
            {
                vector<AdminModel::MediaTypeDefinition>& type_definitions = index_item.type_definitions;

                AdminModel::MediaTypeDefinition* media_type_definition = nullptr;
                for (auto& item : type_definitions)
                {
                    if (item.type_description == progress_item.type_description)
                    {
                        media_type_definition = &item;
                        break;
                    }
                }
                if (nullptr == media_type_definition)
                {
                    AdminModel::MediaTypeDefinition temp_item;
                    temp_item.type_description = std::move(*progress_item.type_description);

                    type_definitions.push_back(std::move(temp_item));
                    media_type_definition = &type_definitions.back();
                }

                uint64_t accumulated = 0;
                auto& frames = media_type_definition->sequence.frames;
                if (false == frames.empty())
                    accumulated = frames.back().count;

                if (accumulated < progress_item.accumulated)
                    throw std::logic_error("library::add: accumulated < progress_item.request.accumulated");

                AdminModel::MediaFrame frame;
                frame.uri = uri;
                frame.count = progress_item.accumulated + progress_item.count;
                frames.push_back(frame);
            }
        }
        else
        {
            m_pimpl->library_tree.insert(path_string, LibraryTree());

            LibraryTree& item = m_pimpl->library_tree.at(path_string);
            if (!item.names)
                item.names = unordered_set<string>();
            item.names->insert(child);
        }
        child = last_name;

        if (progress_item.path.empty())
            break;
        else
            progress_item.path.pop_back();
    }
}
vector<string> library::delete_library(vector<string> const& path)
{
    vector<string> uris;
    string path_string = join_path(path).first;

    if (m_pimpl->library_tree.contains(path_string))
    {
        auto const& item = m_pimpl->library_tree.as_const().at(path_string);

        if (item.checksum)
        {
            auto uris_local = delete_index(*item.checksum, path);
            for (auto const& uri : uris_local)
                uris.push_back(uri);
        }
    }

    return uris;
}

bool library::index(vector<string>&& path,
                    std::unordered_set<AdminModel::MediaTypeDescriptionVariant>&& type_descriptions)
{
    string path_string = join_path(path).first;

    PendingForIndexItem item;
    item.path = std::move(path);
    item.type_descriptions = std::move(type_descriptions);
#if 0
    if (false)
    {
        AdminModel::MediaTypeDescriptionVideoFilter video_filter;
        video_filter.height = 720;
        video_filter.width = 1280;
        video_filter.fps = 15;

        AdminModel::MediaTypeDescriptionAVStreamTranscode video_transcode;
        video_transcode.filter.set(std::move(video_filter));
        video_transcode.codec = "libx265";
        video_transcode.codec_priv_key = "x265-params";
        video_transcode.codec_priv_value = "keyint=60:min-keyint=60:scenecut=0";

        AdminModel::MediaTypeDescriptionAVStream video;
        video.transcode.set(std::move(video_transcode));

        AdminModel::MediaTypeDescriptionAVStreamTranscode audio_transcode;
        //audio_transcode.codec = "???";

        AdminModel::MediaTypeDescriptionAVStream audio;
        //audio.transcode.set(std::move(audio_transcode));

        AdminModel::MediaTypeDescriptionVideoContainer container;
        container.video.set(std::move(video));
        container.audio.set(std::move(audio));
        container.container_extension = "mp4";

        item.type_descriptions.insert(container.to_string());
    }
    {
        AdminModel::MediaTypeDescriptionVideoFilter video_filter;
        video_filter.height = 1080;
        video_filter.width = 1920;
        video_filter.fps = 29;

        AdminModel::MediaTypeDescriptionAVStreamTranscode video_transcode;
        video_transcode.filter.set(std::move(video_filter));
        video_transcode.codec = "libx264";
        video_transcode.codec_priv_key = "";
        video_transcode.codec_priv_value = "";

        AdminModel::MediaTypeDescriptionAVStream video;
        video.transcode.set(std::move(video_transcode));

        AdminModel::MediaTypeDescriptionAVStreamTranscode audio_transcode;
        audio_transcode.codec = "aac";

        AdminModel::MediaTypeDescriptionAVStream audio;
        audio.transcode.set(std::move(audio_transcode));

        AdminModel::MediaTypeDescriptionVideoContainer container;
        container.video.set(std::move(video));
        container.audio.set(std::move(audio));
        container.container_extension = "mp4";

        item.type_descriptions.insert(container.to_string());
    }
    {
        AdminModel::MediaTypeDescriptionVideoFilter video_filter;
        video_filter.height = 720;
        video_filter.width = 1280;
        video_filter.fps = 29;

        AdminModel::MediaTypeDescriptionAVStreamTranscode video_transcode;
        video_transcode.filter.set(std::move(video_filter));
        video_transcode.codec = "libx264";
        video_transcode.codec_priv_key = "";
        video_transcode.codec_priv_value = "";

        AdminModel::MediaTypeDescriptionAVStream video;
        video.transcode.set(std::move(video_transcode));

        AdminModel::MediaTypeDescriptionAVStreamTranscode audio_transcode;
        audio_transcode.codec = "aac";

        AdminModel::MediaTypeDescriptionAVStream audio;
        audio.transcode.set(std::move(audio_transcode));

        AdminModel::MediaTypeDescriptionVideoContainer container;
        container.video.set(std::move(video));
        container.audio.set(std::move(audio));
        container.container_extension = "mp4";

        item.type_descriptions.insert(container.to_string());
    }
    {
        AdminModel::MediaTypeDescriptionVideoFilter video_filter;
        video_filter.height = 360;
        video_filter.width = 640;
        video_filter.fps = 29;

        AdminModel::MediaTypeDescriptionAVStreamTranscode video_transcode;
        video_transcode.filter.set(std::move(video_filter));
        video_transcode.codec = "libx264";
        video_transcode.codec_priv_key = "";
        video_transcode.codec_priv_value = "";

        AdminModel::MediaTypeDescriptionAVStream video;
        video.transcode.set(std::move(video_transcode));

        AdminModel::MediaTypeDescriptionAVStreamTranscode audio_transcode;
        audio_transcode.codec = "aac";

        AdminModel::MediaTypeDescriptionAVStream audio;
        audio.transcode.set(std::move(audio_transcode));

        AdminModel::MediaTypeDescriptionVideoContainer container;
        container.video.set(std::move(video));
        container.audio.set(std::move(audio));
        container.container_extension = "mp4";

        item.type_descriptions.insert(container.to_string());
    }
#endif

    auto& pending_items = m_pimpl->pending_for_index->items;
    for (auto const& pending_item : pending_items)
    {
        if (pending_item.path == item.path)
        {
            for (auto const& pending_item_type : pending_item.type_descriptions)
                item.type_descriptions.erase(pending_item_type);
        }
    }

    if (item.type_descriptions.empty())
        return false;

    pending_items.push_back(std::move(item));

    return true;
}

vector<pair<vector<string>, unordered_set<AdminModel::MediaTypeDescriptionVariant>>> library::process_index()
{
    vector<pair<vector<string>, unordered_set<AdminModel::MediaTypeDescriptionVariant>>> result;

    auto const& pending_items = m_pimpl->pending_for_index.as_const()->items;
    for (size_t index = 0; index != pending_items.size(); ++index)
    {
        if (m_pimpl->processing_for_index == 3)
            break;

        if (index == m_pimpl->processing_for_index)
        {
            ++m_pimpl->processing_for_index;

            result.push_back(std::make_pair(pending_items[index].path,
                                            pending_items[index].type_descriptions));
        }
    }

    return result;
}

unordered_set<AdminModel::MediaTypeDescriptionVariant> library::process_index_store_hash(vector<string> const& path,
                                                                                         unordered_set<AdminModel::MediaTypeDescriptionVariant> const& type_descriptions,
                                                                                         string const& sha256sum)
{
    auto type_descriptions_temp = type_descriptions;

    auto& items = m_pimpl->pending_for_index->items;
    for (size_t index = 0;
         index != m_pimpl->processing_for_index &&
         index != items.size();
         ++index)
    {
        auto& item = items[index];

        if (item.path == path &&
            item.type_descriptions == type_descriptions)
            item.sha256sum = sha256sum;
        else if (item.sha256sum == sha256sum)
        {
            for (auto const& type : item.type_descriptions)
                type_descriptions_temp.erase(type);
        }
    }

    return type_descriptions_temp;
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

void library::process_index_update(vector<string> const& path,
                                   unordered_set<AdminModel::MediaTypeDescriptionVariant> const& type_descriptions_find,
                                   unordered_set<AdminModel::MediaTypeDescriptionVariant> const& type_descriptions_replace)
{
    auto& items = m_pimpl->pending_for_index->items;
    for (size_t index = 0;
         index != m_pimpl->processing_for_index &&
         index != items.size();
         ++index)
    {
        auto& item = items[index];
        if (item.path == path &&
            item.type_descriptions == type_descriptions_find)
        {
            item.type_descriptions = type_descriptions_replace;
            return;
        }
    }

    throw std::logic_error("library::process_index_update");
}

void library::process_index_done(vector<string> const& path,
                                 unordered_set<AdminModel::MediaTypeDescriptionVariant> const& type_descriptions)
{
    --m_pimpl->processing_for_index;

    auto& items = m_pimpl->pending_for_index->items;
    for (size_t index = 0; index != items.size(); ++index)
    {
        auto& item = items[index];
        if (item.path == path &&
            item.type_descriptions == type_descriptions)
        {
            items.erase(items.begin() + index);
            return;
        }
    }

    throw std::logic_error("library::process_index_done");
}

bool library::check(vector<string>&& path, unordered_set<AdminModel::MediaTypeDescriptionVariant> const& type_descriptions)
{
    auto type_descriptions_temp = type_descriptions;

    auto& pending_items = m_pimpl->pending_for_media_check->items;
    for (auto const& pending_item : pending_items)
    {
        if (pending_item.path == path)
        {
            for (auto const& pending_item_type : pending_item.type_descriptions)
                type_descriptions_temp.erase(pending_item_type);
        }
    }

    if (type_descriptions_temp.empty())
        return false;

    ProcessMediaCheckRequest check;
    check.path = std::move(path);
    check.type_descriptions = std::move(type_descriptions_temp);
    process_index_update(check.path, type_descriptions, check.type_descriptions);
    pending_items.push_back(check);

    return true;
}

vector<ProcessMediaCheckRequest> library::process_check()
{
    vector<ProcessMediaCheckRequest> result;

    for (InternalModel::ProcessMediaCheckRequest const& item : m_pimpl->pending_for_media_check.as_const()->items)
    {
        if (m_pimpl->processing_for_check)
            break;
        m_pimpl->processing_for_check = true;

        result.push_back(item);
    }

    return result;
}

unordered_set<AdminModel::MediaTypeDescriptionVariant>
library::process_check_done(ProcessMediaCheckResult&& progress_item,
                            string const& uri)
{
    string string_path = join_path(progress_item.path).first;

    auto& items = m_pimpl->pending_for_media_check->items;

    auto it_item = items.begin();
    //for (; it_item != items.end(); ++it_item)
    if (it_item != items.end())
    {
        InternalModel::ProcessMediaCheckRequest& item = *it_item;

        if (item.path == progress_item.path)
        {
            if (0 == progress_item.count)
            {
                auto result = std::move(item.type_descriptions);
                items.erase(it_item);
                m_pimpl->processing_for_check = false;

                return result;
            }
            else
            {
                string sha256sum = process_index_retrieve_hash(progress_item.path);

                add(std::move(progress_item), uri, sha256sum);

                return unordered_set<AdminModel::MediaTypeDescriptionVariant>();
            }
        }
    }

    throw std::logic_error("library::process_check_done: pending item not found");
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

vector<string> library::delete_index(string const& sha256sum_,
                                     vector<string> const& only_path)
{
    vector<string> uris;
    auto sha256sum = sha256sum_;

    if (m_pimpl->library_index.contains(sha256sum))
    {
        AdminModel::LibraryIndex& index_item = m_pimpl->library_index.at(sha256sum);

        for (size_t path_index = index_item.paths.size() - 1;
             path_index < index_item.paths.size();
             --path_index)
        {
            auto& index_item_path = index_item.paths[path_index];

            if (false == only_path.empty() && index_item_path != only_path)
                continue;

            string child;

            while (true)
            {
                string item_path_string = join_path(index_item_path).first;

                if (m_pimpl->library_tree.contains(item_path_string))
                {
                    auto& item = m_pimpl->library_tree.at(item_path_string);

                    if (child.empty())
                    {
                        if (!item.checksum || sha256sum != *item.checksum)
                            throw std::logic_error("library::delete_index: !item.checksum || sha256sum != *item.checksum");

                        item.checksum = optional<string>();
                    }
                    else
                    {
                        if (!item.names)
                            throw std::logic_error("library::delete_index: !item.names");

                        size_t erased = item.names->erase(child);
                        if (0 == erased)
                            throw std::logic_error("library::delete_index: 0 == erased");
                        
                        if (item.names->empty())
                            item.names = optional<unordered_set<string>>();
                    }

                    if (item.checksum)
                        throw std::logic_error("library::delete_index: item.checksum");

                    if (!item.names)
                    {
                        m_pimpl->library_tree.erase(item_path_string);

                        if (index_item_path.empty())
                            break;

                        child = index_item_path.back();
                        index_item_path.pop_back();
                    }
                    else
                        break;
                }
            }

            index_item.paths.erase(index_item.paths.begin() + path_index);
        }

        if (index_item.paths.empty())
        {
            for (auto& type_definition : index_item.type_definitions)
            {
                for (auto& frame : type_definition.sequence.frames)
                    uris.push_back(frame.uri);
            }

            m_pimpl->library_index.erase(sha256sum);
        }
    }

    return uris;
}

}
