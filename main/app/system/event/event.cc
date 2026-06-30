#include "event.hpp"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <esp_event.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* const TAG = "Event";

namespace app::sys::event {

struct HandlerKey
{
    esp_event_base_t base;
    EventId id;

    bool operator==(const HandlerKey& o) const
    {
        return base == o.base && id == o.id;
    }
};

struct HandlerKeyHash
{
    size_t operator()(const HandlerKey& k) const
    {
        return std::hash<const char*>{}(k.base) ^ (std::hash<EventId>{}(k.id) << 1);
    }
};

static std::unordered_map<HandlerKey, EventHandler, HandlerKeyHash> s_handlers;
static std::mutex s_mtx;
static std::unordered_set<esp_event_base_t> s_bases;
static std::unordered_map<esp_event_base_t, size_t> s_base_cnt;

EventMgr::EventMgr()
    : init_(false)
{
}

EventMgr::~EventMgr()
{
    if (init_)
        deinit();
}

EventMgr& EventMgr::get_instance()
{
    static EventMgr s;
    return s;
}

bool EventMgr::init()
{
    if (init_)
        return true;

    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        return false;
    }

    init_ = true;
    return true;
}

void EventMgr::deinit()
{
    if (!init_)
        return;

    std::vector<esp_event_base_t> bases;
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        bases.reserve(s_bases.size());
        for (const auto& b : s_bases)
        {
            bases.push_back(b);
        }
        s_handlers.clear();
        s_bases.clear();
        s_base_cnt.clear();
    }

    for (auto b : bases)
    {
        esp_event_handler_unregister(b, ESP_EVENT_ANY_ID, &c_handler);
    }

    init_ = false;
}

esp_event_base_t EventMgr::create_base(const char* name)
{
    if (!name)
        return nullptr;
    return name;
}

bool EventMgr::register_handler(esp_event_base_t base, EventId id, EventHandler handler)
{
    if (!init_ || !base || !handler)
        return false;

    HandlerKey key{base, id};

    std::lock_guard<std::mutex> lk(s_mtx);

    bool is_new = (s_handlers.find(key) == s_handlers.end());
    bool need_reg = (s_bases.find(base) == s_bases.end());

    if (need_reg)
    {
        esp_err_t ret = esp_event_handler_register(base, ESP_EVENT_ANY_ID, &c_handler, nullptr);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "注册失败: %s", esp_err_to_name(ret));
            return false;
        }
        s_bases.insert(base);
    }

    s_handlers[key] = handler;

    if (is_new)
    {
        s_base_cnt[base]++;
    }

    return true;
}

bool EventMgr::unregister_handler(esp_event_base_t base, EventId id) const
{
    if (!init_)
        return false;

    HandlerKey key{base, id};

    bool need_unreg = false;
    {
        std::lock_guard<std::mutex> lk(s_mtx);

        auto it = s_handlers.find(key);
        if (it == s_handlers.end())
            return false;

        s_handlers.erase(it);

        auto cnt_it = s_base_cnt.find(base);
        if (cnt_it != s_base_cnt.end())
        {
            if (cnt_it->second > 1)
            {
                cnt_it->second--;
            }
            else
            {
                s_base_cnt.erase(cnt_it);
                if (s_bases.find(base) != s_bases.end())
                {
                    need_unreg = true;
                    s_bases.erase(base);
                }
            }
        }
    }

    if (need_unreg)
    {
        esp_event_handler_unregister(base, ESP_EVENT_ANY_ID, &c_handler);
    }

    return true;
}

bool EventMgr::post(esp_event_base_t base, EventId id, const EventData& data,
                    uint32_t timeout_ms) const
{
    if (!init_ || !base)
        return false;

    TickType_t ticks = timeout_ms > 0 ? pdMS_TO_TICKS(timeout_ms) : 0;

    esp_err_t ret = esp_event_post(base, id, data.data, data.size, ticks);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "发送失败: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

bool EventMgr::post_isr(esp_event_base_t base, EventId id, const EventData& data) const
{
    if (!init_ || !base)
        return false;

    BaseType_t unblocked = pdFALSE;
    esp_err_t ret = esp_event_isr_post(base, id, data.data, data.size, &unblocked);
    if (ret != ESP_OK)
        return false;

    if (unblocked == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }

    return true;
}

void EventMgr::c_handler(void* /*arg*/, esp_event_base_t base, int32_t id, void* data)
{
    HandlerKey key{base, static_cast<EventId>(id)};

    EventHandler specific;
    EventHandler any;
    {
        std::lock_guard<std::mutex> lk(s_mtx);

        auto it = s_handlers.find(key);
        if (it != s_handlers.end())
        {
            specific = it->second;
        }

        HandlerKey any_key{base, ESP_EVENT_ANY_ID};
        auto any_it = s_handlers.find(any_key);
        if (any_it != s_handlers.end())
        {
            any = any_it->second;
        }
    }

    EventData d{data, 0};

    if (specific)
        specific(base, static_cast<EventId>(id), d);
    if (any)
        any(base, static_cast<EventId>(id), d);
}

} // namespace app::sys::event
