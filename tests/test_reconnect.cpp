#include <atomic>
#include <cstdio>
#include <thread>

#include "client/reconnect.h"

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

using std::chrono::milliseconds;

// A device that is reachable again after `failures_before_success` reconnect attempts.
class FakeDevice : public dvr::Device {
public:
    explicit FakeDevice(int failures_before_success = 0, milliseconds attempt_time = milliseconds(0))
        : failures_left(failures_before_success), attempt_time_(attempt_time)
    {
    }

    std::atomic<bool> link_up{true};
    std::atomic<int> attempts{0};
    std::atomic<bool> attempt_finished{true};
    int failures_left;

    bool connected() override { return link_up; }
    bool reconnect(std::string &error) override
    {
        attempt_finished = false;
        attempts++;
        std::this_thread::sleep_for(attempt_time_);
        bool ok = failures_left-- <= 0;
        if (ok)
            link_up = true;
        else
            error = "unreachable";
        attempt_finished = true;
        return ok;
    }

    const dvr::DeviceInfo &info() const override { return info_; }
    const char *recording_extension() const override { return "mkv"; }
    std::unique_ptr<dvr::MediaSession> open_live(int, bool, dvr::FrameHandler) override { return nullptr; }
    std::unique_ptr<dvr::MediaSession> open_playback(int, time_t, time_t, dvr::FrameHandler) override { return nullptr; }
    std::unique_ptr<dvr::TalkSession> open_talk() override { return nullptr; }
    std::vector<time_t> recorded_days() override { return {}; }
    std::vector<dvr::Segment> segments(int, time_t) override { return {}; }
    std::unique_ptr<dvr::Download> download(int, time_t, time_t, const std::string &) override { return nullptr; }

private:
    dvr::DeviceInfo info_;
    milliseconds attempt_time_;
};

static bool run_until_recovered(Reconnector &r, FakeDevice &d, milliseconds limit)
{
    auto deadline = Reconnector::Clock::now() + limit;
    while (Reconnector::Clock::now() < deadline) {
        if (r.update(d) == Reconnector::Event::Recovered)
            return true;
        std::this_thread::sleep_for(milliseconds(1));
    }
    return false;
}

static void test_a_healthy_device_is_left_alone()
{
    FakeDevice d;
    Reconnector r(milliseconds(1), milliseconds(4), milliseconds(0));
    for (int i = 0; i < 50; i++)
        CHECK(r.update(d) == Reconnector::Event::None);
    CHECK(d.attempts == 0 && !r.outage());
}

static void test_recovers_after_failed_attempts()
{
    FakeDevice d(2);
    Reconnector r(milliseconds(2), milliseconds(8), milliseconds(0));
    d.link_up = false;
    bool saw_outage = false;
    auto deadline = Reconnector::Clock::now() + std::chrono::seconds(5);
    bool recovered = false;
    while (!recovered && Reconnector::Clock::now() < deadline) {
        recovered = r.update(d) == Reconnector::Event::Recovered;
        saw_outage = saw_outage || r.outage();
        std::this_thread::sleep_for(milliseconds(1));
    }
    CHECK(recovered);
    CHECK(saw_outage);
    CHECK(d.attempts == 3);                       // two failures, then success
    CHECK(!r.outage());
    CHECK(r.update(d) == Reconnector::Event::None);   // recovery is reported once
    CHECK(d.attempts == 3);
}

static void test_a_second_outage_is_handled_again()
{
    FakeDevice d;
    Reconnector r(milliseconds(1), milliseconds(4), milliseconds(0));
    d.link_up = false;
    CHECK(run_until_recovered(r, d, std::chrono::seconds(5)));
    d.link_up = false;
    CHECK(run_until_recovered(r, d, std::chrono::seconds(5)));
    CHECK(d.attempts == 2);
}

static void test_outage_is_hidden_until_it_lasts()
{
    FakeDevice d(1000, milliseconds(0));
    Reconnector r(milliseconds(1), milliseconds(2), milliseconds(60000));
    d.link_up = false;
    for (int i = 0; i < 20; i++) {
        r.update(d);
        std::this_thread::sleep_for(milliseconds(1));
    }
    CHECK(d.attempts > 0);
    CHECK(!r.outage());                           // down, but not for a minute yet
}

static void test_retry_delay_doubles_up_to_the_limit()
{
    Reconnector r(milliseconds(100), milliseconds(1000));
    CHECK(r.retry_delay(1) == milliseconds(100));
    CHECK(r.retry_delay(2) == milliseconds(200));
    CHECK(r.retry_delay(3) == milliseconds(400));
    CHECK(r.retry_delay(4) == milliseconds(800));
    CHECK(r.retry_delay(5) == milliseconds(1000));
    CHECK(r.retry_delay(50) == milliseconds(1000));
}

static void test_destruction_waits_for_the_attempt()
{
    FakeDevice d(0, milliseconds(80));
    {
        Reconnector r(milliseconds(1), milliseconds(4), milliseconds(0));
        d.link_up = false;
        r.update(d);                              // starts a slow attempt
        auto deadline = Reconnector::Clock::now() + std::chrono::seconds(2);
        while (d.attempts == 0 && Reconnector::Clock::now() < deadline)
            std::this_thread::sleep_for(milliseconds(1));
        CHECK(d.attempts == 1 && !d.attempt_finished);    // it is still running when we leave the scope
    }
    CHECK(d.attempt_finished);                    // so the destructor had to wait for it
}

int main()
{
    test_a_healthy_device_is_left_alone();
    test_recovers_after_failed_attempts();
    test_a_second_outage_is_handled_again();
    test_outage_is_hidden_until_it_lasts();
    test_retry_delay_doubles_up_to_the_limit();
    test_destruction_waits_for_the_attempt();
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all reconnect tests passed\n");
    return 0;
}
