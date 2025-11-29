use serde::{Deserialize, Serialize};
use chrono::{DateTime, Utc, FixedOffset};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ElectricData {
    pub zhuangtai2: String,
    pub querycode: String,
    pub zxzhuangtai2: String,
    pub shengyu: String,
    pub zhuangtai3: String,
    pub zxzhuangtai: String,
    pub price: String,
    pub zhuangtai: String,
    pub leiji: String,
    pub zxzhuangtai3: String,
    pub querymsg: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ElectricResponse {
    pub msg: String,
    pub code: i32,
    pub data: Option<ElectricData>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DatabaseRecord {
    pub id: Option<i64>,
    pub record_time: DateTime<FixedOffset>,
    pub remaining_energy: f64,
    pub total_consumption: f64,
    pub actual_consumption: f64,
    pub price: f64,
    pub meter_status: String,
    pub system_time: DateTime<FixedOffset>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MeterConfig {
    pub enabled: bool,
    pub name: String,
    pub electric_user_uid: String,
    pub consumption_correction: f64,
    pub db_enable: bool,
    pub db_name: String,
    pub low_energy_threshold: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoopConfig {
    pub enabled: bool,
    pub interval_minutes: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RetryConfig {
    pub max_retry_count: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WebConfig {
    pub enable_generate_web: bool,
    pub enable_http_server: bool,
    pub http_port: u16,
    pub enable_output_web: bool,
    pub output_web_path: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WarningConfig {
    pub enabled: bool,
    pub max_warning: i32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EmailConfig {
    pub enabled: bool,
    pub smtp_server: String,
    pub smtp_port: u16,
    pub email_account: String,
    pub email_auth_code: String,
    pub email_receivers: Vec<String>,
    pub receive_error_email: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NapcatConfig {
    pub enabled: bool,
    pub api_url: String,
    pub api_port: u16,
    pub token: String,
    pub qq_group_id: u64,
    pub receive_error_qq_id: u64,
    pub query_keywords: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppConfig {
    pub url: String,
    pub meters: Vec<MeterConfig>,
    pub loop_config: LoopConfig,
    pub retry_config: RetryConfig,
    pub web_config: WebConfig,
    pub warning_config: WarningConfig,
    pub email_config: EmailConfig,
    pub napcat_config: NapcatConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct QQMessage {
    pub self_id: u64,
    pub user_id: u64,
    pub time: u64,
    pub message_id: u64,
    pub message_seq: u64,
    pub real_id: u64,
    pub real_seq: String,
    pub message_type: String,
    pub sender: serde_json::Value,
    pub raw_message: String,
    pub font: u32,
    pub sub_type: String,
    pub message: Vec<serde_json::Value>,
    pub message_format: String,
    pub post_type: String,
    pub group_id: u64,
    pub group_name: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct QQResponse {
    pub status: String,
    pub retcode: i32,
    pub data: QQResponseData,
    pub message: String,
    pub wording: String,
    pub echo: String,
    pub stream: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct QQResponseData {
    pub messages: Vec<QQMessage>,
}