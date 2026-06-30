#include "text/path_encoding.hpp"

#include <cstdio>
#include <cstring>

#include "gfx/utf8.hpp"

namespace app::ebook::text {

namespace {

// kGbkLeadMin / kGbkLeadMax / kGbkTrailCount / kGbkGrid[]
#include "text/gbk_table.inc"

bool gbk_trail_index(uint8_t trail, uint8_t& idx)
{
    if (trail < 0x40 || trail == 0x7F || trail > 0xFE) return false;
    idx = (trail < 0x7F) ? static_cast<uint8_t>(trail - 0x40)
                         : static_cast<uint8_t>(trail - 0x41);
    return true;
}

bool gbk_pair_to_unicode(uint8_t lead, uint8_t trail, uint16_t& uni)
{
    if (lead < kGbkLeadMin || lead > kGbkLeadMax) return false;
    uint8_t ti = 0;
    if (!gbk_trail_index(trail, ti)) return false;
    const size_t li = static_cast<size_t>(lead - kGbkLeadMin);
    uni = kGbkGrid[li * kGbkTrailCount + ti];
    return uni != 0xFFFF;
}

size_t append_utf8(uint32_t cp, char* dst, size_t dst_cap, size_t di)
{
    if (cp < 0x80)
    {
        if (di + 1 >= dst_cap) return di;
        dst[di++] = static_cast<char>(cp);
        return di;
    }
    if (cp < 0x800)
    {
        if (di + 2 >= dst_cap) return di;
        dst[di++] = static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
        dst[di++] = static_cast<char>(0x80 | (cp & 0x3F));
        return di;
    }
    if (cp < 0x10000)
    {
        if (di + 3 >= dst_cap) return di;
        dst[di++] = static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
        dst[di++] = static_cast<char>(0x80 | ((cp >> 6)  & 0x3F));
        dst[di++] = static_cast<char>(0x80 | (cp & 0x3F));
        return di;
    }
    return di;
}

size_t gbk_to_utf8(const char* src, size_t src_len, char* dst, size_t dst_cap)
{
    if (src == nullptr || dst == nullptr || dst_cap == 0) return 0;

    size_t si = 0, di = 0;
    while (si < src_len && src[si] != '\0')
    {
        const auto c = static_cast<uint8_t>(src[si]);
        if (c < 0x80)
        {
            if (di + 1 >= dst_cap) break;
            dst[di++] = static_cast<char>(c);
            ++si;
            continue;
        }
        if (si + 1 >= src_len) break;

        uint16_t uni = 0;
        if (!gbk_pair_to_unicode(c, static_cast<uint8_t>(src[si + 1]), uni))
        {
            if (di + 1 >= dst_cap) break;
            dst[di++] = '?';
            ++si;
            continue;
        }

        const size_t next = append_utf8(uni, dst, dst_cap, di);
        if (next == di) break;
        di = next;
        si += 2;
    }

    dst[di < dst_cap ? di : (dst_cap - 1)] = '\0';
    return di;
}

struct EncodeScore
{
    uint32_t utf8_cjk3{0};
    uint32_t gbk_cjk{0};
    uint32_t high_bytes{0};
};

bool is_cjk(uint32_t cp)
{
    return cp >= 0x4E00U && cp <= 0x9FFFU;
}

void score_sample(const char* data, size_t len, EncodeScore& s)
{
    if (data == nullptr || len == 0) return;

    size_t i = 0;
    while (i < len)
    {
        const auto c0 = static_cast<uint8_t>(data[i]);
        if (c0 < 0x80) { ++i; continue; }
        ++s.high_bytes;

        // 尝试 UTF-8 三字节 CJK
        if ((c0 & 0xF0) == 0xE0 && i + 2 < len)
        {
            const auto c1 = static_cast<uint8_t>(data[i + 1]);
            const auto c2 = static_cast<uint8_t>(data[i + 2]);
            if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 &&
                !(c0 == 0xE0 && c1 < 0xA0))
            {
                const uint32_t cp = (static_cast<uint32_t>(c0 & 0x0F) << 12) |
                                    (static_cast<uint32_t>(c1 & 0x3F) << 6)  |
                                    static_cast<uint32_t>(c2 & 0x3F);
                if (is_cjk(cp)) { ++s.utf8_cjk3; i += 3; continue; }
            }
        }

        // 尝试 GBK 双字节 CJK
        if (i + 1 < len)
        {
            uint16_t uni = 0;
            if (gbk_pair_to_unicode(c0, static_cast<uint8_t>(data[i + 1]), uni) && is_cjk(uni))
            {
                ++s.gbk_cjk;
                i += 2;
                continue;
            }
        }
        ++i;
    }
}

TextEncoding decide_encoding(const EncodeScore& s)
{
    if (s.high_bytes == 0)         return TextEncoding::Utf8;
    if (s.utf8_cjk3 > s.gbk_cjk)   return TextEncoding::Utf8;
    return TextEncoding::Gbk;
}

} // namespace

