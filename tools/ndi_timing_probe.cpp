#include <cstddef>
#include <Processing.NDI.Lib.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

// Transport timing only: synthesized NDI timecodes cannot prove lip sync.
int main(int argc, char** argv)
{
    std::string sourceName;
    int seconds=15;
    bool list=false;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--help") {
            std::cout<<"Usage: kavtor-ndi-timing --list | --source NAME [--seconds 1..3600]\n"
                     <<"Receive only; does not change mixer state. CSV timing is not content lip-sync measurement.\n";
            return 0;
        }
        if(arg=="--list") list=true;
        else if(arg=="--source"&&i+1<argc)sourceName=argv[++i];
        else if(arg=="--seconds"&&i+1<argc) {
            try {size_t used=0;std::string value=argv[++i];seconds=std::stoi(value,&used);if(used!=value.size())throw 1;}
            catch(...) {std::cerr<<"Invalid duration\n";return 2;}
        } else {std::cerr<<"Unknown or incomplete option: "<<arg<<'\n';return 2;}
    }
    if((!list&&sourceName.empty())||seconds<1||seconds>3600) {std::cerr<<"Specify --list or --source NAME, and a valid duration\n";return 2;}
    if(!NDIlib_initialize()) {std::cerr<<"NDI initialization failed\n";return 1;}
    auto finder=NDIlib_find_create_v2(nullptr);
    if(!finder){NDIlib_destroy();return 1;}
    std::this_thread::sleep_for(std::chrono::seconds(3));
    uint32_t count=0;
    auto sources=NDIlib_find_get_current_sources(finder,&count);
    NDIlib_recv_instance_t receiver=nullptr;
    for(uint32_t i=0;i<count;++i) {
        if(list)std::cout<<sources[i].p_ndi_name<<'\n';
        if(!list&&sourceName==sources[i].p_ndi_name) {
            NDIlib_recv_create_v3_t spec;
            spec.source_to_connect_to=sources[i];
            spec.color_format=NDIlib_recv_color_format_fastest;
            receiver=NDIlib_recv_create_v3(&spec);
            break;
        }
    }
    if(list){NDIlib_find_destroy(finder);NDIlib_destroy();return 0;}
    if(!receiver){std::cerr<<"NDI source not found: "<<sourceName<<'\n';NDIlib_find_destroy(finder);NDIlib_destroy();return 1;}
    using Clock=std::chrono::steady_clock;
    const auto begin=Clock::now();auto report=begin;
    uint64_t video=0,audio=0,lastVideo=0,lastAudio=0;
    int64_t videoTc=INT64_MIN,audioTc=INT64_MIN;
    unsigned videoGaps=0,audioGaps=0;
    double videoStep=0,audioStep=0,audioSeconds=0;
    std::cout<<"elapsed_s,video_frames,audio_packets,video_hz,audio_hz,audio_seconds,timecode_gap_ms,video_tc_gaps,audio_tc_gaps,sdk_video_total,sdk_video_dropped,sdk_audio_dropped\n";
    while(Clock::now()-begin<std::chrono::seconds(seconds)) {
        NDIlib_video_frame_v2_t v;NDIlib_audio_frame_v2_t a;NDIlib_metadata_frame_t m;
        const auto type=NDIlib_recv_capture_v2(receiver,&v,&a,&m,100);
        if(type==NDIlib_frame_type_video) {
            ++video;
            if(v.frame_rate_N>0&&v.frame_rate_D>0)videoStep=1e7*double(v.frame_rate_D)/v.frame_rate_N;
            if(v.timecode!=NDIlib_send_timecode_synthesize) {
                if(videoTc!=INT64_MIN&&videoStep>0&&std::abs(double(v.timecode-videoTc)-videoStep)>videoStep*0.5)++videoGaps;
                videoTc=v.timecode;
            }
            NDIlib_recv_free_video_v2(receiver,&v);
        } else if(type==NDIlib_frame_type_audio) {
            ++audio;
            if(a.sample_rate>0) {
                const double step=1e7*double(a.no_samples)/a.sample_rate;
                audioSeconds+=double(a.no_samples)/a.sample_rate;
                if(a.timecode!=NDIlib_send_timecode_synthesize) {
                    if(audioTc!=INT64_MIN&&audioStep>0&&std::abs(double(a.timecode-audioTc)-audioStep)>audioStep*0.5)++audioGaps;
                    audioTc=a.timecode;
                }
                audioStep=step;
            }
            NDIlib_recv_free_audio_v2(receiver,&a);
        } else if(type==NDIlib_frame_type_metadata)NDIlib_recv_free_metadata(receiver,&m);
        const auto now=Clock::now();
        if(now-report>=std::chrono::seconds(1)) {
            const double interval=std::chrono::duration<double>(now-report).count();
            std::cout<<std::chrono::duration<double>(now-begin).count()<<','<<video<<','<<audio<<','
                     <<double(video-lastVideo)/interval<<','<<double(audio-lastAudio)/interval<<','<<audioSeconds<<',';
            if(videoTc!=INT64_MIN&&audioTc!=INT64_MIN)std::cout<<double(audioTc-videoTc)/1e4;
            else std::cout<<"NA";
            // Synthesized wall-clock timecodes can jitter without losing frames.
            // SDK queue-drop counters are separate transport observations.
            NDIlib_recv_performance_t total, dropped;
            NDIlib_recv_get_performance(receiver, &total, &dropped);
            std::cout<<','<<videoGaps<<','<<audioGaps<<','<<total.video_frames
                     <<','<<dropped.video_frames<<','<<dropped.audio_frames<<std::endl;
            lastVideo=video;lastAudio=audio;report=now;
        }
    }
    NDIlib_recv_destroy(receiver);NDIlib_find_destroy(finder);NDIlib_destroy();
    if(!video||!audio){std::cerr<<"Missing video or audio; no timing conclusion is possible\n";return 1;}
    return 0;
}
