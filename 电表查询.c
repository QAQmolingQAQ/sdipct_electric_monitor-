#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wininet.h>
#include <sqlite3.h>
#include <signal.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "sqlite3.lib")

#define BUFFER_SIZE 4096
#define CONFIG_SIZE 1024
#define MAX_RETRY_COUNT 3

/* 电表数据结构 */
typedef struct
{
    double remainingEnergy;
    double remainingAmount;
    double totalConsumption;
    double price;
    char meterStatus[100];
    char meterUpdateTime[50];
    char systemTime[50];
} ElectricMeter;

/* 配置结构 */
typedef struct
{
    int monitorInterval;
    double lowEnergyThreshold;
    char curlCommand[1024];
    char dbPath[256];
    char smtpServer[100];
    int smtpPort;
    char emailAccount[100];
    char emailAuthCode[100];
    char emailReceivers[512];
    char webPath[256];
} Config;

/* 全局变量 */
static volatile int keep_running = 1;

/* 函数声明 */
void set_console_utf8(void);
void pause_program(void);
const char *get_current_time(void);
void create_directory(const char *dirname);
int read_config(const char *filename, Config *config);
int validate_config(const Config *config);
int init_database(const char *db_path);
int save_to_database(const char *db_path, const ElectricMeter *meter);
int save_alert_to_database(const char *db_path, const ElectricMeter *meter, double threshold);
void parse_curl_command(const char *curl_cmd, char *url, char *post_data, char *headers);
int http_post_request(const char *url, const char *post_data, const char *headers, char *response, int response_size);
int parse_json_response(const char *json_str, ElectricMeter *meter);
int get_electric_meter_data_with_retry(const Config *config, ElectricMeter *meter);
int send_email(const Config *config, const ElectricMeter *meter, double threshold);
int generate_html_page(const char *web_path, const ElectricMeter *meter, double threshold);
void display_meter_info(const ElectricMeter *meter, double threshold);
void write_log(const char *level, const char *message);
void signal_handler(int signal);
void start_monitoring(const Config *config);

/* 信号处理函数 */
void signal_handler(int signal)
{
    if (signal == SIGINT)
    {
        printf("\n接收到中断信号，正在退出...\n");
        keep_running = 0;
    }
}

/* 日志函数 */
void write_log(const char *level, const char *message)
{
    FILE *log_file = fopen("monitor.log", "a");
    if (log_file)
    {
        fprintf(log_file, "[%s] %s: %s\n", get_current_time(), level, message);
        fclose(log_file);
    }
    printf("[%s] %s\n", level, message);
}

/* 设置控制台编码 */
void set_console_utf8(void)
{
    system("chcp 65001 > nul");
    SetConsoleOutputCP(65001);
}

/* 暂停程序 */
void pause_program(void)
{
    printf("\n按任意键退出程序...\n");
    system("pause > nul");
}

/* 获取当前时间字符串 */
const char *get_current_time(void)
{
    static char time_str[50];
    SYSTEMTIME st;
    GetLocalTime(&st);
    sprintf(time_str, "%04d-%02d-%02d %02d:%02d:%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return time_str;
}

/* 创建目录 */
void create_directory(const char *dirname)
{
    CreateDirectoryA(dirname, NULL);
}

/* 验证配置 */
int validate_config(const Config *config)
{
    if (config->monitorInterval <= 0)
    {
        printf("错误: 监控间隔必须大于0\n");
        return 0;
    }
    if (config->lowEnergyThreshold <= 0)
    {
        printf("错误: 低电量阈值必须大于0\n");
        return 0;
    }
    if (strlen(config->curlCommand) == 0)
    {
        printf("错误: CURL命令不能为空\n");
        return 0;
    }
    if (strlen(config->dbPath) == 0)
    {
        printf("错误: 数据库路径不能为空\n");
        return 0;
    }
    return 1;
}

/* 读取配置文件 */
int read_config(const char *filename, Config *config)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        printf("无法打开配置文件: %s\n", filename);
        return 0;
    }

    char line[CONFIG_SIZE];
    int found_interval = 0;
    int found_threshold = 0;
    int found_curl = 0;

    // 设置默认值
    strcpy(config->dbPath, "electric_data.db");
    strcpy(config->smtpServer, "smtp.qq.com");
    config->smtpPort = 587;
    strcpy(config->emailAccount, "");
    strcpy(config->emailAuthCode, "");
    strcpy(config->emailReceivers, "");
    strcpy(config->webPath, "web");

    while (fgets(line, sizeof(line), file))
    {
        line[strcspn(line, "\r\n")] = 0;

        if (line[0] == '\0' || line[0] == '#')
            continue;

        if (strstr(line, "MONITOR_INTERVAL") != NULL)
        {
            if (sscanf(line, "MONITOR_INTERVAL=%d", &config->monitorInterval) == 1)
            {
                found_interval = 1;
            }
        }
        else if (strstr(line, "LOW_ENERGY_THRESHOLD") != NULL)
        {
            if (sscanf(line, "LOW_ENERGY_THRESHOLD=%lf", &config->lowEnergyThreshold) == 1)
            {
                found_threshold = 1;
            }
        }
        else if (strstr(line, "CURL_COMMAND") != NULL)
        {
            char *equals = strchr(line, '=');
            if (equals)
            {
                strcpy(config->curlCommand, equals + 1);
                found_curl = 1;
            }
        }
        else if (strstr(line, "DATABASE_PATH") != NULL)
        {
            char *equals = strchr(line, '=');
            if (equals)
            {
                strcpy(config->dbPath, equals + 1);
            }
        }
        else if (strstr(line, "SMTP_SERVER") != NULL)
        {
            char *equals = strchr(line, '=');
            if (equals)
            {
                strcpy(config->smtpServer, equals + 1);
            }
        }
        else if (strstr(line, "SMTP_PORT") != NULL)
        {
            char *equals = strchr(line, '=');
            if (equals)
            {
                config->smtpPort = atoi(equals + 1);
            }
        }
        else if (strstr(line, "EMAIL_ACCOUNT") != NULL)
        {
            char *equals = strchr(line, '=');
            if (equals)
            {
                strcpy(config->emailAccount, equals + 1);
            }
        }
        else if (strstr(line, "EMAIL_AUTH_CODE") != NULL)
        {
            char *equals = strchr(line, '=');
            if (equals)
            {
                strcpy(config->emailAuthCode, equals + 1);
            }
        }
        else if (strstr(line, "EMAIL_RECEIVERS") != NULL)
        {
            char *equals = strchr(line, '=');
            if (equals)
            {
                strcpy(config->emailReceivers, equals + 1);
            }
        }
        else if (strstr(line, "WEB_PATH") != NULL)
        {
            char *equals = strchr(line, '=');
            if (equals)
            {
                strcpy(config->webPath, equals + 1);
            }
        }
    }

    fclose(file);

    if (!found_interval || !found_threshold || !found_curl)
    {
        printf("配置文件缺少必要参数\n");
        printf("需要: MONITOR_INTERVAL, LOW_ENERGY_THRESHOLD, CURL_COMMAND\n");
        return 0;
    }

    if (!validate_config(config))
    {
        return 0;
    }

    return 1;
}