bool is_valid_utf8(const char* s, size_t len)
{
    if (s == nullptr) return false;
    if (len == 0)     len = std::strlen(s);

    size_t i = 0;
    while (i < len)
    {
        const auto c0 = static_cast<uint8_t>(s[i]);
        if (c0 < 0x80) { ++i; continue; }

        // 2 字节序列
        if ((c0 & 0xE0) == 0xC0)
        {
            if (i + 1 >= len) return false;
            const auto c1 = static_cast<uint8_t>(s[i + 1]);
            if ((c1 & 0xC0) != 0x80 || c0 < 0xC2) return false;
            i += 2;
            continue;
        }
        // 3 字节序列
        if ((c0 & 0xF0) == 0xE0)
        {
            if (i + 2 >= len) return false;
            const auto c1 = static_cast<uint8_t>(s[i + 1]);
            const auto c2 = static_cast<uint8_t>(s[i + 2]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
            if (c0 == 0xE0 && c1 < 0xA0) return false;  // 拒绝过长编码
            i += 3;
            continue;
        }
        // 4 字节序列
        if ((c0 & 0xF8) == 0xF0)
        {
            if (i + 3 >= len) return false;
            const auto c1 = static_cast<uint8_t>(s[i + 1]);
            const auto c2 = static_cast<uint8_t>(s[i + 2]);
            const auto c3 = static_cast<uint8_t>(s[i + 3]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
                return false;
            if (c0 == 0xF0 && c1 < 0x90) return false;
            i += 4;
            continue;
        }
        return false;
    }
    return true;
}

size_t normalize_path_segment(const char* src, char* dst, size_t dst_cap)
{
    if (src == nullptr || dst == nullptr || dst_cap == 0) return 0;

    const size_t src_len = std::strlen(src);
    if (is_valid_utf8(src, src_len))
    {
        const size_t copy_len = (src_len < dst_cap - 1) ? src_len : (dst_cap - 1);
        std::memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
        return copy_len;
    }
    return gbk_to_utf8(src, src_len, dst, dst_cap);
}

TextEncoding detect_text_encoding(const char* data, size_t len)
{
    if (data == nullptr || len == 0) return TextEncoding::Utf8;

    // UTF-8 BOM
    if (len >= 3 &&
        static_cast<uint8_t>(data[0]) == 0xEF &&
        static_cast<uint8_t>(data[1]) == 0xBB &&
        static_cast<uint8_t>(data[2]) == 0xBF)
        return TextEncoding::Utf8;

    EncodeScore s{};
    score_sample(data, len, s);
    return decide_encoding(s);
}

TextEncoding detect_file_encoding(const char* path)
{
    if (path == nullptr || path[0] == '\0') return TextEncoding::Utf8;

    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return TextEncoding::Utf8;

    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return TextEncoding::Utf8; }
    const long sz = std::ftell(f);
    if (sz <= 0) { std::fclose(f); return TextEncoding::Utf8; }

    uint8_t buf[4096];
    const long offsets[] = {0, sz / 4, sz / 2, (sz * 3) / 4};
    EncodeScore total{};

    for (long off : offsets)
    {
        if (off < 0 || off >= sz) continue;
        if (std::fseek(f, off, SEEK_SET) != 0) continue;

        size_t to_read = sizeof(buf);
        if (off + static_cast<long>(to_read) > sz)
            to_read = static_cast<size_t>(sz - off);

        const size_t got = std::fread(buf, 1, to_read, f);
        if (got == 0) continue;

        // 文件头 BOM
        if (off == 0 && got >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
        {
            std::fclose(f);
            return TextEncoding::Utf8;
        }
        score_sample(reinterpret_cast<const char*>(buf), got, total);
    }

    std::fclose(f);
    return decide_encoding(total);
}

size_t convert_text_to_utf8(TextEncoding enc, const char* src, size_t src_len,
                            char* dst, size_t dst_cap)
{
    if (src == nullptr || dst == nullptr || dst_cap == 0) return 0;

    if (enc == TextEncoding::Utf8)
    {
        const size_t copy_len = (src_len < dst_cap - 1) ? src_len : (dst_cap - 1);
        std::memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
        return copy_len;
    }
    return gbk_to_utf8(src, src_len, dst, dst_cap);
}

bool next_text_codepoint(TextEncoding enc, const char*& p, const char* end, uint32_t& cp)
{
    if (p == nullptr || end == nullptr || p >= end) return false;

    if (enc == TextEncoding::Utf8)
    {
        if (*p == '\0') return false;
        const uint8_t n = gfx::utf8_decode(p, cp);
        if (n == 0)
        {
            cp = static_cast<uint8_t>(*p);
            ++p;
        }
        else
        {
            p += n;
        }
        return true;
    }

    // GBK
    const auto c0 = static_cast<uint8_t>(*p);
    if (c0 < 0x80) { cp = c0; ++p; return true; }

    if (p + 1 >= end) { cp = '?'; ++p; return true; }

    uint16_t uni = 0;
    if (!gbk_pair_to_unicode(c0, static_cast<uint8_t>(p[1]), uni))
    {
        cp = '?';
        ++p;
        return true;
    }
    cp = uni;
    p += 2;
    return true;
}

} // namespace app::ebook::text
