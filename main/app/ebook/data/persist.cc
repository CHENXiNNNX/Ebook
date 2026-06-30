#include "data/persist.hpp"

#include <nvs.h>
#include <nvs_flash.h>

#include "core/log.hpp"

static const char* const TAG = "Persist";

namespace app::ebook::data {

namespace {

constexpr const char* kNamespace = "ebook";

struct Ctx
{
    nvs_handle_t handle{0};
    bool         ready{false};
};

Ctx g_ctx{};

} // namespace

core::Status Persist::init()
{
    if (g_ctx.ready) return core::Status::Ok;

    const esp_err_t r = nvs_open(kNamespace, NVS_READWRITE, &g_ctx.handle);
    if (r != ESP_OK)
    {
        EBOOK_LOGW(TAG, "nvs_open: %s", esp_err_to_name(r));
        return core::Status::IoError;
    }
    g_ctx.ready = true;
    return core::Status::Ok;
}

void Persist::deinit()
{
    if (!g_ctx.ready) return;
    nvs_close(g_ctx.handle);
    g_ctx = {};
}

bool Persist::get_u8(const char* key, uint8_t& out)
{
    if (!g_ctx.ready) return false;
    return nvs_get_u8(g_ctx.handle, key, &out) == ESP_OK;
}

bool Persist::set_u8(const char* key, uint8_t v)
{
    if (!g_ctx.ready) return false;
    return nvs_set_u8(g_ctx.handle, key, v) == ESP_OK;
}

bool Persist::get_bool(const char* key, bool& out)
{
    uint8_t v = 0;
    if (!get_u8(key, v)) return false;
    out = (v != 0);
    return true;
}

bool Persist::set_bool(const char* key, bool v)
{
    return set_u8(key, v ? 1U : 0U);
}

bool Persist::get_str(const char* key, char* out, size_t out_size)
{
    if (!g_ctx.ready || out == nullptr || out_size == 0) return false;
    size_t len = out_size;
    if (nvs_get_str(g_ctx.handle, key, out, &len) != ESP_OK)
    {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool Persist::set_str(const char* key, const char* v)
{
    if (!g_ctx.ready || v == nullptr) return false;
    return nvs_set_str(g_ctx.handle, key, v) == ESP_OK;
}

void Persist::commit()
{
    if (g_ctx.ready) nvs_commit(g_ctx.handle);
}

} // namespace app::ebook::data
