#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#pragma comment(lib, "sqlite3.lib")

#define BUFFER_SIZE 4096
#define MAX_RECORDS 1000

/* 电表数据结构 */
typedef struct
{
    int id;
    char record_time[50];
    double remaining_energy;
    double remaining_amount;
    double total_consumption;
    double price;
    char meter_status[100];
    char meter_update_time[50];
    char system_time[50];
} ElectricMeter;

/* 函数声明 */
void set_console_utf8(void);
void pause_program(void);
const char *get_current_time(void);
void create_directory(const char *dirname);
int read_database_records(const char *db_path, ElectricMeter **records, int *count);
int read_alerts_records(const char *db_path, ElectricMeter **records, int *count);
int generate_index_html(const char *web_path, ElectricMeter *records, int count);
int generate_history_html(const char *web_path, ElectricMeter *records, int count, ElectricMeter *alerts, int alert_count);
int generate_alerts_html(const char *web_path, ElectricMeter *alerts, int count);
void display_statistics(ElectricMeter *records, int count);

/* 设置控制台编码 */
void set_console_utf8(void)
{
#ifdef _WIN32
    system("chcp 65001 > nul");
    SetConsoleOutputCP(65001);
#else
    // Linux/Mac 系统通常默认使用UTF-8
    printf("设置UTF-8编码（Linux/Mac）\n");
#endif
}

/* 暂停程序 */
void pause_program(void)
{
    printf("\n按任意键退出程序...\n");
#ifdef _WIN32
    system("pause > nul");
#else
    system("read -n 1 -s -p \"\"");
#endif
}

/* 获取当前时间字符串 */
const char *get_current_time(void)
{
    static char time_str[50];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    return time_str;
}

/* 创建目录 */
void create_directory(const char *dirname)
{
#ifdef _WIN32
    CreateDirectoryA(dirname, NULL);
#else
    mkdir(dirname, 0755);
#endif
}

