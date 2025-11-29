mod config;
mod database;
mod electric_meter;
mod email_notifier;
mod models;
mod qq_bot;
mod web_server;

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;
use tokio::time::sleep;
use crate::config::ConfigLoader;
use crate::database::{DatabaseManager, now_beijing};
use crate::electric_meter::ElectricMeterService;
use crate::email_notifier::EmailNotifier;
use crate::qq_bot::QQBot;
use crate::web_server::WebServer;

// 警告计数器结构
struct WarningCounter {
    counters: HashMap<String, i32>, // 电表名称 -> 警告次数
}

impl WarningCounter {
    fn new() -> Self {
        Self {
            counters: HashMap::new(),
        }
    }
    
    // 检查是否可以发送警告
    fn can_send_warning(&mut self, meter_name: &str, max_warning: i32) -> bool {
        if max_warning == 0 {
            // 如果max_warning为0，表示每次循环都发送
            return true;
        }
        
        let count = self.counters.entry(meter_name.to_string()).or_insert(0);
        
        if *count < max_warning {
            *count += 1;
            true
        } else {
            log::info!("电表 {} 已达到最大警告次数限制 {}，跳过邮件发送", meter_name, max_warning);
            false
        }
    }
    
    // 重置计数器（在每次主循环开始时调用）
    fn reset_counters(&mut self) {
        self.counters.clear();
        log::debug!("警告计数器已重置");
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // 初始化日志
    env_logger::init();
    
    log::info!("🚀 启动电表监控系统...");
    log::info!("📋 程序版本: electric-meter-monitor v0.1.0");
    log::info!("📁 工作目录: {}", std::env::current_dir().unwrap().display());
    
    // 检查日志配置文件
    let log_config_path = "log_config.toml";
    if std::path::Path::new(log_config_path).exists() {
        log::info!("📄 检测到日志配置文件: {}", log_config_path);
    } else {
        log::info!("📝 使用默认日志配置，可创建 {} 文件自定义配置", log_config_path);
    }
    
    // 加载配置
    log::info!("📄 加载配置文件: config.toml");
    let config_loader = ConfigLoader::new("config.toml");
    let app_config = config_loader.load()?;
    
    log::info!("✅ 配置加载成功");
    log::info!("📊 共配置 {} 个电表", app_config.meters.len());
    
    // 显示启用的电表信息
    let enabled_meters: Vec<_> = app_config.meters.iter()
        .filter(|m| m.enabled)
        .collect();
    log::info!("🔌 启用电表数量: {}", enabled_meters.len());
    for meter in &enabled_meters {
        log::info!("   - {} (UID: {})", meter.name, meter.electric_user_uid);
    }
    
    // 初始化数据库管理器
    log::info!("💾 初始化数据库管理器...");
    let db_manager = Arc::new(DatabaseManager::new("electric_data.db"));
    
    // 初始化数据库表
    log::info!("📊 初始化主数据库表...");
    if let Err(e) = db_manager.init() {
        log::error!("❌ 数据库初始化失败: {}", e);
        return Err(Box::new(e) as Box<dyn std::error::Error>);
    }
    log::info!("✅ 主数据库初始化成功");
    
    // 为每个电表创建独立的数据库管理器
    log::info!("💽 初始化电表专用数据库...");
    let mut db_managers = HashMap::new();
    let mut db_initialized_count = 0;
    
    for meter in &app_config.meters {
        if meter.enabled && meter.db_enable {
            log::info!("   📁 初始化电表 {} 的数据库: {}", meter.name, meter.db_name);
            let db_manager = Arc::new(DatabaseManager::new(&meter.db_name));
            if let Err(e) = db_manager.init() {
                log::error!("❌ 电表 {} 数据库初始化失败: {}", meter.name, e);
                return Err(Box::new(e) as Box<dyn std::error::Error>);
            }
            db_managers.insert(meter.name.clone(), db_manager);
            log::info!("   ✅ 电表 {} 数据库初始化成功", meter.name);
            db_initialized_count += 1;
        }
    }
    log::info!("✅ 共初始化 {} 个电表专用数据库", db_initialized_count);
    
    // 为QQ机器人创建专门的数据库管理器
    log::info!("🤖 初始化QQ机器人数据库...");
    let qq_bot_db_manager = Arc::new(DatabaseManager::new("qq_bot.db"));
    if let Err(e) = qq_bot_db_manager.init() {
        log::error!("❌ QQ机器人数据库初始化失败: {}", e);
        return Err(Box::new(e) as Box<dyn std::error::Error>);
    }
    log::info!("✅ QQ机器人数据库初始化成功，数据库文件: qq_bot.db");
    
    // 初始化邮件通知器
    log::info!("📧 初始化邮件通知服务...");
    let email_notifier = Arc::new(EmailNotifier::new(app_config.email_config.clone()));
    if app_config.email_config.enabled {
        log::info!("✅ 邮件通知服务已启用");
    } else {
        log::info!("📭 邮件通知服务已禁用");
    }
    
    // 初始化QQ机器人
    log::info!("🤖 初始化QQ机器人服务...");
    let qq_bot = Arc::new(QQBot::new(app_config.napcat_config.clone()));
    if qq_bot.is_enabled() {
        log::info!("✅ QQ机器人服务已启用");
        log::info!("   📡 API地址: {}:{}", app_config.napcat_config.api_url, app_config.napcat_config.api_port);
        log::info!("   👥 监控群组: {}", app_config.napcat_config.qq_group_id);
    } else {
        log::info!("📭 QQ机器人服务已禁用");
    }
    
    // 初始化Web服务器
    log::info!("🌐 初始化Web服务器...");
    let web_server = Arc::new(WebServer::new(app_config.web_config.clone()));
    if app_config.web_config.enable_http_server {
        log::info!("✅ Web服务器已启用，端口: {}", app_config.web_config.http_port);
    } else {
        log::info!("📭 Web服务器已禁用");
    }
    
    if app_config.web_config.enable_generate_web {
        log::info!("📄 网页生成功能已启用");
    }
    
    if app_config.web_config.enable_output_web {
        log::info!("💾 网页文件输出功能已启用，输出目录: {}", app_config.web_config.output_web_path);
    }
    
    // 启动QQ机器人监控（如果启用）
    if qq_bot.is_enabled() {
        log::info!("🚀 启动QQ机器人监控服务...");
        let qq_bot_clone = qq_bot.clone();
        let db_manager_clone = qq_bot_db_manager.clone();
        let meters_clone = app_config.meters.clone();
        
        qq_bot_clone.start_monitoring(db_manager_clone, meters_clone).await;
        log::info!("✅ QQ机器人监控服务已启动");
    }
    
    // 启动Web服务器（如果启用）
    if app_config.web_config.enable_http_server {
        log::info!("🚀 启动Web服务器...");
        let web_server_clone = web_server.clone();
        tokio::spawn(async move {
            log::info!("🌐 Web服务器开始监听端口: {}", app_config.web_config.http_port);
            if let Err(e) = web_server_clone.start_server().await {
                log::error!("❌ Web服务器启动失败: {}", e);
            } else {
                log::info!("✅ Web服务器启动成功，访问地址: http://localhost:{}", app_config.web_config.http_port);
            }
        });
    }
    
    // 初始化警告计数器
    let mut warning_counter = WarningCounter::new();
    
    // 主循环
    log::info!("🔄 开始主监控循环，间隔: {} 分钟", app_config.loop_config.interval_minutes);
    let mut loop_count = 0;
    
    loop {
        loop_count += 1;
        log::info!("🔄 第 {} 轮监控开始", loop_count);
        
        if !app_config.loop_config.enabled {
            log::info!("📭 循环监控已禁用，程序退出");
            break;
        }
        
        // 重置警告计数器（每次循环开始时）
        warning_counter.reset_counters();
        
        let mut latest_data = HashMap::new();
        let mut success_count = 0;
        let mut error_count = 0;
        
        // 处理每个电表
        log::info!("🔌 开始查询电表数据...");
        for meter in &app_config.meters {
            if !meter.enabled {
                log::debug!("📭 电表 {} 已禁用，跳过查询", meter.name);
                continue;
            }
            
            log::info!("📡 查询电表: {} (UID: {})", meter.name, meter.electric_user_uid);
            
            let meter_service = ElectricMeterService::new(
                app_config.url.clone(),
                app_config.retry_config.clone(),
            );
            
            match meter_service.get_electric_data(&meter.electric_user_uid).await {
                Ok(response) => {
                    success_count += 1;
                    
                    // 保存到数据库（使用对应电表的数据库管理器）
                    if let Some(db_manager) = db_managers.get(&meter.name) {
                        match meter_service.save_to_database(&response, db_manager, &meter.name) {
                            Ok(record_id) => {
                            log::info!("✅ 电表 {} 数据保存成功，记录ID: {}", meter.name, record_id);
                            
                            // 创建数据库记录对象
                            let record = if let Some(data) = &response.data {
                                crate::models::DatabaseRecord {
                                    id: Some(record_id),
                                    record_time: now_beijing(),
                                    remaining_energy: data.shengyu.parse::<f64>().unwrap_or(0.0),
                                    total_consumption: data.leiji.parse::<f64>().unwrap_or(0.0),
                                    actual_consumption: data.leiji.parse::<f64>().unwrap_or(0.0),
                                    price: data.price.parse::<f64>().unwrap_or(0.0),
                                    meter_status: data.zhuangtai.clone(),
                                    system_time: now_beijing(),
                                }
                            } else {
                                // 如果data为空，创建默认记录
                                crate::models::DatabaseRecord {
                                    id: Some(record_id),
                                    record_time: now_beijing(),
                                    remaining_energy: 0.0,
                                    total_consumption: 0.0,
                                    actual_consumption: 0.0,
                                    price: 0.0,
                                    meter_status: "数据为空".to_string(),
                                    system_time: now_beijing(),
                                }
                            };
                            
                            latest_data.insert(meter.name.clone(), record);
                            
                            // 检查低电量
                            log::debug!("检查电表 {} 的低电量状态，阈值: {} kWh", meter.name, meter.low_energy_threshold);
                            if meter_service.check_low_energy(&response, meter.low_energy_threshold) {
                                if let Some(data) = &response.data {
                                    log::warn!("电表 {} 电量过低: {} kWh，触发警告通知", meter.name, data.shengyu);
                                } else {
                                    log::warn!("电表 {} 电量过低: 数据为空，触发警告通知", meter.name);
                                }
                                
                                // 创建数据库记录对象用于警告
                                let alert_record = if let Some(data) = &response.data {
                                    crate::models::DatabaseRecord {
                                        id: Some(record_id),
                                        record_time: now_beijing(),
                                        remaining_energy: data.shengyu.parse::<f64>().unwrap_or(0.0),
                                        total_consumption: data.leiji.parse::<f64>().unwrap_or(0.0),
                                        actual_consumption: data.leiji.parse::<f64>().unwrap_or(0.0),
                                        price: data.price.parse::<f64>().unwrap_or(0.0),
                                        meter_status: data.zhuangtai.clone(),
                                        system_time: now_beijing(),
                                    }
                                } else {
                                    // 如果data为空，创建默认记录
                                    crate::models::DatabaseRecord {
                                        id: Some(record_id),
                                        record_time: now_beijing(),
                                        remaining_energy: 0.0,
                                        total_consumption: 0.0,
                                        actual_consumption: 0.0,
                                        price: 0.0,
                                        meter_status: "数据为空".to_string(),
                                        system_time: now_beijing(),
                                    }
                                };
                                
                                // 发送低电量警告
                                let email_notifier_clone = email_notifier.clone();
                                let qq_bot_clone = qq_bot.clone();
                                let meter_clone = meter.clone();
                                let alert_record_clone = alert_record.clone();
                                
                                // 检查是否应该发送邮件警告（基于max_warning配置）
                                let should_send_email = warning_counter.can_send_warning(
                                    &meter.name, 
                                    app_config.warning_config.max_warning
                                );
                                
                                tokio::spawn(async move {
                                    // 只有在应该发送邮件时才发送邮件警告
                                    if should_send_email {
                                        if let Err(e) = email_notifier_clone.send_low_energy_alert(
                                            &meter_clone, 
                                            &alert_record_clone
                                        ) {
                                            log::error!("发送低电量邮件警告失败: {}", e);
                                        }
                                    }
                                    
                                    // 使用新的方法发送低电量QQ群消息（带30秒间隔限制）
                                    if let Err(e) = qq_bot_clone.send_low_energy_alert_to_group(
                                        &meter_clone, 
                                        &alert_record_clone
                                    ).await {
                                        log::error!("发送低电量QQ群警告失败: {}", e);
                                    }
                                });
                            }
                        }
                        Err(e) => {
                            log::error!("❌ 电表 {} 数据保存失败: {}", meter.name, e);
                        }
                    }
                    } else {
                        log::warn!("📭 电表 {} 未启用数据库存储，跳过数据保存", meter.name);
                    }
                }
                Err(e) => {
                    error_count += 1;
                    log::error!("❌ 电表 {} 查询失败: {}", meter.name, e);
                    
                    // 发送错误通知给receive_error_qq
                    let qq_bot_clone = qq_bot.clone();
                    let meter_name = meter.name.clone();
                    let error_msg = format!("电表 {} 查询失败: {}", meter.name, e);
                    
                    tokio::spawn(async move {
                        if let Err(e) = qq_bot_clone.send_error_notification(&meter_name, &error_msg).await {
                            log::error!("发送错误通知失败: {}", e);
                        }
                    });
                }
            }
        }
        
        // 显示本轮监控结果统计
        log::info!("📊 第 {} 轮监控结果: 成功 {} 个，失败 {} 个", loop_count, success_count, error_count);
        
        // 更新Web内容（如果启用）
        if app_config.web_config.enable_generate_web {
            log::info!("🌐 更新Web内容...");
            if let Err(e) = update_web_content(&web_server, &app_config.meters, &latest_data, &db_managers).await {
                log::error!("❌ Web内容更新失败: {}", e);
            } else {
                log::info!("✅ Web内容更新成功");
            }
        }
        
        // 保存网页到文件（如果启用）
        if app_config.web_config.enable_output_web {
            log::info!("💾 保存网页文件...");
            if let Err(e) = web_server.save_to_files() {
                log::error!("❌ 保存网页文件失败: {}", e);
            } else {
                log::info!("✅ 网页文件保存成功");
            }
        }
        
        log::info!("⏰ 第 {} 轮监控完成，等待 {} 分钟后继续...", loop_count, app_config.loop_config.interval_minutes);
        
        // 等待下一次循环
        sleep(Duration::from_secs(app_config.loop_config.interval_minutes * 60)).await;
    }
    
    log::info!("🛑 电表监控系统已停止");
    log::info!("📋 总监控轮次: {}", loop_count);
    Ok(())
}

async fn update_web_content(
    web_server: &WebServer,
    meters: &[crate::models::MeterConfig],
    latest_data: &HashMap<String, crate::models::DatabaseRecord>,
    db_managers: &HashMap<String, Arc<DatabaseManager>>,
) -> Result<(), Box<dyn std::error::Error>> {
    let mut web_content = HashMap::new();
    
    // 生成首页
    let index_content = web_server.generate_index_page(meters, latest_data);
    web_content.insert("index".to_string(), index_content);
    
    // 生成每个电表的详情页
    for meter in meters {
        if let Some(data) = latest_data.get(&meter.name) {
            // 使用对应电表的数据库管理器
            if let Some(db_manager) = db_managers.get(&meter.name) {
                if let Ok(statistics) = db_manager.get_statistics(&meter.name, 7) {
                    let detail_content = web_server.generate_detail_page(meter, data, &statistics);
                    web_content.insert(meter.name.clone(), detail_content);
                }
            }
        }
    }
    
    web_server.update_web_content(web_content);
    
    Ok(())
}