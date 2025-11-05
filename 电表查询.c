#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wininet.h>
#include <sqlite3.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "sqlite3.lib")

#define BUFFER_SIZE 4096
#define CONFIG_SIZE 1024

/* 电表数据结构 */
struct ElectricMeter
{
    double remainingEnergy;
    double remainingAmount;
    double totalConsumption;
    double price;
    char meterStatus[100];
    char meterUpdateTime[50];
    char systemTime[50];
};

/* 配置结构 */
struct Config
{
    int monitorInterval;
    double lowEnergyThreshold;
    char curlCommand[1024];
    char dbPath[256];
    char smtpServer[100];
    int smtpPort;
    char emailAccount[100];
    char emailAuthCode[100];
    char emailReceiver[100];
};

/* 创建目录 */
void create_directory(const char *dirname)
{
    char command[100];
    sprintf(command, "mkdir %s 2>nul", dirname);
    system(command);
}

/* 初始化SQLite数据库 */
int init_database(const char *db_path)
{
    sqlite3 *db;
    char *err_msg = 0;
    int rc;

    rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        printf("数据库打开失败\n");
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
        printf("创建表失败\n");
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
        printf("创建警报表失败\n");
        sqlite3_close(db);
        return 0;
    }

    sqlite3_close(db);
    printf("数据库初始化成功\n");
    return 1;
}

/* 保存电表数据到数据库 */
int save_to_database(const char *db_path, const struct ElectricMeter *meter)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        return 0;
    }

    const char *sql = "INSERT INTO electric_data (remaining_energy, remaining_amount, total_consumption, price, meter_status, meter_update_time, system_time) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?);";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        sqlite3_close(db);
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
        return 0;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 1;
}

/* 保存低电量警报到数据库 */
int save_alert_to_database(const char *db_path, const struct ElectricMeter *meter, double threshold)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        return 0;
    }

    char alert_msg[256];
    snprintf(alert_msg, sizeof(alert_msg), "低电量警报: 剩余%.2f度电", meter->remainingEnergy);

    const char *sql = "INSERT INTO low_energy_alerts (remaining_energy, threshold, alert_message, meter_update_time) VALUES (?, ?, ?, ?);";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        sqlite3_close(db);
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
        return 0;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 1;
}

/* 读取配置文件 */
int read_config(const char *filename, struct Config *config)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        printf("无法打开配置文件\n");
        return 0;
    }

    char line[CONFIG_SIZE];
    int found_interval = 0;
    int found_threshold = 0;
    int found_curl = 0;

    strcpy(config->dbPath, "electric_data.db");
    strcpy(config->smtpServer, "smtp.qq.com");
    config->smtpPort = 587;
    strcpy(config->emailAccount, "");
    strcpy(config->emailAuthCode, "");
    strcpy(config->emailReceiver, "");

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
        else if (strstr(line, "EMAIL_RECEIVER") != NULL)
        {
            char *equals = strchr(line, '=');
            if (equals)
            {
                strcpy(config->emailReceiver, equals + 1);
            }
        }
    }

    fclose(file);

    if (!found_interval || !found_threshold || !found_curl)
    {
        printf("配置文件缺少必要参数\n");
        return 0;
    }

    return 1;
}

