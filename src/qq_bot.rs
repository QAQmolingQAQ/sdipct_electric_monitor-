use crate::database::DatabaseManager;
use crate::models::{DatabaseRecord, MeterConfig, NapcatConfig};
use reqwest::Client;
use serde_json::json;
use std::collections::HashSet;
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

#[derive(Debug, Clone)]
pub struct QQMessage {
    pub message_id: u64,
    pub user_id: u64,
    pub group_id: u64,
    pub message: String,
    pub time: u64,
}

pub struct QQBot {
    config: NapcatConfig,
    client: Client,
    keywords: HashSet<String>,
    last_send_time: Arc<Mutex<SystemTime>>,
}

impl QQBot {
    pub fn new(config: NapcatConfig) -> Self {
        let keywords: HashSet<String> = config
            .query_keywords
            .iter()
            .map(|s| s.trim().to_lowercase())
            .collect();

        Self {
            config,
            client: Client::new(),
            keywords,
            last_send_time: Arc::new(Mutex::new(SystemTime::now())),
        }
    }

    pub async fn send_low_energy_alert(
        &self,
        meter: &MeterConfig,
        data: &DatabaseRecord,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        if !self.config.enabled {
            return Ok(());
        }

        let message = format!(
            "⚠️ 电表低电量警告\n\n电表名称: {}\n剩余电量: {:.2} kWh\n累计用电量: {:.3} kWh\n电表状态: {}\n警告阈值: {:.1} kWh\n\n请及时充值电费！",
            meter.name,
            data.remaining_energy,
            data.total_consumption,
            data.meter_status,
            meter.low_energy_threshold
        );

        self.send_group_message(&message).await
    }

    pub async fn send_error_notification(
        &self,
        meter_name: &str,
        error_message: &str,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        if !self.config.enabled {
            return Ok(());
        }

        // 检查发送间隔（30秒）
        if !self.can_send_message() {
            log::info!("发送间隔限制，跳过错误通知");
            return Ok(());
        }

        let message = format!(
            "❌ 电表监控系统错误\n\n电表名称: {}\n错误信息: {}\n\n请检查系统状态！",
            meter_name, error_message
        );

        // 发送给特定用户（receive_error_qq_id）
        self.send_private_message(&message).await
    }

    pub async fn send_low_energy_alert_to_group(
        &self,
        meter: &MeterConfig,
        data: &DatabaseRecord,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        if !self.config.enabled {
            return Ok(());
        }

        // 低电量警告不受30秒间隔限制，可以立即发送
        log::debug!("发送低电量警告到QQ群，不受间隔限制");

        // 应用矫正值计算实际用电量
        let actual_consumption = data.total_consumption + meter.consumption_correction;

        let message = format!(
            "⚠️ 电表低电量警告\n\n电表名称: {}\n剩余电量: {:.2} kWh\n实际用电量: {:.3} kWh\n电表状态: {}\n警告阈值: {:.1} kWh\n\n请及时充值电费！",
            meter.name,
            data.remaining_energy,
            actual_consumption,
            data.meter_status,
            meter.low_energy_threshold
        );

        self.send_group_message(&message).await
    }

    /// 检查是否可以发送消息（30秒间隔限制）
    fn can_send_message(&self) -> bool {
        let mut last_send_time = self.last_send_time.lock().unwrap();
        let now = SystemTime::now();

        if let Ok(duration) = now.duration_since(*last_send_time) {
            if duration.as_secs() < 30 {
                return false;
            }
        }

        // 只有在实际发送消息时才更新时间
        true
    }

    /// 更新最后发送时间（在成功发送消息后调用）
    fn update_last_send_time(&self) {
        let mut last_send_time = self.last_send_time.lock().unwrap();
        *last_send_time = SystemTime::now();
    }

    /// 发送私聊消息给特定用户
    async fn send_private_message(
        &self,
        message: &str,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        if self.config.receive_error_qq_id == 0 {
            log::warn!("未配置receive_error_qq_id，跳过私聊消息发送");
            return Ok(());
        }

        let url = format!(
            "{}:{}/send_private_msg",
            self.config.api_url, self.config.api_port
        );

        let body = json!({
            "user_id": self.config.receive_error_qq_id,
            "message": message
        });

        let response = self
            .client
            .post(&url)
            .header("Authorization", format!("Bearer {}", self.config.token))
            .header("Content-Type", "application/json")
            .json(&body)
            .send()
            .await?;

        if response.status().is_success() {
            log::info!(
                "私聊消息发送成功给用户: {}",
                self.config.receive_error_qq_id
            );
            self.update_last_send_time();
        } else {
            log::error!("私聊消息发送失败: {}", response.status());
        }

        Ok(())
    }