/* 初始化数据库 */
int init_database(const char *db_path)
{
    sqlite3 *db;
    char *err_msg = 0;
    int rc;

    rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        printf("数据库打开失败: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "CREATE TABLE IF NOT EXISTS electric_data ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "record_time DATETIME DEFAULT CURRENT_TIMESTAMP,"
                      "remaining_energy REAL NOT NULL,"
                      "remaining_amount REAL NOT NULL,"
                      "total_consumption REAL NOT NULL,"
                      "price REAL NOT NULL,"
                      "meter_status TEXT,"
                      "meter_update_time TEXT,"
                      "system_time TEXT);";

    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK)
    {
        printf("创建表失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 0;
    }

    const char *sql2 = "CREATE TABLE IF NOT EXISTS low_energy_alerts ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "alert_time DATETIME DEFAULT CURRENT_TIMESTAMP,"
                       "remaining_energy REAL NOT NULL,"
                       "threshold REAL NOT NULL,"
                       "alert_message TEXT,"
                       "meter_update_time TEXT);";

    rc = sqlite3_exec(db, sql2, 0, 0, &err_msg);
    if (rc != SQLITE_OK)
    {
        printf("创建警报表失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 0;
    }

    sqlite3_close(db);
    printf("数据库初始化成功: %s\n", db_path);
    return 1;
}

/* 保存电表数据到数据库 */
int save_to_database(const char *db_path, const ElectricMeter *meter)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        write_log("ERROR", "无法打开数据库");
        return 0;
    }

    const char *sql = "INSERT INTO electric_data (remaining_energy, remaining_amount, total_consumption, price, meter_status, meter_update_time, system_time) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?);";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        sqlite3_close(db);
        write_log("ERROR", "准备SQL语句失败");
        return 0;
    }

    sqlite3_bind_double(stmt, 1, meter->remainingEnergy);
    sqlite3_bind_double(stmt, 2, meter->remainingAmount);
    sqlite3_bind_double(stmt, 3, meter->totalConsumption);
    sqlite3_bind_double(stmt, 4, meter->price);
    sqlite3_bind_text(stmt, 5, meter->meterStatus, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, meter->meterUpdateTime, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, meter->systemTime, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        write_log("ERROR", "执行SQL语句失败");
        return 0;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    write_log("INFO", "电表数据保存到数据库成功");
    return 1;
}

/* 保存低电量警报到数据库 */
int save_alert_to_database(const char *db_path, const ElectricMeter *meter, double threshold)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        write_log("ERROR", "无法打开数据库保存警报");
        return 0;
    }

    char alert_msg[256];
    snprintf(alert_msg, sizeof(alert_msg), "低电量警报: 剩余%.2f度电", meter->remainingEnergy);

    const char *sql = "INSERT INTO low_energy_alerts (remaining_energy, threshold, alert_message, meter_update_time) VALUES (?, ?, ?, ?);";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        sqlite3_close(db);
        write_log("ERROR", "准备警报SQL语句失败");
        return 0;
    }

    sqlite3_bind_double(stmt, 1, meter->remainingEnergy);
    sqlite3_bind_double(stmt, 2, threshold);
    sqlite3_bind_text(stmt, 3, alert_msg, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, meter->meterUpdateTime, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        write_log("ERROR", "执行警报SQL语句失败");
        return 0;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    write_log("ALERT", "低电量警报保存到数据库");
    return 1;
}

