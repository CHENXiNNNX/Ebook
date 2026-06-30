#include "apps/app_registry.hpp"

#include "apps/clock/clock_app.hpp"
#include "apps/drawing/drawing_app.hpp"
#include "apps/files/files_app.hpp"
#include "apps/music/music_app.hpp"
#include "apps/reader/reader_app.hpp"
#include "apps/settings/settings_app.hpp"
#include "apps/calendar/calendar_app.hpp"
#include "apps/gallery/gallery_app.hpp"
#include "apps/notepad/notepad_app.hpp"
#include "apps/weather/weather_app.hpp"
#include "apps/wooden_fish/wooden_fish_app.hpp"
#include "apps/stub_app.hpp"
#include "core/log.hpp"
#include "gfx/icon.hpp"
#include "ui/strings.hpp"

static const char* const TAG = "AppRegistry";

namespace app::ebook::apps {

namespace {

StubApp s_update_app(AppId::Update, ui::strings::kAppUpdate, gfx::icon::kFaDownload);

} // namespace

AppRegistry& AppRegistry::instance()
{
    static AppRegistry s;
    return s;
}

void AppRegistry::register_app(App* app)
{
    if (app == nullptr || count_ >= kMaxApps)
        return;
    apps_[count_++] = app;
}

void AppRegistry::register_defaults()
{
    register_app(&reader::ReaderApp::instance());
    register_app(&notepad::NotepadApp::instance());
    register_app(&gallery::GalleryApp::instance());
    register_app(&drawing::DrawingApp::instance());
    register_app(&music::MusicApp::instance());
    register_app(&weather::WeatherApp::instance());
    register_app(&clock::ClockApp::instance());
    register_app(&calendar::CalendarApp::instance());
    register_app(&wooden_fish::WoodenFishApp::instance());
    register_app(&files::FilesApp::instance());
    register_app(&s_update_app);
    register_app(&settings::SettingsApp::instance());
    EBOOK_LOGI(TAG, "registered %u apps", static_cast<unsigned>(count_));
}

App* AppRegistry::find(AppId id)
{
    if (id == AppId::None)
        return nullptr;
    for (uint8_t i = 0; i < count_; ++i)
        if (apps_[i] != nullptr && apps_[i]->id() == id)
            return apps_[i];
    return nullptr;
}

const App* AppRegistry::find(AppId id) const
{
    return const_cast<AppRegistry*>(this)->find(id);
}

bool AppRegistry::open(AppId id, uint16_t page)
{
    App* a = find(id);
    if (a == nullptr)
        return false;
    if (active_ != nullptr && active_ != a)
        active_->on_exit();
    active_      = a;
    active_page_ = page;
    active_->on_enter();
    return true;
}

void AppRegistry::close_active()
{
    if (active_ == nullptr)
        return;
    active_->on_exit();
    active_      = nullptr;
    active_page_ = 0;
}

AppEntry AppRegistry::entry_at(uint8_t idx) const
{
    if (idx >= count_ || apps_[idx] == nullptr)
        return AppEntry{AppId::None, "", 0};
    const App* a = apps_[idx];
    return AppEntry{a->id(), a->title(), a->icon_cp()};
}

} // namespace app::ebook::apps
