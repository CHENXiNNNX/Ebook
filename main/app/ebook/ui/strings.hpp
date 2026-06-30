#pragma once

/** @brief 全局 UI 字符串（中文 UTF-8，编译期常量） */

namespace app::ebook::ui::strings {

inline constexpr const char* kBack         = "\u8FD4\u56DE";
inline constexpr const char* kBackArrow    = "< \u8FD4\u56DE";
inline constexpr const char* kOk           = "\u786E\u5B9A";
inline constexpr const char* kCancel       = "\u53D6\u6D88";
inline constexpr const char* kDone         = "\u5B8C\u6210";
inline constexpr const char* kOn           = "\u5F00";
inline constexpr const char* kOff          = "\u5173";
inline constexpr const char* kEnabled      = "\u5DF2\u5F00\u542F";
inline constexpr const char* kDisabled     = "\u5DF2\u5173\u95ED";
inline constexpr const char* kEmpty        = "\u6682\u65E0";
inline constexpr const char* kWip          = "(\u5F00\u53D1\u4E2D)";
inline constexpr const char* kForget       = "\u5FD8\u8BB0";

inline constexpr const char* kSwipeToUnlock = "\u4E0A\u6ED1\u89E3\u9501";
inline constexpr const char* kDailyQuote    = "\u6BCF\u65E5\u91D1\u53E5";

inline constexpr const char* kReading       = "\u6B63\u5728\u9605\u8BFB";
inline constexpr const char* kContinueRead  = "\u7EE7\u7EED\u9605\u8BFB";
inline constexpr const char* kGotoShelf     = "\u6253\u5F00\u4E66\u67B6";
inline constexpr const char* kNoBook        = "\u6682\u65E0\u5728\u8BFB\u4E66\u7C4D";

inline constexpr const char* kAppReader     = "\u9605\u8BFB";
inline constexpr const char* kAppNotepad    = "\u8BB0\u4E8B\u672C";
inline constexpr const char* kAppGallery    = "\u76F8\u518C";
inline constexpr const char* kAppDrawing    = "\u753B\u677F";
inline constexpr const char* kAppWeather    = "\u5929\u6C14";
inline constexpr const char* kAppClock      = "\u65F6\u949F";
inline constexpr const char* kAppCalendar   = "\u65E5\u5386";
inline constexpr const char* kAppMusic      = "\u97F3\u4E50";
inline constexpr const char* kAppFish       = "\u7535\u5B50\u6728\u9C7C";
inline constexpr const char* kFishMeritFmt  = "\u529F\u5FB7 %u";
inline constexpr const char* kFishTapHint   = "\u70B9\u51FB\u6572\u51FB";
inline constexpr const char* kAppFiles      = "\u6587\u4EF6\u7BA1\u7406";
inline constexpr const char* kAppUpdate     = "\u66F4\u65B0";
inline constexpr const char* kAppSettings   = "\u8BBE\u7F6E";
inline constexpr const char* kAppMore       = "\u66F4\u591A";
inline constexpr const char* kAppsTitle     = "\u5E94\u7528";

inline constexpr const char* kReaderShelfEmpty = "\u6682\u65E0\u4E66\u7C4D";
inline constexpr const char* kReaderOpening     = "\u6B63\u5728\u6253\u5F00\u2026";
inline constexpr const char* kReaderOpenFail    = "\u65E0\u6CD5\u6253\u5F00\u4E66\u7C4D";
inline constexpr const char* kReaderTooLarge    = "\u4E66\u7C4D\u6587\u4EF6\u8FC7\u5927";
inline constexpr const char* kReaderBackShelf   = "\u4E66\u5E93";
inline constexpr const char* kReaderReadFmt     = "\u5DF2\u8BFB %u%%";
inline constexpr const char* kReaderJumpEmpty   = "\u8BF7\u8F93\u5165\u9875\u7801";
inline constexpr const char* kReaderJumpInvalid = "\u9875\u7801\u65E0\u6548";

inline constexpr const char* kMusicPlaying  = "\u6B63\u5728\u64AD\u653E";
inline constexpr const char* kMusicEmpty = "\u6682\u65E0\u97F3\u4E50";
inline constexpr const char* kMusicScanning = "\u626B\u63CF\u4E2D\u2026";
inline constexpr const char* kMusicLoading  = "\u52A0\u8F7D\u4E2D\u2026";
inline constexpr const char* kMusicPlayFail = "\u64AD\u653E\u5931\u8D25";
inline constexpr const char* kMusicTooLarge = "\u6587\u4EF6\u8FC7\u5927(\u22643MB)";
inline constexpr const char* kMusicOpenFail = "\u65E0\u6CD5\u6253\u5F00\u6587\u4EF6";
inline constexpr const char* kMusicNoMem    = "\u5185\u5B58\u4E0D\u8DB3";

inline constexpr const char* kGalleryEmpty = "\u6682\u65E0\u7167\u7247";
inline constexpr const char* kGalleryScanning = "\u626B\u63CF\u4E2D\u2026";
inline constexpr const char* kGalleryLoading  = "\u52A0\u8F7D\u4E2D\u2026";
inline constexpr const char* kGalleryOpenFail = "\u65E0\u6CD5\u6253\u5F00\u56FE\u7247";
inline constexpr const char* kGalleryTooLarge = "\u56FE\u7247\u8FC7\u5927(\u22642MB)";
inline constexpr const char* kGalleryNoMem    = "\u5185\u5B58\u4E0D\u8DB3";

inline constexpr const char* kFilesInternal   = "\u7528\u6237";
inline constexpr const char* kFilesSd         = "SD Card";
inline constexpr const char* kFilesAssets     = "\u7CFB\u7EDF";
inline constexpr const char* kFilesSystemProtected =
    "\u7CFB\u7EDF\u6587\u4EF6\u4E0D\u53EF\u5220\u6539";                             // 系统文件不可删改
inline constexpr const char* kFilesParent     = "..";
inline constexpr const char* kFilesEmpty      = "\u65E0\u53EF\u7528\u5B58\u50A8";
inline constexpr const char* kFilesLoading    = "\u52A0\u8F7D\u4E2D...";
inline constexpr const char* kFilesNotMounted = "\u5B58\u50A8\u672A\u6302\u8F7D";
inline constexpr const char* kFilesFileFmt    = "%s (%s)";
inline constexpr const char* kFilesTruncated  = "\u4EC5\u663E\u793A\u524D48\u9879";
inline constexpr const char* kFilesView       = "\u67E5\u770B";
inline constexpr const char* kFilesOpen       = "\u6253\u5F00";
inline constexpr const char* kFilesDelete     = "\u5220\u9664";
inline constexpr const char* kFilesDeleteAsk  = "\u662F\u5426\u5220\u9664\u8BE5\u6587\u4EF6";
inline constexpr const char* kFilesOpenUnsupported = "\u4E0D\u652F\u6301\u6253\u5F00\u8BE5\u6587\u4EF6";
inline constexpr const char* kFilesDeleteFail = "\u5220\u9664\u5931\u8D25";
inline constexpr const char* kFilesInfoName   = "\u540D\u79F0";
inline constexpr const char* kFilesInfoSize   = "\u5927\u5C0F";
inline constexpr const char* kFilesInfoPath   = "\u8DEF\u5F84";
inline constexpr const char* kFilesInfoType   = "\u7C7B\u578B";
inline constexpr const char* kFilesTypeTxt    = "\u6587\u672C";
inline constexpr const char* kFilesTypeBmp    = "\u753B\u677F";
inline constexpr const char* kFilesTypePhoto = "\u7167\u7247";
inline constexpr const char* kFilesTypeAudio  = "\u97F3\u9891";
inline constexpr const char* kFilesTypeFont   = "\u5B57\u4F53";
inline constexpr const char* kFilesTypeFile   = "\u6587\u4EF6";

inline constexpr const char* kCtlTitle       = "\u63A7\u5236\u4E2D\u5FC3";
inline constexpr const char* kCtlWifi        = "WiFi";
inline constexpr const char* kCtlBt          = "\u84DD\u7259";
inline constexpr const char* kCtlNight       = "\u591C\u95F4";
inline constexpr const char* kCtlLock        = "\u9501\u5C4F";
inline constexpr const char* kCtlLowPower    = "\u4F4E\u529F\u8017";
inline constexpr const char* kCtlBrightness  = "\u4EAE\u5EA6";
inline constexpr const char* kCtlVolume      = "\u97F3\u91CF";
inline constexpr const char* kCtlSwipeUp     = "\u4E0A\u6ED1\u6536\u8D77";

inline constexpr const char* kPwrPromptEnable20  =
    "\u7535\u91CF\u4F4E\u4E8E20%\uFF0C\u662F\u5426\u5F00\u542F\u7701\u7535\uFF1F";
inline constexpr const char* kPwrPromptEnable10  =
    "\u7535\u91CF\u4F4E\u4E8E10%\uFF0C\u662F\u5426\u5F00\u542F\u7701\u7535\uFF1F";
inline constexpr const char* kPwrPromptDisable80 =
    "\u7535\u91CF\u5DF2\u6062\u590D\uFF0C\u662F\u5426\u5173\u95ED\u7701\u7535\uFF1F";

inline constexpr const char* kKbDelete = "\u5220";
inline constexpr const char* kKbABC    = "ABC";
inline constexpr const char* kKb123    = "123";
inline constexpr const char* kKbSymbol = "#+=";
inline constexpr const char* kKbExit   = "\u9000\u51FA";
inline constexpr const char* kKbDone   = "\u5B8C\u6210";
inline constexpr const char* kKbSpace  = "\u7A7A\u683C";

inline constexpr const char* kSetWifi     = "\u65E0\u7EBF\u5C40\u57DF\u7F51";
inline constexpr const char* kSetBt       = "\u84DD\u7259";
inline constexpr const char* kSetHotspot  = "\u4E2A\u4EBA\u70ED\u70B9";
inline constexpr const char* kSetDisplay  = "\u663E\u793A";
inline constexpr const char* kSetBattery  = "\u7535\u6C60";
inline constexpr const char* kSetSound    = "\u58F0\u97F3";
inline constexpr const char* kSetTime     = "\u65F6\u95F4";
inline constexpr const char* kSetStorage  = "\u5B58\u50A8";
inline constexpr const char* kSetSecurity = "\u5B89\u5168";
inline constexpr const char* kSetAbout    = "\u5173\u4E8E";
inline constexpr const char* kSetKeys     = "\u6309\u952E";

inline constexpr const char* kKeyCtxReader  = "\u9605\u8BFB\u5668";
inline constexpr const char* kKeyCtxList    = "\u5217\u8868";
inline constexpr const char* kKeyCtxGlobal  = "\u5168\u5C40";
inline constexpr const char* kKeySlotUp     = "\u4E0A\u6321";
inline constexpr const char* kKeySlotMid    = "\u4E2D\u6321";
inline constexpr const char* kKeySlotDown   = "\u4E0B\u6321";
inline constexpr const char* kKeyRestore    = "\u6062\u590D\u9ED8\u8BA4";
inline constexpr const char* kKeyActNone        = "\u65E0";
inline constexpr const char* kKeyActPagePrev    = "\u4E0A\u4E00\u9875";
inline constexpr const char* kKeyActPageNext    = "\u4E0B\u4E00\u9875";
inline constexpr const char* kKeyActListUp      = "\u5217\u8868\u4E0A";
inline constexpr const char* kKeyActListDown    = "\u5217\u8868\u4E0B";
inline constexpr const char* kKeyActBack        = "\u8FD4\u56DE";
inline constexpr const char* kKeyActMenu        = "\u83DC\u5355";
inline constexpr const char* kKeyActHome        = "\u4E3B\u9875";
inline constexpr const char* kKeyActBrightUp    = "\u4EAE\u5EA6+";
inline constexpr const char* kKeyActBrightDown  = "\u4EAE\u5EA6-";
inline constexpr const char* kKeyActVolUp       = "\u97F3\u91CF+";
inline constexpr const char* kKeyActVolDown     = "\u97F3\u91CF-";

inline constexpr const char* kSetBrightness  = "\u4EAE\u5EA6";
inline constexpr const char* kSetNightMode   = "\u591C\u95F4\u6A21\u5F0F";
inline constexpr const char* kSetVolume      = "\u97F3\u91CF";
inline constexpr const char* kSetMute        = "\u9759\u97F3";
inline constexpr const char* kSetCurTime     = "\u5F53\u524D\u65F6\u95F4";
inline constexpr const char* kSetTimezone    = "\u65F6\u533A";
inline constexpr const char* kSetAutoSync    = "\u81EA\u52A8\u540C\u6B65";
inline constexpr const char* kSetSyncNow     = "\u7ACB\u5373\u540C\u6B65";
inline constexpr const char* kSetClickSync   = "\u70B9\u51FB\u540C\u6B65";
inline constexpr const char* kTimeSyncOk     = "\u65F6\u95F4\u5DF2\u540C\u6B65";
inline constexpr const char* kTimeSyncFail   = "\u540C\u6B65\u5931\u8D25";
inline constexpr const char* kNeedWifiFirst  = "\u8BF7\u5148\u8FDE\u63A5 Wi-Fi";

inline constexpr const char* kSetBtName        = "\u8BBE\u5907\u540D\u79F0";
inline constexpr const char* kSetBtClients     = "\u5DF2\u8FDE\u63A5\u8BBE\u5907";
inline constexpr const char* kSetHotspotClients = "\u5DF2\u8FDE\u63A5\u8BBE\u5907";
inline constexpr const char* kSetConnCountFmt   = "%u";
inline constexpr const char* kSetHotspotName   = "\u70ED\u70B9\u540D\u79F0";
inline constexpr const char* kSetHotspotPwd    = "\u70ED\u70B9\u5BC6\u7801";
inline constexpr const char* kSetHotspotSwitch = "\u70ED\u70B9\u5F00\u5173";

inline constexpr const char* kSetWifiScan        = "\u626B\u63CF\u7F51\u7EDC";
inline constexpr const char* kSetWifiScanning    = "\u626B\u63CF\u4E2D...";
inline constexpr const char* kSetWifiClickScan   = "\u70B9\u51FB\u626B\u63CF";
inline constexpr const char* kSetWifiPwd         = "Wi-Fi \u5BC6\u7801";
inline constexpr const char* kWifiConnecting     = "\u6B63\u5728\u8FDE\u63A5\u2026";
inline constexpr const char* kWifiConnectedFmt   = "\u5DF2\u8FDE\u63A5 %s";
inline constexpr const char* kWifiWrongPwd       = "\u5BC6\u7801\u9519\u8BEF";
inline constexpr const char* kWifiTimeout        = "\u8FDE\u63A5\u8D85\u65F6";
inline constexpr const char* kWifiNotFound       = "\u672A\u627E\u5230\u7F51\u7EDC";
inline constexpr const char* kWifiFailed         = "\u8FDE\u63A5\u5931\u8D25";

inline constexpr const char* kSetStInternal   = "userdata";
inline constexpr const char* kSetStAssets     = "\u7CFB\u7EDF";
inline constexpr const char* kSetStSd         = "SD Card";
inline constexpr const char* kSetStNotIn      = "Not inserted";
inline constexpr const char* kSetSecLock      = "\u9501\u5C4F\u5BC6\u7801";
inline constexpr const char* kSetSecAutolock  = "\u81EA\u52A8\u9501\u5C4F";
inline constexpr const char* kSetAutolockDelay = "\u9501\u5C4F\u65F6\u95F4";
inline constexpr const char* kSetSecChange    = "\u4FEE\u6539\u5BC6\u7801";
inline constexpr const char* kSetSecClose     = "\u5173\u95ED\u5BC6\u7801";
inline constexpr const char* kSetPinSet       = "\u5DF2\u8BBE\u7F6E";
inline constexpr const char* kSetSecEnterNew  = "\u8F93\u5165\u65B0\u5BC6\u7801";
inline constexpr const char* kSetSecEnterOld  = "\u8F93\u5165\u5F53\u524D\u5BC6\u7801";
inline constexpr const char* kSetSecConfirm   = "\u518D\u6B21\u8F93\u5165";
inline constexpr const char* kSetSecWrong     = "\u5BC6\u7801\u9519\u8BEF";
inline constexpr const char* kSetSecMismatch  = "\u4E24\u6B21\u4E0D\u4E00\u81F4";
inline constexpr const char* kSetSecSaved     = "\u5BC6\u7801\u5DF2\u8BBE\u7F6E";
inline constexpr const char* kSetSecChanged   = "\u5BC6\u7801\u5DF2\u4FEE\u6539";
inline constexpr const char* kSetSecCleared   = "\u5DF2\u5173\u95ED\u5BC6\u7801";
inline constexpr const char* kSetSecSaveFail  = "\u4FDD\u5B58\u5931\u8D25";
inline constexpr const char* kSetSecPinInvalid = "\u8BF7\u8F93\u51654\u4F4D\u6570\u5B57";
inline constexpr const char* kLockEnterPin    = "\u8F93\u5165\u5BC6\u7801\u89E3\u9501";
inline constexpr const char* kSetUnset        = "\u672A\u8BBE\u7F6E";
inline constexpr const char* kAboutDeviceName = "\u8BBE\u5907\u540D";
inline constexpr const char* kAboutMac        = "MAC";
inline constexpr const char* kAboutIp         = "IP";
inline constexpr const char* kAboutSram       = "SRAM";
inline constexpr const char* kAboutPsram      = "PSRAM";
inline constexpr const char* kAboutBuild      = "\u7F16\u8BD1";
inline constexpr const char* kIpNone          = "\u672A\u8FDE\u63A5";
inline constexpr const char* kSetFirmware     = "\u56FA\u4EF6";
inline constexpr const char* kSetCpu          = "CPU";
inline constexpr const char* kSetDevice       = "\u8BBE\u5907";
inline constexpr const char* kSetDeviceModel  = "Ebook ESP32-S3";

inline constexpr const char* kClkTabClock  = "\u65F6\u949F";
inline constexpr const char* kClkTabAlarm  = "\u95F9\u949F";
inline constexpr const char* kClkAddAlarm  = "+ \u6DFB\u52A0\u95F9\u949F";
inline constexpr const char* kClkAlarmRing = "\u95F9\u949F\u54CD\u4E86";
inline constexpr const char* kClkRepeat    = "\u91CD\u590D";
inline constexpr const char* kClkDelete    = "\u5220\u9664\u95F9\u949F";
inline constexpr const char* kClkNoAlarm   = "\u6682\u65E0\u95F9\u949F";
inline constexpr const char* kClkEditAlarm = "\u7F16\u8F91\u95F9\u949F";
inline constexpr const char* kClkHourLabel = "\u65F6";
inline constexpr const char* kClkMinuteLabel = "\u5206";
inline constexpr const char* kClkHourHint  = "\u5C0F\u65F6 0-23";
inline constexpr const char* kClkMinuteHint = "\u5206\u949F 0-59";
inline constexpr const char* kClkTimeEmpty   = "\u8BF7\u8F93\u5165\u65F6\u95F4";
inline constexpr const char* kClkTimeInvalid = "\u65F6\u95F4\u65E0\u6548";

inline constexpr const char* kDrawPen      = "\u7B14";
inline constexpr const char* kDrawEraser   = "\u64E6";
inline constexpr const char* kDrawClear    = "\u6E05";
inline constexpr const char* kDrawSave     = "\u5B58";
inline constexpr const char* kDrawSizeFine = "\u7EC6";
inline constexpr const char* kDrawSizeMid  = "\u4E2D";
inline constexpr const char* kDrawSizeBold = "\u7C97";
inline constexpr const char* kDrawSaved    = "\u5DF2\u4FDD\u5B58 ";
inline constexpr const char* kDrawSaveFail = "\u4FDD\u5B58\u5931\u8D25";
inline constexpr const char* kDrawNoMem    = "\u5185\u5B58\u4E0D\u8DB3";
inline constexpr const char* kDrawExitAsk  = "\u662F\u5426\u4E22\u5F03\u753B\u4F5C\u9000\u51FA";
inline constexpr const char* kDrawYes      = "\u662F";
inline constexpr const char* kDrawNo       = "\u5426";

inline constexpr const char* kNoteNew       = "\u65B0\u5EFA\u8BB0\u4E8B";
inline constexpr const char* kNoteEmpty     = "\u6682\u65E0\u8BB0\u4E8B";
inline constexpr const char* kNoteEmptyHint = "\u70B9\u300C\u65B0\u5EFA\u8BB0\u4E8B\u300D\u5F00\u59CB";
inline constexpr const char* kNoteTitleHint = "\u8BB0\u4E8B\u6807\u9898";
inline constexpr const char* kNoteEditHint = "\u70B9\u51FB\u300C\u8F93\u5165\u300D\u5F00\u59CB\u8BB0\u5F55";
inline constexpr const char* kNoteInput    = "\u8F93\u5165";
inline constexpr const char* kNoteDelLine  = "\u5220\u9664";
inline constexpr const char* kNoteClear    = "\u6E05\u7A7A";
inline constexpr const char* kNoteClearAsk = "\u6E05\u7A7A\u5168\u90E8\u5185\u5BB9\uFF1F";
inline constexpr const char* kNoteNothingDel = "\u6CA1\u6709\u53EF\u5220\u9664\u7684\u5185\u5BB9";
inline constexpr const char* kNoteCleared  = "\u5DF2\u6E05\u7A7A";
inline constexpr const char* kNoteSave     = "\u4FDD\u5B58";
inline constexpr const char* kNoteSaved    = "\u5DF2\u4FDD\u5B58";
inline constexpr const char* kNoteSaveFail = "\u4FDD\u5B58\u5931\u8D25";
inline constexpr const char* kNoteTooLong  = "\u5185\u5BB9\u8FC7\u957F";

inline constexpr const char* kWxSwitchCity = "\u5207\u6362\u57CE\u5E02";
inline constexpr const char* kWxLoading    = "\u52A0\u8F7D\u4E2D\u2026";
inline constexpr const char* kWxNetFail    = "\u7F51\u7EDC\u8BF7\u6C42\u5931\u8D25";
inline constexpr const char* kWxNoKey      = "\u672A\u914D\u7F6E\u9AD8\u5FB7 Key";
inline constexpr const char* kWxParseFail  = "\u6570\u636E\u89E3\u6790\u5931\u8D25";
inline constexpr const char* kWxHighLow    = "\u4ECA\u65E5";
inline constexpr const char* kWxTapRefresh = "\u70B9\u51FB\u5C4F\u5E55\u5237\u65B0";

} // namespace app::ebook::ui::strings
