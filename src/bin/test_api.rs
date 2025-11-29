use reqwest::Client;
use serde_json::json;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let client = Client::new();
    let api_url = "https://sdxt.sdipct.edu.cn/kddz/electricmeterpost/electricMeterQuery";
    let electric_user_uid = "7733";
    
    let payload = json!({
        "electricUserUid": electric_user_uid
    });

    println!("发送请求到: {}", api_url);
    println!("请求体: {}", payload);
    
    let response = client
        .post(api_url)
        .json(&payload)
        .send()
        .await?;

    println!("响应状态: {}", response.status());
    
    let response_text = response.text().await?;
    println!("响应内容: {}", response_text);
    
    // 尝试解析为JSON查看结构
    if let Ok(json_value) = serde_json::from_str::<serde_json::Value>(&response_text) {
        println!("JSON结构: {:#}", json_value);
    } else {
        println!("无法解析为JSON");
    }
    
    Ok(())
}