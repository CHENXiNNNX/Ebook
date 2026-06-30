# Ebook 框架说明

## 目录

```
app/ebook/
├── router/           # 路由 + 刷新边表（改刷新策略改 refresh_edges.cc）
├── presenter/        # 上屏管线
├── composer/         # 帧合成
├── display/          # DisplayPort 驱动适配
├── shell/            # 系统页面 Lock/Home/AppGrid/AppHost
├── apps/             # Reader / Settings / Files 等内置应用
├── overlays/         # StatusBar / Toast / Keyboard / ControlCenter
├── ui/ui_loop.cc     # UI 主循环
└── platform/boot.cc  # 启动链
```

## 指定跳转刷新

编辑 `router/refresh_edges.cc` 中 `kEdges[]`，为每条 `from → to` 配置 `RefreshIntent`。

调用方也可显式覆盖：

```cpp
router::RefreshIntent force = router::intent_fast_full();
router::Router::instance().navigate(target, router::NavAction::Forward, &force);
```

## App 内刷新与跳转

```cpp
request_repaint();                           // 局刷（默认 intent_partial_full）
request_repaint(router::intent_fast_full()); // 快刷
navigate_page(sub_page_id);                  // App 子页跳转，走 refresh_edges 边表
```
