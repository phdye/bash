package bashclient

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
)

// EncodeFrame encodes a message as an NDJSON line.
func EncodeFrame(msg Message) ([]byte, error) {
	data, err := json.Marshal(msg)
	if err != nil {
		return nil, newProtocolErrorf(err, "cannot encode frame: %v", err)
	}
	return append(data, '\n'), nil
}

// DecodeFrame decodes an NDJSON line into a message.
func DecodeFrame(line []byte) (Message, error) {
	// Strip whitespace
	start, end := 0, len(line)
	for start < end && (line[start] == ' ' || line[start] == '\t' || line[start] == '\r' || line[start] == '\n') {
		start++
	}
	for end > start && (line[end-1] == ' ' || line[end-1] == '\t' || line[end-1] == '\r' || line[end-1] == '\n') {
		end--
	}
	trimmed := line[start:end]

	if len(trimmed) == 0 {
		return nil, newProtocolError("empty frame")
	}
	if len(trimmed) > FrameMaxPayload {
		return nil, newProtocolError(fmt.Sprintf("frame exceeds max payload (%d > %d)", len(trimmed), FrameMaxPayload))
	}

	var msg Message
	if err := json.Unmarshal(trimmed, &msg); err != nil {
		return nil, newProtocolErrorf(err, "invalid JSON: %v", err)
	}
	return msg, nil
}

// B64Encode base64-encodes a string.
func B64Encode(s string) string {
	return base64.StdEncoding.EncodeToString([]byte(s))
}

// B64Decode base64-decodes a string.
func B64Decode(s string) (string, error) {
	data, err := base64.StdEncoding.DecodeString(s)
	if err != nil {
		return "", newProtocolErrorf(err, "base64 decode error: %v", err)
	}
	return string(data), nil
}

// MakeMsg builds a protocol message.
func MakeMsg(channel int, msgType string, extra ...map[string]any) Message {
	msg := Message{"ch": channel, "type": msgType}
	for _, m := range extra {
		for k, v := range m {
			msg[k] = v
		}
	}
	return msg
}

// GetChannel extracts the channel ID from a message, defaulting to 0.
func GetChannel(msg Message) int {
	if ch, ok := msg["ch"]; ok {
		switch v := ch.(type) {
		case float64:
			return int(v)
		case int:
			return v
		}
	}
	return 0
}

// GetType extracts the message type.
func GetType(msg Message) (string, error) {
	t, ok := msg["type"]
	if !ok {
		return "", newProtocolError("message has no 'type' field")
	}
	s, ok := t.(string)
	if !ok {
		return fmt.Sprintf("%v", t), nil
	}
	return s, nil
}

// getString safely gets a string field from a message.
func getString(msg Message, key string) string {
	if v, ok := msg[key]; ok {
		if s, ok := v.(string); ok {
			return s
		}
	}
	return ""
}

// getInt safely gets an int field from a message.
func getInt(msg Message, key string) int {
	if v, ok := msg[key]; ok {
		switch n := v.(type) {
		case float64:
			return int(n)
		case int:
			return n
		}
	}
	return 0
}

// getInt64 safely gets an int64 field from a message.
func getInt64(msg Message, key string) int64 {
	if v, ok := msg[key]; ok {
		switch n := v.(type) {
		case float64:
			return int64(n)
		case int64:
			return n
		case int:
			return int64(n)
		}
	}
	return 0
}

// getBool safely gets a bool field from a message.
func getBool(msg Message, key string) bool {
	if v, ok := msg[key]; ok {
		if b, ok := v.(bool); ok {
			return b
		}
	}
	return false
}

// getStringSlice safely gets a string slice from a message.
func getStringSlice(msg Message, key string) []string {
	if v, ok := msg[key]; ok {
		if arr, ok := v.([]any); ok {
			result := make([]string, 0, len(arr))
			for _, item := range arr {
				if s, ok := item.(string); ok {
					result = append(result, s)
				}
			}
			return result
		}
	}
	return nil
}

// getMap safely gets a map from a message.
func getMap(msg Message, key string) Message {
	if v, ok := msg[key]; ok {
		if m, ok := v.(map[string]any); ok {
			return Message(m)
		}
	}
	return nil
}

// getSlice safely gets a slice from a message.
func getSlice(msg Message, key string) []any {
	if v, ok := msg[key]; ok {
		if arr, ok := v.([]any); ok {
			return arr
		}
	}
	return nil
}
