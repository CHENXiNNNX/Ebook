#pragma once

#include <cstdint>
#include <memory>

#include "esp_audio_simple_dec.h"
#include "system/task/task.hpp"

namespace app::ebook::platform {

enum class MusicPlayState : uint8_t
{
    Idle = 0,
    Loading,
    Playing,
    Paused,
    Error,
};

/** @brief 本地文件播放（esp_audio_simple_dec + ES8311；PSRAM 大栈 Task） */
class MusicPlayer
{
  public:
    static MusicPlayer& get_instance();

    bool init();
    void stop();

    MusicPlayState state() const { return state_; }

    bool play(const char* path);
    void pause();
    void resume();

    const char* current_path() const { return path_; }

    uint16_t elapsed_sec() const;
    uint16_t duration_sec() const { return duration_sec_; }

    /** last_error: 2=fopen 3=mem 4=decode 5=codec 6=dec_reg */
    uint8_t last_error() const { return last_error_; }

    bool stop_requested() const { return stop_req_; }

  private:
    MusicPlayer() = default;

    void run_playback(const char* path);
    void set_state(MusicPlayState s);
    void post_state_hint();
    void post_progress_hint();
    bool try_start_output(esp_audio_simple_dec_handle_t dec);
    bool decode_raw(esp_audio_simple_dec_handle_t dec,
                    esp_audio_simple_dec_raw_t* raw,
                    uint8_t** out_buf, size_t* out_cap,
                    bool* started, bool* failed);
    bool ensure_decoders();
    void clear_play_task();

    volatile bool stop_req_{false};
    volatile bool pause_end_{false};
    MusicPlayState state_{MusicPlayState::Idle};
    uint8_t        last_error_{0};

    char     path_[128]{};
    uint16_t duration_sec_{0};
    int64_t  play_start_us_{0};

    bool          dec_registered_{false};
    volatile bool play_task_alive_{false};
    std::unique_ptr<::app::sys::task::Task> play_task_;
};

} // namespace app::ebook::platform