    async fn send_group_message(
        &self,
        message: &str,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        let url = format!(
            "{}:{}/send_group_msg",
            self.config.api_url, self.config.api_port
        );

        let body = json!({
            "group_id": self.config.qq_group_id,
            "message": message
        });

        let response = self
            .client
            .post(&url)
            .header("Authorization", format!("Bearer {}", self.config.token))
            .header("Content-Type", "application/json")
            .json(&body)
            .send()
            .await?;

        if response.status().is_success() {
            log::info!("QQ消息发送成功");
            self.update_last_send_time();
        } else {
            log::error!("QQ消息发送失败: {}", response.status());
        }

        Ok(())
    }

    pub async fn get_group_message_history(
        &self,
    ) -> Result<Vec<QQMessage>, Box<dyn std::error::Error + Send + Sync>> {
        let url = format!(
            "{}:{}/get_group_msg_history",
            self.config.api_url, self.config.api_port
        );

        let body = json!({
            "group_id": self.config.qq_group_id.to_string(),
            "message_seq": "0",
            "count": "10",
            "reverseOrder": "false"
        });

        let response = self
            .client
            .post(&url)
            .header("Authorization", format!("Bearer {}", self.config.token))
            .header("Content-Type", "application/json")
            .json(&body)
            .send()
            .await?;

        let response_text = response.text().await?;
        let response_json: serde_json::Value = serde_json::from_str(&response_text)?;

        if response_json["retcode"].as_i64() != Some(0) {
            return Err(format!(
                "API返回错误: {}",
                response_json["message"].as_str().unwrap_or("未知错误")
            )
            .into());
        }

        let mut messages = Vec::new();
        if let Some(messages_array) = response_json["data"]["messages"].as_array() {
            for msg in messages_array {
                if let Some(message_id) = msg["message_id"].as_u64() {
                    let qq_message = QQMessage {
                        message_id,
                        user_id: msg["user_id"].as_u64().unwrap_or(0),
                        group_id: msg["group_id"].as_u64().unwrap_or(0),
                        message: Self::extract_message_text(&msg),
                        time: msg["time"].as_u64().unwrap_or(0),
                    };
                    messages.push(qq_message);
                }
            }
        }

        Ok(messages)
    }

    fn extract_message_text(msg: &serde_json::Value) -> String {
        if let Some(message_array) = msg["message"].as_array() {
            for item in message_array {
                if item["type"].as_str() == Some("text") {
                    if let Some(text) = item["data"]["text"].as_str() {
                        return text.to_string();
                    }
                }
            }
        }

        if let Some(raw_message) = msg["raw_message"].as_str() {
            return raw_message.to_string();
        }

        String::new()
    }

    pub fn contains_keyword(&self, message: &str) -> bool {
        let message_lower = message.to_lowercase();
        self.keywords
            .iter()
            .any(|keyword| message_lower.contains(keyword))
    }

    pub fn mark_message_processed(
        &self,
        db_manager: &DatabaseManager,
        message_id: u64,
        keyword: Option<&str>,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        db_manager
            .mark_qq_message_processed(message_id, keyword)
            .map_err(|e| e.into())
    }

    pub fn is_message_processed(
        &self,
        db_manager: &DatabaseManager,
        message_id: u64,
    ) -> Result<bool, Box<dyn std::error::Error + Send + Sync>> {
        db_manager
            .is_qq_message_processed(message_id)
            .map_err(|e| e.into())
    }