/* 从curl命令中提取URL和参数 */
void parse_curl_command(const char *curl_cmd, char *url, char *post_data, char *headers)
{
    url[0] = '\0';
    post_data[0] = '\0';
    headers[0] = '\0';

    const char *url_start = strstr(curl_cmd, "\"http");
    if (!url_start)
        return;

    const char *url_end = strchr(url_start + 1, '\"');
    if (!url_end)
        return;

    int url_len = url_end - url_start - 1;
    strncpy(url, url_start + 1, url_len);
    url[url_len] = '\0';

    const char *data_start = strstr(curl_cmd, "--data-raw");
    if (data_start)
    {
        data_start = strchr(data_start, '\"');
        if (data_start)
        {
            const char *data_end = strchr(data_start + 1, '\"');
            if (data_end)
            {
                int data_len = data_end - data_start - 1;
                strncpy(post_data, data_start + 1, data_len);
                post_data[data_len] = '\0';
            }
        }
    }
}

/* 使用WinINet发送HTTP请求 */
int http_post_request(const char *url, const char *post_data, const char *headers, char *response, int response_size)
{
    HINTERNET hInternet = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;

    int result = 0;
    DWORD bytesRead;
    DWORD totalBytesRead = 0;
    char buffer[1024];

    hInternet = InternetOpenA("ElectricMonitor", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet)
    {
        write_log("ERROR", "InternetOpenA 失败");
        return 0;
    }

    URL_COMPONENTSA urlComp;
    memset(&urlComp, 0, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);

    char host[256] = {0};
    char path[1024] = {0};
    urlComp.lpszHostName = host;
    urlComp.dwHostNameLength = sizeof(host);
    urlComp.lpszUrlPath = path;
    urlComp.dwUrlPathLength = sizeof(path);

    if (!InternetCrackUrlA(url, (DWORD)strlen(url), 0, &urlComp))
    {
        write_log("ERROR", "InternetCrackUrlA 失败");
        InternetCloseHandle(hInternet);
        return 0;
    }

    hConnect = InternetConnectA(hInternet, host, urlComp.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect)
    {
        write_log("ERROR", "InternetConnectA 失败");
        InternetCloseHandle(hInternet);
        return 0;
    }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (urlComp.nPort == 443)
    {
        flags |= INTERNET_FLAG_SECURE;
    }

    hRequest = HttpOpenRequestA(hConnect, "POST", path, NULL, NULL, NULL, flags, 0);
    if (!hRequest)
    {
        write_log("ERROR", "HttpOpenRequestA 失败");
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return 0;
    }

    char full_headers[2048] = "Content-Type: application/x-www-form-urlencoded\r\n";
    if (headers && strlen(headers) > 0)
    {
        strcat(full_headers, headers);
    }

    HttpAddRequestHeadersA(hRequest, full_headers, (DWORD)strlen(full_headers), HTTP_ADDREQ_FLAG_ADD);

    if (!HttpSendRequestA(hRequest, NULL, 0, (LPVOID)post_data, (DWORD)strlen(post_data)))
    {
        DWORD error = GetLastError();
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "HttpSendRequestA 失败，错误代码: %lu", error);
        write_log("ERROR", error_msg);
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return 0;
    }

    while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0)
    {
        if (totalBytesRead + bytesRead < (DWORD)response_size)
        {
            memcpy(response + totalBytesRead, buffer, bytesRead);
            totalBytesRead += bytesRead;
        }
        else
        {
            break;
        }
    }

    response[totalBytesRead] = '\0';
    result = 1;

    if (hRequest)
        InternetCloseHandle(hRequest);
    if (hConnect)
        InternetCloseHandle(hConnect);
    if (hInternet)
        InternetCloseHandle(hInternet);

    return result;
}

