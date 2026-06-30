#pragma once

#include <cstddef>
#include <cstdint>

namespace app::ebook::text {

/** 文本编码 */
enum class TextEncoding : uint8_t
{
    Utf8 = 0,
    Gbk  = 1,
};

/** 严格 UTF-8 校验（拒绝过长编码） */
bool is_valid_utf8(const char* s, size_t len = 0);

/** 检测一段字节的编码（合法 UTF-8 视作 UTF-8，否则 GBK） */
TextEncoding detect_text_encoding(const char* data, size_t len);

/** 多点采样检测整文件编码 */
TextEncoding detect_file_encoding(const char* path);

/**
 * @brief 把文件名 / 目录名规范为 UTF-8 供 UI 显示
 *
 * - 已是 UTF-8 → 原样拷贝
 * - 否则按 GBK 解码并转 UTF-8
 * - 用 readdir/stat 时仍应保留原始字节路径打开文件
 *
 * @return 写入字节数（不含 '\0'）
 */
size_t normalize_path_segment(const char* src, char* dst, size_t dst_cap);

/** 按指定编码把字节流转 UTF-8 */
size_t convert_text_to_utf8(TextEncoding enc, const char* src, size_t src_len,
                            char* dst, size_t dst_cap);

/**
 * @brief 按编码读下一个码点（用于分页 / 折行）
 *
 * @param[in]  p    当前指针，成功后前移
 * @param[in]  end  缓冲末尾（不含）
 * @param[out] cp   输出码点
 * @return 是否成功读到一个码点
 */
bool next_text_codepoint(TextEncoding enc, const char*& p, const char* end, uint32_t& cp);

} // namespace app::ebook::text
