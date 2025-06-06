#include "websocket_protocol.h"
#include "board.h"
#include "system_info.h"
#include "application.h"
#include "settings.h"

#include <cstring>
#include <cJSON.h>
#include <mbedtls/base64.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"

#define TAG "WS"

WebsocketProtocol::WebsocketProtocol() {
    event_group_handle_ = xEventGroupCreate();
}

WebsocketProtocol::~WebsocketProtocol() {
    if (websocket_ != nullptr) {
        delete websocket_;
    }
    vEventGroupDelete(event_group_handle_);
}

bool WebsocketProtocol::Start() {
    // Only connect to server when audio channel is needed
    return true;
}

bool WebsocketProtocol::SendAudio(const AudioStreamPacket& packet) {
    if (websocket_ == nullptr) {
        return false;
    }

    // Calculate required base64 buffer size
    size_t encoded_len = 0;
    
    // Get required size for base64 encoding
    mbedtls_base64_encode(nullptr, 0, &encoded_len, 
        packet.payload.data(), packet.payload.size());
    
    // Allocate buffer (+1 for null terminator)
    std::vector<unsigned char> encoded_data(encoded_len + 1);
    
    // Do the actual encoding
    int ret = mbedtls_base64_encode(encoded_data.data(), encoded_len + 1, &encoded_len,
        packet.payload.data(), packet.payload.size());
    
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encoding failed");
        return false;
    }

    // Add null terminator
    encoded_data[encoded_len] = '\0';

    // Create JSON message matching Python implementation
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "input_audio_buffer.append");
    cJSON_AddStringToObject(root, "audio", (char*)encoded_data.data());
    cJSON_AddStringToObject(root, "event_id", "");

    // Convert to string
    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str == nullptr) {
        ESP_LOGE(TAG, "Failed to create JSON message");
        cJSON_Delete(root);
        return false;
    }

    // Send JSON message
    bool success = SendText(json_str);
    
    // Cleanup
    cJSON_free(json_str);
    cJSON_Delete(root);

    return success;
}

bool WebsocketProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr) {
        return false;
    }

    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }

    return true;
}

bool WebsocketProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel() {
    if (websocket_ != nullptr) {
        delete websocket_;
        websocket_ = nullptr;
    }
}

