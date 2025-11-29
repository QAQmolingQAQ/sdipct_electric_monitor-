use crate::models::DatabaseRecord;
use chrono::{DateTime, FixedOffset, TimeZone, Utc};
use rusqlite::{params, Connection, Result};

/// 获取+8时区（北京时间）的当前时间
pub fn now_beijing() -> DateTime<FixedOffset> {
    let beijing_offset = FixedOffset::east_opt(8 * 3600).unwrap();
    beijing_offset.from_utc_datetime(&Utc::now().naive_utc())
}

/// 将DateTime转换为北京时间字符串格式：年-月-日 时:分:秒
fn format_beijing_time(dt: &DateTime<FixedOffset>) -> String {
    dt.format("%Y-%m-%d %H:%M:%S").to_string()
}

/// 从北京时间字符串解析DateTime
fn parse_beijing_time(time_str: &str) -> Result<DateTime<FixedOffset>, chrono::ParseError> {
    let beijing_offset = FixedOffset::east_opt(8 * 3600).unwrap();
    DateTime::parse_from_str(time_str, "%Y-%m-%d %H:%M:%S")
        .map(|dt| dt.with_timezone(&beijing_offset))
}

#[derive(Debug, thiserror::Error)]
pub enum DatabaseError {
    #[error("数据库连接失败: {0}")]
    ConnectionError(String),
    #[error("数据库操作失败: {0}")]
    OperationError(String),
}

pub struct DatabaseManager {
    db_name: String,
}

impl DatabaseManager {
    pub fn new(db_name: &str) -> Self {
        Self {
            db_name: db_name.to_string(),
        }
    }

