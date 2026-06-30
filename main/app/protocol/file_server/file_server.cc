#include "file_server.hpp"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "storage/storage.hpp"

namespace app::protocol::file_server {

namespace {

static const char* const TAG = "FileServer";

constexpr const char* kIndexPath  = "/assets/html/index.html";
constexpr size_t      kChunkSize  = 4096;
constexpr size_t      kMaxEntries = 256;

httpd_handle_t s_httpd    = nullptr;
int            s_dns_sock = -1;
volatile bool  s_dns_run  = false;

// httpd 单任务串行处理请求，静态缓冲安全
char s_chunk[kChunkSize];

/** 取 AP 的 IPv4（点分文本）；失败回退 192.168.4.1 */
void get_ap_ip_str(char* out, size_t len)
{
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t info{};
    if (ap != nullptr && esp_netif_get_ip_info(ap, &info) == ESP_OK && info.ip.addr != 0)
    {
        snprintf(out, len, IPSTR, IP2STR(&info.ip));
        return;
    }
    snprintf(out, len, "192.168.4.1");
}

uint32_t get_ap_ip_u32()
{
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t info{};
    if (ap != nullptr && esp_netif_get_ip_info(ap, &info) == ESP_OK && info.ip.addr != 0)
    {
        return info.ip.addr; // 网络字节序
    }
    return esp_netif_htonl(0xC0A80401); // 192.168.4.1
}

/** 原地 percent 解码 */
void url_decode(char* s)
{
    char* w = s;
    while (*s != '\0')
    {
        if (*s == '%' && s[1] != '\0' && s[2] != '\0')
        {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(s[1]);
            const int lo = hex(s[2]);
            if (hi >= 0 && lo >= 0)
            {
                *w++ = static_cast<char>((hi << 4) | lo);
                s += 3;
                continue;
            }
        }
        *w++ = (*s == '+') ? ' ' : *s;
        ++s;
    }
    *w = '\0';
}

/** 读取并解码 query 参数；成功返回 true */
bool query_param(httpd_req_t* req, const char* key, char* out, size_t out_len)
{
    char query[512];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    {
        return false;
    }
    if (httpd_query_key_value(query, key, out, out_len) != ESP_OK)
    {
        return false;
    }
    url_decode(out);
    return true;
}

/**
 * 把页面里的虚拟路径（以 / 开头，根即用户分区）映射到 /int 下的实际路径。
 * 拒绝 ".." 与空路径。
 */
bool build_real_path(const char* vpath, char* out, size_t out_len)
{
    if (vpath == nullptr || vpath[0] != '/')
    {
        return false;
    }
    if (std::strstr(vpath, "..") != nullptr)
    {
        return false;
    }
    if (vpath[1] == '\0') // "/"
    {
        snprintf(out, out_len, "%s", common::storage::k_path_internal);
        return true;
    }
    const int n = snprintf(out, out_len, "%s%s", common::storage::k_path_internal, vpath);
    return n > 0 && static_cast<size_t>(n) < out_len;
}

bool internal_ready()
{
    return common::storage::StorageMgr::get_instance().is_mounted(
        common::storage::MountKind::Internal);
}

/** JSON 字符串转义（仅 " \ 与控制字符） */
void json_escape(const char* in, std::string& out)
{
    for (const char* p = in; *p != '\0'; ++p)
    {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '"' || c == '\\')
        {
            out.push_back('\\');
            out.push_back(static_cast<char>(c));
        }
        else if (c < 0x20)
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        }
        else
        {
            out.push_back(static_cast<char>(c));
        }
    }
}

esp_err_t send_plain(httpd_req_t* req, const char* status, const char* msg)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