    pub async fn handle_keyword_query(
        &self,
        db_manager: &DatabaseManager,
        meters: &[MeterConfig],
        message: &QQMessage,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        if !self.config.enabled {
            return Ok(());
        }

        // 检查消息是否已处理
        if self.is_message_processed(db_manager, message.message_id)? {
            log::debug!("消息 {} 已处理，跳过", message.message_id);
            return Ok(());
        }

        // 排除机器人自己发送的消息（防止无限循环）
        // 通过消息内容识别：机器人发送的消息包含特定格式
        if message.message.contains("📊") && message.message.contains("电表信息") {
            log::debug!("消息 {} 是机器人自己发送的，跳过", message.message_id);
            // 标记消息已处理但不发送回复
            self.mark_message_processed(db_manager, message.message_id, Some(&message.message))?;
            return Ok(());
        }

        // 检查是否包含关键词
        if !self.contains_keyword(&message.message) {
            return Ok(());
        }

        log::info!(
            "检测到关键词消息: {} (ID: {})",
            message.message,
            message.message_id
        );

        // 获取最新电表数据
        let mut response_message = String::new();

        for meter in meters {
            if let Ok(Some(latest_record)) = db_manager.get_latest_record(&meter.name) {
                let remaining_cost = latest_record.remaining_energy * latest_record.price;

                response_message.push_str(&format!(
                    "📊 {} 电表信息\n剩余电量: {:.2} kWh\n实际用电: {:.3} kWh\n累计用电: {:.3} kWh\n剩余金额: {:.2} 元\n电表状态: {}\n更新时间: {}\n\n",
                    meter.name,
                    latest_record.remaining_energy,
                    latest_record.actual_consumption,
                    latest_record.total_consumption,
                    remaining_cost,
                    latest_record.meter_status,
                    latest_record.system_time.format("%Y-%m-%d %H:%M:%S")
                ));
            }
        }

        if response_message.is_empty() {
            response_message = "暂无电表数据".to_string();
        }

        // 检查相同内容是否在30秒内已发送过
        let content_hash = format!("{}:{}", message.message, response_message);
        if let Ok(true) = db_manager.is_content_recently_processed(&content_hash) {
            log::info!(
                "相同内容在30秒内已发送过，跳过消息 {} 的处理",
                message.message_id
            );
            // 标记消息已处理但不发送回复
            self.mark_message_processed(db_manager, message.message_id, Some(&message.message))?;
            return Ok(());
        }

        // 检查发送间隔
        if !self.can_send_message() {
            log::info!("发送间隔限制，跳过消息 {} 的处理", message.message_id);
            // 标记消息已处理但不发送回复
            self.mark_message_processed(db_manager, message.message_id, Some(&message.message))?;
            return Ok(());
        }

        // 发送回复消息
        self.send_group_message(&response_message).await?;

        // 更新发送时间戳
        self.update_last_send_time();

        // 标记消息已处理（记录关键词）
        self.mark_message_processed(db_manager, message.message_id, Some(&message.message))?;
        log::info!("已处理消息 {} 并发送回复", message.message_id);

        Ok(())
    }

    pub async fn start_monitoring(
        self: Arc<Self>,
        db_manager: Arc<DatabaseManager>,
        meters: Vec<MeterConfig>,
    ) {
        if !self.config.enabled {
            return;
        }

        log::info!("启动QQ机器人消息监控");

        let self_clone = Arc::clone(&self);
        let db_manager_clone = Arc::clone(&db_manager);
        let meters_clone = meters.clone();

        tokio::spawn(async move {
            let mut service_unavailable_count = 0;
            const MAX_SERVICE_UNAVAILABLE_COUNT: u32 = 5;
            const SERVICE_RETRY_INTERVAL_SECS: u64 = 30;

            loop {
                match self_clone.get_group_message_history().await {
                    Ok(messages) => {
                        // 服务可用，重置计数器
                        service_unavailable_count = 0;

                        for message in messages {
                            // 使用数据库检查消息是否已处理
                            match self_clone
                                .is_message_processed(&db_manager_clone, message.message_id)
                            {
                                Ok(true) => {
                                    continue; // 消息已处理，跳过
                                }
                                Ok(false) => {
                                    // 消息未处理，进行处理
                                    if let Err(e) = self_clone
                                        .handle_keyword_query(
                                            &db_manager_clone,
                                            &meters_clone,
                                            &message,
                                        )
                                        .await
                                    {
                                        log::error!("处理关键词查询失败: {}", e);
                                    }
                                }
                                Err(e) => {
                                    log::error!("检查消息处理状态失败: {}", e);
                                }
                            }
                        }
                    }
                    Err(e) => {
                        service_unavailable_count += 1;

                        if service_unavailable_count == 1 {
                            log::error!("Napcat服务连接失败: {}", e);
                            log::warn!("QQ机器人功能暂时不可用，请检查Napcat服务是否启动");
                        } else if service_unavailable_count >= MAX_SERVICE_UNAVAILABLE_COUNT {
                            log::warn!(
                                "Napcat服务持续不可用，暂停重试 {} 秒",
                                SERVICE_RETRY_INTERVAL_SECS
                            );
                            tokio::time::sleep(Duration::from_secs(SERVICE_RETRY_INTERVAL_SECS))
                                .await;
                            service_unavailable_count = 0; // 重置计数器，重新尝试
                            continue;
                        }
                    }
                }

                tokio::time::sleep(Duration::from_secs(2)).await;
            }
        });
    }

    pub fn is_enabled(&self) -> bool {
        self.config.enabled
    }
}
