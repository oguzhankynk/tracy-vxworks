// vx_probe: prints which OS facilities Tracy needs are available in a
// VxWorks RTP. Used while porting; see README.md.

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <rtpLib.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <taskLib.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>
#include <cpuid.h>

#include <chrono>
#include <thread>

int main()
{
    // TSC
    unsigned a, b, c, d;
    __get_cpuid(1, &a, &b, &c, &d);
    printf("probe: cpuid.1 edx.tsc=%u\n", (d >> 4) & 1);
    __get_cpuid(0x80000007, &a, &b, &c, &d);
    printf("probe: cpuid.80000007 edx.invariant_tsc=%u\n", (d >> 8) & 1);
    unsigned long long t0 = __rdtsc();
    unsigned long long t1 = __rdtsc();
    printf("probe: rdtsc works, increasing=%d\n", t1 > t0);

    // clocks
    struct timespec res;
    clock_getres(CLOCK_MONOTONIC, &res);
    printf("probe: clock_getres(CLOCK_MONOTONIC) = %ld s %ld ns\n", (long)res.tv_sec, res.tv_nsec);
    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    int distinct = 0;
    long long last = ts0.tv_sec * 1000000000LL + ts0.tv_nsec;
    for (int i = 0; i < 100000; ++i) {
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        long long now = ts1.tv_sec * 1000000000LL + ts1.tv_nsec;
        if (now != last) ++distinct;
        last = now;
    }
    printf("probe: distinct CLOCK_MONOTONIC values in 100000 reads: %d\n", distinct);
    printf("probe: high_resolution_clock is_steady=%d\n",
           (int)std::chrono::high_resolution_clock::is_steady);

    // CPU count
    printf("probe: hardware_concurrency=%u\n", std::thread::hardware_concurrency());
#ifdef _SC_NPROCESSORS_ONLN
    printf("probe: sysconf(_SC_NPROCESSORS_ONLN)=%ld\n", sysconf(_SC_NPROCESSORS_ONLN));
#else
    printf("probe: _SC_NPROCESSORS_ONLN not defined\n");
#endif

    // OS / user / host
    struct utsname u;
    if (uname(&u) == 0)
        printf("probe: uname sysname='%s' release='%s' version='%s' machine='%s' kernelversion='%s'\n",
               u.sysname, u.release, u.version, u.machine, u.kernelversion);
    char buf[256] = {};
    int r = getlogin_r(buf, sizeof buf);
    printf("probe: getlogin_r=%d '%s'\n", r, buf);
    memset(buf, 0, sizeof buf);
    r = gethostname(buf, sizeof buf);
    printf("probe: gethostname=%d '%s'\n", r, buf);

    // process / task
    printf("probe: getpid=%d taskIdSelf=%d taskName='%s'\n", (int)getpid(), (int)taskIdSelf(),
           taskName(taskIdSelf()));
    RTP_DESC desc;
    memset(&desc, 0, sizeof desc);
    STATUS st = rtpInfoGet((RTP_ID)getpid(), &desc);
    printf("probe: rtpInfoGet=%d pathName='%s'\n", (int)st, desc.pathName);
    std::thread th([] {
        pthread_setname_np(pthread_self(), "probeThread");
        char n[32] = {};
        pthread_getname_np(pthread_self(), n, sizeof n);
        printf("probe: thread taskIdSelf=%d pthread name='%s' taskName='%s'\n", (int)taskIdSelf(), n,
               taskName(taskIdSelf()));
    });
    th.join();

    // pipe-based safe read (Tracy's SafeCopyProlog)
    int fds[2];
    r = pipe(fds);
    printf("probe: pipe=%d errno=%d\n", r, r ? errno : 0);
    if (r == 0) {
        fcntl(fds[1], F_SETFL, O_NONBLOCK);
        char good[64] = "hello";
        ssize_t w = write(fds[1], good, sizeof good);
        char back[64];
        ssize_t rd = read(fds[0], back, (size_t)w);
        printf("probe: pipe write good=%zd read=%zd\n", w, rd);
        static char big[1 << 20];
        w = write(fds[1], big, sizeof big);
        printf("probe: pipe write 1MiB nonblocking=%zd errno=%d\n", w, w < 0 ? errno : 0);
        if (w > 0) {
            static char sink[1 << 20];
            ssize_t rr = read(fds[0], sink, (size_t)w);
            (void)rr;
        }
        fflush(stdout);
        w = write(fds[1], reinterpret_cast<const void*>(0x10), 16);
        printf("probe: pipe write bad pointer=%zd errno=%d (%s)\n", w, w < 0 ? errno : 0,
               w < 0 ? strerror(errno) : "-");
    }
    printf("probe: done\n");
    return 0;
}
