#include "input/input_bindings.hpp"

#include "apps/app_id.hpp"
#include "apps/app_registry.hpp"
#include "input/key_bindings.hpp"
#include "router/router.hpp"

namespace app::ebook::input {

namespace {

using apps::AppId;
using router::Router;
using router::ShellPage;

} // namespace

InputContext detect_context()
{
    const auto& stack = Router::instance().stack();

    if (stack.shell_top() == ShellPage::AppHost)
    {
        switch (apps::AppRegistry::instance().active_id())
        {
            case AppId::Reader:
                return InputContext::Reader;
            case AppId::Settings:
            case AppId::Files:
            case AppId::Music:
            case AppId::Gallery:
                return InputContext::List;
            default:
                break;
        }
    }

    if (stack.shell_top() == ShellPage::Home || stack.shell_top() == ShellPage::AppGrid)
        return InputContext::List;

    return InputContext::Global;
}

SemanticAction resolve_physical(PhysicalKey key, PhysicalAction action, InputContext ctx)
{
    if (action != PhysicalAction::Press)
        return SemanticAction::None;
    return KeyBindings::instance().lookup(key, ctx);
}

const char* semantic_action_name(SemanticAction a)
{
    switch (a)
    {
        case SemanticAction::None:          return "None";
        case SemanticAction::PageNext:      return "PageNext";
        case SemanticAction::PagePrev:      return "PagePrev";
        case SemanticAction::ListUp:        return "ListUp";
        case SemanticAction::ListDown:      return "ListDown";
        case SemanticAction::Back:          return "Back";
        case SemanticAction::BrightnessUp:  return "BrightnessUp";
        case SemanticAction::BrightnessDown: return "BrightnessDown";
        case SemanticAction::VolumeUp:      return "VolumeUp";
        case SemanticAction::VolumeDown:    return "VolumeDown";
        case SemanticAction::Menu:          return "Menu";
        case SemanticAction::Home:          return "Home";
    }
    return "?";
}

} // namespace app::ebook::input
