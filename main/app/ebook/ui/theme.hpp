#pragma once

#include <cstdint>

#include "core/geometry.hpp"

namespace app::ebook::ui {

/** 全局布局 token（constexpr；颜色由 gfx::Ink 表达） */
struct Theme
{
    static constexpr uint16_t kScreenW = core::kScreenW;
    static constexpr uint16_t kScreenH = core::kScreenH;

    static constexpr uint16_t kPad   = 4;
    static constexpr uint16_t kPadLg = 8;
    static constexpr uint16_t kGap   = 6;

    static constexpr uint8_t kFontSmall  = 10;
    static constexpr uint8_t kFontBody   = 12;
    static constexpr uint8_t kFontTitle  = 14;
    static constexpr uint8_t kFontLarge  = 18;
    static constexpr uint8_t kFontHuge   = 28;
    static constexpr uint8_t kFontIconSm = 12;
    static constexpr uint8_t kFontIconMd = 16;
    static constexpr uint8_t kFontIconLg = 24;

    static constexpr uint16_t kStatusBarH = 22;
    static constexpr uint16_t kToolbarH   = 32;
    static constexpr uint16_t kToolbarY   = kStatusBarH;
    static constexpr uint16_t kBackTapW   = 60;
    static constexpr uint16_t kContentY   = kStatusBarH;
    static constexpr uint16_t kContentH   = kScreenH - kStatusBarH;

    static constexpr uint16_t kListRowH      = 36;
    static constexpr uint16_t kSectionHeaderH = 14;
    static constexpr uint16_t kWifiStatusH = 24;
    static constexpr uint16_t kListStartY    = kStatusBarH + kToolbarH;
    static constexpr uint8_t  kListVisibleRows =
        static_cast<uint8_t>((kScreenH - kListStartY) / kListRowH);
    static constexpr uint16_t kListRegionH = kListVisibleRows * kListRowH;
    static constexpr uint16_t kScrollbarW  = 3;

    static constexpr uint16_t kLockChromeH       = kStatusBarH;
    static constexpr uint16_t kLockTimeY         = 40;
    static constexpr uint16_t kLockQuoteY        = 110;
    static constexpr uint8_t  kLockQuoteMaxLines = 4;
    static constexpr uint16_t kLockQuoteStep     = 16;
    static constexpr uint16_t kLockHintY         = kScreenH - 20;

    static constexpr uint16_t kReadingCardY = kContentY + 6;
    static constexpr uint16_t kReadingCardH = 110;
    static constexpr uint16_t kReadingBarH  = 8;
    static constexpr uint16_t kReadingBtnH  = 24;

    static constexpr uint16_t kQuickY    = kReadingCardY + kReadingCardH + 6;
    static constexpr uint16_t kQuickRowH = (kScreenH - kQuickY) / 2;
    static constexpr uint8_t  kQuickCols = 2;
    static constexpr uint8_t  kQuickRows = 2;

    static constexpr uint16_t kAppGridY     = kStatusBarH + kToolbarH;
    static constexpr uint8_t  kAppGridCols  = 3;
    static constexpr uint16_t kAppGridCellH = (kScreenH - kAppGridY) / 3;
    static constexpr uint16_t kAppIconSize  = 24;

    static constexpr uint16_t kCtlY           = kStatusBarH;
    static constexpr uint16_t kCtlPad         = 8;
    static constexpr uint16_t kCtlTitleH      = 22;
    static constexpr uint8_t  kCtlTileCols    = 4;
    static constexpr uint16_t kCtlTileGap     = 6;
    static constexpr uint16_t kCtlTileIconSz  = 16;
    static constexpr uint16_t kCtlTileCellH   = 34;
    static constexpr uint16_t kCtlSectionGap  = 8;
    static constexpr uint16_t kCtlLevelRowH   = 34;
    static constexpr uint16_t kCtlLevelPitch  = 38;
    static constexpr uint16_t kCtlLevelBtnSz  = 28;
    static constexpr uint16_t kCtlLevelIconSz = 12;
    static constexpr uint16_t kCtlFooterH     = 16;

    static constexpr uint16_t kKbInputH    = 28;
    static constexpr uint16_t kKbToolbarH  = 28;
    static constexpr uint8_t  kKbCols      = 5;
    static constexpr uint8_t  kKbLetterRows = 6;

    static constexpr core::Rect status_bar_rect()
    {
        return core::Rect{0, 0, kScreenW, kStatusBarH};
    }
    static constexpr core::Rect content_rect()
    {
        return core::Rect{0, static_cast<int16_t>(kContentY), kScreenW, kContentH};
    }
    static constexpr core::Rect toolbar_rect()
    {
        return core::Rect{0, static_cast<int16_t>(kToolbarY), kScreenW, kToolbarH};
    }
    static constexpr core::Rect list_region()
    {
        return core::Rect{0, static_cast<int16_t>(kListStartY), kScreenW, kListRegionH};
    }
};

} // namespace app::ebook::ui
