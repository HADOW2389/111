use serde::{Deserialize, Serialize};

use crate::DB;

#[derive(Debug, Serialize)]
struct ChatRequest<'a> {
    model: &'a str,
    messages: Vec<ChatMessage<'a>>,
}

#[derive(Debug, Serialize)]
struct ChatMessage<'a> {
    role: &'a str,
    content: &'a str,
}

#[derive(Debug, Deserialize)]
struct ChatResponse {
    choices: Vec<ChatChoice>,
}

#[derive(Debug, Deserialize)]
struct ChatChoice {
    message: ChatResponseMessage,
}

#[derive(Debug, Deserialize)]
struct ChatResponseMessage {
    content: Option<String>,
}

#[derive(Debug, Deserialize)]
struct ProviderErrorEnvelope {
    error: ProviderError,
}

#[derive(Debug, Deserialize)]
struct ProviderError {
    message: String,
}

/// Sends one stateless turn to any provider implementing the OpenAI-compatible
/// POST /chat/completions contract. Configuration is read at request time so
/// changes made in the GUI apply without recompiling.
pub async fn chat(user_text: &str) -> Result<String, String> {
    let settings = DB.get().ok_or("Settings are not initialized")?.read().clone();

    if !settings.llm_enabled {
        return Err("AI provider is disabled".to_string());
    }
    if settings.llm_base_url.is_empty() || settings.llm_model.is_empty() {
        return Err("AI provider Base URL and model are required".to_string());
    }

    let url = format!("{}/chat/completions", settings.llm_base_url.trim_end_matches('/'));
    let request = ChatRequest {
        model: &settings.llm_model,
        messages: vec![
            ChatMessage { role: "system", content: &settings.llm_system_prompt },
            ChatMessage { role: "user", content: user_text },
        ],
    };

    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(90))
        .build()
        .map_err(|e| format!("Failed to create HTTP client: {e}"))?;
    let mut call = client.post(url).json(&request);
    if !settings.api_keys.openai.trim().is_empty() {
        call = call.bearer_auth(settings.api_keys.openai.trim());
    }

    let response = call.send().await.map_err(|e| format!("AI provider request failed: {e}"))?;
    let status = response.status();
    let body = response.text().await.map_err(|e| format!("Failed to read AI response: {e}"))?;
    if !status.is_success() {
        let message = serde_json::from_str::<ProviderErrorEnvelope>(&body)
            .map(|e| e.error.message)
            .unwrap_or_else(|_| body.chars().take(500).collect());
        return Err(format!("AI provider returned {status}: {message}"));
    }

    let parsed: ChatResponse = serde_json::from_str(&body)
        .map_err(|e| format!("Unsupported AI provider response: {e}"))?;
    parsed.choices.into_iter().next()
        .and_then(|choice| choice.message.content)
        .map(|text| text.trim().to_string())
        .filter(|text| !text.is_empty())
        .ok_or_else(|| "AI provider returned an empty response".to_string())
}
