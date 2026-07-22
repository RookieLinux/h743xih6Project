此目录用于保存后续 LVGL 图形资源。
文件名统一使用 ASCII，文本元数据统一使用无 BOM UTF-8。
推荐将图片转换为 LVGL 8 原生二进制 .bin 格式，运行时使用：
lv_img_set_src(image, "Q:/ui/images/example.bin");
