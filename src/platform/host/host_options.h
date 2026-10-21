#pragma once
// The development host's command line (the remake's capture options, README "Useful game options").
#include <cstdlib>
#include <string>

namespace t3 {

struct HostOptions {
    std::string data_root = "D:/Tekken 3/Tekken3Remake/remake/imported";
    std::string user_dir = "D:/Tekken 3/_tools/host_user";
    std::string screenshot;      // --screenshot FILE.png: save that frame and quit
    int frames = 1;              // --frames N
    int width = 1280, height = 720;
    bool fixed_step = false;     // --fixed-step: the clock advances exactly 1/60 s a frame
    std::string pads;            // --pads FILE: a pad script
    std::string scene;           // --scene NAME: what the app shows (development)
    std::string replay;          // --replay SCENARIO: play a flow trace's pads (the remake's --replay=)
    int replay_frames = -1;      // --replay-frames N: its first N steps
    long long seed = -1;         // --seed N: the random generators (the remake's --seed=)
    int speed = 1;               // --speed N: steps per tick
    bool touch = false;          // --touch: the on-screen touch controls (the mouse is a finger)
    std::string touch_edit;      // --touch-edit KEY: their layout edit open, KEY ("Cross", "Stick") selected; ADD: the wizard
    std::string text_answer;     // --text-answer TEXT: the answer to every text question (Platform::ask_text)
    bool no_movies = false;      // --no-movies: play without the movies (the remake's --no-movies)
    int lose_context_at = -1;    // --lose-context-at N: before frame N the GL context is destroyed and made again (App::graphics_lost)
    std::string audio_out;       // --audio-out FILE.wav: with --screenshot, the mix of every frame (1/60 s) written
    bool replay_writes = false;  // --replay-writes: the replay's memory card reads and writes the save file
    // `--scene stages` (the remake's app/stage_viewer.gd): the stage, the free camera about the
    // midpoint (game units: yaw, distance, height up, pitch), the backdrop's turn and the backdrops.
    int stage = 0, yaw = 0, turn = 0, distance = 5300, camera_height = 1112, pitch = 0;
    std::string backdrops = "arcade";

    static HostOptions parse(int argc, char** argv) {
        HostOptions o;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
            if (a == "--data") o.data_root = next();
            else if (a == "--user") o.user_dir = next();
            else if (a == "--screenshot") o.screenshot = next();
            else if (a == "--frames") o.frames = std::atoi(next().c_str());
            else if (a == "--fixed-step") o.fixed_step = true;
            else if (a == "--pads") o.pads = next();
            else if (a == "--scene") o.scene = next();
            else if (a == "--replay") o.replay = next();
            else if (a == "--replay-frames") o.replay_frames = std::atoi(next().c_str());
            else if (a == "--seed") o.seed = std::atoll(next().c_str());
            else if (a == "--speed") o.speed = std::atoi(next().c_str());
            else if (a == "--touch") o.touch = true;
            else if (a == "--touch-edit") o.touch_edit = next();
            else if (a == "--text-answer") o.text_answer = next();
            else if (a == "--no-movies") o.no_movies = true;
            else if (a == "--lose-context-at") o.lose_context_at = std::atoi(next().c_str());
            else if (a == "--audio-out") o.audio_out = next();
            else if (a == "--replay-writes") o.replay_writes = true;
            else if (a == "--stage") o.stage = std::atoi(next().c_str());
            else if (a == "--yaw") o.yaw = std::atoi(next().c_str());
            else if (a == "--turn") o.turn = std::atoi(next().c_str());
            else if (a == "--distance") o.distance = std::atoi(next().c_str());
            else if (a == "--height") o.camera_height = std::atoi(next().c_str());
            else if (a == "--pitch") o.pitch = std::atoi(next().c_str());
            else if (a == "--backdrops") o.backdrops = next();
            else if (a == "--size") {
                std::string s = next();
                size_t x = s.find('x');
                if (x != std::string::npos) {
                    o.width = std::atoi(s.substr(0, x).c_str());
                    o.height = std::atoi(s.substr(x + 1).c_str());
                }
            }
        }
        if (!o.screenshot.empty()) o.fixed_step = true;
        return o;
    }
};

}  // namespace t3
