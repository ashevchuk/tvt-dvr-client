#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <sys/stat.h>
#include <unistd.h>

#include "client/device_store.h"

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static SavedDevice device(const char *name, const char *host, int port, const char *user)
{
    SavedDevice d;
    d.name = name;
    d.host = host;
    d.port = port;
    d.user = user;
    return d;
}

static void test_round_trip()
{
    DeviceList list;
    list.devices.push_back(device("Office", "192.0.2.10", 6036, "admin"));
    SavedDevice remembered = device("Garage \xD1\x97", "dvr.example.test", 7000, "viewer");
    remembered.remember_password = true;
    remembered.password = "  pass=word with spaces ";
    list.devices.push_back(remembered);
    list.last_used = 1;

    DeviceList back = parse_device_list(serialize_device_list(list));
    CHECK(back.devices.size() == 2);
    CHECK(back.last_used == 1);
    CHECK(back.devices[0].name == "Office" && back.devices[0].host == "192.0.2.10");
    CHECK(back.devices[0].port == 6036 && back.devices[0].user == "admin");
    CHECK(!back.devices[0].remember_password);
    CHECK(back.devices[1].name == remembered.name);
    CHECK(back.devices[1].port == 7000);
    CHECK(back.devices[1].remember_password);
    CHECK(back.devices[1].password == remembered.password);
}

static void test_password_not_written_unless_remembered()
{
    DeviceList list;
    SavedDevice d = device("A", "192.0.2.1", 6036, "u");
    d.password = "placeholder-secret";
    list.devices.push_back(d);
    std::string text = serialize_device_list(list);
    CHECK(text.find("placeholder-secret") == std::string::npos);
    CHECK(text.find("\npassword=") == std::string::npos);
}

static void test_password_ignored_on_load_unless_remembered()
{
    DeviceList list = parse_device_list(
        "[device]\nname=A\nhost=h\nport=6036\nuser=u\nremember_password=0\npassword=leftover\n");
    CHECK(list.devices.size() == 1 && list.devices[0].password.empty());
}

static void test_tolerates_bad_input()
{
    DeviceList list = parse_device_list(
        "last=7\nnoise without equals\n[device]\r\nname=A\r\nhost=h\r\nport=99999\r\nuser=u\r\n"
        "port=abc\r\nunknown=1\r\n[device]\nname=B\nport=0\n");
    CHECK(list.devices.size() == 2);
    CHECK(list.last_used == -1);                  // out of range
    CHECK(list.devices[0].name == "A" && list.devices[0].host == "h");
    CHECK(list.devices[0].port == 6036);          // invalid ports keep the default
    CHECK(list.devices[1].port == 6036);
    CHECK(parse_device_list("").devices.empty());
}

static void test_line_breaks_cannot_inject_fields()
{
    DeviceList list;
    list.devices.push_back(device("A\n[device]\nname=Evil", "h", 6036, "u"));
    DeviceList back = parse_device_list(serialize_device_list(list));
    CHECK(back.devices.size() == 1);
}

static std::string make_temp_directory()
{
    char pattern[] = "/tmp/test_device_store_XXXXXX";
    const char *dir = mkdtemp(pattern);
    if (!dir) {
        perror("mkdtemp");
        exit(2);
    }
    return dir;
}

static void test_save_and_load_file()
{
    std::string root = make_temp_directory();
    std::string path = root + "/config/tvt-dvr-client/devices.conf";
    CHECK(load_device_list(path).devices.empty());   // a missing file is an empty list

    DeviceList list;
    SavedDevice d = device("Office", "192.0.2.10", 6036, "admin");
    d.remember_password = true;
    d.password = "placeholder";
    list.devices.push_back(d);
    list.last_used = 0;
    std::string error;
    CHECK(save_device_list(path, list, error));
    CHECK(error.empty());

    struct stat st;
    CHECK(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    DeviceList back = load_device_list(path);
    CHECK(back.devices.size() == 1 && back.devices[0].password == "placeholder");
    CHECK(access((path + ".tmp").c_str(), F_OK) != 0);

    // A permissive leftover temporary file must not leak into the saved file's mode.
    chmod(path.c_str(), 0644);
    std::ofstream((path + ".tmp").c_str()) << "stale";
    chmod((path + ".tmp").c_str(), 0666);
    CHECK(save_device_list(path, list, error));
    CHECK(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);

    std::string command = "rm -rf '" + root + "'";
    CHECK(system(command.c_str()) == 0);
}

static void test_save_reports_failure()
{
    std::string error;
    CHECK(!save_device_list("/proc/nonexistent/devices.conf", DeviceList{}, error));
    CHECK(!error.empty());
}

static void test_default_path_follows_environment()
{
    setenv("XDG_CONFIG_HOME", "/xdg", 1);
    CHECK(default_device_file() == "/xdg/tvt-dvr-client/devices.conf");
    setenv("XDG_CONFIG_HOME", "relative", 1);     // must be absolute per the spec
    setenv("HOME", "/home/someone", 1);
    CHECK(default_device_file() == "/home/someone/.config/tvt-dvr-client/devices.conf");
    unsetenv("XDG_CONFIG_HOME");
    CHECK(default_device_file() == "/home/someone/.config/tvt-dvr-client/devices.conf");
    unsetenv("HOME");
    CHECK(default_device_file().empty());
}

int main()
{
    test_round_trip();
    test_password_not_written_unless_remembered();
    test_password_ignored_on_load_unless_remembered();
    test_tolerates_bad_input();
    test_line_breaks_cannot_inject_fields();
    test_save_and_load_file();
    test_save_reports_failure();
    test_default_path_follows_environment();
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all device store tests passed\n");
    return 0;
}
