#include <cloudy/admin_server.hpp>
#include <cloudy/storage_server.hpp>
#include <cloudy/worker.hpp>
#include <cloudy/direct_stream.hpp>

#include <belt.pp/log.hpp>
#include <belt.pp/scope_helper.hpp>

#include <mesh.pp/settings.hpp>
#include <mesh.pp/pid.hpp>
#include <mesh.pp/fileutility.hpp>
#include <mesh.pp/log.hpp>

#include <boost/program_options.hpp>
#include <boost/locale.hpp>
#include <boost/filesystem/path.hpp>

#include <iostream>
#include <vector>
#include <exception>
#include <thread>
#include <string>

#include <csignal>

namespace program_options = boost::program_options;

using std::unique_ptr;
using std::string;
using std::cout;
using std::endl;
using std::vector;
using std::runtime_error;
using std::thread;

bool process_command_line(int argc, char** argv,
                          beltpp::ip_address& admin_bind_to_address,
                          beltpp::ip_address& storage_bind_to_address,
                          string& data_directory,
                          meshpp::private_key& pv_key);

static bool g_termination_handled = false;
static cloudy::admin_server* g_admin = nullptr;
static cloudy::storage_server* g_storage = nullptr;
static cloudy::worker* g_worker = nullptr;
void termination_handler(int /*signum*/)
{
    cout << "stopping..." << endl;

    g_termination_handled = true;
    if (g_admin)
        g_admin->wake();
    if (g_storage)
        g_storage->wake();
    if (g_worker)
        g_worker->wake();
}

template <typename SERVER>
void loop(SERVER& server, beltpp::ilog_ptr& plogger_exceptions, bool& termination_handled);

int main(int argc, char** argv)
{
    try
    {
        //  boost filesystem UTF-8 support
        std::locale::global(boost::locale::generator().generate(""));
        boost::filesystem::path::imbue(std::locale());
    }
    catch (...)
    {}  //  don't care for exception, for now
    //
    meshpp::settings::set_application_name("cloudyd");
    meshpp::settings::set_data_directory(meshpp::config_directory_path().string());

    meshpp::config::set_public_key_prefix("Cloudy-");

    beltpp::ip_address admin_bind_to_address;
    beltpp::ip_address storage_bind_to_address;
    string data_directory;
    meshpp::random_seed seed;
    meshpp::private_key pv_key = seed.get_private_key(0);

    if (false == process_command_line(argc, argv,
                                      admin_bind_to_address,
                                      storage_bind_to_address,
                                      data_directory,
                                      pv_key))
        return 1;

    if (false == data_directory.empty())
        meshpp::settings::set_data_directory(data_directory);

#ifdef B_OS_WINDOWS
    signal(SIGINT, termination_handler);
#else
    struct sigaction signal_handler;
    signal_handler.sa_handler = termination_handler;
    ::sigaction(SIGINT, &signal_handler, nullptr);
    ::sigaction(SIGTERM, &signal_handler, nullptr);
#endif

    beltpp::ilog_ptr plogger_admin_exceptions;
    beltpp::ilog_ptr plogger_storage_exceptions;
    beltpp::ilog_ptr plogger_worker_exceptions;
    try
    {
        meshpp::create_config_directory();
        meshpp::create_data_directory();

        using DataDirAttributeLoader = meshpp::file_locker<meshpp::file_loader<PidConfig::DataDirAttribute,
                                                                                &PidConfig::DataDirAttribute::from_string,
                                                                                &PidConfig::DataDirAttribute::to_string>>;
        DataDirAttributeLoader dda(meshpp::data_file_path("running.txt"));
        {
            PidConfig::RunningDuration item;
            item.start.tm = item.end.tm = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

            dda->history.push_back(item);
            dda.save();
        }

        beltpp::finally dda_finally([&dda]
        {
            dda->history.back().end.tm = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            dda.save();
        });

        auto fs_log = meshpp::data_directory_path("log");
        auto fs_storage = meshpp::data_directory_path("storage");
        auto fs_storage_binaries = meshpp::data_directory_path("storage_binaries");
        auto fs_library = meshpp::data_directory_path("library");
        auto fs_admin = meshpp::data_directory_path("admin");
        auto fs_worker = meshpp::data_directory_path("worker");

        cout << "admin bind address: " << admin_bind_to_address.to_string() << endl;
        cout << "storage bind address: " << storage_bind_to_address.to_string() << endl;
        cout << "private key: " << pv_key.get_base58_wif() << endl;
        cout << "public key: " << pv_key.get_public_key().to_string() << endl;

        beltpp::ilog_ptr plogger_admin = beltpp::console_logger("admin", true);
        //plogger_admin->disable();
        beltpp::ilog_ptr plogger_storage = beltpp::console_logger("storage", true);
        //plogger_storage->disable();
        beltpp::ilog_ptr plogger_worker = beltpp::console_logger("worker", true);
        //plogger_worker->disable();
        plogger_admin_exceptions = meshpp::file_logger("admin_exceptions",
                                                       fs_log / "admin_exceptions.txt");
        plogger_storage_exceptions = meshpp::file_logger("storage_exceptions",
                                                         fs_log / "storage_exceptions.txt");
        plogger_worker_exceptions = meshpp::file_logger("worker_exceptions",
                                                         fs_log / "worker_exceptions.txt");

        cloudy::direct_channel direct_channel;

        cloudy::admin_server admin(admin_bind_to_address,
                                   fs_library,
                                   fs_admin,
                                   pv_key,
                                   plogger_admin.get(),
                                   direct_channel);

        g_admin = &admin;

        cloudy::storage_server storage(storage_bind_to_address,
                                       fs_storage,
                                       fs_storage_binaries,
                                       pv_key.get_public_key(),
                                       plogger_storage.get(),
                                       direct_channel);
        g_storage = &storage;

        cloudy::worker worker(plogger_worker.get(),
                              fs_worker,
                              direct_channel);
        g_worker = &worker;

        {
            thread admin_thread([&admin, &plogger_admin_exceptions]
            {
                loop(admin, plogger_admin_exceptions, g_termination_handled);
            });

            beltpp::finally join_admin_thread([&admin_thread](){ admin_thread.join(); });

            thread storage_thread([&storage, &plogger_storage_exceptions]
            {
                loop(storage, plogger_storage_exceptions, g_termination_handled);
            });

            beltpp::finally join_storage_thread([&storage_thread](){ storage_thread.join(); });

            thread worker_thread([&worker, &plogger_worker_exceptions]
            {
                loop(worker, plogger_worker_exceptions, g_termination_handled);
            });

            beltpp::finally join_worker_thread([&worker_thread](){ worker_thread.join(); });
        }
    }
    catch (std::exception const& ex)
    {
        if (plogger_admin_exceptions)
            plogger_admin_exceptions->message(ex.what());
        cout << "exception cought: " << ex.what() << endl;
    }
    catch (...)
    {
        if (plogger_admin_exceptions)
            plogger_admin_exceptions->message("always throw std::exceptions");
        cout << "always throw std::exceptions" << endl;
    }

    cout << "quit." << endl;

    return 0;
}

