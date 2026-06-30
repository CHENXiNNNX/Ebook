#include "input/key_bindings.hpp"

#include "data/persist.hpp"
#include "ui/strings.hpp"

namespace app::ebook::input {

namespace {

constexpr uint8_t kCtxN = 3;
constexpr uint8_t kKeyN = 3;

constexpr const char* kNvsKeys[kCtxN][kKeyN] = {
    {"in.bg.u", "in.bg.m", "in.bg.d"},
    {"in.br.u", "in.br.m", "in.br.d"},
    {"in.bl.u", "in.bl.m", "in.bl.d"},
};

constexpr SemanticAction kReaderBindable[] = {
    SemanticAction::None,
    SemanticAction::PagePrev,
    SemanticAction::PageNext,
    SemanticAction::Menu,
    SemanticAction::Back,
    SemanticAction::Home,
    SemanticAction::BrightnessUp,
    SemanticAction::BrightnessDown,
    SemanticAction::VolumeUp,
    SemanticAction::VolumeDown,
};

constexpr SemanticAction kListBindable[] = {
    SemanticAction::None,
    SemanticAction::ListUp,
    SemanticAction::ListDown,
    SemanticAction::Back,
    SemanticAction::Home,
    SemanticAction::BrightnessUp,
    SemanticAction::BrightnessDown,
    SemanticAction::VolumeUp,
    SemanticAction::VolumeDown,
};

constexpr SemanticAction kGlobalBindable[] = {
    SemanticAction::None,
    SemanticAction::Back,
    SemanticAction::Home,
    SemanticAction::BrightnessUp,
    SemanticAction::BrightnessDown,
    SemanticAction::VolumeUp,
    SemanticAction::VolumeDown,
};

struct BindableList
{
    const SemanticAction* items;
    uint8_t               count;
};

BindableList bindable_list(InputContext ctx)
{
    switch (ctx)
    {
        case InputContext::Reader:
            return {kReaderBindable,
                    static_cast<uint8_t>(sizeof(kReaderBindable) / sizeof(kReaderBindable[0]))};
        case InputContext::List:
            return {kListBindable,
                    static_cast<uint8_t>(sizeof(kListBindable) / sizeof(kListBindable[0]))};
        case InputContext::Global:
        default:
            return {kGlobalBindable,
                    static_cast<uint8_t>(sizeof(kGlobalBindable) / sizeof(kGlobalBindable[0]))};
    }
}

uint8_t ctx_index(InputContext ctx)
{
    return static_cast<uint8_t>(ctx);
}

uint8_t key_index(PhysicalKey key)
{
    return static_cast<uint8_t>(key);
}

} // namespace

KeyBindings& KeyBindings::instance()
{
    static KeyBindings s;
    return s;
}

void KeyBindings::apply_defaults()
{
    for (uint8_t c = 0; c < kContextCount; ++c)
    {
        for (uint8_t k = 0; k < kKeyCount; ++k)
            table_[c][k] = static_cast<uint8_t>(SemanticAction::None);
    }

    table_[ctx_index(InputContext::Reader)][key_index(PhysicalKey::Up)] =
        static_cast<uint8_t>(SemanticAction::PagePrev);
    table_[ctx_index(InputContext::Reader)][key_index(PhysicalKey::Mid)] =
        static_cast<uint8_t>(SemanticAction::Back);
    table_[ctx_index(InputContext::Reader)][key_index(PhysicalKey::Down)] =
        static_cast<uint8_t>(SemanticAction::PageNext);

    table_[ctx_index(InputContext::List)][key_index(PhysicalKey::Up)] =
        static_cast<uint8_t>(SemanticAction::ListUp);
    table_[ctx_index(InputContext::List)][key_index(PhysicalKey::Mid)] =
        static_cast<uint8_t>(SemanticAction::Back);
    table_[ctx_index(InputContext::List)][key_index(PhysicalKey::Down)] =
        static_cast<uint8_t>(SemanticAction::ListDown);
}

void KeyBindings::load()
{
    apply_defaults();

    for (uint8_t c = 0; c < kContextCount; ++c)
    {
        const auto ctx = static_cast<InputContext>(c);
        for (uint8_t k = 0; k < kKeyCount; ++k)
        {
            uint8_t raw = 0;
            if (data::Persist::get_u8(kNvsKeys[c][k], raw) && valid_action(raw, ctx))
                table_[c][k] = raw;
        }
    }
}

void KeyBindings::save()
{
    for (uint8_t c = 0; c < kContextCount; ++c)
    {
        for (uint8_t k = 0; k < kKeyCount; ++k)
            (void)data::Persist::set_u8(kNvsKeys[c][k], table_[c][k]);
    }
    data::Persist::commit();
}

void KeyBindings::restore_defaults()
{
    apply_defaults();
    save();
}

bool KeyBindings::valid_action(uint8_t raw, InputContext ctx) const
{
    return is_bindable(ctx, static_cast<SemanticAction>(raw));
}

bool KeyBindings::is_bindable(InputContext ctx, SemanticAction action)
{
    const BindableList list = bindable_list(ctx);
    for (uint8_t i = 0; i < list.count; ++i)
    {
        if (list.items[i] == action)
            return true;
    }
    return false;
}

SemanticAction KeyBindings::lookup(PhysicalKey key, InputContext ctx) const
{
    const uint8_t c = ctx_index(ctx);
    const uint8_t k = key_index(key);
    if (c >= kContextCount || k >= kKeyCount)
        return SemanticAction::None;
    return static_cast<SemanticAction>(table_[c][k]);
}

SemanticAction KeyBindings::get(InputContext ctx, PhysicalKey key) const
{
    return lookup(key, ctx);
}

void KeyBindings::set(InputContext ctx, PhysicalKey key, SemanticAction action)
{
    const uint8_t c = ctx_index(ctx);
    const uint8_t k = key_index(key);
    if (c >= kContextCount || k >= kKeyCount)
        return;
    if (!is_bindable(ctx, action))
        return;
    table_[c][k] = static_cast<uint8_t>(action);
}

void KeyBindings::cycle(InputContext ctx, PhysicalKey key)
{
    const BindableList list = bindable_list(ctx);
    const SemanticAction cur = get(ctx, key);
    uint8_t              idx = 0;
    for (uint8_t i = 0; i < list.count; ++i)
    {
        if (list.items[i] == cur)
        {
            idx = static_cast<uint8_t>((i + 1U) % list.count);
            break;
        }
    }
    set(ctx, key, list.items[idx]);
    save();
}

uint8_t KeyBindings::bindable_count(InputContext ctx)
{
    return bindable_list(ctx).count;
}

SemanticAction KeyBindings::bindable_at(InputContext ctx, uint8_t index)
{
    const BindableList list = bindable_list(ctx);
    if (index >= list.count)
        return SemanticAction::None;
    return list.items[index];
}

const char* KeyBindings::action_label(SemanticAction action)
{
    switch (action)
    {
        case SemanticAction::None:           return ui::strings::kKeyActNone;
        case SemanticAction::PagePrev:       return ui::strings::kKeyActPagePrev;
        case SemanticAction::PageNext:       return ui::strings::kKeyActPageNext;
        case SemanticAction::ListUp:         return ui::strings::kKeyActListUp;
        case SemanticAction::ListDown:       return ui::strings::kKeyActListDown;
        case SemanticAction::Back:           return ui::strings::kKeyActBack;
        case SemanticAction::Menu:           return ui::strings::kKeyActMenu;
        case SemanticAction::Home:           return ui::strings::kKeyActHome;
        case SemanticAction::BrightnessUp:   return ui::strings::kKeyActBrightUp;
        case SemanticAction::BrightnessDown: return ui::strings::kKeyActBrightDown;
        case SemanticAction::VolumeUp:       return ui::strings::kKeyActVolUp;
        case SemanticAction::VolumeDown:     return ui::strings::kKeyActVolDown;
    }
    return ui::strings::kKeyActNone;
}

const char* KeyBindings::context_label(InputContext ctx)
{
    switch (ctx)
    {
        case InputContext::Reader: return ui::strings::kKeyCtxReader;
        case InputContext::List:   return ui::strings::kKeyCtxList;
        case InputContext::Global: return ui::strings::kKeyCtxGlobal;
    }
    return "";
}

const char* KeyBindings::key_label(PhysicalKey key)
{
    switch (key)
    {
        case PhysicalKey::Up:   return ui::strings::kKeySlotUp;
        case PhysicalKey::Mid:  return ui::strings::kKeySlotMid;
        case PhysicalKey::Down: return ui::strings::kKeySlotDown;
    }
    return "";
}

} // namespace app::ebook::input
