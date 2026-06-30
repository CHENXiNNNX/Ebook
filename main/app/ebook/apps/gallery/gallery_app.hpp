#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "apps/app.hpp"
#include "system/task/task.hpp"
#include "ui/list_view.hpp"
#include "ui/ui_event.hpp"

namespace app::ebook::apps::gallery {

/** @brief 相册：扫描 photos 目录，浏览 JPEG/JPG 图片 */
class GalleryApp : public App
{
  public:
    static GalleryApp& instance();

    AppId       id()      const override { return AppId::Gallery; }
    const char* title()   const override;
    uint32_t    icon_cp() const override;

    void on_enter() override;
    void on_exit()  override;
    void paint(gfx::Canvas& canvas) override;
    shell::InputResult on_input(const ::app::ebook::input::Event& ev) override;
    void on_ui_event(const ui::UiEvent& ev) override;

    static void request_open_on_enter(const char* path);

    /** 与 is_jpeg_name() / scan_dir() 一致，供空状态提示 */
    static constexpr const char* kScanFmtHint  = "JPG / JPEG";
    static constexpr const char* kScanPathInt  = "/int/Ebook/photos";
    static constexpr const char* kScanPathSd   = "/sd/Ebook/photos";

  private:
    GalleryApp();

    enum class View : uint8_t { List = 0, Viewer };

    void start_scan();
    void scan_photos();
    void open_photo(uint8_t idx);
    void show_prev();
    void show_next();
    void start_decode(const char* path);
    void cancel_decode();

    void fill_row(uint8_t idx, ui::widgets::RowStyle& rs);
    void on_row_tap(uint8_t idx);

    void paint_list(gfx::Canvas& c);
    void paint_viewer(gfx::Canvas& c);

    static void title_from_path(const char* path, char* out, size_t out_size);
    static bool is_jpeg_name(const char* name);
    void scan_dir(const char* dir, uint8_t& count);

    static void decode_worker(void* arg);

    static core::Rect viewer_rect();
    static core::Rect viewer_image_rect();

    View    view_{View::List};
    uint8_t sel_{0};
    bool    scanning_{false};
    bool    decoding_{false};
    uint8_t decode_job_id_{0};
    uint8_t decode_err_{0};
    bool    image_ready_{false};

    struct PhotoItem
    {
        char     path[128]{};
        char     title[40]{};
        uint32_t size{0};
    };

    static constexpr uint8_t  kMaxPhotos    = 48;
    static constexpr uint32_t kDecodeStack  = 8192;
    static constexpr uint32_t kScanStack    = 3072;
    static constexpr uint32_t kMaxJpegBytes = 2U * 1024U * 1024U;
    PhotoItem photos_[kMaxPhotos]{};
    char      size_buf_[kMaxPhotos][12]{};
    uint8_t   count_{0};

    char path_[128]{};
    char title_buf_[40]{};

    uint8_t* viewer_fb_{nullptr};
    core::Rect image_rect_{};

    std::unique_ptr<::app::sys::task::Task> scan_task_;
    std::unique_ptr<::app::sys::task::Task> decode_task_;
    ui::ListView                            list_;
};

} // namespace app::ebook::apps::gallery
