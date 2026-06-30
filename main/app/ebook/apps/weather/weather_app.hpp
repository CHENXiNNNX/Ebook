#pragma once

#include <cstdint>
#include <memory>

#include "apps/app.hpp"
#include "system/task/task.hpp"
#include "ui/list_view.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::apps::weather {

/**
 * @brief 天气：Main / CityPick；高德 HTTP 拉取，结果经 SystemHint::WeatherFetchDone 回 UI
 */
class WeatherApp : public App
{
  public:
    static WeatherApp& instance();

    AppId       id()      const override { return AppId::Weather; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    void on_enter() override;
    void on_exit()  override;
    void paint(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;
    void on_ui_event(const ui::UiEvent& ev) override;

  private:
    WeatherApp() = default;

    enum class View : uint8_t { Main = 0, CityPick };

    void start_fetch();
    void apply_ip_locate();
    static void fetch_worker(void* arg);

    void paint_main(gfx::Canvas& c);
    void paint_city_pick(gfx::Canvas& c);

    shell::InputResult handle_main(int16_t x, int16_t y);
    shell::InputResult handle_city_pick(int16_t x, int16_t y);

    View          view_{View::Main};
    uint8_t       city_idx_{0};
    bool          fetching_{false};
    volatile bool fetch_cancel_{false};
    volatile bool ip_locate_next_{false};

    bool     data_valid_{false};
    int16_t  temp_c_{0};
    int16_t  temp_max_{0};
    int16_t  temp_min_{0};
    char     weather_desc_[16]{};

    uint8_t last_error_{0};

    ui::ListView city_list_{};

    std::unique_ptr<::app::sys::task::Task> fetch_task_;
};

} // namespace app::ebook::apps::weather
