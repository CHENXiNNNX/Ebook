#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

extern "C"
{
    typedef const char* esp_event_base_t;
}

namespace app::sys::event {

using EventId = int32_t;

struct EventData
{
    void* data;
    size_t size;

    EventData()
        : data(nullptr)
        , size(0)
    {
    }
    EventData(void* d, size_t s)
        : data(d)
        , size(s)
    {
    }
};

using EventHandler = std::function<void(esp_event_base_t base, EventId id, const EventData& data)>;

/** @brief esp_event 默认循环 + C++ handler 映射 */
class EventMgr
{
  public:
    static EventMgr& get_instance();

    bool init();
    void deinit();

    esp_event_base_t create_base(const char* name);
    bool register_handler(esp_event_base_t base, EventId id, EventHandler handler);
    bool unregister_handler(esp_event_base_t base, EventId id) const;

    bool post(esp_event_base_t base, EventId id, const EventData& data = EventData(),
              uint32_t timeout_ms = 0) const;
    bool post_isr(esp_event_base_t base, EventId id, const EventData& data = EventData()) const;

    bool is_init() const { return init_; }

  private:
    EventMgr();
    ~EventMgr();
    EventMgr(const EventMgr&) = delete;
    EventMgr& operator=(const EventMgr&) = delete;

    static void c_handler(void* arg, esp_event_base_t base, int32_t id, void* data);

    bool init_;
};

} // namespace app::sys::event