esp_err_t index_handler(httpd_req_t* req)
{
    FILE* f = fopen(kIndexPath, "rb");
    if (f == nullptr)
    {
        return send_plain(req, "500 Internal Server Error",
                          "页面缺失: /assets/html/index.html");
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    size_t n = 0;
    while ((n = fread(s_chunk, 1, sizeof(s_chunk), f)) > 0)
    {
        if (httpd_resp_send_chunk(req, s_chunk, n) != ESP_OK)
        {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

/** Captive Portal：未知路径一律 302 到首页（触发系统弹窗） */
esp_err_t redirect_handler(httpd_req_t* req)
{
    char ip[16];
    get_ap_ip_str(ip, sizeof(ip));
    char loc[40];
    snprintf(loc, sizeof(loc), "http://%s/", ip);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", loc);
    return httpd_resp_send(req, nullptr, 0);
}

/**
 * 静默丢弃非本服务的请求。
 * 连热点后手机后台 App（微信 /mmtls、各类 PATCH 探测等）会被 DNS 劫持到本机，
 * 若无对应 method 处理器，httpd 会打 405 警告；此处统一回 404 并吞掉 body。
 */
esp_err_t discard_handler(httpd_req_t* req)
{
    int remaining = static_cast<int>(req->content_len);
    while (remaining > 0)
    {
        const int chunk = remaining > 64 ? 64 : remaining;
        const int got   = httpd_req_recv(req, s_chunk, static_cast<size_t>(chunk));
        if (got <= 0)
        {
            break;
        }
        remaining -= got;
    }

    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t list_handler(httpd_req_t* req)
{
    if (!internal_ready())
    {
        return send_plain(req, "503 Service Unavailable", "存储被 USB 占用");
    }

    char vpath[256];
    char real[288];
    if (!query_param(req, "path", vpath, sizeof(vpath)) ||
        !build_real_path(vpath, real, sizeof(real)))
    {
        return send_plain(req, "400 Bad Request", "路径无效");
    }

    DIR* dir = opendir(real);
    if (dir == nullptr)
    {
        return send_plain(req, "404 Not Found", "目录不存在");
    }

    std::string json;
    json.reserve(2048);
    json += "{\"path\":\"";
    json_escape(vpath, json);
    json += "\",\"entries\":[";

    size_t count = 0;
    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr && count < kMaxEntries)
    {
        if (ent->d_name[0] == '.')
        {
            continue;
        }
        // 隐藏 Windows 自动生成目录
        if (strcasecmp(ent->d_name, "System Volume Information") == 0 ||
            strcasecmp(ent->d_name, "$RECYCLE.BIN") == 0)
        {
            continue;
        }

        char child[560];
        snprintf(child, sizeof(child), "%s/%s", real, ent->d_name);
        struct stat st{};
        const bool ok     = (stat(child, &st) == 0);
        const bool is_dir = ok && S_ISDIR(st.st_mode);

        if (count > 0)
        {
            json += ',';
        }
        json += "{\"n\":\"";
        json_escape(ent->d_name, json);
        json += "\",\"d\":";
        json += is_dir ? "1" : "0";
        json += ",\"s\":";
        char num[24];
        snprintf(num, sizeof(num), "%u",
                 static_cast<unsigned>(ok && !is_dir ? st.st_size : 0));
        json += num;
        json += '}';
        ++count;
    }
    closedir(dir);
    json += "]}";

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, json.c_str(), json.size());
}

esp_err_t download_handler(httpd_req_t* req)
{
    if (!internal_ready())
    {
        return send_plain(req, "503 Service Unavailable", "存储被 USB 占用");
    }

    char vpath[256];
    char real[288];
    if (!query_param(req, "path", vpath, sizeof(vpath)) ||
        !build_real_path(vpath, real, sizeof(real)))
    {
        return send_plain(req, "400 Bad Request", "路径无效");
    }

    FILE* f = fopen(real, "rb");
    if (f == nullptr)
    {
        return send_plain(req, "404 Not Found", "文件不存在");
    }

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment");

    size_t n = 0;
    while ((n = fread(s_chunk, 1, sizeof(s_chunk), f)) > 0)
    {
        if (httpd_resp_send_chunk(req, s_chunk, n) != ESP_OK)
        {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t upload_handler(httpd_req_t* req)
{
    if (!internal_ready())
    {
        return send_plain(req, "503 Service Unavailable", "存储被 USB 占用");
    }

    char vdir[224];
    char name[128];
    if (!query_param(req, "path", vdir, sizeof(vdir)) ||
        !query_param(req, "name", name, sizeof(name)))
    {
        return send_plain(req, "400 Bad Request", "参数缺失");
    }
    if (name[0] == '\0' || std::strchr(name, '/') != nullptr ||
        std::strstr(name, "..") != nullptr)
    {
        return send_plain(req, "400 Bad Request", "文件名无效");
    }

    char vpath[368];
    snprintf(vpath, sizeof(vpath), "%s%s%s",
             vdir, (vdir[1] == '\0') ? "" : "/", name);
    char real[400];
    if (!build_real_path(vpath, real, sizeof(real)))
    {
        return send_plain(req, "400 Bad Request", "路径无效");
    }

    FILE* f = fopen(real, "wb");
    if (f == nullptr)
    {
        return send_plain(req, "500 Internal Server Error", "无法创建文件");
    }

    size_t remaining = req->content_len;
    while (remaining > 0)
    {
        const size_t want = remaining < sizeof(s_chunk) ? remaining : sizeof(s_chunk);
        const int    got  = httpd_req_recv(req, s_chunk, want);
        if (got <= 0)
        {
            if (got == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }
            fclose(f);
            unlink(real);
            return ESP_FAIL;
        }
        if (fwrite(s_chunk, 1, static_cast<size_t>(got), f) !=
            static_cast<size_t>(got))
        {
            fclose(f);
            unlink(real);
            return send_plain(req, "500 Internal Server Error", "写入失败(空间不足?)");
        }
        remaining -= static_cast<size_t>(got);
    }
    fclose(f);

    ESP_LOGI(TAG, "上传完成: %s (%u 字节)", real,
             static_cast<unsigned>(req->content_len));
    return send_plain(req, "200 OK", "ok");
}

esp_err_t delete_handler(httpd_req_t* req)
{
    if (!internal_ready())
    {
        return send_plain(req, "503 Service Unavailable", "存储被 USB 占用");
    }

    char vpath[256];
    char real[288];
    if (!query_param(req, "path", vpath, sizeof(vpath)) ||
        !build_real_path(vpath, real, sizeof(real)) || vpath[1] == '\0')
    {
        return send_plain(req, "400 Bad Request", "路径无效");
    }

    char dirflag[8] = "0";
    query_param(req, "dir", dirflag, sizeof(dirflag));

    const int rc = (dirflag[0] == '1') ? rmdir(real) : unlink(real);
    if (rc != 0)
    {
        return send_plain(req, "500 Internal Server Error", "删除失败");
    }
    return send_plain(req, "200 OK", "ok");
}

esp_err_t mkdir_handler(httpd_req_t* req)
{
    if (!internal_ready())
    {
        return send_plain(req, "503 Service Unavailable", "存储被 USB 占用");
    }

    char vpath[256];
    char real[288];
    if (!query_param(req, "path", vpath, sizeof(vpath)) ||
        !build_real_path(vpath, real, sizeof(real)) || vpath[1] == '\0')
    {
        return send_plain(req, "400 Bad Request", "路径无效");
    }

    if (mkdir(real, 0777) != 0)
    {
        return send_plain(req, "500 Internal Server Error", "创建失败");
    }
    return send_plain(req, "200 OK", "ok");
}

void dns_task(void*)
{
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "DNS socket 创建失败");
        vTaskDelete(nullptr);
        return;
    }
    s_dns_sock = sock;

    sockaddr_in bind_addr{};
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(53);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0)
    {
        ESP_LOGE(TAG, "DNS bind 53 失败");
        close(sock);
        s_dns_sock = -1;
        vTaskDelete(nullptr);
        return;
    }

    timeval tv{};
    tv.tv_sec = 1;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512 + 16];
    while (s_dns_run)
    {
        sockaddr_in src{};
        socklen_t   src_len = sizeof(src);
        const int   len     = recvfrom(sock, buf, 512, 0,
                                       reinterpret_cast<sockaddr*>(&src), &src_len);
        if (len < 12 + 5) // DNS 头 12 字节 + 最小问题段
        {
            continue;
        }

        // 跳过 QNAME 找到问题段结束（zero byte + QTYPE/QCLASS 各 2 字节）
        int pos = 12;
        while (pos < len && buf[pos] != 0)
        {
            pos += buf[pos] + 1;
        }
        pos += 1 + 4;
        if (pos > len)
        {
            continue;
        }

        // 头部：响应 + 递归可用，1 问题 1 应答
        buf[2] = 0x81; buf[3] = 0x80;
        buf[4] = 0x00; buf[5] = 0x01; // QDCOUNT
        buf[6] = 0x00; buf[7] = 0x01; // ANCOUNT
        buf[8] = 0x00; buf[9] = 0x00;
        buf[10] = 0x00; buf[11] = 0x00;

        // 应答：指针指向问题名，A 记录，TTL 60s，IP 4 字节
        const uint32_t ip = get_ap_ip_u32();
        uint8_t*       p  = buf + pos;
        *p++ = 0xC0; *p++ = 0x0C;
        *p++ = 0x00; *p++ = 0x01; // TYPE A
        *p++ = 0x00; *p++ = 0x01; // CLASS IN
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x3C;
        *p++ = 0x00; *p++ = 0x04;
        std::memcpy(p, &ip, 4);
        p += 4;

        sendto(sock, buf, static_cast<size_t>(p - buf), 0,
               reinterpret_cast<sockaddr*>(&src), src_len);
    }

    close(sock);
    s_dns_sock = -1;
    vTaskDelete(nullptr);
}

/** 让 DHCP 把本机下发为 DNS 服务器（自动弹页的前提） */
void setup_dhcp_dns()
{
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap == nullptr)
    {
        return;
    }

    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(ap, &info) != ESP_OK)
    {
        return;
    }

    esp_netif_dns_info_t dns{};
    dns.ip.type            = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = info.ip.addr;

    esp_netif_dhcps_stop(ap);
    uint8_t offer = 1; // OFFER_DNS
    esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                           &offer, sizeof(offer));
    esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_dhcps_start(ap);
}

} // namespace

