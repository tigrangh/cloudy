#include "common.hpp"

#include "admin_model.hpp"
#include "storage_model.hpp"
#include "internal_model.hpp"

#include <mesh.pp/cryptoutility.hpp>

#include <unordered_set>

using std::string;
using std::pair;
using std::vector;
namespace chrono = std::chrono;

namespace cloudy
{
beltpp::void_unique_ptr get_admin_putl()
{
    beltpp::message_loader_utility utl;
    AdminModel::detail::extension_helper(utl);

    auto ptr_utl =
        beltpp::new_void_unique_ptr<beltpp::message_loader_utility>(std::move(utl));

    return ptr_utl;
}

beltpp::void_unique_ptr get_storage_putl()
{
    beltpp::message_loader_utility utl;
    StorageModel::detail::extension_helper(utl);

    auto ptr_utl =
        beltpp::new_void_unique_ptr<beltpp::message_loader_utility>(std::move(utl));

    return ptr_utl;
}

beltpp::void_unique_ptr get_internal_putl()
{
    beltpp::message_loader_utility utl;
    InternalModel::detail::extension_helper(utl);

    auto ptr_utl =
        beltpp::new_void_unique_ptr<beltpp::message_loader_utility>(std::move(utl));

    return ptr_utl;
}
bool verify_storage_order(string const& storage_order_token,
                          string& channel_address,
                          string& file_uri,
                          string& session_id,
                          uint64_t& seconds,
                          chrono::system_clock::time_point& tp)
{
    AdminModel::SignedStorageAuthorization msg_verfy_sig_storage_order;

    msg_verfy_sig_storage_order.from_string(meshpp::from_base64(storage_order_token), nullptr);

    channel_address = msg_verfy_sig_storage_order.authorization.address;
    file_uri = msg_verfy_sig_storage_order.token.file_uri;
    session_id = msg_verfy_sig_storage_order.token.session_id;
    seconds = msg_verfy_sig_storage_order.token.seconds;
    tp = chrono::system_clock::from_time_t(msg_verfy_sig_storage_order.token.time_point.tm);

    auto now = chrono::system_clock::now();
    auto signed_time_point = chrono::system_clock::from_time_t(msg_verfy_sig_storage_order.token.time_point.tm);
    auto expiring_time_point = signed_time_point + chrono::seconds(msg_verfy_sig_storage_order.token.seconds);

    if (signed_time_point > now + chrono::seconds(storage_order_sign_instant_precision) ||
        expiring_time_point <= now - chrono::seconds(storage_order_sign_instant_precision))
        return false;

    bool correct = meshpp::verify_signature(
                       msg_verfy_sig_storage_order.authorization.address,
                       msg_verfy_sig_storage_order.token.to_string(),
                       msg_verfy_sig_storage_order.authorization.signature);

    if (false == correct)
        return false;

    return true;
}

pair<string, string> join_path(vector<string> const& path)
{
    string path_string;
    string last_name;
    for (auto const& name : path)
    {
        if (name == "." || name == "..")
            throw std::runtime_error("self or parent directories are not supported");
        path_string += "/" + name;
        last_name = name;
    }

    return std::make_pair(path_string, last_name);
}
pair<boost::filesystem::path, string> check_path(vector<string> const& path)
{
    boost::filesystem::path fs_path("/");
    string last_name;
    for (auto const& name : path)
    {
        if (name == "." || name == "..")
            throw std::runtime_error("self or parent directories are not supported");
        fs_path /= name;
        last_name = name;
    }

    return std::make_pair(fs_path, last_name);
}

namespace detail
{
wait_result_item wait_and_receive_one(wait_result& wait_result_info,
                                      beltpp::event_handler& eh,
                                      beltpp::stream& event_stream,
                                      beltpp::stream* on_demand_stream)
{
    auto& info = wait_result_info.m_wait_result;

    if (info == beltpp::event_handler::wait_result::nothing)
    {
        assert(wait_result_info.on_demand_packets.second.empty());
        if (false == wait_result_info.on_demand_packets.second.empty())
            throw std::logic_error("false == wait_result_info.on_demand_packets.second.empty()");
        assert(wait_result_info.event_packets.second.empty());
        if (false == wait_result_info.event_packets.second.empty())
            throw std::logic_error("false == wait_result_info.event_packets.second.empty()");

        std::unordered_set<beltpp::event_item const*> wait_streams;

        info = eh.wait(wait_streams);

        if (info & beltpp::event_handler::event)
        {
            for (auto& pevent_item : wait_streams)
            {
                B_UNUSED(pevent_item);

                beltpp::socket::packets received_packets;
                beltpp::socket::peer_id peerid;
                received_packets = event_stream.receive(peerid);

                wait_result_info.event_packets = std::make_pair(peerid,
                                                                std::move(received_packets));
            }
        }

        /*if (wait_result & beltpp::event_handler::timer_out)
        {
        }*/

        if (on_demand_stream && (info & beltpp::event_handler::on_demand))
        {
            beltpp::socket::packets received_packets;
            beltpp::socket::peer_id peerid;
            received_packets = on_demand_stream->receive(peerid);

            wait_result_info.on_demand_packets = std::make_pair(peerid,
                                                                std::move(received_packets));
        }
    }

    auto result = wait_result_item::empty_result();

    if (info & beltpp::event_handler::event)
    {
        if (false == wait_result_info.event_packets.second.empty())
        {
            auto packet = std::move(wait_result_info.event_packets.second.front());
            auto peerid = wait_result_info.event_packets.first;

            wait_result_info.event_packets.second.pop_front();

            result = wait_result_item::event_result(peerid, std::move(packet));
        }

        if (wait_result_info.event_packets.second.empty())
            info = beltpp::event_handler::wait_result(info & ~beltpp::event_handler::event);

        return result;
    }

    if (info & beltpp::event_handler::timer_out)
    {
        info = beltpp::event_handler::wait_result(info & ~beltpp::event_handler::timer_out);
        result = wait_result_item::timer_result();
        return result;
    }

    if (info & beltpp::event_handler::on_demand)
    {
        if (false == wait_result_info.on_demand_packets.second.empty())
        {
            auto packet = std::move(wait_result_info.on_demand_packets.second.front());
            auto peerid = wait_result_info.on_demand_packets.first;

            wait_result_info.on_demand_packets.second.pop_front();

            result = wait_result_item::on_demand_result(peerid, std::move(packet));
        }

        if (wait_result_info.on_demand_packets.second.empty())
            info = beltpp::event_handler::wait_result(info & ~beltpp::event_handler::on_demand);

        return result;
    }

    return result;
}
std::string dashboard()
{
    return R"dashboard_here(
<script>
    class RequestInfo {
        constructor() {
            this.currentdir = [];
            this.admin = '';

            var s1 = location.search.substring(1, location.search.length).split('&');
            var s2, i;
            for (i = 0; i < s1.length; ++i) {
                s2 = s1[i].split('=');

                var key = decodeURIComponent(s2[0]).toLowerCase();
                var value = decodeURIComponent(s2[1]);
                if (key == "admin" && location.protocol != "http:")
                    this.admin = value;
                else if (key == "storage")
                    this.storage = value;
                else if (key == "dir")
                    this.currentdir.push(value);
            }

            this.location = location;
        }

        getHref()
        {
            var admin_part = '?';
            var parts = [];
            if (this.admin.length)
                parts.push("admin=" + encodeURIComponent(this.admin));
            
            if (this.storage.length)
                parts.push("storage=" + encodeURIComponent(this.storage));

            for (var dir_item of this.currentdir)
                parts.push("dir=" + dir_item);

            var href = this.location.origin + this.location.pathname;
            if (parts.length)
                href += "?" + parts.join("&");
            
            return href;
        }
    };

    const log_type = {
        sucess: 'sucess',
        warning: 'warning',
        fail: 'fail',
        exception: 'exception',
    };
    class LogEntry {
        constructor(type, value, path = [])
        {
            this.type = type;
            this.value = value;
            this.path = path;
        }
    };

    class Dashboard {
        constructor() {
            this.requestInfo = new RequestInfo();
            this.log = [];
            this.autorefresh = 1;
            this.showlogdiv = true;
            
            this.currentdir_lib_files = [];
            this.currentdir_lib_dirs = [];
            this.currentdir_fs_files = [];
            this.currentdir_fs_dirs = [];

            this.filetocheck = '';
            this.filechecksum = '';

            this.mediainfo = [];

            this.needsuiupdate = true;
            this.waiting = 0;
        }
    };

    class Singleton {
        static Dashboard() {
            if (!Singleton._dashboard)
                Singleton._dashboard = new Dashboard();

            return Singleton._dashboard;
        }
    };

    function selectFSFile(name)
    {
        Singleton.Dashboard().filetocheck = name;
        Singleton.Dashboard().filechecksum = '';
        readInput();
    }
    function selectLibFile(name, checksum)
    {
        Singleton.Dashboard().filetocheck = name;
        Singleton.Dashboard().filechecksum = checksum;
        readInput();
    }
    function dirGoUp()
    {
        Singleton.Dashboard().requestInfo.currentdir.pop();
        Singleton.Dashboard().filetocheck = '';
        Singleton.Dashboard().filechecksum = '';
        readInput();
    }
    function dirGoIn(dir_name)
    {
        Singleton.Dashboard().requestInfo.currentdir.push(dir_name);
        Singleton.Dashboard().filetocheck = '';
        Singleton.Dashboard().filechecksum = '';
        
        readInput(function () {
            Singleton.Dashboard().waiting = 0;
            dirGoUp();
        });
    }

    function readInput(recovery_function = undefined)
    {
        if (Singleton.Dashboard().requestInfo.admin != document.getElementById("admin").value) {
            Singleton.Dashboard().requestInfo.admin = document.getElementById("admin").value;
            Singleton.Dashboard().waiting = 0;
        }
        Singleton.Dashboard().requestInfo.storage = document.getElementById("storage").value

        window.history.replaceState(null, null, Singleton.Dashboard().requestInfo.getHref());

        if (Singleton.Dashboard().autorefresh != document.getElementById("autorefresh").checked)
            Singleton.Dashboard().autorefresh = !Singleton.Dashboard().autorefresh;

        Singleton.Dashboard().showlogdiv = document.getElementById("showlog").checked;

        Singleton.Dashboard().needsuiupdate = true;
        update(false, recovery_function);
    }
    function update(automatic_call, recovery_function = undefined)
    {
        if (false == automatic_call && Singleton.Dashboard().waiting)
        {
            if (recovery_function)
                recovery_function();
            return;
        }

        if (0 == Singleton.Dashboard().waiting)
        try {
            if (0 == document.getElementById("admin").value.length &&
                0 != Singleton.Dashboard().requestInfo.admin.length)
                document.getElementById("admin").value = Singleton.Dashboard().requestInfo.admin;
            if (0 == document.getElementById("storage").value.length)
                document.getElementById("storage").value = Singleton.Dashboard().requestInfo.storage;

            getLog(recovery_function);
            getDir(recovery_function);
            getMediaInfo(recovery_function);
        } catch (error) {
            Singleton.Dashboard().log.push("EXCEPTION: " + error);
        }

        if (false == Singleton.Dashboard().needsuiupdate)
            return;

        updateUI();

        Singleton.Dashboard().needsuiupdate = false;
    }

    function updateUI()
    {
        if (location.protocol == "http:") {
            document.getElementById("div_admin").style.display = "none";
        }

        var log_list = document.getElementById('log');
        log_list.innerHTML = "";

        for (var log_entry of Singleton.Dashboard().log)
        {
            var entry = document.createElement('li');
            entry.appendChild(document.createTextNode(log_entry));
            log_list.appendChild(entry);
        }

        document.getElementById("autorefresh").checked = Singleton.Dashboard().autorefresh;

        if (Singleton.Dashboard().log.length)
            document.getElementById("div_log_controls").style.display = "block";
        else
            document.getElementById("div_log_controls").style.display = "none";

        document.getElementById("showlog").checked = Singleton.Dashboard().showlogdiv;

        if (Singleton.Dashboard().log.length && Singleton.Dashboard().showlogdiv)
            document.getElementById("div_log").style.display = "block";
        else
            document.getElementById("div_log").style.display = "none";

        var fs_dirs_list = document.getElementById('fs_dirs');
        fs_dirs_list.innerHTML = "";

        if (Singleton.Dashboard().requestInfo.currentdir.length)
        {
            var entry = document.createElement('li');
            entry.innerHTML = "<a href='javascript:dirGoUp();'>UP</a>";
            fs_dirs_list.appendChild(entry);

            entry = document.createElement('li');
            entry.innerHTML = "&nbsp";
            fs_dirs_list.appendChild(entry);
        }
        for (var dir_entry of Singleton.Dashboard().currentdir_fs_dirs)
        {
            var entry = document.createElement('li');
            entry.innerHTML = "<a href='javascript:dirGoIn(\"" + dir_entry.name + "\");'>" + dir_entry.name +"</a>";
            fs_dirs_list.appendChild(entry);
        }
        var fs_files_list = document.getElementById('fs_files');
        fs_files_list.innerHTML = "";

        for (var file_entry of Singleton.Dashboard().currentdir_fs_files)
        {
            var entry = document.createElement('li');
            entry.innerHTML = "<a href='javascript:selectFSFile(\"" + file_entry.name + "\");' class=\"fsfile\">" + file_entry.name +"</a>";

            fs_files_list.appendChild(entry);
        }

        var lib_dirs_list = document.getElementById('lib_dirs');
        lib_dirs_list.innerHTML = "";

        if (Singleton.Dashboard().requestInfo.currentdir.length)
        {
            var entry = document.createElement('li');
            entry.innerHTML = "<a href='javascript:dirGoUp();'>UP</a>";
            lib_dirs_list.appendChild(entry);

            entry = document.createElement('li');
            entry.innerHTML = "&nbsp";
            lib_dirs_list.appendChild(entry);
        }
        for (var dir_entry of Singleton.Dashboard().currentdir_lib_dirs)
        {
            var entry = document.createElement('li');
            entry.innerHTML = "<a href='javascript:dirGoIn(\"" + dir_entry.name + "\");'>" + dir_entry.name +"</a>";
            lib_dirs_list.appendChild(entry);
        }
        var lib_files_list = document.getElementById('lib_files');
        lib_files_list.innerHTML = "";

        for (var file_entry of Singleton.Dashboard().currentdir_lib_files)
        {
            var entry = document.createElement('li');
            entry.innerHTML = "<a href='javascript:selectLibFile(\"" + file_entry.name + "\", \"" + file_entry.checksum + "\");' class=\"fsfile\">" + file_entry.name +"</a>";
            lib_files_list.appendChild(entry);
        }

        if (0 == Singleton.Dashboard().filetocheck.length)
            document.getElementById("div_fs_file").style.display = "none";
        else {
            document.getElementById("div_fs_file").style.display = "block";
            document.getElementById("fs_file_name").textContent = Singleton.Dashboard().filetocheck;
        }
        if (0 == Singleton.Dashboard().filechecksum.length) {
            document.getElementById("div_lib_file").style.display = "none";
            document.getElementById("authorization").href = "";
            document.getElementById("authorization").innerHTML = "";
        } else {

            document.getElementById("div_lib_file").style.display = "block";
            document.getElementById("label_lib_checksum").textContent = Singleton.Dashboard().filechecksum;

            var lib_uris_list = document.getElementById('uris');
            lib_uris_list.innerHTML = "";

            for (var media_info of Singleton.Dashboard().mediainfo) {

                for (var frame_info of media_info.frames) {
                    var entry = document.createElement('li');
                    entry.innerHTML += "<a href='javascript:getAuthorization(\"" + frame_info.uri + "\");'>" + frame_info.uri +"</a>";
                    if (media_info.video)
                    {
                        entry.innerHTML += "<br>&nbsp&nbsp&nbsp video: duration - " + frame_info.count + " ms, height - " + media_info.video.height + ", width - " + media_info.video.width + ", fps - " + media_info.video.fps + " ";
                    }
                    lib_uris_list.appendChild(entry);
                }
            }
        }
    }

    function getLog(recovery_function = undefined)
    {
        var get_log_request = new XMLHttpRequest();
        get_log_request.onreadystatechange = function() {
            
            if (this.readyState == 4) {

                Singleton.Dashboard().waiting--;
                var result = [];

                try {
                    if (this.status != 200)
                        throw this.status;

                    var log_response = JSON.parse(this.responseText);

                    if (log_response.rtt != 12)
                        throw log_response;
                    
                    for (var log_entry of log_response.log) {
                        if (log_entry.rtt == 13)
                            result.push(new LogEntry(log_type.sucess, "/" + log_entry.path.join("/"), log_entry.path));
                        else if (log_entry.rtt == 14)
                            result.push(new LogEntry(log_type.fail, log_entry.reason + ": /" + log_entry.path.join("/")));
                        else if (log_entry.rtt == 15)
                            result.push(new LogEntry(log_type.warning, log_entry.reason + ": /" + log_entry.path.join("/")));
                        else
                            throw log_entry;
                    }
                    
                } catch (error) {
                    Singleton.Dashboard().log.push("EXCEPTION: " + error);
                    Singleton.Dashboard().waiting++;
                    if (recovery_function)
                        recovery_function();
                }

                if (result.length) {

                    Singleton.Dashboard().filechecksum = '';

                    for (var result_item of result) {
                        var string_log;
                        if (result_item.type == log_type.fail)
                            string_log = result_item.value;
                        else if (result_item.type == log_type.warning)
                            string_log = "WARNING: " + result_item.value;
                        else
                            string_log = "OK: " + result_item.value;

                        Singleton.Dashboard().log.push(string_log);
                    }
                    
                    var delete_log_request = new XMLHttpRequest();

                    delete_log_request.open("DELETE", Singleton.Dashboard().requestInfo.admin + "/log/" + result.length, true);
                    delete_log_request.send();

                    updateUI();
                }
            }
        };

        get_log_request.open("GET", Singleton.Dashboard().requestInfo.admin + "/log", true);
        get_log_request.send();
        Singleton.Dashboard().waiting++;
    }

    function getMediaInfo(recovery_function = undefined)
    {
        if (0 == Singleton.Dashboard().filechecksum.length)
            return;

        var index_request = new XMLHttpRequest();
        index_request.onreadystatechange = function() {
            
            if (this.readyState == 4) {

                Singleton.Dashboard().waiting--;
                var result = [];

                try {
                    if (this.status != 200)
                        throw this.status;

                    var index_response = JSON.parse(this.responseText);

                    if (index_response.rtt != 22)
                        throw index_response;

                    for (type_definition of index_response.type_definitions) {
                        if (type_definition.rtt != 21)
                            throw type_definition;

                        var result_info = {};

                        if (type_definition.type_description.rtt == 24 &&
                            type_definition.type_description.video &&
                            type_definition.type_description.video.transcode &&
                            type_definition.type_description.video.transcode.filter) {

                            var filter = type_definition.type_description.video.transcode.filter;
                            var video_info = {};
                            video_info.width = filter.width;
                            video_info.height = filter.height;
                            video_info.fps = filter.fps;

                            result_info.video = video_info;

                        }

                        result_info.frames = [];

                        for (frame of type_definition.sequence.frames) {
                            var frame_info = {};
                            frame_info.count = frame.count;
                            frame_info.uri = frame.uri;

                            result_info.frames.push(frame_info);
                        }

                        result.push(result_info);
                    }
                } catch (error) {
                    Singleton.Dashboard().log.push("EXCEPTION: " + error);
                    Singleton.Dashboard().waiting++;
                    if (recovery_function)
                        recovery_function();
                }

                Singleton.Dashboard().mediainfo = result;

                updateUI();
            }
        };
        
        index_request.open("GET", Singleton.Dashboard().requestInfo.admin + "/index/" + Singleton.Dashboard().filechecksum, true);
        index_request.send();
        Singleton.Dashboard().waiting++;
    }

    function getAuthorization(uri, recovery_function = undefined)
    {
        var authorization_request = new XMLHttpRequest();
        authorization_request.onreadystatechange = function() {
            
            if (this.readyState == 4) {

                var text = '';

                Singleton.Dashboard().waiting--;

                try {
                    if (this.status != 200)
                        throw this.status;

                    text = this.responseText;
                } catch (error) {
                    Singleton.Dashboard().log.push("EXCEPTION: " + error);
                    Singleton.Dashboard().waiting++;
                    if (recovery_function)
                        recovery_function();
                }

                var link = document.getElementById("authorization");
                var authorization = Singleton.Dashboard().requestInfo.storage + "/storage?authorization=" + encodeURIComponent(text);
                link.innerHTML = authorization;
                link.href = authorization;
            }
        };

        var seconds = document.getElementById("seconds").value;
        
        authorization_request.open("GET", Singleton.Dashboard().requestInfo.admin + "/authorization?file=" + uri + "&seconds=" + seconds, true);
        authorization_request.send();
        Singleton.Dashboard().waiting++;
    }

    function delete_checksum(recovery_function = undefined)
    {
        if (0 == Singleton.Dashboard().filechecksum.length)
            return;

        var delete_request = new XMLHttpRequest();
        delete_request.onreadystatechange = function() {
            
            if (this.readyState == 4) {

                Singleton.Dashboard().waiting--;

                try {
                    if (this.status != 200)
                        throw this.status;

                    var index_response = JSON.parse(this.responseText);

                    if (index_response.rtt != 22)
                        throw index_response;
                } catch (error) {
                    Singleton.Dashboard().log.push("EXCEPTION: " + error);
                    Singleton.Dashboard().waiting++;
                    if (recovery_function)
                        recovery_function();
                }

                Singleton.Dashboard().filechecksum = '';
                updateUI();
            }
        };
        
        delete_request.open("DELETE", Singleton.Dashboard().requestInfo.admin + "/index/" + Singleton.Dashboard().filechecksum, true);
        delete_request.send();
        Singleton.Dashboard().waiting++;
    }

    function delete_path(recovery_function = undefined)
    {
        if (0 == Singleton.Dashboard().filetocheck.length)
            return;

        var delete_request = new XMLHttpRequest();
        delete_request.onreadystatechange = function() {
            
            if (this.readyState == 4) {

                Singleton.Dashboard().waiting--;

                try {
                    if (this.status != 200)
                        throw this.status;

                    var delete_response = JSON.parse(this.responseText);

                    if (delete_response.rtt != 7)
                        throw delete_response;
                } catch (error) {
                    Singleton.Dashboard().log.push("EXCEPTION: " + error);
                    Singleton.Dashboard().waiting++;
                    if (recovery_function)
                        recovery_function();
                }

                Singleton.Dashboard().filechecksum = '';
                updateUI();
            }
        };
        
        delete_request.open("DELETE", Singleton.Dashboard().requestInfo.admin + "/library/" + Singleton.Dashboard().requestInfo.currentdir.join("/") + "/" + Singleton.Dashboard().filetocheck, true);
        delete_request.send();
        Singleton.Dashboard().waiting++;
    }

    function addraw(recovery_function = undefined)
    {
        if (0 == Singleton.Dashboard().filetocheck.length)
            return;
        
        var mime_type = document.getElementById("raw_mime").value;
        if (!mime_type || 0 == mime_type.length) {
            alert('the mime type is empty');
            return;
        }

        var addraw_request = new XMLHttpRequest();
        addraw_request.onreadystatechange = function() {
            
            if (this.readyState == 4) {

                Singleton.Dashboard().waiting--;

                try {
                    if (this.status != 200)
                        throw this.status;

                    var addraw_response = JSON.parse(this.responseText);

                    if (addraw_response.rtt != 7)
                        throw addraw_response;
                } catch (error) {
                    Singleton.Dashboard().log.push("EXCEPTION: " + error);
                    Singleton.Dashboard().waiting++;
                    if (recovery_function)
                        recovery_function();
                }
            }
        };

        var check_options = {"rtt":28, "mime_type":"text/html"};
        check_options.mime_type = mime_type;
        
        addraw_request.open("PUT", Singleton.Dashboard().requestInfo.admin + "/library/" + Singleton.Dashboard().requestInfo.currentdir.join("/") + "/" + Singleton.Dashboard().filetocheck, true);
        addraw_request.send(JSON.stringify([check_options]));
        Singleton.Dashboard().waiting++;
    }

    function transcode(recovery_function = undefined)
    {
        if (0 == Singleton.Dashboard().filetocheck.length)
            return;

        var transcode_request = new XMLHttpRequest();
        transcode_request.onreadystatechange = function() {
            
            if (this.readyState == 4) {

                Singleton.Dashboard().waiting--;

                try {
                    if (this.status != 200)
                        throw this.status;

                    var transcode_response = JSON.parse(this.responseText);

                    if (transcode_response.rtt != 7)
                        throw transcode_response;
                } catch (error) {
                    Singleton.Dashboard().log.push("EXCEPTION: " + error);
                    Singleton.Dashboard().waiting++;
                    if (recovery_function)
                        recovery_function();
                }
            }
        };

        var check_options = {"rtt":24, "container_extension":"mp4", "muxer_opt_key":"", "muxer_opt_value":"", "audio":{"rtt":25, "transcode":{"rtt":26, "codec":"aac", "codec_priv_key":"", "codec_priv_value":""}}, "video":{"rtt":25, "transcode":{"rtt":26, "codec":"libx264", "codec_priv_key":"x264-params", "codec_priv_value":"keyint=60:min-keyint=60:scenecut=0:force-cfr=1", "filter":{"rtt":27, "height":1080, "width":1920, "fps":29}}}};

        check_options.video.transcode.filter.fps = parseInt(document.getElementById("transcode_fps").value, 10);
        check_options.video.transcode.filter.width = parseInt(document.getElementById("transcode_width").value, 10);
        check_options.video.transcode.filter.height = parseInt(document.getElementById("transcode_height").value, 10);
        
        transcode_request.open("PUT", Singleton.Dashboard().requestInfo.admin + "/library/" + Singleton.Dashboard().requestInfo.currentdir.join("/") + "/" + Singleton.Dashboard().filetocheck, true);
        transcode_request.send(JSON.stringify([check_options]));
        Singleton.Dashboard().waiting++;
    }

    function arrays_same(first, second)
    {
        return JSON.stringify(first) == JSON.stringify(second);
    }

    function getDir(recovery_function = undefined)
    {
        var get_dir_request = new XMLHttpRequest();
        get_dir_request.onreadystatechange = function() {
            
            if (this.readyState == 4) {

                Singleton.Dashboard().waiting--;
                var result_lib_files = [];
                var result_lib_dirs = [];
                var result_fs_files = [];
                var result_fs_dirs = [];

                try {
                    if (this.status != 200)
                        throw this.status;

                    var dir_response = JSON.parse(this.responseText);

                    if (dir_response.rtt != 7)
                        throw dir_response;
                    
                    for (var file_item of dir_response.lib_files) {
                        if (file_item.rtt != 8)
                            throw file_item;
                            result_lib_files.push(file_item);
                    }
                    for (var dir_item of dir_response.lib_directories) {
                        if (dir_item.rtt != 9)
                            throw dir_item;
                            result_lib_dirs.push(dir_item);
                    }
                    for (var file_item of dir_response.fs_files) {
                        if (file_item.rtt != 8)
                            throw file_item;
                            result_fs_files.push(file_item);
                    }
                    for (var dir_item of dir_response.fs_directories) {
                        if (dir_item.rtt != 9)
                            throw dir_item;
                            result_fs_dirs.push(dir_item);
                    }
                } catch (error) {
                    Singleton.Dashboard().log.push("EXCEPTION: " + error);
                    Singleton.Dashboard().waiting++;
                    if (recovery_function)
                        recovery_function();
                }

                var temp_currentdir_fs_files = Singleton.Dashboard().currentdir_fs_files;
                var temp_currentdir_fs_dirs = Singleton.Dashboard().currentdir_fs_dirs;
                var temp_currentdir_lib_files = Singleton.Dashboard().currentdir_lib_files;
                var temp_currentdir_lib_dirs = Singleton.Dashboard().currentdir_lib_dirs;

                if (!arrays_same(temp_currentdir_fs_files, result_fs_files) ||
                    !arrays_same(temp_currentdir_fs_dirs, result_fs_dirs) ||
                    !arrays_same(temp_currentdir_lib_files, result_lib_files) ||
                    !arrays_same(temp_currentdir_lib_dirs, result_lib_dirs)) {

                    Singleton.Dashboard().currentdir_fs_files = result_fs_files;
                    Singleton.Dashboard().currentdir_fs_dirs = result_fs_dirs;
                    Singleton.Dashboard().currentdir_lib_files = result_lib_files;
                    Singleton.Dashboard().currentdir_lib_dirs = result_lib_dirs;

                    updateUI();
                }
            }
        };
        
        get_dir_request.open("GET", Singleton.Dashboard().requestInfo.admin + "/library/" + Singleton.Dashboard().requestInfo.currentdir.join("/"), true);
        get_dir_request.send();
        Singleton.Dashboard().waiting++;
    }
    function autoupdate() {
        if (Singleton.Dashboard().autorefresh)
            update(true);
    }
    function eventloop() {
        autoupdate();
        var intervalID = window.setInterval(autoupdate, 2000);
    }
</script>
<html>
    <head>
        <title>Cloudy Dashboard</title>
    </head>
    <body onload = eventloop()>
        <style>
            div {
                padding: 1em;
            }
            html {
                max-width: 65em;
                margin: 1em auto;
            }
            div.vscrolllog {
                overflow-y: scroll;
                height:5em;
            }
            div.vscroll {
                overflow-y: scroll;
                height: 25em;
                width: 12em;
                display: inline-block;
            }
            ul.nobullets {
                margin: 0em;
                padding: 0em;
                list-style-type: none;
            }
            a.fsfile {
                color: black;
            }
        </style>

        <div id="div_admin">
            <label>admin:&nbsp&nbsp</label>
            <input type="text" id="admin">
            <button onclick = "readInput()">Set</button><br>
        </div>
        <div>
            <label>storage:</label>
            <input type="text" id="storage">
            <button onclick = "readInput()">Set</button><br><br>
        </div>
        <label class="switch">
            auto refresh
            <input type="checkbox" id="autorefresh" onclick="readInput()">
        </label>

        <div id="div_log_controls">
            <button onclick = "Singleton.Dashboard().log = []; Singleton.Dashboard().waiting = 0; updateUI()">clear the log</button> 
            <label class="switch">
                show the log
                <input type="checkbox" id="showlog" onclick="readInput()">
            </label>
        </div>

        <div id="div_log" class="vscrolllog">
            <ol id="log">
            </ol>
        </div>

        <div>
            <div id="div_lib_dirs" class="vscroll">
                <ul id="lib_dirs" class="nobullets">
                </ul>
            </div>
            <div id="div_lib_files" class="vscroll">
                <ul id="lib_files" class="nobullets">
                </ul>
            </div>
            <div id="div_fs_dirs" class="vscroll">
                <ul id="fs_dirs" class="nobullets">
                </ul>
            </div>
            <div id="div_fs_files" class="vscroll">
                <ul id="fs_files" class="nobullets">
                </ul>
            </div>
        </div>
        
        <div id="div_fs_file">
            <label><strong>add to library</strong></label><br>
            <label id="fs_file_name"></label><br>
            
            <input type="number" id="transcode_width" value="640"> <label>width</label><br>
            <input type="number" id="transcode_height" value="480"> <label>height</label><br>
            <input type="number" id="transcode_fps" value=24> <label>fps</label><br>
            
            <button onclick = "transcode()">transcode</button><br><br>
            
            <input type="text" id="raw_mime"> <label>mime type</label><br>
            
            <button onclick = "addraw()">add raw</button>
        </div>
        <div id="div_lib_file">
            <label>media checksum: </label>
            <label id="label_lib_checksum"></label><br><br>

            <input type="number" id="seconds" value="86400"> <label>authorization seconds</label><br><br>
            <a target="_blank" href="" id="authorization"></a><br><br>

            <ul id="uris" class="nobullets">
            </ul>

            <br><label><strong>remove from library</strong></label><br>
                        
            <button onclick = "delete_path()">path</button>
            
            <label> or </label>
            
            <button onclick = "delete_checksum()">checksum</button><br><br><br>
        </div>
    </body>
</html>
    )dashboard_here";
}
} // end detail
} // end cloudy


