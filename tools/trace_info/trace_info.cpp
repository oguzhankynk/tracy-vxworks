// tracy-trace-info: prints the contents of a .tracy file and checks that the
// Tracy demo for VxWorks produced what it should.
//
// Usage: tracy-trace-info [--expect-frames N] <file.tracy>
// Exit status 0 if every check passes.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "../../tracy/server/TracyFileRead.hpp"
#include "../../tracy/server/TracyWorker.hpp"

static int g_failures = 0;

static void Check( bool ok, const char* what )
{
    printf( "  [%s] %s\n", ok ? "ok" : "FAIL", what );
    if( !ok ) g_failures++;
}

int main( int argc, char** argv )
{
    long expectFrames = -1;
    const char* file = nullptr;
    for( int i = 1; i < argc; i++ )
    {
        if( strcmp( argv[i], "--expect-frames" ) == 0 && i + 1 < argc ) expectFrames = atol( argv[++i] );
        else file = argv[i];
    }
    if( !file )
    {
        fprintf( stderr, "usage: %s [--expect-frames N] <file.tracy>\n", argv[0] );
        return 2;
    }
    std::unique_ptr<tracy::FileRead> f( tracy::FileRead::Open( file ) );
    if( !f )
    {
        fprintf( stderr, "cannot open %s\n", file );
        return 2;
    }
    tracy::Worker worker( *f );

    printf( "Program:   %s\n", worker.GetCaptureProgram().c_str() );
    printf( "PID:       %llu\n", (unsigned long long)worker.GetPid() );
    printf( "Host info:\n%s\n", worker.GetHostInfo().c_str() );

    printf( "Threads:\n" );
    std::map<std::string, uint64_t> threadZones;
    for( auto td : worker.GetThreadData() )
    {
        const char* name = worker.GetThreadName( td->id );
        printf( "  tid %-8llu %-20s zones %llu\n", (unsigned long long)td->id, name, (unsigned long long)td->count );
        threadZones[name] += td->count;
    }

    const auto& frames = *worker.GetFramesBase();
    const size_t frameCount = worker.GetFrameCount( frames );
    bool framesIncreasing = true;
    for( size_t i = 1; i < frameCount; i++ )
    {
        if( worker.GetFrameBegin( frames, i ) < worker.GetFrameBegin( frames, i - 1 ) ) framesIncreasing = false;
    }

    printf( "Zones:     %llu\n", (unsigned long long)worker.GetZoneCount() );
    printf( "Frames:    %zu\n", frameCount );
    printf( "Locks:     %llu lock events\n", (unsigned long long)worker.GetLockCount() );

    printf( "Plots:\n" );
    bool plotsIncreasing = true;
    size_t userPlots = 0;
    for( auto plot : worker.GetPlots() )
    {
        printf( "  %-20s %zu points\n", worker.GetString( plot->name ), plot->data.size() );
        if( plot->type == tracy::PlotType::User ) userPlots++;
        for( size_t i = 1; i < plot->data.size(); i++ )
        {
            if( plot->data[i].time.Val() < plot->data[i-1].time.Val() ) plotsIncreasing = false;
        }
    }

    printf( "Messages:\n" );
    const auto& msgs = worker.GetMessages();
    bool msgsIncreasing = true;
    int64_t prev = INT64_MIN;
    for( auto& m : msgs )
    {
        printf( "  %s\n", worker.GetString( m->ref ) );
        if( m->time < prev ) msgsIncreasing = false;
        prev = m->time;
    }

    printf( "Checks:\n" );
    Check( worker.GetZoneCount() > 0, "trace has zones" );
    // The server shows the client's main thread as "Main thread".
    Check( threadZones["Main thread"] + threadZones["main"] > 0, "main thread has zones" );
    int workers = 0;
    for( auto& kv : threadZones ) if( kv.first.rfind( "worker ", 0 ) == 0 && kv.second > 0 ) workers++;
    Check( workers == 3, "3 named worker threads have zones" );
    if( expectFrames >= 0 ) Check( (long)frameCount >= expectFrames, "frame count >= expected" );
    Check( framesIncreasing, "frame timestamps increase" );
    Check( userPlots >= 2, "user plots 'result' and 'frame' present" );
    Check( plotsIncreasing, "plot timestamps increase" );
    Check( msgs.size() >= 2, "messages present" );
    Check( msgsIncreasing, "message timestamps increase" );
    Check( worker.GetCaptureProgram() == "tracy_demo.vxe", "program name is tracy_demo.vxe" );
    Check( worker.GetHostInfo().find( "OS: VxWorks" ) != std::string::npos, "host info reports VxWorks" );
    printf( "%s\n", g_failures ? "TRACE CHECK FAILED" : "TRACE CHECK PASSED" );
    return g_failures ? 1 : 0;
}