esp_err_t start()
{
    if (s_httpd != nullptr)
    {
        return ESP_OK;
    }

    setup_dhcp_dns();

    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size       = 8192;
    cfg.max_uri_handlers = 16;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/",             .method = HTTP_GET,  .handler = index_handler,    .user_ctx = nullptr},
        {.uri = "/api/list",     .method = HTTP_GET,  .handler = list_handler,     .user_ctx = nullptr},
        {.uri = "/api/download", .method = HTTP_GET,  .handler = download_handler, .user_ctx = nullptr},
        {.uri = "/api/upload",   .method = HTTP_POST, .handler = upload_handler,   .user_ctx = nullptr},
        {.uri = "/api/delete",   .method = HTTP_POST, .handler = delete_handler,   .user_ctx = nullptr},
        {.uri = "/api/mkdir",    .method = HTTP_POST, .handler = mkdir_handler,    .user_ctx = nullptr},
        // 兜底：所有未知 GET（含各系统门户探测路径）→ 302 首页
        {.uri = "/*",            .method = HTTP_GET,     .handler = redirect_handler, .user_ctx = nullptr},
        // 兜底：手机后台 App 被 DNS 劫持发来的非 GET 请求 → 静默 404
        {.uri = "/*",            .method = HTTP_POST,    .handler = discard_handler, .user_ctx = nullptr},
        {.uri = "/*",            .method = HTTP_PUT,     .handler = discard_handler, .user_ctx = nullptr},
        {.uri = "/*",            .method = HTTP_PATCH,   .handler = discard_handler, .user_ctx = nullptr},
        {.uri = "/*",            .method = HTTP_DELETE,  .handler = discard_handler, .user_ctx = nullptr},
        {.uri = "/*",            .method = HTTP_OPTIONS, .handler = discard_handler, .user_ctx = nullptr},
        {.uri = "/*",            .method = HTTP_HEAD,    .handler = discard_handler, .user_ctx = nullptr},
    };
    for (const auto& r : routes)
    {
        httpd_register_uri_handler(s_httpd, &r);
    }

    s_dns_run = true;
    if (xTaskCreate(dns_task, "dns_hijack", 3072, nullptr, 5, nullptr) != pdPASS)
    {
        ESP_LOGW(TAG, "DNS 任务创建失败, 自动弹页不可用");
        s_dns_run = false;
    }

    char ip[16];
    get_ap_ip_str(ip, sizeof(ip));
    ESP_LOGI(TAG, "局域网传输已启动: http://%s/", ip);
    return ESP_OK;
}

void stop()
{
    s_dns_run = false;
    if (s_dns_sock >= 0)
    {
        shutdown(s_dns_sock, SHUT_RDWR);
    }

    if (s_httpd != nullptr)
    {
        httpd_stop(s_httpd);
        s_httpd = nullptr;
        ESP_LOGI(TAG, "局域网传输已停止");
    }
}

bool is_running()
{
    return s_httpd != nullptr;
}

} // namespace app::protocol::file_server
