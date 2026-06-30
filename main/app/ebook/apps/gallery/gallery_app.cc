#include "apps/gallery/gallery_app.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include <dirent.h>
#include <esp_heap_caps.h>
#include <jpeg_decoder.h>

#include "bsp/driver/gdey027t91/framebuffer.hpp"
#include "gfx/icon.hpp"
#include "gfx/text_layout.hpp"
#include "overlays/toast.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "ui/ui_bus.hpp"
#include "ui/widgets.hpp"

namespace app::ebook::apps::gallery {

namespace {

using ui::Theme;
using Fb = ::app::bsp::driver::gdey027t91::Framebuffer;

constexpr const char* kPhotoRoot   = "/int/Ebook";
constexpr const char* kPhotoDirInt = GalleryApp::kScanPathInt;
constexpr const char* kPhotoDirSd  = GalleryApp::kScanPathSd;

bool s_open_on_enter_ = false;
constexpr uint8_t kOpenPathCap = 96;
char s_open_path_[kOpenPathCap]{};

uint8_t rgb565_gray(uint16_t px)
{
    const uint8_t r5 = static_cast<uint8_t>((px >> 11) & 0x1FU);
    const uint8_t g6 = static_cast<uint8_t>((px >> 5) & 0x3FU);
    const uint8_t b5 = static_cast<uint8_t>(px & 0x1FU);
    const uint8_t r  = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    const uint8_t g  = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    const uint8_t b  = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    return static_cast<uint8_t>((static_cast<uint16_t>(r) * 77U +
                                 static_cast<uint16_t>(g) * 150U +
                                 static_cast<uint16_t>(b) * 29U) >> 8);
}

uint8_t sample_gray_bilinear(const uint8_t* src, uint16_t sw, uint16_t sh,
                             float sx, float sy)
{
    if (src == nullptr || sw == 0 || sh == 0)
        return 0;

    if (sx < 0.0f)
        sx = 0.0f;
    if (sy < 0.0f)
        sy = 0.0f;
    if (sx > static_cast<float>(sw - 1U))
        sx = static_cast<float>(sw - 1U);
    if (sy > static_cast<float>(sh - 1U))
        sy = static_cast<float>(sh - 1U);

    const uint16_t x0 = static_cast<uint16_t>(sx);
    const uint16_t y0 = static_cast<uint16_t>(sy);
    const uint16_t x1 = (x0 + 1U < sw) ? static_cast<uint16_t>(x0 + 1U) : x0;
    const uint16_t y1 = (y0 + 1U < sh) ? static_cast<uint16_t>(y0 + 1U) : y0;
    const float    fx = sx - static_cast<float>(x0);
    const float    fy = sy - static_cast<float>(y0);
    const float    w00 = (1.0f - fx) * (1.0f - fy);
    const float    w10 = fx * (1.0f - fy);
    const float    w01 = (1.0f - fx) * fy;
    const float    w11 = fx * fy;

    const float v =
        static_cast<float>(src[static_cast<uint32_t>(y0) * sw + x0]) * w00 +
        static_cast<float>(src[static_cast<uint32_t>(y0) * sw + x1]) * w10 +
        static_cast<float>(src[static_cast<uint32_t>(y1) * sw + x0]) * w01 +
        static_cast<float>(src[static_cast<uint32_t>(y1) * sw + x1]) * w11;

    if (v <= 0.0f)
        return 0;
    if (v >= 255.0f)
        return 255;
    return static_cast<uint8_t>(v + 0.5f);
}

void fit_draw_size(uint16_t src_w, uint16_t src_h, uint16_t max_w, uint16_t max_h,
                   uint16_t& dst_w, uint16_t& dst_h)
{
    if (src_w == 0 || src_h == 0 || max_w == 0 || max_h == 0)
    {
        dst_w = src_w;
        dst_h = src_h;
        return;
    }

    const float sx = static_cast<float>(max_w) / static_cast<float>(src_w);
    const float sy = static_cast<float>(max_h) / static_cast<float>(src_h);
    const float  s = (sx < sy) ? sx : sy;

    dst_w = static_cast<uint16_t>(static_cast<float>(src_w) * s + 0.5f);
    dst_h = static_cast<uint16_t>(static_cast<float>(src_h) * s + 0.5f);
    if (dst_w == 0)
        dst_w = 1;
    if (dst_h == 0)
        dst_h = 1;
    if (dst_w > max_w)
        dst_w = max_w;
    if (dst_h > max_h)
        dst_h = max_h;
}

bool scale_gray_fit(const uint8_t* src, uint16_t src_w, uint16_t src_h,
                    uint8_t* dst, uint16_t dst_w, uint16_t dst_h)
{
    if (src == nullptr || dst == nullptr || src_w == 0 || src_h == 0 ||
        dst_w == 0 || dst_h == 0)
        return false;

    for (uint16_t y = 0; y < dst_h; ++y)
    {
        const float sy =
            (static_cast<float>(y) + 0.5f) * static_cast<float>(src_h) /
                static_cast<float>(dst_h) -
            0.5f;
        for (uint16_t x = 0; x < dst_w; ++x)
        {
            const float sx =
                (static_cast<float>(x) + 0.5f) * static_cast<float>(src_w) /
                    static_cast<float>(dst_w) -
                0.5f;
            dst[static_cast<uint32_t>(y) * dst_w + x] =
                sample_gray_bilinear(src, src_w, src_h, sx, sy);
        }
    }
    return true;
}

void dither_gray_to_fb(uint8_t* gray, uint16_t w, uint16_t h, uint8_t* fb,
                       int16_t ox, int16_t oy)
{
    if (gray == nullptr || fb == nullptr || w == 0 || h == 0)
        return;

    for (uint16_t y = 0; y < h; ++y)
    {
        for (uint16_t x = 0; x < w; ++x)
        {
            const uint32_t idx = static_cast<uint32_t>(y) * w + x;
            int16_t        old = static_cast<int16_t>(gray[idx]);
            if (old < 0)
                old = 0;
            if (old > 255)
                old = 255;

            const uint8_t new_px = (old < 128) ? 0U : 255U;
            const int16_t err    = static_cast<int16_t>(old - static_cast<int16_t>(new_px));
            gray[idx]            = new_px;

            Fb::set_pixel(fb, static_cast<int16_t>(ox + x),
                          static_cast<int16_t>(oy + y), new_px == 0U);

            if (x + 1U < w)
            {
                int16_t v = static_cast<int16_t>(gray[idx + 1U]) +
                            static_cast<int16_t>((err * 7) / 16);
                gray[idx + 1U] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
            if (y + 1U < h)
            {
                if (x > 0)
                {
                    const uint32_t li = static_cast<uint32_t>(y + 1U) * w + (x - 1U);
                    int16_t        v  = static_cast<int16_t>(gray[li]) +
                                       static_cast<int16_t>((err * 3) / 16);
                    gray[li] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
                {
                    const uint32_t ci = static_cast<uint32_t>(y + 1U) * w + x;
                    int16_t        v  = static_cast<int16_t>(gray[ci]) +
                                       static_cast<int16_t>((err * 5) / 16);
                    gray[ci] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
                if (x + 1U < w)
                {
                    const uint32_t ri = static_cast<uint32_t>(y + 1U) * w + (x + 1U);
                    int16_t        v  = static_cast<int16_t>(gray[ri]) +
                                       static_cast<int16_t>((err * 1) / 16);
                    gray[ri] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
            }
        }
    }
}

uint8_t jpeg_scale_div(esp_jpeg_image_scale_t scale)
{
    switch (scale)
    {
        case JPEG_IMAGE_SCALE_0:   return 1U;
        case JPEG_IMAGE_SCALE_1_2: return 2U;
        case JPEG_IMAGE_SCALE_1_4: return 4U;
        case JPEG_IMAGE_SCALE_1_8: return 8U;
        default:                   return 1U;
    }
}

bool ends_with_ci(const char* name, const char* ext)
{
    if (name == nullptr || ext == nullptr)
        return false;
    const size_t nlen = std::strlen(name);
    const size_t elen = std::strlen(ext);
    if (nlen < elen)
        return false;
    const char* tail = name + nlen - elen;
    for (size_t i = 0; i < elen; ++i)
    {
        const unsigned char a = static_cast<unsigned char>(tail[i]);
        const unsigned char b = static_cast<unsigned char>(ext[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

} // namespace

GalleryApp& GalleryApp::instance()
{
    static GalleryApp s;
    return s;
}

GalleryApp::GalleryApp()
{
    list_.set_area(Theme::list_region());
    list_.set_provider([this](uint8_t i, ui::widgets::RowStyle& rs) { fill_row(i, rs); });
    list_.set_tap_handler([this](uint8_t i) { on_row_tap(i); });
}

const char* GalleryApp::title() const
{
    if (view_ == View::Viewer && title_buf_[0] != '\0')
        return title_buf_;
    return ui::strings::kAppGallery;
}

uint32_t GalleryApp::icon_cp() const { return gfx::icon::kFaImages; }

core::Rect GalleryApp::viewer_rect()
{
    return core::Rect{0, Theme::kContentY, Theme::kScreenW,
                      static_cast<uint16_t>(Theme::kScreenH - Theme::kContentY)};
}

core::Rect GalleryApp::viewer_image_rect()
{
    return viewer_rect();
}

bool GalleryApp::is_jpeg_name(const char* name)
{
    if (name == nullptr || name[0] == '.')
        return false;
    return ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg");
}

void GalleryApp::title_from_path(const char* path, char* out, size_t out_size)
{
    if (path == nullptr || out == nullptr || out_size == 0)
        return;
    const char* name = std::strrchr(path, '/');
    name             = (name != nullptr) ? (name + 1) : path;
    (void)std::strncpy(out, name, out_size - 1);
    out[out_size - 1] = '\0';
    char* dot         = std::strrchr(out, '.');
    if (dot != nullptr)
        *dot = '\0';
}

void GalleryApp::scan_dir(const char* dir, uint8_t& count)
{
    if (dir == nullptr || count >= kMaxPhotos)
        return;

    struct stat st{};
    if (::stat(dir, &st) != 0)
        return;

    DIR* d = ::opendir(dir);
    if (d == nullptr)
        return;

    struct dirent* ent = nullptr;
    while ((ent = ::readdir(d)) != nullptr && count < kMaxPhotos)
    {
        if (!is_jpeg_name(ent->d_name))
            continue;

        PhotoItem& item = photos_[count];
        const int plen  = std::snprintf(item.path, sizeof(item.path), "%s/%s", dir, ent->d_name);
        if (plen < 0 || static_cast<size_t>(plen) >= sizeof(item.path))
            continue;

        title_from_path(item.path, item.title, sizeof(item.title));
        if (::stat(item.path, &st) == 0)
            item.size = static_cast<uint32_t>(st.st_size);
        else
            item.size = 0;

        ++count;
    }
    ::closedir(d);
}

void GalleryApp::scan_photos()
{
    count_ = 0;
    (void)::mkdir(kPhotoRoot, 0775);
    (void)::mkdir(kPhotoDirInt, 0775);
    (void)::mkdir(kPhotoDirSd, 0775);

    scan_dir(kPhotoDirInt, count_);
    scan_dir(kPhotoDirSd, count_);

    for (uint8_t i = 0; i + 1 < count_; ++i)
    {
        for (uint8_t j = 0; j + 1 < count_ - i; ++j)
        {
            if (std::strcmp(photos_[j].title, photos_[j + 1].title) > 0)
            {
                PhotoItem tmp = photos_[j];
                photos_[j]    = photos_[j + 1];
                photos_[j + 1] = tmp;
            }
        }
    }
}

void GalleryApp::request_open_on_enter(const char* path)
{
    if (path == nullptr || path[0] == '\0')
        return;
    (void)std::strncpy(s_open_path_, path, kOpenPathCap - 1);
    s_open_path_[kOpenPathCap - 1] = '\0';
    s_open_on_enter_               = true;
}

void GalleryApp::on_enter()
{
    view_         = View::List;
    sel_          = 0;
    decoding_   = false;
    decode_err_ = 0;
    image_ready_  = false;
    path_[0]      = '\0';
    title_buf_[0] = '\0';
    list_.set_scroll(0);

    if (viewer_fb_ == nullptr)
    {
        viewer_fb_ = static_cast<uint8_t*>(
            heap_caps_malloc(core::kFbBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (viewer_fb_ == nullptr)
            viewer_fb_ = static_cast<uint8_t*>(heap_caps_malloc(core::kFbBytes, MALLOC_CAP_8BIT));
    }

    start_scan();
}

void GalleryApp::on_exit()
{
    cancel_decode();
    scan_task_.reset();
    scanning_ = false;
    view_     = View::List;
}

void GalleryApp::start_scan()
{
    if (scanning_)
        return;
    scanning_ = true;
    request_repaint();

    scan_task_.reset();

    ::app::sys::task::Cfg cfg = ::app::sys::task::Cfg::light("gallery_scan",
                                                             ::app::sys::task::Priority::LOW);
    cfg.stack_size = kScanStack;
    cfg.use_psram  = true;

    scan_task_ = std::make_unique<::app::sys::task::Task>(
        [](void* arg) {
            auto* self = static_cast<GalleryApp*>(arg);
            self->scan_photos();
            (void)ui::UiBus::get_instance().post_system_hint(
                ui::SystemHintKind::GalleryScanDone, 0);
        },
        cfg, this);

    if (!scan_task_->start())
    {
        scan_task_.reset();
        scanning_ = false;
        scan_photos();
        request_repaint();
    }
}

void GalleryApp::on_ui_event(const ui::UiEvent& ev)
{
    if (ev.kind != ui::UiEventKind::SystemHint)
        return;

    switch (ev.payload.system.hint)
    {
        case ui::SystemHintKind::GalleryScanDone:
        {
            scanning_ = false;
            scan_task_.reset();
            list_.set_total(count_);
            list_.set_scroll(0);

            if (s_open_on_enter_)
            {
                s_open_on_enter_ = false;
                for (uint8_t i = 0; i < count_; ++i)
                {
                    if (std::strcmp(photos_[i].path, s_open_path_) == 0)
                    {
                        open_photo(i);
                        return;
                    }
                }
                (void)std::strncpy(path_, s_open_path_, sizeof(path_) - 1);
                path_[sizeof(path_) - 1] = '\0';
                title_from_path(path_, title_buf_, sizeof(title_buf_));
                view_ = View::Viewer;
                start_decode(path_);
            }
            request_repaint();
            break;
        }
        case ui::SystemHintKind::GalleryDecodeDone:
        {
            const uint32_t packed = ev.payload.system.value;
            const uint8_t  job_id = static_cast<uint8_t>(packed >> 8);
            const uint8_t  code   = static_cast<uint8_t>(packed & 0xFFU);
            if (job_id != decode_job_id_)
                return;
            decoding_    = false;
            decode_err_  = code;
            image_ready_ = (decode_err_ == 0);
            decode_task_.reset();
            request_repaint();
            break;
        }
        default:
            break;
    }
}

void GalleryApp::fill_row(uint8_t idx, ui::widgets::RowStyle& rs)
{
    rs = {};
    if (idx >= count_)
        return;
    rs.label        = photos_[idx].title;
    rs.value        = size_buf_[idx];
    rs.icon_cp      = gfx::icon::kFaImages;
    rs.show_chevron = true;
    gfx::format_file_size(photos_[idx].size, size_buf_[idx], sizeof(size_buf_[idx]));
}

void GalleryApp::open_photo(uint8_t idx)
{
    if (idx >= count_)
        return;
    sel_ = idx;
    (void)std::strncpy(path_, photos_[idx].path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';
    title_from_path(path_, title_buf_, sizeof(title_buf_));
    view_        = View::Viewer;
    image_ready_ = false;
    decode_err_  = 0;
    start_decode(path_);
}

void GalleryApp::on_row_tap(uint8_t idx)
{
    if (scanning_ || decoding_)
        return;
    open_photo(idx);
}

void GalleryApp::show_prev()
{
    if (count_ == 0 || decoding_)
        return;
    if (sel_ == 0)
        sel_ = static_cast<uint8_t>(count_ - 1);
    else
        --sel_;
    open_photo(sel_);
}

void GalleryApp::show_next()
{
    if (count_ == 0 || decoding_)
        return;
    sel_ = static_cast<uint8_t>((sel_ + 1U) % count_);
    open_photo(sel_);
}

void GalleryApp::cancel_decode()
{
    ++decode_job_id_;
    decode_task_.reset();
    decoding_ = false;
}

void GalleryApp::start_decode(const char* path)
{
    cancel_decode();
    decoding_ = true;
    image_ready_   = false;
    decode_err_    = 0;
    request_repaint();

    decode_task_.reset();

    (void)std::strncpy(path_, path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';

    ::app::sys::task::Cfg cfg = ::app::sys::task::Cfg::light("gallery_dec",
                                                             ::app::sys::task::Priority::LOW);
    cfg.stack_size = kDecodeStack;
    cfg.use_psram  = true;

    decode_task_ = std::make_unique<::app::sys::task::Task>(
        &GalleryApp::decode_worker, cfg, this);

    if (!decode_task_->start())
    {
        decode_task_.reset();
        decoding_   = false;
        decode_err_ = 4;
        request_repaint();
    }
}

void GalleryApp::decode_worker(void* arg)
{
    auto* self = static_cast<GalleryApp*>(arg);
    const uint8_t job_id = self->decode_job_id_;
    uint8_t       err    = 0;

    if (self->viewer_fb_ == nullptr)
    {
        err = 4;
        goto done;
    }

    {
        const char* path = self->path_;
        struct stat st{};
        if (::stat(path, &st) != 0)
        {
            err = 1;
            goto done;
        }
        const uint32_t file_sz = static_cast<uint32_t>(st.st_size);
        if (file_sz == 0 || file_sz > kMaxJpegBytes)
        {
            err = 2;
            goto done;
        }

        uint8_t* file_buf = static_cast<uint8_t*>(
            heap_caps_malloc(file_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (file_buf == nullptr)
            file_buf = static_cast<uint8_t*>(heap_caps_malloc(file_sz, MALLOC_CAP_8BIT));
        if (file_buf == nullptr)
        {
            err = 4;
            goto done;
        }

        FILE* f = std::fopen(path, "rb");
        if (f == nullptr)
        {
            heap_caps_free(file_buf);
            err = 1;
            goto done;
        }
        const size_t nread = std::fread(file_buf, 1, file_sz, f);
        std::fclose(f);
        if (nread != file_sz)
        {
            heap_caps_free(file_buf);
            err = 1;
            goto done;
        }

        const core::Rect vr   = viewer_image_rect();
        const uint16_t max_w = vr.w;
        const uint16_t max_h = vr.h;

        Fb::fill(self->viewer_fb_, core::kFbBytes, Fb::kWhite);

        static const esp_jpeg_image_scale_t scales[] = {
            JPEG_IMAGE_SCALE_0,
            JPEG_IMAGE_SCALE_1_2,
            JPEG_IMAGE_SCALE_1_4,
            JPEG_IMAGE_SCALE_1_8,
        };

        const uint32_t max_rgb_bytes =
            static_cast<uint32_t>(max_w) * max_h * 2U;

        esp_jpeg_image_cfg_t    cfg{};
        cfg.indata                 = file_buf;
        cfg.indata_size            = file_sz;
        cfg.out_format             = JPEG_IMAGE_FORMAT_RGB565;
        cfg.flags.swap_color_bytes = 1;

        esp_jpeg_image_output_t pick_info{};
        bool                    have_scale = false;

        for (size_t si = 0; si < sizeof(scales) / sizeof(scales[0]); ++si)
        {
            cfg.out_scale = scales[si];
            esp_jpeg_image_output_t info{};
            if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK)
                continue;
            if (info.width == 0 || info.height == 0)
                continue;

            const uint8_t scale_div = jpeg_scale_div(scales[si]);
            const uint16_t scaled_w =
                static_cast<uint16_t>(info.width / scale_div);
            const uint16_t scaled_h =
                static_cast<uint16_t>(info.height / scale_div);
            if (scaled_w == 0 || scaled_h == 0)
                continue;
            if (scaled_w > max_w || scaled_h > max_h)
                continue;
            if (info.output_len == 0 ||
                info.output_len > max_rgb_bytes)
                continue;

            pick_info  = info;
            have_scale = true;
            break;
        }

        bool                    decoded = false;
        esp_jpeg_image_output_t outimg{};
        uint8_t*                rgb_buf = nullptr;

        if (have_scale)
        {
            const uint32_t out_cap = static_cast<uint32_t>(pick_info.output_len);
            rgb_buf = static_cast<uint8_t*>(
                heap_caps_malloc(out_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (rgb_buf == nullptr)
                rgb_buf = static_cast<uint8_t*>(heap_caps_malloc(out_cap, MALLOC_CAP_8BIT));

            if (rgb_buf != nullptr)
            {
                cfg.outbuf      = rgb_buf;
                cfg.outbuf_size = out_cap;
                if (esp_jpeg_decode(&cfg, &outimg) == ESP_OK &&
                    outimg.width > 0 && outimg.height > 0)
                    decoded = true;
            }
        }

        if (decoded && rgb_buf != nullptr)
        {
            const uint32_t gray_sz =
                static_cast<uint32_t>(outimg.width) * outimg.height;
            uint8_t* gray_src = static_cast<uint8_t*>(
                heap_caps_malloc(gray_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (gray_src == nullptr)
                gray_src = static_cast<uint8_t*>(heap_caps_malloc(gray_sz, MALLOC_CAP_8BIT));

            if (gray_src != nullptr)
            {
                for (uint16_t row = 0; row < outimg.height; ++row)
                {
                    const uint16_t* src_row = reinterpret_cast<const uint16_t*>(
                        rgb_buf + static_cast<uint32_t>(row) * outimg.width * 2U);
                    uint8_t* dst_row =
                        gray_src + static_cast<uint32_t>(row) * outimg.width;
                    for (uint16_t col = 0; col < outimg.width; ++col)
                        dst_row[col] = rgb565_gray(src_row[col]);
                }

                uint16_t draw_w = 0;
                uint16_t draw_h = 0;
                fit_draw_size(outimg.width, outimg.height, max_w, max_h, draw_w, draw_h);

                const uint32_t scaled_sz = static_cast<uint32_t>(draw_w) * draw_h;
                uint8_t* scaled_gray     = gray_src;
                if (draw_w != outimg.width || draw_h != outimg.height)
                {
                    scaled_gray = static_cast<uint8_t*>(
                        heap_caps_malloc(scaled_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                    if (scaled_gray == nullptr)
                        scaled_gray = static_cast<uint8_t*>(
                            heap_caps_malloc(scaled_sz, MALLOC_CAP_8BIT));
                    if (scaled_gray != nullptr &&
                        scale_gray_fit(gray_src, outimg.width, outimg.height,
                                       scaled_gray, draw_w, draw_h))
                    {
                        heap_caps_free(gray_src);
                    }
                    else
                    {
                        if (scaled_gray != nullptr && scaled_gray != gray_src)
                            heap_caps_free(scaled_gray);
                        scaled_gray = gray_src;
                        draw_w      = outimg.width;
                        draw_h      = outimg.height;
                    }
                }

                const int16_t ox =
                    static_cast<int16_t>(vr.x + (max_w - draw_w) / 2U);
                const int16_t oy =
                    static_cast<int16_t>(vr.y + (max_h - draw_h) / 2U);
                dither_gray_to_fb(scaled_gray, draw_w, draw_h, self->viewer_fb_, ox, oy);
                self->image_rect_ = core::Rect{ox, oy, draw_w, draw_h};
                heap_caps_free(scaled_gray);
                decoded = true;
            }
            else
            {
                decoded = false;
            }
            heap_caps_free(rgb_buf);
            rgb_buf = nullptr;
        }

        heap_caps_free(file_buf);
        if (!decoded)
            err = 3;
    }

done:
    (void)ui::UiBus::get_instance().post_system_hint(
        ui::SystemHintKind::GalleryDecodeDone,
        (static_cast<uint32_t>(job_id) << 8) | static_cast<uint32_t>(err));
}

void GalleryApp::paint_list(gfx::Canvas& c)
{
    if (scanning_)
    {
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontBody;
        ts.h       = gfx::HAlign::Center;
        ts.v       = gfx::VAlign::Middle;
        c.text_in(Theme::list_region(), ui::strings::kGalleryScanning, ts);
        return;
    }

    if (count_ == 0)
    {
        static const char* const kHints[] = {
            GalleryApp::kScanFmtHint,
            GalleryApp::kScanPathInt,
            GalleryApp::kScanPathSd,
        };
        ui::widgets::empty_state(c, Theme::list_region(), ui::strings::kGalleryEmpty, kHints, 3);
        return;
    }

    list_.set_total(count_);
    list_.paint(c);
}

void GalleryApp::paint_viewer(gfx::Canvas& c)
{
    const core::Rect vr = viewer_rect();

    if (decoding_)
    {
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontBody;
        ts.h       = gfx::HAlign::Center;
        ts.v       = gfx::VAlign::Middle;
        c.text_in(vr, ui::strings::kGalleryLoading, ts);
        return;
    }

    if (decode_err_ != 0 || !image_ready_)
    {
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontBody;
        ts.h       = gfx::HAlign::Center;
        ts.v       = gfx::VAlign::Middle;
        const char* msg = ui::strings::kGalleryOpenFail;
        if (decode_err_ == 2)
            msg = ui::strings::kGalleryTooLarge;
        else if (decode_err_ == 4)
            msg = ui::strings::kGalleryNoMem;
        c.text_in(vr, msg, ts);
        return;
    }

    if (viewer_fb_ != nullptr && !image_rect_.empty())
        Fb::blit_rect(c.fb(), viewer_fb_, image_rect_.x, image_rect_.y,
                      image_rect_.w, image_rect_.h, core::kStride);

    if (count_ > 1)
    {
        char hint[24];
        (void)std::snprintf(hint, sizeof(hint), "%u / %u",
                            static_cast<unsigned>(sel_ + 1U),
                            static_cast<unsigned>(count_));
        gfx::Canvas::TextStyle ts{};
        ts.size_px = Theme::kFontSmall;
        ts.h       = gfx::HAlign::Center;
        ts.v       = gfx::VAlign::Bottom;
        const core::Rect hint_r{vr.x, static_cast<int16_t>(vr.bottom() - 16), vr.w, 14};
        c.text_in(hint_r, hint, ts);
    }
}

void GalleryApp::paint(gfx::Canvas& canvas)
{
    ui::widgets::toolbar(canvas, title());
    if (view_ == View::List)
        paint_list(canvas);
    else
        paint_viewer(canvas);
}

shell::InputResult GalleryApp::on_input(const ::app::ebook::input::Event& ev)
{
    using ::app::ebook::input::EventType;

    const int16_t x = static_cast<int16_t>(ev.x);
    const int16_t y = static_cast<int16_t>(ev.y);

    if (view_ == View::List)
    {
        if (ev.type == EventType::Tap && ui::widgets::hit_toolbar_back(x, y))
            return {};

        const auto out = list_.handle_input(ev);
        if (out.scroll_changed)
            request_repaint();
        if (out.consumed)
            return {true};
        return {};
    }

    if (ev.type == EventType::Tap && ui::widgets::hit_toolbar_back(x, y))
    {
        cancel_decode();
        view_         = View::List;
        image_ready_  = false;
        decode_err_   = 0;
        request_repaint();
        return {true};
    }

    if (ev.type == EventType::SwipeLeft)
    {
        show_next();
        return {true};
    }
    if (ev.type == EventType::SwipeRight)
    {
        show_prev();
        return {true};
    }
    if (ev.type == EventType::Tap)
    {
        const core::Rect vr = viewer_rect();
        if (!vr.contains(x, y))
            return {};

        const int16_t mid = static_cast<int16_t>(vr.x + vr.w / 2);
        if (x < mid)
            show_prev();
        else
            show_next();
        return {true};
    }
    return {};
}

} // namespace app::ebook::apps::gallery