/* 读取数据库中的电表记录 */
int read_database_records(const char *db_path, ElectricMeter **records, int *count)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        printf("无法打开数据库: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "SELECT id, record_time, remaining_energy, remaining_amount, "
                      "total_consumption, price, meter_status, meter_update_time, system_time "
                      "FROM electric_data ORDER BY record_time DESC LIMIT ?;";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        printf("准备SQL语句失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, MAX_RECORDS);

    *records = malloc(MAX_RECORDS * sizeof(ElectricMeter));
    if (!*records)
    {
        printf("内存分配失败\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 0;
    }

    *count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && *count < MAX_RECORDS)
    {
        ElectricMeter *record = &(*records)[*count];

        record->id = sqlite3_column_int(stmt, 0);

        const char *record_time = (const char *)sqlite3_column_text(stmt, 1);
        strncpy(record->record_time, record_time ? record_time : "", sizeof(record->record_time) - 1);

        record->remaining_energy = sqlite3_column_double(stmt, 2);
        record->remaining_amount = sqlite3_column_double(stmt, 3);
        record->total_consumption = sqlite3_column_double(stmt, 4);
        record->price = sqlite3_column_double(stmt, 5);

        const char *meter_status = (const char *)sqlite3_column_text(stmt, 6);
        strncpy(record->meter_status, meter_status ? meter_status : "", sizeof(record->meter_status) - 1);

        const char *meter_update_time = (const char *)sqlite3_column_text(stmt, 7);
        strncpy(record->meter_update_time, meter_update_time ? meter_update_time : "", sizeof(record->meter_update_time) - 1);

        const char *system_time = (const char *)sqlite3_column_text(stmt, 8);
        strncpy(record->system_time, system_time ? system_time : "", sizeof(record->system_time) - 1);

        (*count)++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    printf("成功读取 %d 条电表记录\n", *count);
    return 1;
}

/* 读取警报记录 */
int read_alerts_records(const char *db_path, ElectricMeter **records, int *count)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK)
    {
        printf("无法打开数据库: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    const char *sql = "SELECT id, alert_time, remaining_energy, threshold, alert_message, meter_update_time "
                      "FROM low_energy_alerts ORDER BY alert_time DESC LIMIT ?;";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        printf("准备SQL语句失败: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, MAX_RECORDS);

    *records = malloc(MAX_RECORDS * sizeof(ElectricMeter));
    if (!*records)
    {
        printf("内存分配失败\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 0;
    }

    *count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && *count < MAX_RECORDS)
    {
        ElectricMeter *record = &(*records)[*count];

        record->id = sqlite3_column_int(stmt, 0);

        const char *alert_time = (const char *)sqlite3_column_text(stmt, 1);
        strncpy(record->record_time, alert_time ? alert_time : "", sizeof(record->record_time) - 1);

        record->remaining_energy = sqlite3_column_double(stmt, 2);
        record->price = sqlite3_column_double(stmt, 3); // 使用price字段存储threshold

        const char *alert_message = (const char *)sqlite3_column_text(stmt, 4);
        strncpy(record->meter_status, alert_message ? alert_message : "", sizeof(record->meter_status) - 1);

        const char *meter_update_time = (const char *)sqlite3_column_text(stmt, 5);
        strncpy(record->meter_update_time, meter_update_time ? meter_update_time : "", sizeof(record->meter_update_time) - 1);

        (*count)++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    printf("成功读取 %d 条警报记录\n", *count);
    return 1;
}

/* 生成实时监控HTML页面（带暗黑模式） */
int generate_index_html(const char *web_path, ElectricMeter *records, int count)
{
    create_directory(web_path);

    char filepath[512];
    sprintf(filepath, "%s/index.html", web_path);

    FILE *file = fopen(filepath, "w");
    if (!file)
    {
        printf("无法创建HTML文件: %s\n", filepath);
        return 0;
    }

    // 获取最新记录
    ElectricMeter latest = {0};
    if (count > 0)
    {
        latest = records[0]; // 最新记录在第一个
    }

    double threshold = 100.0; // 默认阈值

    fprintf(file,
            "<!DOCTYPE html>\n"
            "<html lang=\"zh-CN\">\n"
            "<head>\n"
            "    <meta charset=\"UTF-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <title>电表实时监控</title>\n"
            "    <style>\n"
            "        :root {\n"
            "            --bg-primary: #f5f5f5;\n"
            "            --bg-secondary: white;\n"
            "            --text-primary: #2c3e50;\n"
            "            --text-secondary: #7f8c8d;\n"
            "            --border-color: #ecf0f1;\n"
            "            --header-bg: #2c3e50;\n"
            "            --nav-bg: #34495e;\n"
            "            --card-shadow: 0 2px 10px rgba(0,0,0,0.1);\n"
            "        }\n"
            "        \n"
            "        .dark-mode {\n"
            "            --bg-primary: #1a1a1a;\n"
            "            --bg-secondary: #2d2d2d;\n"
            "            --text-primary: #ffffff;\n"
            "            --text-secondary: #b0b0b0;\n"
            "            --border-color: #404040;\n"
            "            --header-bg: #1a1a1a;\n"
            "            --nav-bg: #2d2d2d;\n"
            "            --card-shadow: 0 2px 10px rgba(0,0,0,0.3);\n"
            "        }\n"
            "        \n"
            "        * { margin: 0; padding: 0; box-sizing: border-box; transition: background-color 0.3s, color 0.3s; }\n"
            "        body { font-family: 'Microsoft YaHei', Arial, sans-serif; background: var(--bg-primary); color: var(--text-primary); min-height: 100vh; padding: 20px; }\n"
            "        .container { max-width: 1000px; margin: 0 auto; background: var(--bg-secondary); border-radius: 10px; box-shadow: var(--card-shadow); overflow: hidden; }\n"
            "        .header { background: var(--header-bg); color: white; padding: 20px; text-align: center; position: relative; }\n"
            "        .header h1 { font-size: 2em; margin-bottom: 10px; }\n"
            "        .theme-toggle { position: absolute; top: 20px; right: 20px; background: rgba(255,255,255,0.2); border: none; color: white; padding: 8px 12px; border-radius: 20px; cursor: pointer; font-size: 14px; }\n"
            "        .theme-toggle:hover { background: rgba(255,255,255,0.3); }\n"
            "        .nav { background: var(--nav-bg); padding: 10px; text-align: center; }\n"
            "        .nav a { color: white; text-decoration: none; margin: 0 15px; padding: 5px 10px; border-radius: 3px; }\n"
            "        .nav a:hover { background: rgba(255,255,255,0.2); }\n"
            "        .content { padding: 20px; }\n"
            "        .status-card { background: var(--bg-secondary); border-radius: 8px; padding: 20px; margin-bottom: 20px; border-left: 5px solid #3498db; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }\n"
            "        .status-card.low-energy { border-left-color: #e74c3c; background: var(--bg-secondary); }\n"
            "        .status-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }\n"
            "        .status-title { font-size: 1.5em; color: var(--text-primary); font-weight: bold; }\n"
            "        .status-badge { padding: 5px 10px; border-radius: 15px; font-weight: bold; }\n"
            "        .badge-normal { background: #27ae60; color: white; }\n"
            "        .badge-low { background: #e74c3c; color: white; }\n"
            "        .stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin-bottom: 20px; }\n"
            "        .stat-card { background: var(--bg-secondary); padding: 15px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); text-align: center; border-top: 4px solid #3498db; }\n"
            "        .stat-card.energy { border-top-color: #e74c3c; }\n"
            "        .stat-card.amount { border-top-color: #27ae60; }\n"
            "        .stat-card.consumption { border-top-color: #f39c12; }\n"
            "        .stat-card.price { border-top-color: #9b59b6; }\n"
            "        .stat-value { font-size: 1.8em; font-weight: bold; margin: 8px 0; }\n"
            "        .energy-value { color: #e74c3c; }\n"
            "        .amount-value { color: #27ae60; }\n"
            "        .consumption-value { color: #f39c12; }\n"
            "        .price-value { color: #9b59b6; }\n"
            "        .stat-label { color: var(--text-secondary); font-size: 0.9em; }\n"
            "        .info-table { width: 100%%; border-collapse: collapse; background: var(--bg-secondary); border-radius: 8px; overflow: hidden; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }\n"
            "        .info-table th, .info-table td { padding: 12px; text-align: left; border-bottom: 1px solid var(--border-color); }\n"
            "        .info-table th { background: var(--nav-bg); color: white; font-weight: 600; }\n"
            "        .footer { background: var(--header-bg); color: white; text-align: center; padding: 15px; margin-top: 20px; }\n"
            "        .update-time { text-align: center; color: var(--text-secondary); margin: 10px 0; }\n"
            "        .alert-banner { background: #e74c3c; color: white; padding: 12px; text-align: center; border-radius: 6px; margin: 15px 0; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <div class=\"header\">\n"
            "            <h1>⚡ 电表监控系统</h1>\n"
            "            <div>实时电力监控</div>\n"
            "            <button class=\"theme-toggle\" onclick=\"toggleTheme()\">🌙 暗黑模式</button>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"nav\">\n"
            "            <a href=\"index.html\" style=\"background:rgba(255,255,255,0.2);\">实时监控</a>\n"
            "            <a href=\"history.html\">历史记录</a>\n"
            "            <a href=\"alerts.html\">警报记录</a>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"content\">\n");

    if (count > 0)
    {
        const char *status_class = (latest.remaining_energy <= threshold) ? "low-energy" : "normal";
        const char *status_text = (latest.remaining_energy <= threshold) ? "低电量" : "正常";
        const char *status_emoji = (latest.remaining_energy <= threshold) ? "⚠️" : "✅";

        double estimated_days = (latest.remaining_energy > 0 && latest.total_consumption > 0) ? (latest.remaining_energy / (latest.total_consumption / 30.0)) : 0;

        fprintf(file,
                "            <div class=\"status-card %s\">\n"
                "                <div class=\"status-header\">\n"
                "                    <div class=\"status-title\">当前电表状态</div>\n"
                "                    <div class=\"status-badge %s\">%s %s</div>\n"
                "                </div>\n",
                status_class,
                (latest.remaining_energy <= threshold) ? "badge-low" : "badge-normal",
                status_emoji, status_text);

        if (latest.remaining_energy <= threshold)
        {
            fprintf(file,
                    "                <div class=\"alert-banner\">\n"
                    "                    <strong>⚠️ 低电量警告！</strong> 剩余 %.2f 度电，请及时充值！\n"
                    "                </div>\n",
                    latest.remaining_energy);
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
                "            </table>\n",
                latest.remaining_energy,
                latest.remaining_amount,
                latest.total_consumption,
                latest.price,
                latest.meter_status,
                latest.meter_update_time,
                latest.system_time,
                threshold,
                estimated_days);
    }
    else
    {
        fprintf(file,
                "            <div class=\"status-card\">\n"
                "                <div class=\"status-header\">\n"
                "                    <div class=\"status-title\">当前电表状态</div>\n"
                "                    <div class=\"status-badge\">无数据</div>\n"
                "                </div>\n"
                "                <div class=\"alert-banner\">\n"
                "                    <strong>⚠️ 无数据！</strong> 数据库中没有找到电表记录\n"
                "                </div>\n"
                "            </div>\n");
    }

    fprintf(file,
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
            "        // 主题切换功能\n"
            "        function toggleTheme() {\n"
            "            document.body.classList.toggle('dark-mode');\n"
            "            const button = document.querySelector('.theme-toggle');\n"
            "            if (document.body.classList.contains('dark-mode')) {\n"
            "                button.textContent = '☀️ 明亮模式';\n"
            "                localStorage.setItem('theme', 'dark');\n"
            "            } else {\n"
            "                button.textContent = '🌙 暗黑模式';\n"
            "                localStorage.setItem('theme', 'light');\n"
            "            }\n"
            "        }\n"
            "        \n"
            "        // 加载保存的主题\n"
            "        document.addEventListener('DOMContentLoaded', function() {\n"
            "            const savedTheme = localStorage.getItem('theme');\n"
            "            if (savedTheme === 'dark') {\n"
            "                document.body.classList.add('dark-mode');\n"
            "                document.querySelector('.theme-toggle').textContent = '☀️ 明亮模式';\n"
            "            }\n"
            "        });\n"
            "        \n"
            "        // 自动刷新页面（每5分钟）\n"
            "        setTimeout(function() {\n"
            "            location.reload();\n"
            "        }, 300000);\n"
            "    </script>\n"
            "</body>\n"
            "</html>",
            get_current_time());

    fclose(file);

    printf("实时监控页面已生成: %s\n", filepath);
    return 1;
}

/* 生成历史记录HTML页面（带暗黑模式） */
int generate_history_html(const char *web_path, ElectricMeter *records, int count, ElectricMeter *alerts, int alert_count)
{
    char filepath[512];
    sprintf(filepath, "%s/history.html", web_path);

    FILE *file = fopen(filepath, "w");
    if (!file)
    {
        printf("无法创建HTML文件: %s\n", filepath);
        return 0;
    }

    // 计算统计信息
    double min_energy = 999999, max_energy = 0, avg_energy = 0;
    double min_amount = 999999, max_amount = 0, avg_amount = 0;
    double total_consumption = 0;

    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            if (records[i].remaining_energy < min_energy)
                min_energy = records[i].remaining_energy;
            if (records[i].remaining_energy > max_energy)
                max_energy = records[i].remaining_energy;
            if (records[i].remaining_amount < min_amount)
                min_amount = records[i].remaining_amount;
            if (records[i].remaining_amount > max_amount)
                max_amount = records[i].remaining_amount;
            avg_energy += records[i].remaining_energy;
            avg_amount += records[i].remaining_amount;
            total_consumption = records[i].total_consumption; // 取最新的总用电量
        }
        avg_energy /= count;
        avg_amount /= count;
    }

    fprintf(file,
            "<!DOCTYPE html>\n"
            "<html lang=\"zh-CN\">\n"
            "<head>\n"
            "    <meta charset=\"UTF-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <title>电表历史记录 - 山东石油化工学院</title>\n"
            "    <style>\n"
            "        :root {\n"
            "            --bg-primary: #f5f5f5;\n"
            "            --bg-secondary: white;\n"
            "            --text-primary: #2c3e50;\n"
            "            --text-secondary: #7f8c8d;\n"
            "            --border-color: #ecf0f1;\n"
            "            --header-bg: #2c3e50;\n"
            "            --nav-bg: #34495e;\n"
            "            --card-shadow: 0 2px 10px rgba(0,0,0,0.1);\n"
            "        }\n"
            "        \n"
            "        .dark-mode {\n"
            "            --bg-primary: #1a1a1a;\n"
            "            --bg-secondary: #2d2d2d;\n"
            "            --text-primary: #ffffff;\n"
            "            --text-secondary: #b0b0b0;\n"
            "            --border-color: #404040;\n"
            "            --header-bg: #1a1a1a;\n"
            "            --nav-bg: #2d2d2d;\n"
            "            --card-shadow: 0 2px 10px rgba(0,0,0,0.3);\n"
            "        }\n"
            "        \n"
            "        * { margin: 0; padding: 0; box-sizing: border-box; transition: background-color 0.3s, color 0.3s; }\n"
            "        body { font-family: 'Microsoft YaHei', Arial, sans-serif; background: var(--bg-primary); color: var(--text-primary); min-height: 100vh; padding: 20px; }\n"
            "        .container { max-width: 1400px; margin: 0 auto; background: var(--bg-secondary); border-radius: 10px; box-shadow: var(--card-shadow); overflow: hidden; }\n"
            "        .header { background: var(--header-bg); color: white; padding: 20px; text-align: center; position: relative; }\n"
            "        .header h1 { font-size: 2em; margin-bottom: 10px; }\n"
            "        .theme-toggle { position: absolute; top: 20px; right: 20px; background: rgba(255,255,255,0.2); border: none; color: white; padding: 8px 12px; border-radius: 20px; cursor: pointer; font-size: 14px; }\n"
            "        .theme-toggle:hover { background: rgba(255,255,255,0.3); }\n"
            "        .nav { background: var(--nav-bg); padding: 10px; text-align: center; }\n"
            "        .nav a { color: white; text-decoration: none; margin: 0 15px; padding: 5px 10px; border-radius: 3px; }\n"
            "        .nav a:hover { background: rgba(255,255,255,0.2); }\n"
            "        .content { padding: 20px; }\n"
            "        .stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin-bottom: 20px; }\n"
            "        .stat-card { background: var(--bg-secondary); padding: 15px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); text-align: center; border-top: 4px solid #3498db; }\n"
            "        .stat-card.records { border-top-color: #3498db; }\n"
            "        .stat-card.alerts { border-top-color: #e74c3c; }\n"
            "        .stat-card.consumption { border-top-color: #f39c12; }\n"
            "        .stat-card.energy { border-top-color: #27ae60; }\n"
            "        .stat-value { font-size: 1.8em; font-weight: bold; margin: 8px 0; }\n"
            "        .stat-label { color: var(--text-secondary); font-size: 0.9em; }\n"
            "        .history-table { width: 100%%; border-collapse: collapse; background: var(--bg-secondary); border-radius: 8px; overflow: hidden; box-shadow: 0 2px 5px rgba(0,0,0,0.1); margin-bottom: 20px; }\n"
            "        .history-table th, .history-table td { padding: 12px; text-align: left; border-bottom: 1px solid var(--border-color); }\n"
            "        .history-table th { background: var(--nav-bg); color: white; font-weight: 600; position: sticky; top: 0; }\n"
            "        .history-table tr:hover { background: var(--bg-primary); }\n"
            "        .low-energy { background-color: rgba(231, 76, 60, 0.1) !important; }\n"
            "        .table-container { max-height: 600px; overflow-y: auto; margin-bottom: 30px; }\n"
            "        .footer { background: var(--header-bg); color: white; text-align: center; padding: 15px; margin-top: 20px; }\n"
            "        .update-time { text-align: center; color: var(--text-secondary); margin: 10px 0; }\n"
            "        .section-title { font-size: 1.5em; color: var(--text-primary); margin: 20px 0 15px 0; padding-bottom: 10px; border-bottom: 2px solid var(--border-color); }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <div class=\"header\">\n"
            "            <h1>⚡ 电表监控系统 - 历史记录</h1>\n"
            "            <div>山东石油化工学院 - 电力数据历史记录</div>\n"
            "            <button class=\"theme-toggle\" onclick=\"toggleTheme()\">🌙 暗黑模式</button>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"nav\">\n"
            "            <a href=\"index.html\">实时监控</a>\n"
            "            <a href=\"history.html\" style=\"background:rgba(255,255,255,0.2);\">历史记录</a>\n"
            "            <a href=\"alerts.html\">警报记录</a>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"content\">\n"
            "            <div class=\"stats-grid\">\n"
            "                <div class=\"stat-card records\">\n"
            "                    <div class=\"stat-label\">总记录数</div>\n"
            "                    <div class=\"stat-value\">%d 条</div>\n"
            "                    <div>Total Records</div>\n"
            "                </div>\n"
            "                <div class=\"stat-card alerts\">\n"
            "                    <div class=\"stat-label\">警报次数</div>\n"
            "                    <div class=\"stat-value\">%d 次</div>\n"
            "                    <div>Total Alerts</div>\n"
            "                </div>\n"
            "                <div class=\"stat-card consumption\">\n"
            "                    <div class=\"stat-label\">累计用电</div>\n"
            "                    <div class=\"stat-value\">%.2f kWh</div>\n"
            "                    <div>Total Consumption</div>\n"
            "                </div>\n"
            "                <div class=\"stat-card energy\">\n"
            "                    <div class=\"stat-label\">平均剩余电量</div>\n"
            "                    <div class=\"stat-value\">%.2f 度</div>\n"
            "                    <div>Avg Energy</div>\n"
            "                </div>\n"
            "            </div>\n"
            "            \n"
            "            <div class=\"section-title\">📊 电量统计</div>\n"
            "            <div class=\"stats-grid\">\n"
            "                <div class=\"stat-card\">\n"
            "                    <div class=\"stat-label\">最低剩余电量</div>\n"
            "                    <div class=\"stat-value\">%.2f 度</div>\n"
            "                    <div>Min Energy</div>\n"
            "                </div>\n"
            "                <div class=\"stat-card\">\n"
            "                    <div class=\"stat-label\">最高剩余电量</div>\n"
            "                    <div class=\"stat-value\">%.2f 度</div>\n"
            "                    <div>Max Energy</div>\n"
            "                </div>\n"
            "                <div class=\"stat-card\">\n"
            "                    <div class=\"stat-label\">最低剩余金额</div>\n"
            "                    <div class=\"stat-value\">%.2f 元</div>\n"
            "                    <div>Min Amount</div>\n"
            "                </div>\n"
            "                <div class=\"stat-card\">\n"
            "                    <div class=\"stat-label\">最高剩余金额</div>\n"
            "                    <div class=\"stat-value\">%.2f 元</div>\n"
            "                    <div>Max Amount</div>\n"
            "                </div>\n"
            "            </div>\n"
            "            \n"
            "            <div class=\"section-title\">📈 详细历史记录（最近%d条）</div>\n"
            "            <div class=\"table-container\">\n"
            "                <table class=\"history-table\">\n"
            "                    <thead>\n"
            "                        <tr>\n"
            "                            <th>ID</th>\n"
            "                            <th>记录时间</th>\n"
            "                            <th>剩余电量 (度)</th>\n"
            "                            <th>剩余金额 (元)</th>\n"
            "                            <th>累计用电 (kWh)</th>\n"
            "                            <th>电价 (元/度)</th>\n"
            "                            <th>电表状态</th>\n"
            "                            <th>数据更新时间</th>\n"
            "                        </tr>\n"
            "                    </thead>\n"
            "                    <tbody>\n",
            count, alert_count, total_consumption, avg_energy, min_energy, max_energy, min_amount, max_amount, count);

    // 输出记录数据
    for (int i = 0; i < count; i++)
    {
        const char *row_class = (records[i].remaining_energy < 50) ? "class=\"low-energy\"" : "";
        fprintf(file,
                "                        <tr %s>\n"
                "                            <td>%d</td>\n"
                "                            <td>%s</td>\n"
                "                            <td>%.2f</td>\n"
                "                            <td>%.2f</td>\n"
                "                            <td>%.2f</td>\n"
                "                            <td>%.4f</td>\n"
                "                            <td>%s</td>\n"
                "                            <td>%s</td>\n"
                "                        </tr>\n",
                row_class,
                records[i].id,
                records[i].record_time,
                records[i].remaining_energy,
                records[i].remaining_amount,
                records[i].total_consumption,
                records[i].price,
                records[i].meter_status,
                records[i].meter_update_time);
    }

    fprintf(file,
            "                    </tbody>\n"
            "                </table>\n"
            "            </div>\n"
            "            \n"
            "            <div class=\"update-time\">\n"
            "                页面生成时间: %s\n"
            "            </div>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"footer\">\n"
            "            <p>历史记录页面</p>\n"
            "            <p></p>\n"
            "        </div>\n"
            "    </div>\n"
            "    \n"
            "    <script>\n"
            "        // 主题切换功能\n"
            "        function toggleTheme() {\n"
            "            document.body.classList.toggle('dark-mode');\n"
            "            const button = document.querySelector('.theme-toggle');\n"
            "            if (document.body.classList.contains('dark-mode')) {\n"
            "                button.textContent = '☀️ 明亮模式';\n"
            "                localStorage.setItem('theme', 'dark');\n"
            "            } else {\n"
            "                button.textContent = '🌙 暗黑模式';\n"
            "                localStorage.setItem('theme', 'light');\n"
            "            }\n"
            "        }\n"
            "        \n"
            "        // 加载保存的主题\n"
            "        document.addEventListener('DOMContentLoaded', function() {\n"
            "            const savedTheme = localStorage.getItem('theme');\n"
            "            if (savedTheme === 'dark') {\n"
            "                document.body.classList.add('dark-mode');\n"
            "                document.querySelector('.theme-toggle').textContent = '☀️ 明亮模式';\n"
            "            }\n"
            "            \n"
            "            // 表格排序功能\n"
            "            const table = document.querySelector('.history-table');\n"
            "            const headers = table.querySelectorAll('th');\n"
            "            \n"
            "            headers.forEach((header, index) => {\n"
            "                header.style.cursor = 'pointer';\n"
            "                header.addEventListener('click', () => {\n"
            "                    sortTable(index);\n"
            "                });\n"
            "            });\n"
            "            \n"
            "            function sortTable(column) {\n"
            "                const tbody = table.querySelector('tbody');\n"
            "                const rows = Array.from(tbody.querySelectorAll('tr'));\n"
            "                \n"
            "                rows.sort((a, b) => {\n"
            "                    const aText = a.cells[column].textContent.trim();\n"
            "                    const bText = b.cells[column].textContent.trim();\n"
            "                    \n"
            "                    // 尝试转换为数字比较\n"
            "                    const aNum = parseFloat(aText);\n"
            "                    const bNum = parseFloat(bText);\n"
            "                    \n"
            "                    if (!isNaN(aNum) && !isNaN(bNum)) {\n"
            "                        return aNum - bNum;\n"
            "                    } else {\n"
            "                        return aText.localeCompare(bText);\n"
            "                    }\n"
            "                });\n"
            "                \n"
            "                // 清空并重新添加排序后的行\n"
            "                rows.forEach(row => tbody.appendChild(row));\n"
            "            }\n"
            "        });\n"
            "        \n"
            "        // 自动刷新页面（每5分钟）\n"
            "        setTimeout(function() {\n"
            "            location.reload();\n"
            "        }, 300000);\n"
            "    </script>\n"
            "</body>\n"
            "</html>",
            get_current_time());

    fclose(file);

    printf("历史记录页面已生成: %s\n", filepath);
    return 1;
}

/* 生成警报记录HTML页面（带暗黑模式） */
int generate_alerts_html(const char *web_path, ElectricMeter *alerts, int count)
{
    char filepath[512];
    sprintf(filepath, "%s/alerts.html", web_path);

    FILE *file = fopen(filepath, "w");
    if (!file)
    {
        printf("无法创建HTML文件: %s\n", filepath);
        return 0;
    }

    fprintf(file,
            "<!DOCTYPE html>\n"
            "<html lang=\"zh-CN\">\n"
            "<head>\n"
            "    <meta charset=\"UTF-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <title>电表警报记录 - 山东石油化工学院</title>\n"
            "    <style>\n"
            "        :root {\n"
            "            --bg-primary: #f5f5f5;\n"
            "            --bg-secondary: white;\n"
            "            --text-primary: #2c3e50;\n"
            "            --text-secondary: #7f8c8d;\n"
            "            --border-color: #ecf0f1;\n"
            "            --header-bg: #e74c3c;\n"
            "            --nav-bg: #c0392b;\n"
            "            --card-shadow: 0 2px 10px rgba(0,0,0,0.1);\n"
            "        }\n"
            "        \n"
            "        .dark-mode {\n"
            "            --bg-primary: #1a1a1a;\n"
            "            --bg-secondary: #2d2d2d;\n"
            "            --text-primary: #ffffff;\n"
            "            --text-secondary: #b0b0b0;\n"
            "            --border-color: #404040;\n"
            "            --header-bg: #c0392b;\n"
            "            --nav-bg: #a93226;\n"
            "            --card-shadow: 0 2px 10px rgba(0,0,0,0.3);\n"
            "        }\n"
            "        \n"
            "        * { margin: 0; padding: 0; box-sizing: border-box; transition: background-color 0.3s, color 0.3s; }\n"
            "        body { font-family: 'Microsoft YaHei', Arial, sans-serif; background: var(--bg-primary); color: var(--text-primary); min-height: 100vh; padding: 20px; }\n"
            "        .container { max-width: 1200px; margin: 0 auto; background: var(--bg-secondary); border-radius: 10px; box-shadow: var(--card-shadow); overflow: hidden; }\n"
            "        .header { background: var(--header-bg); color: white; padding: 20px; text-align: center; position: relative; }\n"
            "        .header h1 { font-size: 2em; margin-bottom: 10px; }\n"
            "        .theme-toggle { position: absolute; top: 20px; right: 20px; background: rgba(255,255,255,0.2); border: none; color: white; padding: 8px 12px; border-radius: 20px; cursor: pointer; font-size: 14px; }\n"
            "        .theme-toggle:hover { background: rgba(255,255,255,0.3); }\n"
            "        .nav { background: var(--nav-bg); padding: 10px; text-align: center; }\n"
            "        .nav a { color: white; text-decoration: none; margin: 0 15px; padding: 5px 10px; border-radius: 3px; }\n"
            "        .nav a:hover { background: rgba(255,255,255,0.2); }\n"
            "        .content { padding: 20px; }\n"
            "        .stats-card { background: rgba(231, 76, 60, 0.1); padding: 20px; border-radius: 8px; border-left: 5px solid #e74c3c; margin-bottom: 20px; }\n"
            "        .stats-value { font-size: 2em; font-weight: bold; color: #e74c3c; }\n"
            "        .stats-label { color: var(--text-secondary); font-size: 1em; }\n"
            "        .alerts-table { width: 100%%; border-collapse: collapse; background: var(--bg-secondary); border-radius: 8px; overflow: hidden; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }\n"
            "        .alerts-table th, .alerts-table td { padding: 12px; text-align: left; border-bottom: 1px solid var(--border-color); }\n"
            "        .alerts-table th { background: var(--nav-bg); color: white; font-weight: 600; }\n"
            "        .alerts-table tr:hover { background: var(--bg-primary); }\n"
            "        .alert-critical { background-color: rgba(231, 76, 60, 0.1) !important; font-weight: bold; color: #e74c3c; }\n"
            "        .footer { background: var(--header-bg); color: white; text-align: center; padding: 15px; margin-top: 20px; }\n"
            "        .update-time { text-align: center; color: var(--text-secondary); margin: 10px 0; }\n"
            "        .section-title { font-size: 1.5em; color: #e74c3c; margin: 20px 0 15px 0; padding-bottom: 10px; border-bottom: 2px solid var(--border-color); }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <div class=\"header\">\n"
            "            <h1>🚨 电表监控系统 - 警报记录</h1>\n"
            "            <div>山东石油化工学院 - 低电量警报历史记录</div>\n"
            "            <button class=\"theme-toggle\" onclick=\"toggleTheme()\">🌙 暗黑模式</button>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"nav\">\n"
            "            <a href=\"index.html\">实时监控</a>\n"
            "            <a href=\"history.html\">历史记录</a>\n"
            "            <a href=\"alerts.html\" style=\"background:rgba(255,255,255,0.2);\">警报记录</a>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"content\">\n"
            "            <div class=\"stats-card\">\n"
            "                <div class=\"stats-value\">%d 次</div>\n"
            "                <div class=\"stats-label\">总警报次数</div>\n"
            "            </div>\n"
            "            \n"
            "            <div class=\"section-title\">📋 警报记录详情</div>\n"
            "            <table class=\"alerts-table\">\n"
            "                <thead>\n"
            "                    <tr>\n"
            "                        <th>ID</th>\n"
            "                        <th>警报时间</th>\n"
            "                        <th>剩余电量</th>\n"
            "                        <th>阈值</th>\n"
            "                        <th>警报信息</th>\n"
            "                        <th>数据更新时间</th>\n"
            "                    </tr>\n"
            "                </thead>\n"
            "                <tbody>\n",
            count);

    // 输出警报数据
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            fprintf(file,
                    "                    <tr class=\"alert-critical\">\n"
                    "                        <td>%d</td>\n"
                    "                        <td>%s</td>\n"
                    "                        <td>%.2f 度</td>\n"
                    "                        <td>%.1f 度</td>\n"
                    "                        <td>%s</td>\n"
                    "                        <td>%s</td>\n"
                    "                    </tr>\n",
                    alerts[i].id,
                    alerts[i].record_time,
                    alerts[i].remaining_energy,
                    alerts[i].price,        // 使用price字段存储threshold
                    alerts[i].meter_status, // 使用meter_status字段存储alert_message
                    alerts[i].meter_update_time);
        }
    }
    else
    {
        fprintf(file,
                "                    <tr>\n"
                "                        <td colspan=\"6\" style=\"text-align: center; color: var(--text-secondary);\">暂无警报记录</td>\n"
                "                    </tr>\n");
    }

    fprintf(file,
            "                </tbody>\n"
            "            </table>\n"
            "            \n"
            "            <div class=\"update-time\">\n"
            "                页面生成时间: %s\n"
            "            </div>\n"
            "        </div>\n"
            "        \n"
            "        <div class=\"footer\">\n"
            "            <p>警报记录页面</p>\n"
            "            <p></p>\n"
            "        </div>\n"
            "    </div>\n"
            "    \n"
            "    <script>\n"
            "        // 主题切换功能\n"
            "        function toggleTheme() {\n"
            "            document.body.classList.toggle('dark-mode');\n"
            "            const button = document.querySelector('.theme-toggle');\n"
            "            if (document.body.classList.contains('dark-mode')) {\n"
            "                button.textContent = '☀️ 明亮模式';\n"
            "                localStorage.setItem('theme', 'dark');\n"
            "            } else {\n"
            "                button.textContent = '🌙 暗黑模式';\n"
            "                localStorage.setItem('theme', 'light');\n"
            "            }\n"
            "        }\n"
            "        \n"
            "        // 加载保存的主题\n"
            "        document.addEventListener('DOMContentLoaded', function() {\n"
            "            const savedTheme = localStorage.getItem('theme');\n"
            "            if (savedTheme === 'dark') {\n"
            "                document.body.classList.add('dark-mode');\n"
            "                document.querySelector('.theme-toggle').textContent = '☀️ 明亮模式';\n"
            "            }\n"
            "        });\n"
            "        \n"
            "        // 自动刷新页面（每5分钟）\n"
            "        setTimeout(function() {\n"
            "            location.reload();\n"
            "        }, 300000);\n"
            "    </script>\n"
            "</body>\n"
            "</html>",
            get_current_time());

    fclose(file);

    printf("警报记录页面已生成: %s\n", filepath);
    return 1;
}

