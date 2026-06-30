#include "task.hpp"

#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "system/info/info.hpp"

static const char* const TAG = "Task";

namespace app::sys::task {

struct StartParam
{
    Task::Func fn;
    void*      user_param;
    Task*      self;
};

static std::unordered_map<TaskHandle_t, std::unique_ptr<StartParam>> s_params;
static std::mutex s_mtx;

Task::Task(Func fn, const Cfg& cfg, void* param)
    : fn_(fn)
    , cfg_(cfg)
    , param_(param)
    , handle_(nullptr)
    , started_(false)
    , stack_buf_(nullptr)
    , tcb_(nullptr)
{
}

Task::~Task() = default;

void Task::wrapper(void* param)
{
    // 等待 start() 完成 s_params 注册后再执行，避免与创建线程竞态
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    auto* p = static_cast<StartParam*>(param);
    if (p && p->fn)
    {
        p->fn(p->user_param);
    }

    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        s_params.erase(self);
    }
    vTaskDelete(nullptr);
}

bool Task::start()
{
    if (started_ || handle_ != nullptr)
    {
        ESP_LOGW(TAG, "%s 已启动", cfg_.name);
        return false;
    }

    if (!fn_)
    {
        ESP_LOGE(TAG, "任务函数为空");
        return false;
    }

    auto sp = std::make_unique<StartParam>(StartParam{fn_, param_, this});

    if (cfg_.delay_ms > 0)
    {
        vTaskDelay(pdMS_TO_TICKS(cfg_.delay_ms));
    }

    size_t depth = cfg_.stack_size / sizeof(StackType_t);

    if (cfg_.use_psram)
    {
        stack_buf_ = static_cast<StackType_t*>(
            heap_caps_malloc(cfg_.stack_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        tcb_ = static_cast<StaticTask_t*>(
            heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

        if (!stack_buf_ || !tcb_)
        {
            ESP_LOGE(TAG, "PSRAM分配失败: %s", cfg_.name);
            if (stack_buf_)
                heap_caps_free(stack_buf_);
            if (tcb_)
                heap_caps_free(tcb_);
            stack_buf_ = nullptr;
            tcb_ = nullptr;
            return false;
        }

        if (cfg_.core_id == -1)
        {
            handle_ = xTaskCreateStatic(wrapper, cfg_.name, depth, sp.get(),
                                        static_cast<UBaseType_t>(cfg_.priority), stack_buf_, tcb_);
        }
        else
        {
            handle_ = xTaskCreateStaticPinnedToCore(wrapper, cfg_.name, depth, sp.get(),
                                                    static_cast<UBaseType_t>(cfg_.priority),
                                                    stack_buf_, tcb_, cfg_.core_id);
        }

        if (!handle_)
        {
            ESP_LOGE(TAG, "创建失败(PSRAM): %s", cfg_.name);
            heap_caps_free(stack_buf_);
            heap_caps_free(tcb_);
            stack_buf_ = nullptr;
            tcb_ = nullptr;
            return false;
        }

        ESP_LOGI(TAG, "%s 已创建 (PSRAM, %uB)", cfg_.name, static_cast<unsigned>(cfg_.stack_size));
    }
    else
    {
        BaseType_t ret;
        if (cfg_.core_id == -1)
        {
            ret = xTaskCreate(wrapper, cfg_.name, depth, sp.get(),
                              static_cast<UBaseType_t>(cfg_.priority), &handle_);
        }
        else
        {
            ret = xTaskCreatePinnedToCore(wrapper, cfg_.name, depth, sp.get(),
                                          static_cast<UBaseType_t>(cfg_.priority), &handle_,
                                          cfg_.core_id);
        }

        if (ret != pdPASS)
        {
            ESP_LOGE(TAG, "创建失败: %s", cfg_.name);
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lk(s_mtx);
        s_params[handle_] = std::move(sp);
    }
    (void)xTaskNotifyGive(handle_);

    started_ = true;
    return true;
}

void Task::destroy()
{
    if (handle_)
    {
        vTaskDelete(handle_);

        {
            std::lock_guard<std::mutex> lk(s_mtx);
            s_params.erase(handle_);
        }

        handle_ = nullptr;
        started_ = false;

        if (stack_buf_)
        {
            heap_caps_free(stack_buf_);
            stack_buf_ = nullptr;
        }
        if (tcb_)
        {
            heap_caps_free(tcb_);
            tcb_ = nullptr;
        }
    }
}

void Task::suspend()
{
    if (handle_)
        vTaskSuspend(handle_);
}

void Task::resume()
{
    if (handle_)
        vTaskResume(handle_);
}

void Task::set_priority(Priority p)
{
    if (handle_)
    {
        vTaskPrioritySet(handle_, static_cast<UBaseType_t>(p));
        cfg_.priority = p;
    }
}

Priority Task::get_priority() const
{
    if (handle_)
    {
        return static_cast<Priority>(uxTaskPriorityGet(handle_));
    }
    return cfg_.priority;
}

Info Task::info() const
{
    Info i;
    if (handle_)
    {
        i.name = cfg_.name;
        i.priority = uxTaskPriorityGet(handle_);

        eTaskState st = eTaskGetState(handle_);
        switch (st)
        {
            case eRunning:
                i.state = State::RUNNING;
                break;
            case eReady:
                i.state = State::READY;
                break;
            case eBlocked:
                i.state = State::BLOCKED;
                break;
            case eSuspended:
                i.state = State::SUSPENDED;
                break;
            default:
                i.state = State::DELETED;
                break;
        }
    }
    else
    {
        i.name = cfg_.name;
        i.priority = static_cast<UBaseType_t>(cfg_.priority);
        i.state = State::DELETED;
    }
    return i;
}

TaskMgr& TaskMgr::get_instance()
{
    static TaskMgr s;
    return s;
}

TaskHandle_t TaskMgr::find(const char* name)
{
    return xTaskGetHandle(name);
}

Info TaskMgr::get_info(TaskHandle_t h)
{
    Info i;
    if (!h)
        return i;

    i.name = pcTaskGetName(h);
    i.priority = uxTaskPriorityGet(h);

    eTaskState st = eTaskGetState(h);
    switch (st)
    {
        case eRunning:
            i.state = State::RUNNING;
            break;
        case eReady:
            i.state = State::READY;
            break;
        case eBlocked:
            i.state = State::BLOCKED;
            break;
        case eSuspended:
            i.state = State::SUSPENDED;
            break;
        default:
            i.state = State::DELETED;
            break;
    }

    return i;
}

Info TaskMgr::current_info()
{
    return get_info(xTaskGetCurrentTaskHandle());
}

uint32_t TaskMgr::count()
{
    return uxTaskGetNumberOfTasks();
}

void TaskMgr::delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void TaskMgr::delay_us(uint32_t us)
{
    if (us == 0)
        return;

    auto cpu_info = app::sys::info::CpuInfo::get_cpu_info();
    uint32_t freq_hz = cpu_info.get_cpu_freq();

    constexpr uint64_t US_PER_SEC = 1000000ULL;
    uint64_t cycles = static_cast<uint64_t>(us) * freq_hz / US_PER_SEC;
    uint64_t start = esp_cpu_get_cycle_count();

    while ((esp_cpu_get_cycle_count() - start) < cycles)
    {
    }
}

uint32_t TaskMgr::tick_count()
{
    return xTaskGetTickCount();
}

} // namespace app::sys::task
