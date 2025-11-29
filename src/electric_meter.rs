use reqwest::Client;
use serde_json::json;
use tokio::time::sleep;
use std::time::Duration;
use crate::models::{ElectricResponse, DatabaseRecord};
use crate::database::{DatabaseManager, now_beijing};
use chrono::{DateTime, Utc};

#[derive(Debug, thiserror::Error)]
pub enum ElectricMeterError {
    #[error("HTTP请求失败: {0}")]
    HttpError(String),
    #[error("JSON解析失败: {0}")]
    JsonError(String),
    #[error("API返回错误: {0}")]
    ApiError(String),
    #[error("数据库操作失败: {0}")]
    DatabaseError(String),
}

pub struct ElectricMeterService {
    client: Client,
    api_url: String,
    retry_config: crate::models::RetryConfig,
}

impl ElectricMeterService {
    pub fn new(api_url: String, retry_config: crate::models::RetryConfig) -> Self {
        Self {
            client: Client::new(),
            api_url,
            retry_config,
        }
    }

    pub async fn get_electric_data(
        &self, 
        electric_user_uid: &str
    ) -> Result<ElectricResponse, ElectricMeterError> {
        let mut retry_count = 0;
        
        loop {
            match self._get_electric_data(electric_user_uid).await {
                Ok(response) => {
                    // 检查数据是否有效（不为0）
                    if self.is_valid_data(&response) {
                        return Ok(response);
                    } else {
                        retry_count += 1;
                        if retry_count >= self.retry_config.max_retry_count {
                            return Err(ElectricMeterError::ApiError(
                                "数据无效且已达到最大重试次数".to_string()
                            ));
                        }
                        log::warn!(
                            "第{}次重试获取电表数据，UID: {}", 
                            retry_count, 
                            electric_user_uid
                        );
                        sleep(Duration::from_secs(10)).await; // 等待10秒后重试
                    }
                }
                Err(e) => {
                    retry_count += 1;
                    if retry_count >= self.retry_config.max_retry_count {
                        return Err(e);
                    }
                    log::warn!(
                        "第{}次重试获取电表数据，UID: {}，错误: {}", 
                        retry_count, 
                        electric_user_uid,
                        e
                    );
                    sleep(Duration::from_secs(10)).await; // 等待10秒后重试
                }
            }
        }
    }

    async fn _get_electric_data(
        &self, 
        electric_user_uid: &str
    ) -> Result<ElectricResponse, ElectricMeterError> {
        let payload = format!("electricUserUid={}", electric_user_uid);

        let response = self.client
            .post(&self.api_url)
            .header("Content-Type", "application/x-www-form-urlencoded")
            .body(payload)
            .send()
            .await
            .map_err(|e| ElectricMeterError::HttpError(e.to_string()))?;

        if !response.status().is_success() {
            return Err(ElectricMeterError::HttpError(
                format!("HTTP错误: {}", response.status())
            ));
        }

        let response_text = response.text()
            .await
            .map_err(|e| ElectricMeterError::HttpError(e.to_string()))?;

        let electric_response: ElectricResponse = serde_json::from_str(&response_text)
            .map_err(|e| ElectricMeterError::JsonError(e.to_string()))?;

        if electric_response.code != 200 {
            return Err(ElectricMeterError::ApiError(
                format!("API错误: {} - {}", electric_response.code, electric_response.msg)
            ));
        }

        Ok(electric_response)
    }

    fn is_valid_data(&self, response: &ElectricResponse) -> bool {
        // 检查data字段是否存在且有效
        if let Some(data) = &response.data {
            // 检查剩余电量和累计电量是否有效（不为0或空）
            !data.shengyu.is_empty() 
                && data.shengyu != "0" 
                && data.shengyu != "0.00"
                && !data.leiji.is_empty()
                && data.leiji != "0"
                && data.leiji != "0.00"
        } else {
            false
        }
    }

