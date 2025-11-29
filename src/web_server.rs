use warp::Filter;
use std::collections::HashMap;
use std::sync::{Arc, RwLock};
use crate::models::{MeterConfig, DatabaseRecord};
use crate::database::DatabaseManager;
use chrono::{DateTime, Utc};

pub type WebContent = Arc<RwLock<HashMap<String, String>>>;

pub struct WebServer {
    web_content: WebContent,
    web_config: crate::models::WebConfig,
}

impl WebServer {
    pub fn new(web_config: crate::models::WebConfig) -> Self {
        Self {
            web_content: Arc::new(RwLock::new(HashMap::new())),
            web_config,
        }
    }

    pub fn generate_index_page(
        &self, 
        meters: &[MeterConfig], 
        latest_data: &HashMap<String, DatabaseRecord>
    ) -> String {
        let mut meter_cards = String::new();
        
        for meter in meters {
            if let Some(data) = latest_data.get(&meter.name) {
                let status_class = if data.meter_status == "合闸" {
                    "status-on"
                } else {
                    "status-off"
                };
                
                let remaining_cost = data.remaining_energy * data.price;
                
                // 应用矫正值计算实际用电量
                let actual_consumption = data.total_consumption + meter.consumption_correction;
                
                meter_cards.push_str(&format!(
                    r#"<a href="/{}" class="meter-card-link">
                        <div class="meter-card">
                            <div class="meter-header">
                                <div class="meter-name">{}</div>
                                <div class="meter-status {}">{}</div>
                            </div>
                            <div class="meter-details">
                                <div class="detail-item">
                                    <div class="detail-label">剩余电量</div>
                                    <div class="detail-value">{:.2} kWh</div>
                                </div>
                                <div class="detail-item">
                                    <div class="detail-label">实际用电量</div>
                                    <div class="detail-value">{:.3} kWh</div>
                                </div>
                                <div class="detail-item">
                                    <div class="detail-label">剩余金额</div>
                                    <div class="detail-value">{:.2} 元</div>
                                </div>
                                <div class="detail-item">
                                    <div class="detail-label">电表状态</div>
                                    <div class="detail-value">{}</div>
                                </div>
                            </div>
                            <div class="update-time">更新时间: {}</div>
                        </div>
                    </a>"#,
                    meter.name,
                    meter.name,
                    status_class,
                    data.meter_status,
                    data.remaining_energy,
                    actual_consumption,
                    remaining_cost,
                    data.meter_status,
                    data.system_time.format("%Y-%m-%d %H:%M:%S")
                ));
            }
        }

        let html_content = format!(r#"<!DOCTYPE html><html lang="zh-CN"><head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>电表监控系统</title>
    <style>
        :root {{
            --bg-primary: #f5f5f5;
            --bg-secondary: white;
            --text-primary: #333;
            --text-secondary: #999;
            --border-color: #eee;
            --header-bg: #2c3e50;
            --card-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
            --status-on-bg: #e6f7ed;
            --status-on-color: #13c2c2;
            --status-off-bg: #fff1f0;
            --status-off-color: #ff4d4f;
        }}
        .dark-mode {{
            --bg-primary: #1a1a1a;
            --bg-secondary: #2d2d2d;
            --text-primary: #ffffff;
            --text-secondary: #b0b0b0;
            --border-color: #404040;
            --card-shadow: 0 2px 10px rgba(0, 0, 0, 0.3);
            --status-on-bg: #1a4d4d;
            --status-on-color: #66cccc;
            --status-off-bg: #4d2626;
            --status-off-color: #ff9999;
        }}
        * {{
            transition: background-color 0.3s, color 0.3s;
        }}
        body {{
            font-family: Arial, sans-serif;
            background-color: var(--bg-primary);
            color: var(--text-primary);
            margin: 0;
            padding: 20px;
        }}
        .container {{
            max-width: 1200px;
            margin: 0 auto;
            background: var(--bg-secondary);
            border-radius: 10px;
            padding: 20px;
            box-shadow: var(--card-shadow);
        }}
        .header {{
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 30px;
            padding-bottom: 15px;
            border-bottom: 2px solid var(--border-color);
        }}
        h1 {{
            color: var(--text-primary);
            margin: 0;
        }}
        .theme-toggle {{
            background: rgba(0, 0, 0, 0.1);
            border: none;
            color: var(--text-primary);
            padding: 8px 12px;
            border-radius: 20px;
            cursor: pointer;
            font-size: 14px;
        }}
        .dark-mode .theme-toggle {{
            background: rgba(255, 255, 255, 0.2);
        }}
        .theme-toggle:hover {{
            background: rgba(0, 0, 0, 0.2);
        }}
        .dark-mode .theme-toggle:hover {{
            background: rgba(255, 255, 255, 0.3);
        }}
        .meter-card {{
            background-color: var(--bg-secondary);
            border-radius: 8px;
            box-shadow: var(--card-shadow);
            padding: 20px;
            margin-bottom: 20px;
        }}
        .meter-header {{
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 15px;
        }}
        .meter-name {{
            font-size: 24px;
            font-weight: bold;
            color: var(--text-primary);
        }}
        .meter-status {{
            padding: 5px 15px;
            border-radius: 20px;
            font-size: 14px;
            font-weight: bold;
        }}
        .status-on {{
            background-color: var(--status-on-bg);
            color: var(--status-on-color);
        }}
        .status-off {{
            background-color: var(--status-off-bg);
            color: var(--status-off-color);
        }}
        .meter-details {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
        }}
        .detail-item {{
            text-align: center;
        }}
        .detail-label {{
            font-size: 14px;
            color: var(--text-secondary);
            margin-bottom: 8px;
        }}
        .detail-value {{
            font-size: 24px;
            font-weight: bold;
            color: var(--text-primary);
        }}
        .update-time {{
            text-align: right;
            font-size: 12px;
            color: var(--text-secondary);
            margin-top: 15px;
        }}
        .meter-card-link {{
            text-decoration: none;
            color: inherit;
        }}
        .no-meters {{
            text-align: center;
            padding: 40px;
            color: var(--text-secondary);
        }}
    </style>
</head><body>
    <div class="container">
        <div class="header">
            <h1>电表监控系统</h1>
            <button class="theme-toggle" onclick="toggleTheme()">🌙 暗黑模式</button>
        </div>
        {}
    </div>
    <script>
        function toggleTheme() {{
            document.body.classList.toggle("dark-mode");
            const button = document.querySelector(".theme-toggle");
            if (document.body.classList.contains("dark-mode")) {{
                button.textContent = "☀️ 明亮模式";
                localStorage.setItem("theme", "dark");
            }} else {{
                button.textContent = "🌙 暗黑模式";
                localStorage.setItem("theme", "light");
            }}
        }}

        document.addEventListener("DOMContentLoaded", function () {{
            const savedTheme = localStorage.getItem("theme");
            if (savedTheme === "dark") {{
                document.body.classList.add("dark-mode");
                document.querySelector(".theme-toggle").textContent = "☀️ 明亮模式";
            }}
        }});
    </script>
</body></html>"#, meter_cards);

        html_content
    }