/* 显示统计信息 */
void display_statistics(ElectricMeter *records, int count)
{
    if (count == 0)
    {
        printf("没有数据可显示统计信息\n");
        return;
    }

    double min_energy = 999999, max_energy = 0, avg_energy = 0;
    double min_amount = 999999, max_amount = 0, avg_amount = 0;

    for (int i = 0; i < count; i++)
    {
        if (records[i].remaining_energy < min_energy)
            min_energy = records[i].remaining_energy;
        if (records[i].remaining_energy > max_energy)
            max_energy = records[i].remaining_energy;
        if (records[i].remaining_amount < min_amount)
            min_amount = records[i].remaining_amount;
        if (records[i].remaining_amount > max_amount)
            max_amount = records[i].remaining_amount;
        avg_energy += records[i].remaining_energy;
        avg_amount += records[i].remaining_amount;
    }
    avg_energy /= count;
    avg_amount /= count;

    printf("\n=== 数据统计 ===\n");
    printf("记录数量: %d 条\n", count);
    printf("剩余电量 - 最小: %.2f度, 最大: %.2f度, 平均: %.2f度\n",
           min_energy, max_energy, avg_energy);
    printf("剩余金额 - 最小: %.2f元, 最大: %.2f元, 平均: %.2f元\n",
           min_amount, max_amount, avg_amount);
    printf("最新累计用电: %.2f kWh\n", records[0].total_consumption);
    printf("================\n");
}

