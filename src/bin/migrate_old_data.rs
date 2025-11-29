use rusqlite::{Connection, Result};
use std::path::Path;

fn main() -> Result<()> {
    println!("开始迁移旧版数据库数据...");
    
    // 打开旧版数据库
    let old_db_path = Path::new("olddb/electric_data.db");
    let conn_old = Connection::open(&old_db_path)?;
    
    // 打开新版数据库
    let new_db_path = Path::new("electric_data.db");
    let conn_new = Connection::open(&new_db_path)?;
    
    // 检查旧版数据库中的数据
    let mut stmt_old = conn_old.prepare("SELECT id, \"\", remaining_energy, remaining_amount, total_consumption, price, meter_status, meter_update_time, system_time FROM electric_data ORDER BY id")?;
    
    let rows = stmt_old.query_map([], |row| {
        Ok((
            row.get::<_, i64>(0)?,     // id
            row.get::<_, Option<String>>(1)?, // 空列
            row.get::<_, f64>(2)?,     // remaining_energy
            row.get::<_, f64>(3)?,     // remaining_amount
            row.get::<_, f64>(4)?,     // total_consumption
            row.get::<_, f64>(5)?,     // price
            row.get::<_, Option<String>>(6)?, // meter_status
            row.get::<_, Option<String>>(7)?, // meter_update_time
            row.get::<_, Option<String>>(8)?, // system_time
        ))
    })?;
    
    let mut count = 0;
    
    for row in rows {
        let result: (i64, Option<String>, f64, f64, f64, f64, Option<String>, Option<String>, Option<String>) = row?;
        let (id, _, remaining_energy, remaining_amount, total_consumption, price, meter_status, meter_update_time, system_time) = result;
        
        // 确定使用哪个时间字段
        let record_time = if let Some(ref time) = meter_update_time {
            time.clone()
        } else if let Some(ref time) = system_time {
            time.clone()
        } else {
            continue; // 没有时间信息，跳过
        };
        
        // 计算实际消耗量（新版本需要这个字段）
        // 由于旧版没有存储实际消耗量，我们使用累计电量作为近似值
        let actual_consumption = total_consumption;
        
        // 插入到新版数据库
        let mut stmt_new = conn_new.prepare(
            "INSERT OR IGNORE INTO electric_records 
            (meter_name, record_time, remaining_energy, total_consumption, actual_consumption, price, meter_status, system_time)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
        )?;
        
        stmt_new.execute([
            "主电表",           // meter_name
            &record_time,        // record_time
            &remaining_energy.to_string(), // remaining_energy
            &total_consumption.to_string(), // total_consumption
            &actual_consumption.to_string(), // actual_consumption
            &price.to_string(),  // price
            &meter_status.unwrap_or_else(|| "合闸".to_string()), // meter_status
            &record_time,        // system_time（使用相同时间）
        ])?;
        
        count += 1;
        
        if count % 100 == 0 {
            println!("已迁移 {} 条记录...", count);
        }
    }
    
    println!("数据迁移完成！共迁移 {} 条记录", count);
    
    // 验证迁移结果
    let mut stmt_count = conn_new.prepare("SELECT COUNT(*) FROM electric_records")?;
    let new_count: i64 = stmt_count.query_row([], |row| row.get(0))?;
    
    println!("新版数据库中共有 {} 条记录", new_count);
    
    // 显示一些迁移后的数据
    println!("\n迁移后的前5条记录：");
    let mut stmt_sample = conn_new.prepare("SELECT id, meter_name, record_time, remaining_energy, total_consumption, meter_status FROM electric_records ORDER BY id LIMIT 5")?;
    
    let sample_rows = stmt_sample.query_map([], |row| {
        Ok((
            row.get::<_, i64>(0)?,
            row.get::<_, String>(1)?,
            row.get::<_, String>(2)?,
            row.get::<_, f64>(3)?,
            row.get::<_, f64>(4)?,
            row.get::<_, String>(5)?,
        ))
    })?;
    
    for row in sample_rows {
        let (id, meter_name, record_time, remaining_energy, total_consumption, meter_status) = row?;
        println!("ID: {}, 电表: {}, 时间: {}, 剩余电量: {:.2} kWh, 累计电量: {:.2} kWh, 状态: {}", 
                 id, meter_name, record_time, remaining_energy, total_consumption, meter_status);
    }
    
    Ok(())
}