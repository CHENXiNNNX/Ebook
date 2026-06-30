Ebook assets 出厂镜像目录说明
================================

本目录用于编译时生成 assets 分区镜像（LittleFS）。
编译后，此处内容会烧录到 Flash 的 assets 分区；
设备启动后挂载为 /assets，仅供系统只读使用。

文件管理 App 可浏览 /assets，但不可删除或修改其中文件。
用户个人数据请放入 /int（userdata）或 TF 卡，勿放此处。


目录结构（请按此放置）
----------------------

assets/littlefs_assets/
├── README.txt              （本说明，设备上为 /assets/README.txt）
├── fonts/                  系统字库
│   ├── simhei_12.bin       12px 点阵字库（必需）
│   ├── simhei_14.bin       14px 点阵字库（必需）
│   ├── simhei_16.bin       16px 点阵字库（必需）
│   ├── font_awesome_6.ttf  图标字体（必需）
│   └── simhei.ttf          矢量中文备用（可选，见下方说明）
├── html/
│   └── index.html          文件传输 Web 页面（热点模式）
└── audio/
    └── woden_fish.wav      电子木鱼敲击音效


各目录对应关系
--------------

  本目录（编译前）              设备路径                    用途
  ----------------              --------                    ----
  fonts/simhei_*.bin   →    /assets/fonts/simhei_*.bin   UI 中文点阵渲染
  fonts/font_awesome_6.ttf → /assets/fonts/font_awesome_6.ttf  状态栏/图标
  fonts/simhei.ttf     →    /assets/fonts/simhei.ttf     大字号 FreeType 备用
  html/index.html      →    /assets/html/index.html      文件服务器首页
  audio/woden_fish.wav →    /assets/audio/woden_fish.wav  木鱼 App 音效


字库生成与更新
--------------

  点阵字库（simhei_12/14/16.bin）：
    源文件：tools/fonts/simhei.ttf（不直接写入 Flash）
    生成命令：
      python tools/gen_bitmap_font.py
    输出格式：EBF1 v1，含 ASCII + 常用中文与标点

  图标字体（font_awesome_6.ttf）：
    源文件：others/fontawesome-free-6.4.0-web/webfonts/fa-solid-900.ttf
    生成命令：
      python tools/gen_icon_font.py
    按 main/app/ebook/gfx/icon.hpp 中的码点裁剪子集

  矢量中文备用（simhei.ttf，可选）：
    若需 24px 以上大字号 FreeType 渲染，将 tools/fonts/simhei.ttf
    复制到 fonts/simhei.ttf；未放置时系统仅使用点阵字库，不影响正常使用。


构建与烧录
----------

  - CMake：littlefs_create_partition_image(assets ../assets/littlefs_assets)
  - Kconfig：Ebook 存储 → 构建并烧录 assets (LittleFS) 镜像
  - 分区：4 MB，偏移 0x400000（见 partitions/partition_16mb.csv）
  - 随 idf.py build flash 一并写入，或从 dist/.../flash/assets.bin 单独烧录


注意事项
--------

  - 修改本目录后需重新编译，assets 镜像才会更新。
  - 分区为只读挂载；运行时无法通过 App 或 USB 修改其中文件。
  - 新增资源请控制总体积不超过 4 MB 分区容量。
  - html/、audio/ 为系统功能依赖，删除会导致对应功能异常。