    pub fn init(&self) -> Result<(), DatabaseError> {
        let conn = Connection::open(&self.db_name)
            .map_err(|e| DatabaseError::ConnectionError(e.to_string()))?;

        // 根据数据库文件名判断数据库类型，只创建相关的表
        if self.db_name.contains("electric_data") {
            // 电表数据库：只创建电表记录表
            conn.execute(
                "CREATE TABLE IF NOT EXISTS electric_records (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    meter_name TEXT NOT NULL,
                    record_time TEXT NOT NULL,
                    remaining_energy REAL,
                    total_consumption REAL,
                    actual_consumption REAL,
                    price REAL,
                    meter_status TEXT,
                    system_time TEXT NOT NULL
                )",
                [],
            )
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;
        } else if self.db_name.contains("qq_bot") {
            // QQ机器人数据库：只创建QQ消息记录表
            conn.execute(
                "CREATE TABLE IF NOT EXISTS qq_message_records (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    message_id INTEGER UNIQUE NOT NULL,
                    processed_time TEXT NOT NULL,
                    keyword TEXT,
                    response_sent BOOLEAN DEFAULT FALSE
                )",
                [],
            )
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;
        }

        Ok(())
    }

    pub fn insert_electric_record(
        &self,
        meter_name: &str,
        record: &DatabaseRecord,
    ) -> Result<i64, DatabaseError> {
        let conn = Connection::open(&self.db_name)
            .map_err(|e| DatabaseError::ConnectionError(e.to_string()))?;

        let mut stmt = conn.prepare(
            "INSERT INTO electric_records 
            (meter_name, record_time, remaining_energy, total_consumption, actual_consumption, price, meter_status, system_time)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
        ).map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        // 使用传入记录中的时间，而不是当前时间
        let record_time_str = format_beijing_time(&record.record_time);
        let system_time_str = format_beijing_time(&record.system_time);

        let record_id = stmt
            .insert(params![
                meter_name,
                record_time_str,
                record.remaining_energy,
                record.total_consumption,
                record.actual_consumption,
                record.price,
                record.meter_status,
                system_time_str
            ])
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        Ok(record_id)
    }

    pub fn get_latest_record(
        &self,
        meter_name: &str,
    ) -> Result<Option<DatabaseRecord>, DatabaseError> {
        let conn = Connection::open(&self.db_name)
            .map_err(|e| DatabaseError::ConnectionError(e.to_string()))?;

        let mut stmt = conn.prepare(
            "SELECT id, record_time, remaining_energy, total_consumption, actual_consumption, price, meter_status, system_time
             FROM electric_records 
             WHERE meter_name = ?
             ORDER BY id DESC LIMIT 1"
        ).map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        let record = stmt.query_row(params![meter_name], |row| {
            let record_time: String = row.get(1)?;
            let system_time: String = row.get(7)?;

            Ok(DatabaseRecord {
                id: Some(row.get(0)?),
                record_time: parse_beijing_time(&record_time).map_err(|e| {
                    rusqlite::Error::InvalidParameterName(format!("Invalid datetime: {}", e))
                })?,
                remaining_energy: row.get(2)?,
                total_consumption: row.get(3)?,
                actual_consumption: row.get(4)?,
                price: row.get(5)?,
                meter_status: row.get(6)?,
                system_time: parse_beijing_time(&system_time).map_err(|e| {
                    rusqlite::Error::InvalidParameterName(format!("Invalid datetime: {}", e))
                })?,
            })
        });

        match record {
            Ok(record) => Ok(Some(record)),
            Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
            Err(e) => Err(DatabaseError::OperationError(e.to_string())),
        }
    }

    pub fn is_qq_message_processed(&self, message_id: u64) -> Result<bool, DatabaseError> {
        // 只在QQ机器人数据库中可用
        if !self.db_name.contains("qq_bot") {
            return Ok(false);
        }

        let conn = Connection::open(&self.db_name)
            .map_err(|e| DatabaseError::ConnectionError(e.to_string()))?;

        let mut stmt = conn
            .prepare("SELECT 1 FROM qq_message_records WHERE message_id = ?")
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        let result = stmt
            .exists(params![message_id])
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        Ok(result)
    }

    pub fn mark_qq_message_processed(
        &self,
        message_id: u64,
        keyword: Option<&str>,
    ) -> Result<(), DatabaseError> {
        // 只在QQ机器人数据库中可用
        if !self.db_name.contains("qq_bot") {
            return Ok(());
        }

        let conn = Connection::open(&self.db_name)
            .map_err(|e| DatabaseError::ConnectionError(e.to_string()))?;

        let mut stmt = conn
            .prepare(
                "INSERT OR IGNORE INTO qq_message_records 
            (message_id, processed_time, keyword, response_sent)
            VALUES (?, ?, ?, ?)",
            )
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        stmt.insert(params![
            message_id,
            format_beijing_time(&now_beijing()),
            keyword.unwrap_or(""),
            true
        ])
        .map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        Ok(())
    }

    /// 检查相同内容的消息是否在短时间内已处理（30秒内）
    pub fn is_content_recently_processed(&self, content_hash: &str) -> Result<bool, DatabaseError> {
        // 只在QQ机器人数据库中可用
        if !self.db_name.contains("qq_bot") {
            return Ok(false);
        }

        let conn = Connection::open(&self.db_name)
            .map_err(|e| DatabaseError::ConnectionError(e.to_string()))?;

        let mut stmt = conn
            .prepare(
                "SELECT 1 FROM qq_message_records 
             WHERE keyword LIKE ? 
             AND datetime(processed_time) > datetime('now', '-30 seconds')",
            )
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        let search_pattern = format!("%{}%", content_hash);
        let result = stmt
            .exists(params![search_pattern])
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        Ok(result)
    }

    pub fn get_statistics(&self, meter_name: &str, days: i64) -> Result<Statistics, DatabaseError> {
        let conn = Connection::open(&self.db_name)
            .map_err(|e| DatabaseError::ConnectionError(e.to_string()))?;

        // 获取所有可用记录，按时间排序，并过滤掉无效数据（剩余电量和用电量为0的记录）
        let mut stmt = conn
            .prepare(
                "SELECT remaining_energy, total_consumption, price, record_time
             FROM electric_records 
             WHERE meter_name = ?
             ORDER BY record_time ASC",
            )
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        let rows = stmt
            .query_map(params![meter_name], |row| {
                Ok((
                    row.get::<_, f64>(0)?,
                    row.get::<_, f64>(1)?,
                    row.get::<_, f64>(2)?,
                    row.get::<_, String>(3)?,
                ))
            })
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        let all_records: Vec<(f64, f64, f64, String)> = rows
            .collect::<Result<Vec<_>, _>>()
            .map_err(|e| DatabaseError::OperationError(e.to_string()))?;

        // 过滤掉无效数据（剩余电量和用电量都为0的记录，通常是查询失败的数据）
        let valid_records: Vec<(f64, f64, f64, String)> = all_records
            .into_iter()
            .filter(|record| record.0 > 0.0 || record.1 > 0.0) // 至少有一个数据不为0
            .collect();

        if valid_records.is_empty() {
            return Ok(Statistics::default());
        }

        // 智能选择数据范围：如果数据量少，使用所有数据；如果数据量足够一周，按天和周分开计算
        let (daily_avg, weekly_avg) = self.calculate_smart_averages(&valid_records);

        let last_record = &valid_records[valid_records.len() - 1];
        let current_energy = last_record.0;
        let estimated_days = if daily_avg > 0.0 {
            current_energy / daily_avg
        } else {
            0.0
        };

        let total_cost = last_record.1 * last_record.2;

        Ok(Statistics {
            daily_avg_consumption: (daily_avg * 100.0).round() / 100.0,
            weekly_avg_consumption: (weekly_avg * 100.0).round() / 100.0,
            estimated_days_left: (estimated_days * 10.0).round() / 10.0,
            total_cost: (total_cost * 100.0).round() / 100.0,
        })
    }

    fn calculate_smart_averages(&self, records: &[(f64, f64, f64, String)]) -> (f64, f64) {
        if records.len() < 2 {
            return (0.0, 0.0);
        }

        // 计算数据覆盖的时间范围
        let first_time = parse_beijing_time(&records[0].3).unwrap_or(now_beijing());
        let last_time = parse_beijing_time(&records[records.len() - 1].3).unwrap_or(now_beijing());
        let total_hours = (last_time - first_time).num_hours() as f64;
        let total_days = total_hours / 24.0;

        // 如果数据量少（少于一周的数据），使用所有数据计算
        if total_days < 7.0 || records.len() < 24 {
            let total_consumption_diff = records[records.len() - 1].1 - records[0].1;
            let daily_avg = if total_days > 0.0 {
                total_consumption_diff / total_days
            } else {
                total_consumption_diff
            };
            return (daily_avg, daily_avg * 7.0);
        }

        // 数据量足够，按天和周分开计算
        let (daily_avg, weekly_avg) = self.calculate_separate_averages(records);
        (daily_avg, weekly_avg)
    }

    fn calculate_separate_averages(&self, records: &[(f64, f64, f64, String)]) -> (f64, f64) {
        let now = now_beijing();

        // 计算最近7天的数据（周均）
        let week_start = now - chrono::Duration::days(7);

        let week_records: Vec<&(f64, f64, f64, String)> = records
            .iter()
            .filter(|record| {
                parse_beijing_time(&record.3)
                    .map(|time| time >= week_start)
                    .unwrap_or(false)
            })
            .collect();

        let weekly_avg = if week_records.len() >= 2 {
            let first_week = week_records[0];
            let last_week = week_records[week_records.len() - 1];
            let week_consumption_diff = last_week.1 - first_week.1;
            let week_days = 7.0; // 7天
            week_consumption_diff / week_days
        } else {
            0.0
        };

        // 计算最近1天的数据（日均）
        let day_start = now - chrono::Duration::days(1);

        let day_records: Vec<&(f64, f64, f64, String)> = records
            .iter()
            .filter(|record| {
                parse_beijing_time(&record.3)
                    .map(|time| time >= day_start)
                    .unwrap_or(false)
            })
            .collect();

        let daily_avg = if day_records.len() >= 2 {
            let first_day = day_records[0];
            let last_day = day_records[day_records.len() - 1];
            let day_consumption_diff = last_day.1 - first_day.1;
            let day_days = 1.0; // 1天
            day_consumption_diff / day_days
        } else {
            // 如果一天内数据不足，使用周均/7作为日均
            weekly_avg / 7.0
        };

        // 如果计算出的日均用电量非常小（小于0.01度），则使用更长时间范围的数据重新计算
        if daily_avg < 0.01 && weekly_avg < 0.01 {
            // 使用最近30天的数据计算平均值
            let month_start = now - chrono::Duration::days(30);
            let month_records: Vec<&(f64, f64, f64, String)> = records
                .iter()
                .filter(|record| {
                    parse_beijing_time(&record.3)
                        .map(|time| time >= month_start)
                        .unwrap_or(false)
                })
                .collect();

            if month_records.len() >= 2 {
                let first_month = month_records[0];
                let last_month = month_records[month_records.len() - 1];
                let month_consumption_diff = last_month.1 - first_month.1;
                let month_days = 30.0;
                let month_avg = month_consumption_diff / month_days;
                
                // 如果月均用电量仍然很小，使用所有数据计算
                if month_avg < 0.01 {
                    let total_consumption_diff = records[records.len() - 1].1 - records[0].1;
                    let total_days = (parse_beijing_time(&records[records.len() - 1].3).unwrap_or(now) 
                        - parse_beijing_time(&records[0].3).unwrap_or(now)).num_days() as f64;
                    
                    if total_days > 0.0 {
                        let total_avg = total_consumption_diff / total_days;
                        return (total_avg, total_avg * 7.0);
                    }
                }
                
                return (month_avg, month_avg * 7.0);
            }
        }

        (daily_avg, weekly_avg)
    }
}

#[derive(Debug, Clone, Default)]
pub struct Statistics {
    pub daily_avg_consumption: f64,
    pub weekly_avg_consumption: f64,
    pub estimated_days_left: f64,
    pub total_cost: f64,
}
