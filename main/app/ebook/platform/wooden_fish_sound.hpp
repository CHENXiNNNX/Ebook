#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "system/task/task.hpp"

namespace app::ebook::platform {

/** @brief 木鱼敲击短音（启动期预加载 PCM；play_knock 仅 write） */
class WoodenFishSound
{
  public:
    static WoodenFishSound& get_instance();

    bool init();
    bool ready() const { return ready_; }

    /** 静音/音乐播放中/上一段未播完时跳过 */
    bool play_knock();

  private:
    WoodenFishSound() = default;

    void run_play();
    void clear_play_task();

    bool     ready_{false};
    uint32_t dst_rate_{16000};
    int16_t* pcm_{nullptr};
    size_t   pcm_count_{0};

    volatile bool play_task_alive_{false};
    std::unique_ptr<::app::sys::task::Task> play_task_;
};

} // namespace app::ebook::platform