    pub fn generate_detail_page(
        &self, 
        meter: &MeterConfig, 
        data: &DatabaseRecord,
        statistics: &crate::database::Statistics
    ) -> String {
        let status_class = if data.meter_status == "合闸" {
            "status-online"
        } else {
            "status-off"
        };

        // 应用矫正值计算实际用电量
        let actual_consumption = data.total_consumption + meter.consumption_correction;
        
        let html_content = format!(r#"<!DOCTYPE html>
<html lang=\"zh-CN\">
<head>
    <meta charset=\"UTF-8\">
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">
    <title>{0}</title>
    <style>
        :root {{
            --bg-primary: #f5f5f5;
            --bg-secondary: white;
            --text-primary: #2c3e50;
            --text-secondary: #7f8c8d;
            --border-color: #ecf0f1;
            --header-bg: #2c3e50;
            --nav-bg: #34495e;
            --card-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
            --back-button-bg: #3498db;
            --back-button-hover: #2980b9;
        }}

        .dark-mode {{
            --bg-primary: #1a1a1a;
            --bg-secondary: #2d2d2d;
            --text-primary: #ffffff;
            --text-secondary: #b0b0b0;
            --border-color: #404040;
            --header-bg: #1a1a1a;
            --nav-bg: #2d2d2d;
            --card-shadow: 0 2px 10px rgba(0, 0, 0, 0.3);
            --back-button-bg: #2980b9;
            --back-button-hover: #3498db;
        }}

        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            transition: background-color 0.3s, color 0.3s;
        }}