template <typename SERVER>
void loop(SERVER& server, beltpp::ilog_ptr& plogger_exceptions, bool& termination_handled)
{
    bool stop_check = false;
    while (false == stop_check)
    {
        try
        {
            if (termination_handled)
                break;
            server.run(stop_check);
            if (stop_check)
            {
                termination_handler(0);
                break;
            }
        }
        catch (std::bad_alloc const& ex)
        {
            if (plogger_exceptions)
                plogger_exceptions->message(ex.what());
            cout << "exception cought: " << ex.what() << endl;
            cout << "will exit now" << endl;
            termination_handler(0);
            break;
        }
        catch (std::logic_error const& ex)
        {
            if (plogger_exceptions)
                plogger_exceptions->message(ex.what());
            cout << "logic error cought: " << ex.what() << endl;
            cout << "will exit now" << endl;
            termination_handler(0);
            break;
        }
        catch (std::exception const& ex)
        {
            if (plogger_exceptions)
                plogger_exceptions->message(ex.what());
            cout << "exception cought: " << ex.what() << endl;
        }
        catch (...)
        {
            if (plogger_exceptions)
                plogger_exceptions->message("always throw std::exceptions, will exit now");
            cout << "always throw std::exceptions, will exit now" << endl;
            termination_handler(0);
            break;
        }
    }
}

bool process_command_line(int argc, char** argv,
                          beltpp::ip_address& admin_bind_to_address,
                          beltpp::ip_address& storage_bind_to_address,
                          string& data_directory,
                          meshpp::private_key& pv_key)
{
    string admin_bind_interface;
    string storage_bind_interface;
    string str_pv_key;

    program_options::options_description options_description;
    try
    {
        auto desc_init = options_description.add_options()
            ("help,h", "print this help message and exit.")
            ("admin-interface,a", program_options::value<string>(&admin_bind_interface)->required(),
                            "admin interface")
            ("storage-interface,s", program_options::value<string>(&storage_bind_interface)->required(),
                            "storage interface")
            ("data-directory,d", program_options::value<string>(&data_directory),
                            "Data directory path")
            ("daemon-private-key,k", program_options::value<string>(&str_pv_key),
                            "daemon private key");
        (void)(desc_init);

        program_options::variables_map options;

        program_options::store(
                    program_options::parse_command_line(argc, argv, options_description),
                    options);

        program_options::notify(options);

        if (options.count("help"))
        {
            throw std::runtime_error("");
        }

        admin_bind_to_address.from_string(admin_bind_interface);
        storage_bind_to_address.from_string(storage_bind_interface);

        if (false == str_pv_key.empty())
            pv_key = meshpp::private_key(str_pv_key);
    }
    catch (std::exception const& ex)
    {
        std::stringstream ss;
        ss << options_description;

        string ex_message = ex.what();
        if (false == ex_message.empty())
            cout << ex.what() << endl << endl;
        cout << ss.str();
        return false;
    }
    catch (...)
    {
        cout << "always throw std::exceptions" << endl;
        return false;
    }

    return true;
}
