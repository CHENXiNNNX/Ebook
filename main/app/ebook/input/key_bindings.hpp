#pragma once

#include <cstdint>

#include "input/input_bindings.hpp"
#include "input/physical_types.hpp"

namespace app::ebook::input {

/**
 * @brief 拨码映射表（NVS）；Context × Key → SemanticAction
 */
class KeyBindings
{
  public:
    static KeyBindings& instance();

    void load();
    void restore_defaults();
    void save();

    SemanticAction lookup(PhysicalKey key, InputContext ctx) const;

    SemanticAction get(InputContext ctx, PhysicalKey key) const;
    void           set(InputContext ctx, PhysicalKey key, SemanticAction action);
    void           cycle(InputContext ctx, PhysicalKey key);

    static uint8_t        bindable_count(InputContext ctx);
    static SemanticAction bindable_at(InputContext ctx, uint8_t index);
    static bool           is_bindable(InputContext ctx, SemanticAction action);
    static const char*    action_label(SemanticAction action);

    static const char* context_label(InputContext ctx);
    static const char* key_label(PhysicalKey key);

  private:
    KeyBindings() = default;

    static constexpr uint8_t kContextCount = 3;
    static constexpr uint8_t kKeyCount     = 3;

    void apply_defaults();
    bool valid_action(uint8_t raw, InputContext ctx) const;

    uint8_t table_[kContextCount][kKeyCount]{};
};

} // namespace app::ebook::input
