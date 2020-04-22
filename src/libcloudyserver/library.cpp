#include "library.hpp"
#include "common.hpp"
#include "internal_model.hpp"
#include "admin_model.hpp"

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
            tree_item.checksums.insert(sha256sum);

            m_pimpl->library_index.insert(sha256sum, AdminModel::LibraryIndex());

            AdminModel::LibraryIndex& index_item = m_pimpl->library_index.at(sha256sum);
            index_item.paths.insert(path_string);

            if (false == progress_item.type.empty())
            {
                AdminModel::MediaDefinition& media_definition = index_item.media_definition;

                AdminModel::MediaTypeDefinition* media_type_definition = nullptr;
                for (auto& item : media_definition.types_definitions)
                {
                    if (item.type.to_string() == progress_item.type.to_string())
                    {
                        media_type_definition = &item;
                        break;
                    }
                }
                if (nullptr == media_type_definition)
                {
                    media_definition.types_definitions.push_back(AdminModel::MediaTypeDefinition());
                    media_type_definition = &media_definition.types_definitions.back();
                    media_type_definition->type = std::move(progress_item.type);
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
            item.names.insert(child);
        }
        child = last_name;

        if (progress_item.path.empty())
            break;
        else
            progress_item.path.pop_back();
    }
}

bool library::index(vector<string>&& path)
{
    string path_string = join_path(path).first;

    PendingForIndexItem item;
    item.path = std::move(path);
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

        item.types_definitions.insert(container.to_string());
    }
    {
        AdminModel::MediaTypeDescriptionVideoFilter video_filter;
        video_filter.height = 1080;
        video_filter.width = 1920;
        video_filter.fps = 29;

        AdminModel::MediaTypeDescriptionAVStreamTranscode video_transcode;
        video_transcode.filter.set(std::move(video_filter));
        video_transcode.codec = "libx264";
        video_transcode.codec_priv_key = "x264-params";
        video_transcode.codec_priv_value = "keyint=60:min-keyint=60:scenecut=0:force-cfr=1";

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

        item.types_definitions.insert(container.to_string());
    }
    {
        AdminModel::MediaTypeDescriptionVideoFilter video_filter;
        video_filter.height = 720;
        video_filter.width = 1280;
        video_filter.fps = 29;

        AdminModel::MediaTypeDescriptionAVStreamTranscode video_transcode;
        video_transcode.filter.set(std::move(video_filter));
        video_transcode.codec = "libx264";
        video_transcode.codec_priv_key = "x264-params";
        video_transcode.codec_priv_value = "keyint=60:min-keyint=60:scenecut=0:force-cfr=1";

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

        item.types_definitions.insert(container.to_string());
    }
    {
        AdminModel::MediaTypeDescriptionVideoFilter video_filter;
        video_filter.height = 360;
        video_filter.width = 640;
        video_filter.fps = 29;

        AdminModel::MediaTypeDescriptionAVStreamTranscode video_transcode;
        video_transcode.filter.set(std::move(video_filter));
        video_transcode.codec = "libx264";
        video_transcode.codec_priv_key = "x264-params";
        video_transcode.codec_priv_value = "keyint=60:min-keyint=60:scenecut=0:force-cfr=1";

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

        item.types_definitions.insert(container.to_string());
    }

    auto& pending_items = m_pimpl->pending_for_index->items;
    for (auto const& pending_item : pending_items)
    {
        if (pending_item.path == item.path)
        {
            for (auto const& pending_item_type : pending_item.types_definitions)
                item.types_definitions.erase(pending_item_type);
        }
    }

    if (item.types_definitions.empty())
        return false;

    pending_items.push_back(std::move(item));

    return true;
}

vector<pair<vector<string>, unordered_set<string>>> library::process_index()
{
    vector<pair<vector<string>, unordered_set<string>>> result;

    auto const& pending_items = m_pimpl->pending_for_index.as_const()->items;
    for (size_t index = 0; index != pending_items.size(); ++index)
    {
        if (m_pimpl->processing_for_index == 3)
            break;

        if (index == m_pimpl->processing_for_index)
        {
            ++m_pimpl->processing_for_index;

            result.push_back(std::make_pair(pending_items[index].path,
                                            pending_items[index].types_definitions));
        }
    }

    return result;
}

unordered_set<string> library::process_index_store_hash(vector<string> const& path,
                                                        unordered_set<string> const& types_definitions,
                                                        string const& sha256sum)
{
    auto types_definitions_temp = types_definitions;

    auto& items = m_pimpl->pending_for_index->items;
    size_t count = 0;
    for (auto& item : items)
    {
        if (item.path == path &&
            item.types_definitions == types_definitions &&
            count < m_pimpl->processing_for_index)
            item.sha256sum = sha256sum;
        else if (item.sha256sum == sha256sum)
        {
            for (auto const& type : item.types_definitions)
                types_definitions_temp.erase(type);
        }
        ++count;
    }

    return types_definitions_temp;
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
                                   unordered_set<string> const& types_definitions_find,
                                   unordered_set<string> const& types_definitions_replace)
{
    size_t count = 0;
    auto& items = m_pimpl->pending_for_index->items;
    for (auto& item : items)
    {
        if (item.path == path &&
            item.types_definitions == types_definitions_find &&
            count < m_pimpl->processing_for_index)
        {
            item.types_definitions = types_definitions_replace;
            return;
        }
    }

    throw std::logic_error("library::process_index_update");
}

void library::process_index_done(vector<string> const& path,
                                 unordered_set<string> const& types_definitions)
{
    --m_pimpl->processing_for_index;

    auto& items = m_pimpl->pending_for_index->items;
    for (size_t index = 0; index != items.size(); ++index)
    {
        auto& item = items[index];
        if (item.path == path &&
            item.types_definitions == types_definitions)
        {
            items.erase(items.begin() + index);
            return;
        }
    }

    throw std::logic_error("library::process_index_done");
}

bool library::check(vector<string>&& path, unordered_set<string>&& types_definitions)
{
    auto types_definitions_temp = types_definitions;

    auto& pending_items = m_pimpl->pending_for_media_check->items;
    for (auto const& pending_item : pending_items)
    {
        if (pending_item.path == path)
        {
            for (auto const& pending_item_type : pending_item.types_definitions)
                types_definitions_temp.erase(pending_item_type);
        }
    }

    if (types_definitions_temp.empty())
        return false;

    ProcessMediaCheckRequest check;
    check.path = std::move(path);
    check.types_definitions = std::move(types_definitions_temp);
    process_index_update(check.path, types_definitions, check.types_definitions);
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

unordered_set<string>
library::process_check_done(ProcessMediaCheckResult&& progress_item,
                            string const& uri)
{
    string string_path = join_path(progress_item.path).first;

    auto& items = m_pimpl->pending_for_media_check->items;

    auto it_item = items.begin();
    //for (; it_item != items.end(); ++it_item)
    {
        InternalModel::ProcessMediaCheckRequest& item = *it_item;

        if (item.path == progress_item.path)
        {
            if (0 == progress_item.count)
            {
                auto result = std::move(item.types_definitions);
                items.erase(it_item);
                m_pimpl->processing_for_check = false;

                return result;
            }
            else
            {
                string sha256sum = process_index_retrieve_hash(progress_item.path);

                add(std::move(progress_item), uri, sha256sum);

                return unordered_set<string>();
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

}