    pub fn save_to_database(
        &self, 
        response: &ElectricResponse, 
        db_manager: &DatabaseManager,
        meter_name: &str
    ) -> Result<i64, ElectricMeterError> {
        if let Some(data) = &response.data {
            // 获取当前时间作为记录时间（因为API不返回时间信息）
            let current_time = now_beijing();
            
            // 解析电量数据
            let remaining_energy = data.shengyu.parse::<f64>()
                .map_err(|e| ElectricMeterError::ApiError(format!("解析剩余电量失败: {}", e)))?;
            let total_consumption = data.leiji.parse::<f64>()
                .map_err(|e| ElectricMeterError::ApiError(format!("解析累计电量失败: {}", e)))?;
            
            // 计算实际用电量（需要从数据库获取上一条记录来计算差值）
            let actual_consumption = match db_manager.get_latest_record(meter_name) {
                Ok(Some(prev_record)) => {
                    // 计算与上一条记录的差值
                    let consumption_diff = total_consumption - prev_record.total_consumption;
                    // 确保实际用电量不为负数
                    if consumption_diff >= 0.0 {
                        consumption_diff
                    } else {
                        0.0  // 如果出现负数，可能是电表重置，设为0
                    }
                }
                Ok(None) => {
                    // 没有历史记录，实际用电量为0（这是第一条记录）
                    0.0
                }
                Err(_) => {
                    // 数据库查询失败，实际用电量为0
                    0.0
                }
            };
            
            let record = DatabaseRecord {
                id: None,
                record_time: current_time,
                remaining_energy,
                total_consumption,
                actual_consumption,
                price: data.price.parse::<f64>()
                    .map_err(|e| ElectricMeterError::ApiError(format!("解析电价失败: {}", e)))?,
                meter_status: data.zhuangtai.clone(),
                system_time: current_time,
            };

            db_manager.insert_electric_record(meter_name, &record)
                .map_err(|e| ElectricMeterError::DatabaseError(e.to_string()))
        } else {
            Err(ElectricMeterError::ApiError("API返回数据为空".to_string()))
        }
    }

    pub fn check_low_energy(
        &self, 
        response: &ElectricResponse, 
        threshold: f64
    ) -> bool {
        log::debug!("开始检查低电量，阈值: {} kWh", threshold);
        
        if let Some(data) = &response.data {
            log::debug!("API返回数据: 剩余电量='{}', 累计电量='{}', 状态='{}'", 
                data.shengyu, data.leiji, data.zhuangtai);
            
            if let Ok(remaining_energy) = data.shengyu.parse::<f64>() {
                log::debug!("解析剩余电量成功: {} kWh", remaining_energy);
                
                let is_low_energy = remaining_energy <= threshold && remaining_energy > 0.0;
                
                if is_low_energy {
                    log::warn!("检测到低电量: {} kWh <= 阈值 {} kWh", remaining_energy, threshold);
                } else {
                    log::debug!("电量正常: {} kWh > 阈值 {} kWh", remaining_energy, threshold);
                }
                
                is_low_energy
            } else {
                log::error!("解析剩余电量失败: '{}'", data.shengyu);
                false
            }
        } else {
            log::warn!("API返回数据为空");
            false
        }
    }

    pub fn format_electric_info(&self, response: &ElectricResponse, meter_name: &str) -> String {
        if let Some(data) = &response.data {
            format!(
                "电表名称: {}\n剩余电量: {} 度\n累计电量: {} 度\n电价: {} 元/度\n电表状态: {}\n查询时间: {}",
                meter_name,
                data.shengyu,
                data.leiji,
                data.price,
                data.zhuangtai,
                now_beijing().format("%Y-%m-%d %H:%M:%S")
            )
        } else {
            format!(
                "电表名称: {}\nAPI返回数据为空\n查询时间: {}",
                meter_name,
                now_beijing().format("%Y-%m-%d %H:%M:%S")
            )
        }
    }
}