/* 解析JSON响应 */
int parse_json_response(const char *json_str, ElectricMeter *meter)
{
    memset(meter, 0, sizeof(ElectricMeter));

    if (strlen(json_str) == 0)
    {
        write_log("ERROR", "JSON响应为空");
        return 0;
    }

    // 查找data字段
    const char *data_start = strstr(json_str, "\"data\"");
    if (!data_start)
    {
        write_log("ERROR", "未找到data字段");
        return 0;
    }

    const char *shengyu_str = strstr(data_start, "\"shengyu\"");
    const char *leiji_str = strstr(data_start, "\"leiji\"");
    const char *price_str = strstr(data_start, "\"price\"");
    const char *zhuangtai_str = strstr(data_start, "\"zhuangtai\"");

    if (shengyu_str)
    {
        char shengyu_value[50];
        if (sscanf(shengyu_str, "\"shengyu\":\"%[^\"]\"", shengyu_value) == 1)
        {
            meter->remainingEnergy = atof(shengyu_value);
        }
    }
    else
    {
        write_log("ERROR", "未找到剩余电量字段");
        return 0;
    }

    if (leiji_str)
    {
        char leiji_value[50];
        if (sscanf(leiji_str, "\"leiji\":\"%[^\"]\"", leiji_value) == 1)
        {
            meter->totalConsumption = atof(leiji_value);
        }
    }

    if (price_str)
    {
        char price_value[50];
        if (sscanf(price_str, "\"price\":\"%[^\"]\"", price_value) == 1)
        {
            meter->price = atof(price_value);
            meter->remainingAmount = meter->remainingEnergy * meter->price;
        }
    }

    if (zhuangtai_str)
    {
        const char *status_start = strchr(zhuangtai_str, ':');
        if (status_start)
        {
            status_start++;
            const char *quote1 = strchr(status_start, '\"');
            if (quote1)
            {
                const char *quote2 = strchr(quote1 + 1, '\"');
                if (quote2)
                {
                    size_t len = quote2 - quote1 - 1;
                    if (len < sizeof(meter->meterStatus) - 1)
                    {
                        strncpy(meter->meterStatus, quote1 + 1, len);
                        meter->meterStatus[len] = '\0';
                    }
                }
            }
        }
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    sprintf(meter->systemTime, "%04d-%02d-%02d %02d:%02d:%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    strcpy(meter->meterUpdateTime, meter->systemTime);

    return 1;
}

/* 获取电表数据（带重试机制） */
int get_electric_meter_data_with_retry(const Config *config, ElectricMeter *meter)
{
    char url[512] = {0};
    char post_data[512] = {0};
    char headers[1024] = {0};
    char response[BUFFER_SIZE] = {0};

    parse_curl_command(config->curlCommand, url, post_data, headers);

    if (strlen(url) == 0)
    {
        write_log("ERROR", "无法从CURL命令中解析URL");
        return 0;
    }

    for (int attempt = 1; attempt <= MAX_RETRY_COUNT; attempt++)
    {
        char attempt_msg[128];
        snprintf(attempt_msg, sizeof(attempt_msg), "第%d次尝试获取数据 (共%d次)...", attempt, MAX_RETRY_COUNT);
        write_log("INFO", attempt_msg);
        printf("%s\n", attempt_msg);

        if (http_post_request(url, post_data, headers, response, BUFFER_SIZE))
        {
            if (parse_json_response(response, meter))
            {
                char success_msg[128];
                snprintf(success_msg, sizeof(success_msg), "数据获取成功 (第%d次尝试)", attempt);
                write_log("INFO", success_msg);
                printf("✅ %s\n", success_msg);
                return 1;
            }
            else
            {
                char parse_error_msg[128];
                snprintf(parse_error_msg, sizeof(parse_error_msg), "JSON解析失败 (第%d次尝试)", attempt);
                write_log("ERROR", parse_error_msg);
                printf("❌ %s\n", parse_error_msg);
            }
        }
        else
        {
            char http_error_msg[128];
            snprintf(http_error_msg, sizeof(http_error_msg), "HTTP请求失败 (第%d次尝试)", attempt);
            write_log("ERROR", http_error_msg);
            printf("❌ %s\n", http_error_msg);
        }

        // 如果不是最后一次尝试，等待后重试
        if (attempt < MAX_RETRY_COUNT)
        {
            printf("⏳ 等待3秒后重试...\n");
            write_log("INFO", "等待3秒后重试");
            Sleep(3000); // 等待3秒
        }
    }

    write_log("ERROR", "所有重试次数已用完，数据获取失败");
    printf("❌ 所有%d次重试均已失败，跳过本次数据获取\n", MAX_RETRY_COUNT);
    return 0;
}

/* 发送邮件主函数 */
/* 发送邮件主函数（带时间延迟） */
int send_email(const Config *config, const ElectricMeter *meter, double threshold)
{
    printf("检查邮件配置...\n");
    printf("发件人: %s\n", config->emailAccount);
    printf("收件人: %s\n", config->emailReceivers);
    printf("SMTP服务器: %s:%d\n", config->smtpServer, config->smtpPort);

    // 检查邮箱配置是否完整
    if (strlen(config->emailAccount) == 0 || strlen(config->emailAuthCode) == 0 ||
        strlen(config->emailReceivers) == 0)
    {
        printf("邮箱配置不完整，跳过邮件发送\n");
        printf("需要配置: EMAIL_ACCOUNT, EMAIL_AUTH_CODE, EMAIL_RECEIVERS\n");
        return 0;
    }

    printf("准备发送邮件...\n");

    // 创建PowerShell脚本内容 - 添加时间延迟和重试机制
    char ps_script[8192];
    snprintf(ps_script, sizeof(ps_script),
             "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8\n"
             "[Console]::InputEncoding = [System.Text.Encoding]::UTF8\n"
             "\n"
             "function Send-EmailWithRetry {\n"
             "    param($From, $To, $Subject, $Body, $SmtpServer, $Port, $Credential, $RetryCount = 3)\n"
             "    \n"
             "    for ($i = 1; $i -le $RetryCount; $i++) {\n"
             "        try {\n"
             "            Write-Output (\"尝试第 $i 次发送给: \" + $To)\n"
             "            \n"
             "            # 使用SmtpClient对象，更稳定\n"
             "            $smtpClient = New-Object System.Net.Mail.SmtpClient($SmtpServer, $Port)\n"
             "            $smtpClient.EnableSsl = $true\n"
             "            $smtpClient.Credentials = $Credential\n"
             "            $smtpClient.Timeout = 30000  # 30秒超时\n"
             "            \n"
             "            $mailMessage = New-Object System.Net.Mail.MailMessage\n"
             "            $mailMessage.From = $From\n"
             "            $mailMessage.To.Add($To)\n"
             "            $mailMessage.Subject = $Subject\n"
             "            $mailMessage.Body = $Body\n"
             "            $mailMessage.IsBodyHtml = $true\n"
             "            $mailMessage.BodyEncoding = [System.Text.Encoding]::UTF8\n"
             "            $mailMessage.SubjectEncoding = [System.Text.Encoding]::UTF8\n"
             "            \n"
             "            $smtpClient.Send($mailMessage)\n"
             "            Write-Output (\"✅ 成功发送给: \" + $To)\n"
             "            \n"
             "            # 清理资源\n"
             "            $mailMessage.Dispose()\n"
             "            $smtpClient.Dispose()\n"
             "            \n"
             "            return $true\n"
             "            \n"
             "        } catch {\n"
             "            Write-Output (\"❌ 第 $i 次发送失败给 \" + $To + \": \" + $_.Exception.Message)\n"
             "            \n"
             "            # 清理资源（即使失败也要清理）\n"
             "            if ($mailMessage) { $mailMessage.Dispose() }\n"
             "            if ($smtpClient) { $smtpClient.Dispose() }\n"
             "            \n"
             "            # 如果不是最后一次尝试，等待后重试\n"
             "            if ($i -lt $RetryCount) {\n"
             "                $delaySeconds = 5 * $i  # 递增延迟：5秒, 10秒, 15秒\n"
             "                Write-Output (\"等待 $delaySeconds 秒后重试...\")\n"
             "                Start-Sleep -Seconds $delaySeconds\n"
             "            }\n"
             "        }\n"
             "    }\n"
             "    return $false\n"
             "}\n"
             "\n"
             "Try {\n"
             "    # 邮件参数\n"
             "    $EmailFrom = '%s'\n"
             "    $EmailTo = '%s'\n"
             "    $Subject = '电表低电量提醒 - 剩余%.2f度电（山东石油化工学院）'\n"
             "    $SMTPServer = '%s'\n"
             "    $SMTPPort = %d\n"
             "    $Username = '%s'\n"
             "    $Password = '%s'\n"
             "    \n"
             "    # 创建邮件内容\n"
             "    $Body = @\"\n"
             "<!DOCTYPE html>\n"
             "<html>\n"
             "<head>\n"
             "    <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
             "    <title>Electric Meter Low Energy Alert</title>\n"
             "    <style>\n"
             "        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }\n"
             "        .container { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); max-width: 600px; margin: 0 auto; }\n"
             "        .header { color: #d63031; font-size: 28px; font-weight: bold; margin-bottom: 25px; text-align: center; border-bottom: 3px solid #d63031; padding-bottom: 15px; }\n"
             "        .info-table { border-collapse: collapse; width: 100%%; margin: 25px 0; font-size: 16px; }\n"
             "        .info-table th, .info-table td { border: 2px solid #ddd; padding: 15px; text-align: left; }\n"
             "        .info-table th { background-color: #f8f9fa; font-weight: bold; width: 30%%; color: #2d3436; }\n"
             "        .info-table td { background-color: #fff; }\n"
             "        .warning { color: #d63031; font-weight: bold; font-size: 20px; margin: 25px 0; text-align: center; background: #ffebee; padding: 15px; border-radius: 8px; border-left: 5px solid #d63031; }\n"
             "        .critical { background-color: #fff5f5 !important; font-weight: bold; color: #d63031; }\n"
             "        .footer { margin-top: 30px; padding-top: 20px; border-top: 1px solid #ddd; color: #636e72; font-size: 14px; text-align: center; }\n"
             "        .energy-value { font-size: 24px; font-weight: bold; color: #d63031; }\n"
             "    </style>\n"
             "</head>\n"
             "<body>\n"
             "    <div class=\"container\">\n"
             "        <div class=\"header\">⚠️ 电表低电量提醒 Low Energy Alert</div>\n"
             "        <p>系统检测到电表电量低于设定阈值，请及时充值！System detected low energy, please recharge!</p>\n"
             "        <table class=\"info-table\">\n"
             "            <tr><th>剩余电量 Remaining Energy</th><td class=\"critical\"><span class=\"energy-value\">%.2f 度 kWh</span></td></tr>\n"
             "            <tr><th>剩余金额 Remaining Amount</th><td>%.2f 元 CNY</td></tr>\n"
             "            <tr><th>累计用电 Total Consumption</th><td>%.2f kWh</td></tr>\n"
             "            <tr><th>当前电价 Current Price</th><td>%.4f 元/度 CNY/kWh</td></tr>\n"
             "            <tr><th>电表状态 Meter Status</th><td>%s</td></tr>\n"
             "            <tr><th>数据更新时间 Data Update Time</th><td>%s</td></tr>\n"
             "            <tr><th>系统记录时间 System Time</th><td>%s</td></tr>\n"
             "            <tr><th>低电量阈值 Low Energy Threshold</th><td>%.1f 度 kWh</td></tr>\n"
             "        </table>\n"
             "        <div class=\"warning\">⚠️ 紧急：电量已低于设定阈值，请及时充值以避免断电！Urgent: Energy below threshold, please recharge to avoid power outage!</div>\n"
             "        <div class=\"footer\">此邮件由山东石油化工学院电表监控系统自动生成<br>Auto-generated by Shandong Institute of Petroleum and Chemical Technology Electric Monitor System</div>\n"
             "    </div>\n"
             "</body>\n"
             "</html>\n"
             "\"@\n"
             "    \n"
             "    # 创建凭据\n"
             "    $secpasswd = ConvertTo-SecureString $Password -AsPlainText -Force\n"
             "    $cred = New-Object System.Management.Automation.PSCredential ($Username, $secpasswd)\n"
             "    \n"
             "    # 分割多个收件人邮箱\n"
             "    $Recipients = $EmailTo -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' }\n"
             "    \n"
             "    Write-Output (\"开始发送邮件给 \" + $Recipients.Count + \" 个收件人...\")\n"
             "    \n"
             "    # 发送邮件给每个收件人\n"
             "    $successCount = 0\n"
             "    $recipientIndex = 0\n"
             "    \n"
             "    foreach ($recipient in $Recipients) {\n"
             "        $recipientIndex++\n"
             "        Write-Output (\"\n处理收件人 $recipientIndex/$($Recipients.Count): \" + $recipient)\n"
             "        \n"
             "        # 在收件人之间添加延迟（第一个收件人不需要延迟）\n"
             "        if ($recipientIndex -gt 1) {\n"
             "            Write-Output \"等待3秒后发送下一个收件人...\"\n"
             "            Start-Sleep -Seconds 3\n"
             "        }\n"
             "        \n"
             "        # 发送邮件（带重试机制）\n"
             "        if (Send-EmailWithRetry -From $EmailFrom -To $recipient -Subject $Subject -Body $Body -SmtpServer $SMTPServer -Port $SMTPPort -Credential $cred -RetryCount 3) {\n"
             "            $successCount++\n"
             "        }\n"
             "    }\n"
             "    \n"
             "    Write-Output (\"\\n=== 发送完成 ===\")\n"
             "    Write-Output (\"成功发送: $successCount/$($Recipients.Count)\")\n"
             "    \n"
             "    if ($successCount -eq $Recipients.Count) {\n"
             "        Write-Output 'SUCCESS_ALL'\n"
             "    } elseif ($successCount -gt 0) {\n"
             "        Write-Output 'SUCCESS_PARTIAL'\n"
             "    } else {\n"
             "        Write-Output 'FAILED_ALL'\n"
             "        exit 1\n"
             "    }\n"
             "    exit 0\n"
             "} Catch {\n"
             "    Write-Output (\"❌ 全局错误: \" + $_.Exception.Message)\n"
             "    Write-Output (\"   错误类型: \" + $_.Exception.GetType().FullName)\n"
             "    exit 1\n"
             "}",
             config->emailAccount,
             config->emailReceivers,
             meter->remainingEnergy,
             config->smtpServer,
             config->smtpPort,
             config->emailAccount,
             config->emailAuthCode,
             meter->remainingEnergy,
             meter->remainingAmount,
             meter->totalConsumption,
             meter->price,
             meter->meterStatus,
             meter->meterUpdateTime,
             meter->systemTime,
             threshold);

    // 将PowerShell脚本保存到临时文件
    FILE *ps_file = fopen("send_email.ps1", "wb");
    if (!ps_file)
    {
        write_log("ERROR", "无法创建PowerShell脚本文件");
        printf("❌ 无法创建PowerShell脚本文件\n");
        return 0;
    }

    // 写入UTF-8 BOM
    unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    fwrite(bom, 1, 3, ps_file);
    fprintf(ps_file, "%s", ps_script);
    fclose(ps_file);

    // 执行PowerShell脚本
    printf("正在发送邮件警告...\n");
    write_log("INFO", "执行PowerShell脚本发送邮件");

    int result = system("powershell -ExecutionPolicy Bypass -File send_email.ps1");

    // 删除临时文件
    remove("send_email.ps1");

    if (result == 0)
    {
        write_log("SUCCESS", "邮件警告发送成功");
        printf("✅ 邮件警告发送成功\n");
        return 1;
    }
    else
    {
        write_log("ERROR", "邮件发送失败");
        printf("❌ 邮件发送失败，返回码: %d\n", result);

        // 提供详细的故障排除建议
        printf("💡 故障排除建议:\\n");
        printf("   1. 检查SMTP服务器地址和端口是否正确\\n");
        printf("   2. 确认邮箱密码是授权码而不是登录密码\\n");
        printf("   3. 检查网络连接是否正常\\n");
        printf("   4. QQ邮箱需要开启SMTP服务并获取授权码\\n");
        printf("   5. 尝试使用端口465（SSL）或587（TLS）\\n");

        return 0;
    }
}

/* 生成HTML页面 */
int generate_html_page(const char *web_path, const ElectricMeter *meter, double threshold)
{
    create_directory(web_path);

    char filepath[512];
    sprintf(filepath, "%s/index.html", web_path);

    FILE *file = fopen(filepath, "w");
    if (!file)
    {
        write_log("ERROR", "无法创建HTML文件");
        return 0;
    }

    const char *status_class = (meter->remainingEnergy <= threshold) ? "low-energy" : "normal";
    const char *status_text = (meter->remainingEnergy <= threshold) ? "低电量" : "正常";
    const char *status_emoji = (meter->remainingEnergy <= threshold) ? "⚠️" : "✅";

    double estimated_days = (meter->remainingEnergy > 0 && meter->totalConsumption > 0) ? (meter->remainingEnergy / (meter->totalConsumption / 30.0)) : 0;

    fprintf(file,
            "<!DOCTYPE html>\n"
            "<html lang=\"zh-CN\">\n"
            "<head>\n"
            "    <meta charset=\"UTF-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <title>电表监控系统 - 山东石油化工学院</title>\n"
            "    <style>\n"
            "        * { margin: 0; padding: 0; box-sizing: border-box; }\n"
            "        body { font-family: 'Microsoft YaHei', Arial, sans-serif; background: #f5f5f5; min-height: 100vh; padding: 20px; }\n"
            "        .container { max-width: 1000px; margin: 0 auto; background: white; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); overflow: hidden; }\n"
            "        .header { background: #2c3e50; color: white; padding: 20px; text-align: center; }\n"
            "        .header h1 { font-size: 2em; margin-bottom: 10px; }\n"
            "        .content { padding: 20px; }\n"
            "        .status-card { background: #f8f9fa; border-radius: 8px; padding: 20px; margin-bottom: 20px; border-left: 5px solid #3498db; }\n"
            "        .status-card.low-energy { border-left-color: #e74c3c; background: #fff5f5; }\n"
            "        .status-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }\n"
            "        .status-title { font-size: 1.5em; color: #2c3e50; font-weight: bold; }\n"
            "        .status-badge { padding: 5px 10px; border-radius: 15px; font-weight: bold; }\n"
            "        .badge-normal { background: #27ae60; color: white; }\n"
            "        .badge-low { background: #e74c3c; color: white; }\n"
            "        .stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin-bottom: 20px; }\n"
            "        .stat-card { background: white; padding: 15px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); text-align: center; border-top: 4px solid #3498db; }\n"
            "        .stat-card.energy { border-top-color: #e74c3c; }\n"
            "        .stat-card.amount { border-top-color: #27ae60; }\n"
            "        .stat-card.consumption { border-top-color: #f39c12; }\n"
            "        .stat-card.price { border-top-color: #9b59b6; }\n"
            "        .stat-value { font-size: 1.8em; font-weight: bold; margin: 8px 0; }\n"
            "        .energy-value { color: #e74c3c; }\n"
            "        .amount-value { color: #27ae60; }\n"
            "        .consumption-value { color: #f39c12; }\n"
            "        .price-value { color: #9b59b6; }\n"
            "        .stat-label { color: #7f8c8d; font-size: 0.9em; }\n"
            "        .info-table { width: 100%%; border-collapse: collapse; background: white; border-radius: 8px; overflow: hidden; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }\n"
            "        .info-table th, .info-table td { padding: 12px; text-align: left; border-bottom: 1px solid #ecf0f1; }\n"
            "        .info-table th { background: #34495e; color: white; font-weight: 600; }\n"
            "        .footer { background: #2c3e50; color: white; text-align: center; padding: 15px; margin-top: 20px; }\n"
            "        .update-time { text-align: center; color: #7f8c8d; margin: 10px 0; }\n"
            "        .alert-banner { background: #e74c3c; color: white; padding: 12px; text-align: center; border-radius: 6px; margin: 15px 0; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <div class=\"header\">\n"
            "            <h1>⚡ 电表监控系统</h1>\n"
            "            <div>山东石油化工学院 - 实时电力监控</div>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"content\">\n"
            "            <div class=\"status-card %s\">\n"
            "                <div class=\"status-header\">\n"
            "                    <div class=\"status-title\">当前电表状态</div>\n"
            "                    <div class=\"status-badge %s\">%s %s</div>\n"
            "                </div>\n",
            status_class,
            (meter->remainingEnergy <= threshold) ? "badge-low" : "badge-normal",
            status_emoji, status_text);

    if (meter->remainingEnergy <= threshold)
    {
        fprintf(file,
                "                <div class=\"alert-banner\">\n"
                "                    <strong>⚠️ 低电量警告！</strong> 剩余 %.2f 度电，请及时充值！\n"
                "                </div>\n",
                meter->remainingEnergy);
    }

    fprintf(file,
            "            </div>\n"
            "            \n"
            "            <div class=\"stats-grid\">\n"
            "                <div class=\"stat-card energy\">\n"
            "                    <div class=\"stat-label\">剩余电量</div>\n"
            "                    <div class=\"stat-value energy-value\">%.2f 度</div>\n"
            "                    <div>Remaining Energy</div>\n"
            "                </div>\n"
            "                <div class=\"stat-card amount\">\n"
            "                    <div class=\"stat-label\">剩余金额</div>\n"
            "                    <div class=\"stat-value amount-value\">%.2f 元</div>\n"
            "                    <div>Remaining Amount</div>\n"
            "                </div>\n"
            "                <div class=\"stat-card consumption\">\n"
            "                    <div class=\"stat-label\">累计用电</div>\n"
            "                    <div class=\"stat-value consumption-value\">%.2f kWh</div>\n"
            "                    <div>Total Consumption</div>\n"
            "                </div>\n"
            "                <div class=\"stat-card price\">\n"
            "                    <div class=\"stat-label\">当前电价</div>\n"
            "                    <div class=\"stat-value price-value\">%.4f 元/度</div>\n"
            "                    <div>Current Price</div>\n"
            "                </div>\n"
            "            </div>\n"
            "            \n"
            "            <table class=\"info-table\">\n"
            "                <tr><th>项目</th><th>数值</th><th>说明</th></tr>\n"
            "                <tr><td>电表状态</td><td>%s</td><td>当前电表工作状态</td></tr>\n"
            "                <tr><td>数据更新时间</td><td>%s</td><td>电表数据最后更新时间</td></tr>\n"
            "                <tr><td>系统记录时间</td><td>%s</td><td>系统获取数据时间</td></tr>\n"
            "                <tr><td>低电量阈值</td><td>%.1f 度</td><td>触发警报的阈值</td></tr>\n"
            "                <tr><td>预估可用天数</td><td>%.1f 天</td><td>基于历史用电量估算</td></tr>\n"
            "            </table>\n"
            "            \n"
            "            <div class=\"update-time\">\n"
            "                页面最后更新: %s\n"
            "            </div>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"footer\">\n"
            "            <p>山东石油化工学院电表监控系统 | 自动更新</p>\n"
            "            <p>© 2024 电表监控系统</p>\n"
            "        </div>\n"
            "    </div>\n"
            "    \n"
            "    <script>\n"
            "        setTimeout(function() {\n"
            "            location.reload();\n"
            "        }, 300000);\n"
            "    </script>\n"
            "</body>\n"
            "</html>",
            meter->remainingEnergy,
            meter->remainingAmount,
            meter->totalConsumption,
            meter->price,
            meter->meterStatus,
            meter->meterUpdateTime,
            meter->systemTime,
            threshold,
            estimated_days,
            meter->systemTime);

    fclose(file);

    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg), "HTML页面已生成: %s", filepath);
    write_log("INFO", success_msg);
    return 1;
}

