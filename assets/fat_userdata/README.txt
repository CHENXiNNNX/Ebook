Ebook userdata 出厂镜像目录说明
================================

本目录用于编译时生成 userdata 分区镜像（FAT32）。
编译后，此处内容会烧录到 Flash 的 userdata 分区；
设备启动后挂载为 /int（设置中显示为 userdata）。

也可通过 USB 大容量存储或 TF 卡，按相同目录结构放置文件。


目录结构（请按此放置）
----------------------

assets/fat_userdata/
├── README.txt          （本说明，会出现在设备 /int/README.txt）
└── Ebook/
    ├── txt/            阅读器：TXT 电子书
    ├── music/          音乐：MP3 / WAV
    ├── photos/         相册：JPG / JPEG
    ├── notes/          记事本：TXT 笔记（可选，App 会自动创建）
    └── drawings/       画板：BMP 作品（可选，App 会自动创建）


各目录对应关系
--------------

  本目录（编译前）              设备路径              应用
  ----------------              --------              ----
  Ebook/txt/         →    /int/Ebook/txt/      阅读
  Ebook/music/       →    /int/Ebook/music/    音乐
  Ebook/photos/      →    /int/Ebook/photos/   相册
  Ebook/notes/       →    /int/Ebook/notes/    记事本
  Ebook/drawings/    →    /int/Ebook/drawings/ 画板


文件格式要求
------------

  txt/       仅扫描 .txt 文件（UTF-8 / GBK 编码）
  music/     .mp3、.wav
  photos/    .jpg、.jpeg（单张建议 ≤ 2 MB）
  notes/     .txt（记事本保存的笔记）
  drawings/  .bmp（1-bit 黑白位图，画板导出格式）


使用示例
--------

  1. 出厂预置（开发者）
     将书籍、音乐、图片放入上述子目录后重新编译烧录即可。

  2. USB 传输（用户）
     设置 → 存储 → 开启 USB 存储，用数据线连接电脑；
     在 U 盘根目录下创建 Ebook/ 及对应子目录，放入文件后安全弹出。

  3. TF 卡（用户）
     在 TF 卡根目录使用相同结构：/sd/Ebook/txt、music、photos 等。
     阅读、音乐、相册会同时扫描 /int 与 /sd 下的对应目录。


注意事项
--------

  - 各应用只扫描固定子目录，不会全盘检索。
  - txt/、music/、photos/ 需在出厂镜像中预先创建，否则对应 App 书架为空。
  - notes/、drawings/ 首次使用时会由 App 自动创建，出厂可不预置。
  - 阅读进度、索引等缓存由 App 在运行时生成，无需手动放入。