bool WebsocketProtocol::OpenAudioChannel() {
    if (websocket_ != nullptr) {
        delete websocket_;
    }

    Settings settings("websocket", false);
    std::string endpoint = settings.GetString("endpoint","https://build2025-demo-resource.services.ai.azure.com");
    std::string api_key = settings.GetString("api_key","#YOUR_API_KEY#");
    std::string model = settings.GetString("model", "gpt-4o");
    std::string api_version = settings.GetString("api_version", "2025-05-01-preview");
    int version = settings.GetInt("version");
    if (version != 0) {
        version_ = version;
    }

    error_occurred_ = false;

    // Construct URL following voice-live-quickstart.py format
    std::string url = endpoint;
    if (!url.empty() && url.back() == '/') {
        url.pop_back();  // Remove trailing slash if present
    }
    url += "/voice-live/realtime?api-version=" + api_version + "&model=" + model;
    
    // Convert https:// to wss:// for WebSocket connection
    if (url.substr(0, 8) == "https://") {
        url.replace(0, 8, "wss://");
    } else if (url.substr(0, 7) == "http://") {
        url.replace(0, 7, "ws://");
    }

    websocket_ = Board::GetInstance().CreateWebSocket();
    
    // Set headers following voice-live-quickstart.py
    websocket_->SetHeader("api-key", api_key.c_str());
    websocket_->SetHeader("x-ms-client-request-id", Board::GetInstance().GetUuid().c_str());
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (!binary) {
            // Parse JSON data
            auto root = cJSON_Parse(data);
            if (root == nullptr) {
                ESP_LOGE(TAG, "Failed to parse JSON data");
                return;
            }

            auto type = cJSON_GetObjectItem(root, "type");
            if (!cJSON_IsString(type)) {
                ESP_LOGE(TAG, "Missing message type, data: %s", data);
                cJSON_Delete(root);
                return;
            }

            ESP_LOGI(TAG, "Received event type: %s", type->valuestring);

            if (strcmp(type->valuestring, "hello") == 0) {
                ParseServerHello(root);
            }
            else if (strcmp(type->valuestring, "session.created") == 0) {
                auto session = cJSON_GetObjectItem(root, "session");
                if (session && cJSON_IsObject(session)) {
                    auto id = cJSON_GetObjectItem(session, "id");
                    if (id && cJSON_IsString(id)) {
                        ESP_LOGI(TAG, "Session created: %s", id->valuestring);
                        session_id_ = id->valuestring;
                    }
                }
            }
            else if (strcmp(type->valuestring, "response.audio.delta") == 0) {
                auto delta = cJSON_GetObjectItem(root, "delta");
                if (delta && cJSON_IsString(delta)) {
                    // Store item_id if needed for deduplication
                    //auto item_id = cJSON_GetObjectItem(root, "item_id");
                    //if (item_id && cJSON_IsString(item_id)) {
                    //    last_audio_item_id_ = item_id->valuestring;
                    //}

                    const char* input = delta->valuestring;
                    size_t input_len = strlen(input);
                    size_t decoded_len = 0;

                    // Get required decode buffer size
                    mbedtls_base64_decode(nullptr, 0, &decoded_len,
                        (const unsigned char*)input, input_len);

                    // Allocate buffer
                    std::vector<unsigned char> decoded_data(decoded_len);

                    // Do the actual decoding
                    int ret = mbedtls_base64_decode(decoded_data.data(), decoded_len, &decoded_len,
                        (const unsigned char*)input, input_len);

                    if (ret == 0 && on_incoming_audio_ != nullptr) {
                        on_incoming_audio_(AudioStreamPacket{
                            .timestamp = 0,
                            .payload = std::move(decoded_data)
                        });
                    } else if (ret != 0) {
                        ESP_LOGE(TAG, "Base64 decoding failed");
                    }
                }
            }
            else if (strcmp(type->valuestring, "error") == 0) {
                auto error = cJSON_GetObjectItem(root, "error");
                if (error && cJSON_IsObject(error)) {
                    auto error_type = cJSON_GetObjectItem(error, "type");
                    auto error_code = cJSON_GetObjectItem(error, "code");
                    auto error_message = cJSON_GetObjectItem(error, "message");
                
                    std::string error_str = "Error received: ";
                    if (error_type && cJSON_IsString(error_type)) {
                        error_str += "Type=" + std::string(error_type->valuestring) + ", ";
                    }
                    if (error_code && cJSON_IsString(error_code)) {
                        error_str += "Code=" + std::string(error_code->valuestring) + ", ";
                    }
                    if (error_message && cJSON_IsString(error_message)) {
                        error_str += "Message=" + std::string(error_message->valuestring);
                    }
                
                    ESP_LOGE(TAG, "%s", error_str.c_str());
                    SetError(error_str);
                }
            }
            else {
                ESP_LOGI(TAG,"Unknown event msg: %s", data);
                if (on_incoming_json_ != nullptr) {
                    on_incoming_json_(root);
                }
            }

            cJSON_Delete(root);
            last_incoming_time_ = std::chrono::steady_clock::now();
        }
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Websocket disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    ESP_LOGI(TAG, "Connecting to websocket server: %s with version: %d", url.c_str(), version_);
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to websocket server");
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    // Send session update message to configurate the endpoint
    auto message = GetSessionUpdateMessage();
    if (!SendText(message)) {
        ESP_LOGE(TAG, "Failed to send session update");
        return false;
    }


    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

std::string WebsocketProtocol::GetSessionUpdateMessage() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "session.update");
    
    cJSON* session = cJSON_CreateObject();
    
    // Turn detection
    cJSON* turn_detection = cJSON_CreateObject();
    cJSON_AddStringToObject(turn_detection, "type", "azure_semantic_vad");
    cJSON_AddNumberToObject(turn_detection, "threshold", 0.3);
    cJSON_AddNumberToObject(turn_detection, "prefix_padding_ms", 200);
    cJSON_AddNumberToObject(turn_detection, "silence_duration_ms", 200);
    cJSON_AddBoolToObject(turn_detection, "remove_filler_words", false);
    
    // End of utterance detection
    cJSON* end_detection = cJSON_CreateObject();
    cJSON_AddStringToObject(end_detection, "model", "semantic_detection_v1");
    cJSON_AddNumberToObject(end_detection, "threshold", 0.1);
    cJSON_AddNumberToObject(end_detection, "timeout", 4);
    cJSON_AddItemToObject(turn_detection, "end_of_utterance_detection", end_detection);
    
    // Audio processing features
    cJSON* noise_reduction = cJSON_CreateObject();
    cJSON_AddStringToObject(noise_reduction, "type", "azure_deep_noise_suppression");
    
    cJSON* echo_cancellation = cJSON_CreateObject();
    cJSON_AddStringToObject(echo_cancellation, "type", "server_echo_cancellation");
    
    // Voice settings
    cJSON* voice = cJSON_CreateObject();
    cJSON_AddStringToObject(voice, "name", "en-US-Aria:DragonHDLatestNeural");
    cJSON_AddStringToObject(voice, "type", "azure-standard");
    cJSON_AddNumberToObject(voice, "temperature", 0.8);
    
    // Add all components to session object
    cJSON_AddItemToObject(session, "turn_detection", turn_detection);
    cJSON_AddItemToObject(session, "input_audio_noise_reduction", noise_reduction);
    cJSON_AddItemToObject(session, "input_audio_echo_cancellation", echo_cancellation);
    cJSON_AddItemToObject(session, "voice", voice);
    
    // Add session to root
    cJSON_AddItemToObject(root, "session", session);
    //cJSON_AddStringToObject(root, "event_id", "");
    
    char* json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    
    cJSON_free(json_str);
    cJSON_Delete(root);
    
    return message;
}

std::string WebsocketProtocol::GetHelloMessage() {
    // keys: message type, version, audio_params (format, sample_rate, channels)
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", version_);
    cJSON* features = cJSON_CreateObject();
#if CONFIG_USE_SERVER_AEC
    cJSON_AddBoolToObject(features, "aec", true);
#endif
#if CONFIG_IOT_PROTOCOL_MCP
    cJSON_AddBoolToObject(features, "mcp", true);
#endif
    cJSON_AddItemToObject(root, "features", features);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", OPUS_FRAME_DURATION_MS);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

void WebsocketProtocol::ParseServerHello(const cJSON* root) {
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (transport == nullptr || strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport->valuestring);
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
        }
        auto frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ = frame_duration->valueint;
        }
    }

    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}