        body {{
            font-family: 'Microsoft YaHei', Arial, sans-serif;
            background: var(--bg-primary);
            color: var(--text-primary);
            min-height: 100vh;
            padding: 20px;
        }}

        .container {{
            max-width: 800px;
            margin: 0 auto;
            background: var(--bg-secondary);
            border-radius: 10px;
            box-shadow: var(--card-shadow);
            overflow: hidden;
        }}

        .header {{
            background: var(--header-bg);
            color: white;
            padding: 20px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
        }}

        .header h1 {{
            font-size: 1.5rem;
            font-weight: normal;
        }}

        .theme-toggle {{
            background: rgba(255, 255, 255, 0.2);
            border: none;
            color: white;
            padding: 8px 15px;
            border-radius: 20px;
            cursor: pointer;
            font-size: 0.9rem;
        }}

        .theme-toggle:hover {{
            background: rgba(255, 255, 255, 0.3);
        }}

        .back-button {{
            display: inline-block;
            background: var(--back-button-bg);
            color: white;
            padding: 10px 20px;
            border-radius: 5px;
            text-decoration: none;
            margin-bottom: 20px;
            font-size: 1rem;
            transition: background-color 0.3s;
        }}

        .back-button:hover {{
            background: var(--back-button-hover);
        }}

        .meter-card {{
            padding: 30px;
        }}

        .meter-header {{
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 25px;
            padding-bottom: 15px;
            border-bottom: 1px solid var(--border-color);
        }}

        .meter-name {{
            font-size: 1.8rem;
            font-weight: bold;
        }}

        .meter-status {{
            padding: 5px 15px;
            border-radius: 15px;
            font-size: 0.9rem;
            font-weight: bold;
        }}

        .status-online {{
            background: #e8f5e9;
            color: #2e7d32;
        }}

        .status-off {{
            background: #ffebee;
            color: #c62828;
        }}

        .dark-mode .status-online {{
            background: #1b5e20;
            color: #c8e6c9;
        }}

        .dark-mode .status-off {{
            background: #b71c1c;
            color: #ffcdd2;
        }}

        .section-title {{
            font-size: 1.5em;
            color: var(--text-primary);
            margin: 20px 0 15px 0;
            padding-bottom: 10px;
            border-bottom: 2px solid var(--border-color);
        }}

        .meter-details {{
            display: grid;
            grid-template-columns: 1fr;
            gap: 20px;
        }}

        .detail-item {{
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 15px;
            background: var(--bg-primary);
            border-radius: 8px;
        }}

        .dark-mode .detail-item {{
            background: #3a3a3a;
        }}

        .detail-label {{
            font-size: 1rem;
            color: var(--text-secondary);
        }}

        .detail-value {{
            font-size: 1.2rem;
            font-weight: bold;
        }}

        .value-highlight {{
            color: #e74c3c;
        }}

        .update-time {{
            text-align: center;
            margin-top: 30px;
            padding-top: 20px;
            border-top: 1px solid var(--border-color);
            color: var(--text-secondary);
            font-size: 0.9rem;
        }}

        @media (max-width: 600px) {{
            .container {{
                border-radius: 0;
            }}
            
            .meter-card {{
                padding: 20px;
            }}
            
            .meter-name {{
                font-size: 1.5rem;
            }}
            
            .detail-item {{
                flex-direction: column;
                align-items: flex-start;
                gap: 8px;
            }}
            
            .detail-value {{
                font-size: 1.1rem;
            }}
        }}
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>{0}</h1>
            <button class="theme-toggle" onclick="toggleTheme()">🌙 暗黑模式</button>
        </div>
        
        <a href="/" class="back-button">⬅ 返回首页</a>
        
        <div class="meter-card">
            <div class="meter-header">
                <div class="meter-name">{0}</div>
                <div class="meter-status {1}" id="meterStatus">{2}</div>
            </div>

            <div class="meter-details">
                <div class="detail-item">
                    <span class="detail-label">剩余电量</span>
                    <span class="detail-value">{3:.2} kWh</span>
                </div>

                <div class="detail-item">
                    <span class="detail-label">累计用电量</span>
                    <span class="detail-value">{4:.3} kWh</span>
                </div>

