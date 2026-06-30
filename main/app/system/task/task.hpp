#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace app::sys::task {

enum class Priority : UBaseType_t
{
    IDLE = 0,
    LOW = 1,
    NORMAL = 5,
    HIGH = 10,
    REALTIME = 15,
};

enum class State
{
    RUNNING,
    READY,
    BLOCKED,
    SUSPENDED,
    DELETED,
};

struct Cfg
{
    const char* name;
    size_t stack_size;
    Priority priority;
    BaseType_t core_id;
    uint32_t delay_ms;
    bool use_psram;

    Cfg()
        : name("task")
        , stack_size(4096)
        , priority(Priority::NORMAL)
        , core_id(-1)
        , delay_ms(0)
        , use_psram(false)
    {
    }

    static Cfg light(const char* n, Priority p = Priority::NORMAL)
    {
        Cfg c;
        c.name = n;
        c.stack_size = 2048;
        c.priority = p;
        return c;
    }

    static Cfg small(const char* n, Priority p = Priority::LOW)
    {
        Cfg c;
        c.name = n;
        c.stack_size = 1024;
        c.priority = p;
        return c;
    }

    static Cfg large(const char* n, Priority p = Priority::NORMAL)
    {
        Cfg c;
        c.name = n;
        c.stack_size = 8192;
        c.priority = p;
        return c;
    }
};

struct Info
{
    const char* name;
    State state;
    UBaseType_t priority;
    uint32_t runtime_pct;

    Info()
        : name(nullptr)
        , state(State::DELETED)
        , priority(0)
        , runtime_pct(0)
    {
    }
};

/** FreeRTOS 任务包装（支持 PSRAM 栈 + 静态创建） */
class Task
{
  public:
    using Func = std::function<void(void*)>;

    Task(Func fn, const Cfg& cfg, void* param = nullptr);
    ~Task();

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator=(Task&&) = delete;

    bool start();
    void destroy();
    void suspend();
    void resume();

    void set_priority(Priority p);
    Priority get_priority() const;

    TaskHandle_t handle() const { return handle_; }
    const char* name() const { return cfg_.name; }
    Info info() const;
    bool valid() const { return handle_ != nullptr; }

  private:
    static void wrapper(void* param);

    Func fn_;
    Cfg cfg_;
    void* param_;
    TaskHandle_t handle_;
    bool started_;
    StackType_t* stack_buf_;
    StaticTask_t* tcb_;
};

/** 任务查询与 delay_ms / tick_count 工具 */
class TaskMgr
{
  public:
    static TaskMgr& get_instance();

    TaskHandle_t find(const char* name);
    Info get_info(TaskHandle_t h);
    Info current_info();
    uint32_t count();

    static void delay_ms(uint32_t ms);
    static void delay_us(uint32_t us);
    static uint32_t tick_count();

  private:
    TaskMgr() = default;
    ~TaskMgr() = default;
    TaskMgr(const TaskMgr&) = delete;
    TaskMgr& operator=(const TaskMgr&) = delete;
};

} // namespace app::sys::task