/* 从curl命令中提取URL和参数 */
void parse_curl_command(const char *curl_cmd, char *url, char *post_data, char *headers)
{
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

    const char *cookie_start = strstr(curl_cmd, "Cookie:");
    if (cookie_start)
    {
        cookie_start += 7;
        const char *cookie_end = strstr(cookie_start, "\\r\\n");
        if (cookie_end)
        {
            int cookie_len = cookie_end - cookie_start;
            snprintf(headers, 1024, "Cookie: %.*s", cookie_len, cookie_start);
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
        return 0;

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
        InternetCloseHandle(hInternet);
        return 0;
    }

    hConnect = InternetConnectA(hInternet, host, urlComp.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect)
    {
        InternetCloseHandle(hInternet);
        return 0;
    }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;

    hRequest = HttpOpenRequestA(hConnect, "POST", path, NULL, NULL, NULL, flags, 0);
    if (!hRequest)
    {
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
int parse_json_response(const char *json_str, struct ElectricMeter *meter)
{
    memset(meter, 0, sizeof(struct ElectricMeter));

    if (strlen(json_str) == 0)
        return 0;

    const char *shengyu_str = strstr(json_str, "\"shengyu\"");
    const char *leiji_str = strstr(json_str, "\"leiji\"");
    const char *price_str = strstr(json_str, "\"price\"");
    const char *zhuangtai_str = strstr(json_str, "\"zhuangtai\"");

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

/* 使用PowerShell发送邮件 */
/*int send_email_powershell(const struct Config *config, const struct ElectricMeter *meter, double threshold) {
    char filename[100];
    SYSTEMTIME st;
    GetLocalTime(&st);
    sprintf(filename, "temp_mail/email_%04d%02d%02d_%02d%02d%02d.ps1",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    FILE *file = fopen(filename, "w");
    if (!file) return 0;

    fprintf(file,
        "$From = '%s'\n"
        "$To = '%s'\n"
        "$Subject = '电表低电量提醒 - 剩余%.2f度电'\n"
        "$Body = '剩余电量: %.2f 度\\n剩余金额: %.2f 元\\n累计用电: %.2f kWh\\n当前电价: %.4f 元/度\\n电表状态: %s\\n数据时间: %s\\n阈值: %.1f 度\\n\\n请及时充值!'\n"
        "$SMTPServer = '%s'\n"
        "$Port = %d\n"
        "$Username = '%s'\n"
        "$Password = '%s'\n"
        "\n"
        "$SecurePassword = ConvertTo-SecureString $Password -AsPlainText -Force\n"
        "$Credential = New-Object System.Management.Automation.PSCredential ($Username, $SecurePassword)\n"
        "\n"
        "Send-MailMessage -From $From -To $To -Subject $Subject -Body $Body -SmtpServer $SMTPServer -Port $Port -Credential $Credential -UseSsl\n",
        config->emailAccount,
        config->emailReceiver,
        meter->remainingEnergy,
        meter->remainingEnergy,
        meter->remainingAmount,
        meter->totalConsumption,
        meter->price,
        meter->meterStatus,
        meter->meterUpdateTime,
        threshold,
        config->smtpServer,
        config->smtpPort,
        config->emailAccount,
        config->emailAuthCode);

    fclose(file);

    char command[256];
    sprintf(command, "powershell -ExecutionPolicy Bypass -File %s", filename);
    int result = system(command);

    remove(filename);

    return result == 0;
}*/
/* 使用PowerShell发送邮件 - 修复编码问题 */
/*int send_email_powershell(const struct Config *config, const struct ElectricMeter *meter, double threshold)
{
    char ps_script[8192]; /* 增大缓冲区 */

/* 创建UTF-8编码的PowerShell脚本 */
/* FILE *ps_file = fopen("send_email.ps1", "wb"); */ /* 二进制模式写入 */
                                                     /* if (!ps_file)
                                                      {
                                                          printf("❌ 无法创建PowerShell脚本文件\n");
                                                          return 0;
                                                      }*/

/* 写入UTF-8 BOM头 */
/*unsigned char bom[] = {0xEF, 0xBB, 0xBF};
fwrite(bom, 1, 3, ps_file);

fprintf(ps_file,
        "$ErrorActionPreference = 'Stop'\n"
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
        "    # 创建邮件内容 - 使用英文和简单中文避免编码问题\n"
        "    $Body = @'\n"
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
        "'@\n"
        "    \n"
        "    # 创建凭据\n"
        "    $secpasswd = ConvertTo-SecureString $Password -AsPlainText -Force\n"
        "    $cred = New-Object System.Management.Automation.PSCredential ($Username, $secpasswd)\n"
        "    \n"
        "    # 发送邮件\n"
        "    Send-MailMessage -From $EmailFrom -To $EmailTo -Subject $Subject -Body $Body -BodyAsHtml -SmtpServer $SMTPServer -Port $SMTPPort -Credential $cred -UseSsl -Encoding UTF8\n"
        "    \n"
        "    Write-Output 'SUCCESS'\n"
        "    exit 0\n"
        "} Catch {\n"
        "    Write-Output (\"ERROR: \" + $_.Exception.Message)\n"
        "    exit 1\n"
        "}",
        config->emailAccount,
        config->emailReceiver,
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

fclose(ps_file);

printf("执行PowerShell发送邮件...\n");*/

/* 使用UTF-8编码执行PowerShell */
/*int result = system("chcp 65001 >nul && powershell -ExecutionPolicy Bypass -File send_email.ps1");

remove("send_email.ps1");

if (result == 0)
{
    printf("✅ PowerShell邮件发送成功\n");
    return 1;
}
else
{
    printf("❌ PowerShell发送失败，返回代码: %d\n", result);
    return 0;
}
}*/
/* 使用PowerShell发送邮件 - 支持多个收件人 */
int send_email_powershell(const struct Config *config, const struct ElectricMeter *meter, double threshold)
{
    char ps_script[8192];

    /* 创建UTF-8编码的PowerShell脚本 */
    FILE *ps_file = fopen("send_email.ps1", "wb");
    if (!ps_file)
    {
        printf("❌ 无法创建PowerShell脚本文件\n");
        return 0;
    }

    /* 写入UTF-8 BOM头 */
    unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    fwrite(bom, 1, 3, ps_file);

    fprintf(ps_file,
            "$ErrorActionPreference = 'Stop'\n"
            "Try {\n"
            "    # 邮件参数\n"
            "    $EmailFrom = '%s'\n"
            "    $EmailTo = '%s'\n" // 这里直接使用配置中的多个邮箱
            "    $Subject = '电表低电量提醒 - 剩余%.2f度电（山东石油化工学院）'\n"
            "    $SMTPServer = '%s'\n"
            "    $SMTPPort = %d\n"
            "    $Username = '%s'\n"
            "    $Password = '%s'\n"
            "    \n"
            "    # 分割多个收件人邮箱\n"
            "    $Recipients = $EmailTo -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' }\n"
            "    \n"
            "    # 创建邮件内容\n"
            "    $Body = @'\n"
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
            "'@\n"
            "    \n"
            "    # 创建凭据\n"
            "    $secpasswd = ConvertTo-SecureString $Password -AsPlainText -Force\n"
            "    $cred = New-Object System.Management.Automation.PSCredential ($Username, $secpasswd)\n"
            "    \n"
            "    # 发送邮件给每个收件人\n"
            "    $successCount = 0\n"
            "    foreach ($recipient in $Recipients) {\n"
            "        try {\n"
            "            Send-MailMessage -From $EmailFrom -To $recipient -Subject $Subject -Body $Body -BodyAsHtml -SmtpServer $SMTPServer -Port $SMTPPort -Credential $cred -UseSsl -Encoding UTF8\n"
            "            Write-Output (\"成功发送给: \" + $recipient)\n"
            "            $successCount++\n"
            "        } catch {\n"
            "            Write-Output (\"发送失败给 \" + $recipient + \": \" + $_.Exception.Message)\n"
            "        }\n"
            "    }\n"
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
            "    Write-Output (\"ERROR: \" + $_.Exception.Message)\n"
            "    exit 1\n"
            "}",
            config->emailAccount,
            config->emailReceiver, // 使用多个收件人
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

    fclose(ps_file);

    printf("执行PowerShell发送邮件给多个收件人...\n");

    /* 使用UTF-8编码执行PowerShell */
    int result = system("chcp 65001 >nul && powershell -ExecutionPolicy Bypass -File send_email.ps1");

    remove("send_email.ps1");

    if (result == 0)
    {
        printf("✅ 邮件发送成功（所有收件人）\n");
        return 1;
    }
    else
    {
        printf("❌ 邮件发送出现问题，返回代码: %d\n", result);
        return 0;
    }
}

/* 发送邮件 */
int send_email(const struct Config *config, const struct ElectricMeter *meter, double threshold)
{
    if (send_email_powershell(config, meter, threshold))
    {
        printf("邮件发送成功\n");
        return 1;
    }
    printf("邮件发送失败\n");
    return 0;
}

/* 显示电表信息 */
void display_meter_info(const struct ElectricMeter *meter, double threshold)
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

/* 获取电表数据 */
int get_electric_meter_data(const struct Config *config, struct ElectricMeter *meter)
{
    char url[512] = {0};
    char post_data[512] = {0};
    char headers[1024] = {0};
    char response[BUFFER_SIZE] = {0};

    parse_curl_command(config->curlCommand, url, post_data, headers);

    if (strlen(url) == 0)
        return 0;

    for (int i = 0; i < 2; i++)
    { // 重试2次
        if (http_post_request(url, post_data, headers, response, BUFFER_SIZE))
        {
            if (parse_json_response(response, meter))
            {
                return 1;
            }
        }
        if (i < 1)
            Sleep(3000); // 等待3秒后重试
    }

    return 0;
}

/* 主监控循环 */
/*void start_monitoring(const struct Config *config) {
    printf("开始电表监控\n");
    printf("监控间隔: %d 分钟\n", config->monitorInterval);
    printf("低电量阈值: %.1f 度\n", config->lowEnergyThreshold);
    printf("数据库: %s\n", config->dbPath);
    printf("按 Ctrl+C 停止\n\n");

    int count = 0;
    int alert_sent = 0;

    while (1) {
        count++;
        printf("\n第 %d 次查询\n", count);

        struct ElectricMeter meter;
        if (get_electric_meter_data(config, &meter)) {
            printf("数据获取成功\n");

            save_to_database(config->dbPath, &meter);

            display_meter_info(&meter, config->lowEnergyThreshold);

            if (meter.remainingEnergy <= config->lowEnergyThreshold) {
                if (!alert_sent) {
                    printf("低电量警报!\n");
                    send_email(config, &meter, config->lowEnergyThreshold);
                    save_alert_to_database(config->dbPath, &meter, config->lowEnergyThreshold);
                    alert_sent = 1;
                }
            } else {
                alert_sent = 0;
            }
        } else {
            printf("数据获取失败\n");
        }

        printf("等待 %d 分钟...\n", config->monitorInterval);
        Sleep(config->monitorInterval * 60 * 1000);
    }
}*/
/* 主监控循环 */
void start_monitoring(const struct Config *config)
{
    printf("开始电表监控\n");
    printf("监控间隔: %d 分钟\n", config->monitorInterval);
    printf("低电量阈值: %.1f 度\n", config->lowEnergyThreshold);
    printf("数据库: %s\n", config->dbPath);
    printf("按 Ctrl+C 停止\n\n");

    int count = 0;
    int alert_count = 0;      // 已发送警报次数
    int consecutive_low = 0;  // 连续低电量次数
    const int max_alerts = 3; // 最大警报次数

    while (1)
    {
        count++;
        printf("\n第 %d 次查询\n", count);

        struct ElectricMeter meter;
        if (get_electric_meter_data(config, &meter))
        {
            printf("数据获取成功\n");

            save_to_database(config->dbPath, &meter);

            display_meter_info(&meter, config->lowEnergyThreshold);

            if (meter.remainingEnergy <= config->lowEnergyThreshold)
            {
                consecutive_low++; // 增加连续低电量计数

                // 只有在未达到最大警报次数时才发送警报
                if (alert_count < max_alerts)
                {
                    printf("低电量警报! (连续第%d次检测到低电量, 第%d次警报)\n",
                           consecutive_low, alert_count + 1);

                    if (send_email(config, &meter, config->lowEnergyThreshold))
                    {
                        save_alert_to_database(config->dbPath, &meter, config->lowEnergyThreshold);
                        alert_count++; // 增加已发送警报计数

                        if (alert_count == max_alerts)
                        {
                            printf("已达到最大警报次数(%d次)，停止发送警报\n", max_alerts);
                        }
                    }
                }
                else
                {
                    printf("低电量状态持续，但已达到最大警报次数(%d次)\n", max_alerts);
                }
            }
            else
            {
                // 电量恢复正常，重置计数器
                if (consecutive_low > 0 || alert_count > 0)
                {
                    printf("电量已恢复正常，重置警报计数器\n");
                }
                consecutive_low = 0;
                alert_count = 0;
            }
        }
        else
        {
            printf("数据获取失败\n");
            // 获取数据失败时，不清除计数器，因为可能是网络问题
        }

        printf("等待 %d 分钟...\n", config->monitorInterval);
        Sleep(config->monitorInterval * 60 * 1000);
    }
}

/* 设置控制台编码 */
void set_console_utf8(void)
{
    system("chcp 65001 > nul");
}

int main(void)
{
    set_console_utf8();

    printf("电表监控系统\n\n");

    create_directory("temp_mail");

    struct Config config;
    if (!read_config("config.txt", &config))
    {
        printf("配置文件读取失败\n");
        return 1;
    }

    printf("配置加载成功\n");

    if (!init_database(config.dbPath))
    {
        printf("数据库初始化失败\n");
        return 1;
    }

    start_monitoring(&config);

    return 0;
}