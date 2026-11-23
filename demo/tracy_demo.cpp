// Tracy demo for VxWorks 7 RTPs.
//
// A few worker threads do some arithmetic inside nested zones, the main
// thread marks frames, plots values and logs messages. Built with
// TRACY_NO_EXIT, the process stays alive after main() returns until a Tracy
// server (the GUI or `capture`) has connected and received all data.
//
// Usage: tracy_demo.vxe [frames]   (RTP, default 200 frames)
//        -> tracyDemo <frames>        (DKM, from the kernel shell after `ld`)

#include <tracy/Tracy.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running{true};
TracyLockable(std::mutex, g_resultLock);
double g_result = 0.0;

double Work(int n)
{
    ZoneScoped;
    double acc = 0.0;
    for (int i = 1; i <= n; ++i) {
        acc += 1.0 / (static_cast<double>(i) * i);
    }
    return acc;
}

void Inner(int id)
{
    ZoneScopedN("Inner");
    ZoneValue(id);
    const double r = Work(2000 + id * 500);
    std::lock_guard<LockableBase(std::mutex)> lock(g_resultLock);
    LockMark(g_resultLock);
    g_result += r;
}

void Worker(int id)
{
    const std::string name = "worker " + std::to_string(id);
    tracy::SetThreadName(name.c_str());
    while (g_running.load(std::memory_order_relaxed)) {
        ZoneScopedN("Worker iteration");
        ZoneColor(0x2080F0);
        Inner(id);
        Inner(id + 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(2 + id));
    }
}

} // namespace

static int RunDemo(int frames)
{
    tracy::SetThreadName("main");
    TracyMessageL("tracy_demo starting");

    // Tell the host script when the listen socket is open, then wait (up to
    // 60 s) for a server, so the whole run is captured.
    for (int i = 0; i < 100 && !tracy::IsDataPortListening(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::printf("tracy_demo: listening=%d, waiting for a server\n", tracy::IsDataPortListening() ? 1 : 0);
    std::fflush(stdout);
    for (int i = 0; i < 600 && !TracyIsConnected; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::printf("tracy_demo: server %s\n", TracyIsConnected ? "connected" : "not connected");
    std::fflush(stdout);

    std::vector<std::thread> workers;
    for (int i = 0; i < 3; ++i) {
        workers.emplace_back(Worker, i);
    }

    for (int frame = 0; frame < frames; ++frame) {
        {
            ZoneScopedN("Frame");
            Work(5000);
            double snapshot;
            {
                std::lock_guard<LockableBase(std::mutex)> lock(g_resultLock);
                LockMark(g_resultLock);
                snapshot = g_result;
            }
            TracyPlot("result", snapshot);
            TracyPlot("frame", static_cast<int64_t>(frame));
            if (frame % 50 == 0) {
                char msg[64];
                const int len = std::snprintf(msg, sizeof msg, "frame %d", frame);
                TracyMessage(msg, static_cast<size_t>(len));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        FrameMark;
    }

    g_running = false;
    for (auto& t : workers) {
        t.join();
    }
    TracyMessageL("tracy_demo done");
    std::printf("tracy_demo: %d frames done\n", frames);
    std::fflush(stdout);
    return 0;
}

#ifdef _WRS_KERNEL
// Kernel module entry point, called from the kernel shell.
extern "C" int tracyDemo(int frames)
{
    return RunDemo(frames > 0 ? frames : 200);
}
#else
int main(int argc, char** argv)
{
    return RunDemo(argc > 1 ? std::atoi(argv[1]) : 200);
}
#endif
