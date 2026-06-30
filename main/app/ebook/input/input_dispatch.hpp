#pragma once

#include "input/input_bindings.hpp"
#include "input/physical_types.hpp"

namespace app::ebook::input {

/** @brief 将语义动作转为现有触摸分发或全局副作用（UI 线程内调用） */
void dispatch_semantic(SemanticAction action);

/** @brief 拨码事件入口：解析上下文 → 语义 → dispatch_semantic */
void dispatch_physical(const PhysicalEvent& ev);

} // namespace app::ebook::input