/* 显示电表信息 */
void display_meter_info(const ElectricMeter *meter, double threshold)
{
    printf("\n=== 电表信息 ===\n");
    printf("更新时间: %s\n", meter->meterUpdateTime);
    printf("剩余电量: %.2f 度", meter->remainingEnergy);
    if (meter->remainingEnergy <= threshold)
    {
        printf(" (低电量!)\n");
    }
    else
    {
        printf("\n");
    }
    printf("剩余金额: %.2f 元\n", meter->remainingAmount);
    printf("累计用电: %.2f kWh\n", meter->totalConsumption);
    printf("当前电价: %.4f 元/度\n", meter->price);
    if (strlen(meter->meterStatus) > 0)
    {
        printf("电表状态: %s\n", meter->meterStatus);
    }
    printf("================\n");
}

/* 主监控循环 */
void start_monitoring(const Config *config)
{
    write_log("INFO", "开始电表监控");

    printf("开始电表监控\n");
    printf("监控间隔: %d 分钟\n", config->monitorInterval);
    printf("低电量阈值: %.1f 度\n", config->lowEnergyThreshold);
    printf("数据库: %s\n", config->dbPath);
    printf("网页路径: %s\n", config->webPath);
    printf("最大重试次数: %d 次\n", MAX_RETRY_COUNT);
    printf("按 Ctrl+C 停止监控\n\n");

    int count = 0;
    int alert_count = 0;
    const int max_alerts = 3;
    int was_low = 0;

    write_log("INFO", "监控系统已启动，开始循环...");

    while (keep_running)
    {
        count++;
        printf("\n=== 第 %d 次查询 ===\n", count);
        printf("当前时间: %s\n", get_current_time());

        ElectricMeter meter;
        memset(&meter, 0, sizeof(meter));

        printf("正在获取电表数据...\n");
        if (get_electric_meter_data_with_retry(config, &meter))
        {
            printf("✅ 数据获取成功\n");

            save_to_database(config->dbPath, &meter);
            generate_html_page(config->webPath, &meter, config->lowEnergyThreshold);
            display_meter_info(&meter, config->lowEnergyThreshold);

            if (meter.remainingEnergy <= config->lowEnergyThreshold)
            {
                if (alert_count < max_alerts)
                {
                    char alert_msg[128];
                    snprintf(alert_msg, sizeof(alert_msg), "低电量警报! (第%d次警报)", alert_count + 1);
                    write_log("ALERT", alert_msg);

                    printf("🚨 %s\n", alert_msg);
                    send_email(config, &meter, config->lowEnergyThreshold);
                    save_alert_to_database(config->dbPath, &meter, config->lowEnergyThreshold);
                    alert_count++;
                }
                was_low = 1;
            }
            else
            {
                if (was_low)
                {
                    write_log("INFO", "电量已恢复正常");
                    printf("✅ 电量已恢复正常\n");
                    was_low = 0;
                    alert_count = 0;
                }
            }
        }
        else
        {
            printf("❌ 数据获取失败，跳过本次处理\n");
        }

        if (keep_running)
        {
            printf("⏰ 等待 %d 分钟...\n", config->monitorInterval);

            // 分段等待，便于响应Ctrl+C
            int total_wait = config->monitorInterval * 60; // 转换为秒
            for (int i = 0; i < total_wait && keep_running; i++)
            {
                Sleep(1000); // 每秒检查一次
            }
        }
    }

    write_log("INFO", "监控系统已停止");
}