                <div class="detail-item">
                    <span class="detail-label">实际用电量</span>
                    <span class="detail-value">{5:.3} kWh</span>
                </div>

                <div class="detail-item">
                    <span class="detail-label">总金额</span>
                    <span class="detail-value value-highlight">{6:.2} 元</span>
                </div>

                <div class="detail-item">
                    <span class="detail-label">电表状态</span>
                    <span class="detail-value">{2}</span>
                </div>
            </div>
            
            <!-- 电量趋势 -->
            <div class="section-title">📈 电量趋势</div>
            <div class="meter-details">
                <div class="detail-item">
                    <span class="detail-label">日均用电量</span>
                    <span class="detail-value">{7:.1} 度/天</span>
                </div>
                <div class="detail-item">
                    <span class="detail-label">周均用电量</span>
                    <span class="detail-value">{8:.1} 度/周</span>
                </div>
                <div class="detail-item">
                    <span class="detail-label">预估可用天数</span>
                    <span class="detail-value">{9:.1} 天</span>
                </div>
            </div>
            
            <div class="update-time">更新时间: {10}</div>
        </div>
    </div>

    <script>
        function toggleTheme() {{
            document.body.classList.toggle("dark-mode");
            const button = document.querySelector(".theme-toggle");
            if (document.body.classList.contains("dark-mode")) {{
                button.textContent = "☀️ 明亮模式";
                localStorage.setItem("theme", "dark");
            }} else {{
                button.textContent = "🌙 暗黑模式";
                localStorage.setItem("theme", "light");
            }}
        }}

        document.addEventListener("DOMContentLoaded", function () {{
            const savedTheme = localStorage.getItem("theme");
            if (savedTheme === "dark") {{
                document.body.classList.add("dark-mode");
                document.querySelector(".theme-toggle").textContent = "☀️ 明亮模式";
            }}
        }});
    </script>
</body>
</html>"#, 
            meter.name, status_class, data.meter_status,
            data.remaining_energy, data.total_consumption,
            actual_consumption,
            data.remaining_energy * data.price,
            statistics.daily_avg_consumption, statistics.weekly_avg_consumption,
            statistics.estimated_days_left, data.system_time.format("%Y-%m-%d %H:%M:%S")
        );

        html_content
    }

    pub fn update_web_content(&self, content: HashMap<String, String>) {
        let mut web_content = self.web_content.write().unwrap();
        *web_content = content;
    }

    pub async fn start_server(&self) -> Result<(), Box<dyn std::error::Error>> {
        let web_content = self.web_content.clone();
        
        // 首页路由
        let index = warp::path::end()
            .map({
                let web_content = web_content.clone();
                move || {
                    let content = web_content.read().unwrap();
                    warp::reply::html(content.get("index").unwrap_or(&"页面未生成".to_string()).clone())
                }
            });

        // 详情页路由 - 支持URL解码
        let detail = warp::path!(String)
            .map({
                let web_content = web_content.clone();
                move |encoded_meter_name: String| {
                    // 尝试URL解码
                    let meter_name = match urlencoding::decode(&encoded_meter_name) {
                        Ok(decoded) => decoded.to_string(),
                        Err(_) => encoded_meter_name.clone(), // 如果解码失败，使用原始字符串
                    };
                    
                    let content = web_content.read().unwrap();
                    warp::reply::html(content.get(&meter_name).unwrap_or(&"页面未生成".to_string()).clone())
                }
            });

        let routes = index.or(detail);

        log::info!("启动Web服务器，端口: {}", self.web_config.http_port);
        warp::serve(routes)
            .run(([0, 0, 0, 0], self.web_config.http_port))
            .await;

        Ok(())
    }

    pub fn save_to_files(&self) -> Result<(), Box<dyn std::error::Error>> {
        let content = self.web_content.read().unwrap();
        
        // 创建输出目录
        std::fs::create_dir_all(&self.web_config.output_web_path)?;
        
        for (filename, html_content) in content.iter() {
            let file_path = format!("{}/{}.html", self.web_config.output_web_path, filename);
            std::fs::write(file_path, html_content)?;
        }
        
        log::info!("网页已保存到: {}", self.web_config.output_web_path);
        Ok(())
    }
}