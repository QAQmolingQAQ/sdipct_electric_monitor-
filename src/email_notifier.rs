use lettre::transport::smtp::authentication::Credentials;
use lettre::{Message, SmtpTransport, Transport};
use crate::models::{EmailConfig, MeterConfig, DatabaseRecord};
use crate::database::now_beijing;
use std::process::Command;

pub struct EmailNotifier {
    config: EmailConfig,
    transport: Option<SmtpTransport>,
}

impl EmailNotifier {
    pub fn new(config: EmailConfig) -> Self {
        let transport = if config.enabled {
            match Self::create_transport(&config) {
                Ok(transport) => Some(transport),
                Err(e) => {
                    log::error!("创建邮件传输失败: {}", e);
                    None
                }
            }
        } else {
            None
        };

        Self { config, transport }
    }

    fn create_transport(config: &EmailConfig) -> Result<SmtpTransport, Box<dyn std::error::Error>> {
        let creds = Credentials::new(
            config.email_account.clone(),
            config.email_auth_code.clone(),
        );

        // 创建SMTP传输配置，添加SSL/TLS配置选项
        let mut builder = SmtpTransport::relay(&config.smtp_server)?
            .port(config.smtp_port)
            .credentials(creds);

        // 对于QQ邮箱，使用STARTTLS和Login认证
        if config.smtp_server.contains("qq.com") {
            builder = builder
                .authentication(vec![lettre::transport::smtp::authentication::Mechanism::Login]);
        }

        let transport = builder.build();

        Ok(transport)
    }

    pub fn send_low_energy_alert(
        &self,
        meter: &MeterConfig,
        data: &DatabaseRecord,
    ) -> Result<(), Box<dyn std::error::Error>> {
        if !self.config.enabled || self.transport.is_none() {
            return Ok(());
        }

        let subject = format!("⚠️ 电表低电量警告 - {}", meter.name);
        let body = format!(
            r#"电表低电量警告

电表名称: {}
剩余电量: {:.2} kWh
累计用电量: {:.3} kWh
电表状态: {}
警告阈值: {:.1} kWh
记录时间: {}

请及时充值电费！"#,
            meter.name,
            data.remaining_energy,
            data.total_consumption,
            data.meter_status,
            meter.low_energy_threshold,
            data.system_time.format("%Y-%m-%d %H:%M:%S")
        );

        self.send_email(&subject, &body, false)
    }

    pub fn send_error_notification(
        &self,
        meter_name: &str,
        error_message: &str,
    ) -> Result<(), Box<dyn std::error::Error>> {
        if !self.config.enabled || self.transport.is_none() {
            return Ok(());
        }

        let subject = format!("❌ 电表监控系统错误 - {}", meter_name);
        let body = format!(
            r#"电表监控系统发生错误

电表名称: {}
错误信息: {}
发生时间: {}

请检查系统状态！"#,
            meter_name,
            error_message,
            now_beijing().format("%Y-%m-%d %H:%M:%S")
        );

        self.send_email(&subject, &body, true)
    }

    fn send_email(
        &self,
        subject: &str,
        body: &str,
        is_error: bool,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let transport = self.transport.as_ref().unwrap();

        let recipients = if is_error {
            vec![self.config.receive_error_email.clone()]
        } else {
            self.config.email_receivers.clone()
        };

        for receiver in recipients {
            if receiver.is_empty() {
                continue;
            }

            let email = Message::builder()
                .from(
                    format!("电表监控系统 <{}>", self.config.email_account)
                        .parse()
                        .unwrap(),
                )
                .to(receiver.parse().unwrap())
                .subject(subject)
                .body(body.to_string())?;

            match transport.send(&email) {
                Ok(_) => {
                    log::info!("邮件发送成功: {}", subject);
                }
                Err(e) => {
                    log::warn!("邮件发送失败，尝试备用方法: {}", e);
                    
                    // 使用备用方法：PowerShell Send-MailMessage
                    if let Err(ps_error) = self.send_email_powershell(subject, body, &receiver) {
                        log::error!("备用邮件发送方法也失败: {}", ps_error);
                        return Err(Box::new(e));
                    } else {
                        log::info!("备用邮件发送方法成功: {}", subject);
                    }
                }
            }
        }

        Ok(())
    }

    fn send_email_powershell(
        &self,
        subject: &str,
        body: &str,
        receiver: &str,
    ) -> Result<(), Box<dyn std::error::Error>> {
        // 使用PowerShell的Send-MailMessage命令
        let powershell_script = format!(
            "$smtpServer = '{}'; $smtpPort = {}; $username = '{}'; $password = '{}'; $from = '{}'; $to = '{}'; $subject = '{}'; $body = '{}'; $securePassword = ConvertTo-SecureString $password -AsPlainText -Force; $credential = New-Object System.Management.Automation.PSCredential($username, $securePassword); Send-MailMessage -SmtpServer $smtpServer -Port $smtpPort -UseSsl -Credential $credential -From $from -To $to -Subject $subject -Body $body -Encoding UTF8",
            self.config.smtp_server,
            self.config.smtp_port,
            self.config.email_account,
            self.config.email_auth_code,
            self.config.email_account,
            receiver,
            subject,
            body.replace("'", "''")  // 转义单引号
        );

        let output = Command::new("powershell")
            .args(["-Command", &powershell_script])
            .output()?;

        if !output.status.success() {
            let error_msg = String::from_utf8_lossy(&output.stderr);
            return Err(format!("PowerShell邮件发送失败: {}", error_msg).into());
        }

        Ok(())
    }

    pub fn is_enabled(&self) -> bool {
        self.config.enabled && self.transport.is_some()
    }
}