/* 主函数 */
int main(void)
{
    set_console_utf8();

    // 注册信号处理
    signal(SIGINT, signal_handler);

    printf("========================================\n");
    printf("       山东石油化工学院电表监控系统\n");
    printf("========================================\n\n");

    // 检查配置文件
    FILE *config_test = fopen("config.txt", "r");
    if (!config_test)
    {
        write_log("ERROR", "找不到 config.txt 配置文件");
        printf("❌ 错误: 找不到 config.txt 配置文件\n");
        pause_program();
        return 1;
    }
    fclose(config_test);

    create_directory("temp_mail");
    create_directory("web");

    Config config;
    if (!read_config("config.txt", &config))
    {
        write_log("ERROR", "配置文件读取失败");
        printf("❌ 配置文件读取失败\n");
        pause_program();
        return 1;
    }

    write_log("INFO", "配置加载成功");
    printf("✅ 配置加载成功\n");
    printf("监控间隔: %d 分钟\n", config.monitorInterval);
    printf("低电量阈值: %.1f 度\n", config.lowEnergyThreshold);
    printf("数据库: %s\n", config.dbPath);
    printf("网页路径: %s\n", config.webPath);

    if (!init_database(config.dbPath))
    {
        write_log("ERROR", "数据库初始化失败");
        printf("❌ 数据库初始化失败\n");
        pause_program();
        return 1;
    }

    write_log("INFO", "系统启动完成，开始监控");
    printf("✅ 系统启动完成，开始监控...\n\n");

    start_monitoring(&config);

    write_log("INFO", "程序正常退出");
    printf("\n程序已退出\n");
    return 0;
}