/* 主函数 */
int main(void)
{
    set_console_utf8();

    printf("========================================\n");
    printf("   电表历史记录生成器 - 山东石油化工学院\n");
    printf("========================================\n\n");

    // 数据库路径
    const char *db_path = "electric_data.db";
    const char *web_path = "web";

    // 检查数据库文件是否存在
    FILE *db_test = fopen(db_path, "r");
    if (!db_test)
    {
        printf("❌ 错误: 找不到数据库文件 %s\n", db_path);
        printf("请确保电表监控程序已经运行并生成了数据库文件\n");
        pause_program();
        return 1;
    }
    fclose(db_test);

    // 读取电表记录
    ElectricMeter *records = NULL;
    int record_count = 0;

    if (!read_database_records(db_path, &records, &record_count))
    {
        printf("❌ 读取电表记录失败\n");
        pause_program();
        return 1;
    }

    // 读取警报记录
    ElectricMeter *alerts = NULL;
    int alert_count = 0;

    if (!read_alerts_records(db_path, &alerts, &alert_count))
    {
        printf("⚠️ 读取警报记录失败或没有警报记录\n");
        // 继续执行，警报记录不是必需的
    }

    // 显示统计信息
    display_statistics(records, record_count);

    // 生成HTML页面
    printf("\n正在生成HTML页面...\n");

    // 1. 生成实时监控页面
    if (generate_index_html(web_path, records, record_count))
    {
        printf("✅ 实时监控页面生成成功\n");
    }
    else
    {
        printf("❌ 实时监控页面生成失败\n");
    }

    // 2. 生成历史记录页面
    if (generate_history_html(web_path, records, record_count, alerts, alert_count))
    {
        printf("✅ 历史记录页面生成成功\n");
    }
    else
    {
        printf("❌ 历史记录页面生成失败\n");
    }

    // 3. 生成警报记录页面（即使没有警报也要生成）
    if (generate_alerts_html(web_path, alerts, alert_count))
    {
        printf("✅ 警报记录页面生成成功\n");
    }
    else
    {
        printf("❌ 警报记录页面生成失败\n");
    }

    // 释放内存
    if (records)
        free(records);
    if (alerts)
        free(alerts);

    printf("\n✅ 所有页面生成完成！\n");
    printf("📁 页面位置: %s/ 目录\n", web_path);
    printf("   1. index.html   - 实时监控\n");
    printf("   2. history.html - 完整历史记录\n");
    printf("   3. alerts.html  - 警报记录\n");
    printf("\n💡 新功能: 所有页面都支持 🌙 暗黑模式切换！\n");
    printf("💡 提示: 在浏览器中打开 web/index.html 查看实时监控\n");

    pause_program();
    return 0;
}