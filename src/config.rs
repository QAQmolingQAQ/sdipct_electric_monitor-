use std::fs;
use config::{Config, File, FileFormat};
use serde::Deserialize;
use crate::models::AppConfig;

#[derive(Debug, thiserror::Error)]
pub enum ConfigError {
    #[error("配置文件加载失败: {0}")]
    LoadError(String),
    #[error("配置解析失败: {0}")]
    ParseError(String),
}

pub struct ConfigLoader {
    config_file: String,
}

impl ConfigLoader {
    pub fn new(config_file: &str) -> Self {
        Self {
            config_file: config_file.to_string(),
        }
    }

    pub fn load(&self) -> Result<AppConfig, ConfigError> {
        let config = Config::builder()
            .add_source(File::new(&self.config_file, FileFormat::Toml))
            .build()
            .map_err(|e| ConfigError::LoadError(e.to_string()))?;

        config.try_deserialize::<AppConfig>()
            .map_err(|e| ConfigError::ParseError(e.to_string()))
    }

    pub fn load_from_str(config_content: &str) -> Result<AppConfig, ConfigError> {
        let config = Config::builder()
            .add_source(File::from_str(config_content, FileFormat::Toml))
            .build()
            .map_err(|e| ConfigError::LoadError(e.to_string()))?;

        config.try_deserialize::<AppConfig>()
            .map_err(|e| ConfigError::ParseError(e.to_string()))
    }
}

// 默认配置
impl Default for AppConfig {
    fn default() -> Self {
        Self {
            url: "https://sdxt.sdipct.edu.cn/kddz/electricmeterpost/electricMeterQuery".to_string(),
            meters: vec![
                crate::models::MeterConfig {
                    enabled: true,
                    name: "生活用电".to_string(),
                    electric_user_uid: "7429".to_string(),
                    consumption_correction: 0.0,
                    db_enable: true,
                    db_name: "electric_data.db".to_string(),
                    low_energy_threshold: 10.0,
                },
                crate::models::MeterConfig {
                    enabled: true,
                    name: "空调用电".to_string(),
                    electric_user_uid: "7733".to_string(),
                    consumption_correction: 0.0,
                    db_enable: true,
                    db_name: "electric_data_ac.db".to_string(),
                    low_energy_threshold: 10.0,
                }
            ],
            loop_config: crate::models::LoopConfig {
                enabled: true,
                interval_minutes: 1,
            },
            retry_config: crate::models::RetryConfig {
                max_retry_count: 3,
            },
            web_config: crate::models::WebConfig {
                enable_generate_web: true,
                enable_http_server: true,
                http_port: 8080,
                enable_output_web: true,
                output_web_path: "web".to_string(),
            },
            warning_config: crate::models::WarningConfig {
                enabled: true,
                max_warning: 0,
            },
            email_config: crate::models::EmailConfig {
                enabled: false,
                smtp_server: "smtp.qq.com".to_string(),
                smtp_port: 587,
                email_account: "2242103798@qq.com".to_string(),
                email_auth_code: "dwgxeaepabfqecea".to_string(),
                email_receivers: vec![
                    "1229184410@qq.com".to_string(),
                    "qaqmolingqaq@outlook.com".to_string()
                ],
                receive_error_email: "error-receiver@example.com".to_string(),
            },
            napcat_config: crate::models::NapcatConfig {
                enabled: true,
                api_url: "http://localhost".to_string(),
                api_port: 6090,
                token: "ufwOQQZdeua04yiC2gahAqMNsb3B7SaR".to_string(),
                qq_group_id: 809924774,
                receive_error_qq_id: 1229184410,
                query_keywords: vec![
                    "电量".to_string(),
                    "电费".to_string(),
                    "电表".to_string(),
                    "剩余电量".to_string(),
                    "电费查询".to_string()
                ],
            },
        }
    }
}