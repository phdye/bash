package bashclient

import (
	"encoding/json"
	"errors"
	"strings"
	"testing"
)

func TestEncodeFrameSimple(t *testing.T) {
	msg := Message{"ch": 0, "type": "ping"}
	data, err := EncodeFrame(msg)
	if err != nil {
		t.Fatal(err)
	}
	if data[len(data)-1] != '\n' {
		t.Error("frame should end with newline")
	}
	var parsed Message
	if err := json.Unmarshal(data[:len(data)-1], &parsed); err != nil {
		t.Fatal(err)
	}
}

func TestEncodeFrameNested(t *testing.T) {
	msg := Message{"ch": 2, "type": "value", "data": map[string]any{"name": "x", "value": "1"}}
	data, err := EncodeFrame(msg)
	if err != nil {
		t.Fatal(err)
	}
	var parsed map[string]any
	json.Unmarshal(data, &parsed)
	d := parsed["data"].(map[string]any)
	if d["name"] != "x" {
		t.Errorf("expected name=x, got %v", d["name"])
	}
}

func TestDecodeFrameSimple(t *testing.T) {
	line := []byte(`{"ch":0,"type":"pong"}` + "\n")
	msg, err := DecodeFrame(line)
	if err != nil {
		t.Fatal(err)
	}
	if getString(msg, "type") != "pong" {
		t.Errorf("expected type=pong, got %v", msg["type"])
	}
}

func TestDecodeFrameStripsWhitespace(t *testing.T) {
	line := []byte(`  {"ch":1,"type":"eval"}  ` + "\n")
	msg, err := DecodeFrame(line)
	if err != nil {
		t.Fatal(err)
	}
	if GetChannel(msg) != 1 {
		t.Errorf("expected ch=1, got %d", GetChannel(msg))
	}
}

func TestDecodeFrameEmptyRaises(t *testing.T) {
	_, err := DecodeFrame([]byte(""))
	if err == nil {
		t.Fatal("expected error for empty frame")
	}
	var pe *ProtocolError
	if !errors.As(err, &pe) {
		t.Errorf("expected ProtocolError, got %T", err)
	}
	if !strings.Contains(pe.Error(), "empty") {
		t.Errorf("expected 'empty' in error, got %q", pe.Error())
	}
}

func TestDecodeFrameInvalidJSON(t *testing.T) {
	_, err := DecodeFrame([]byte("not json\n"))
	if err == nil {
		t.Fatal("expected error for invalid JSON")
	}
	var pe *ProtocolError
	if !errors.As(err, &pe) {
		t.Errorf("expected ProtocolError, got %T", err)
	}
}

func TestDecodeFrameNonObject(t *testing.T) {
	// JSON arrays decode into Message (map) and fail
	_, err := DecodeFrame([]byte("[1,2,3]\n"))
	if err == nil {
		t.Fatal("expected error for non-object JSON")
	}
}

func TestDecodeFrameOversized(t *testing.T) {
	line := []byte("{" + strings.Repeat("x", FrameMaxPayload+1) + "}\n")
	_, err := DecodeFrame(line)
	if err == nil {
		t.Fatal("expected error for oversized frame")
	}
	var pe *ProtocolError
	if !errors.As(err, &pe) {
		t.Errorf("expected ProtocolError, got %T", err)
	}
}

func TestBase64Roundtrip(t *testing.T) {
	text := "hello world"
	encoded := B64Encode(text)
	decoded, err := B64Decode(encoded)
	if err != nil {
		t.Fatal(err)
	}
	if decoded != text {
		t.Errorf("expected %q, got %q", text, decoded)
	}
}

func TestBase64UnicodeRoundtrip(t *testing.T) {
	text := "日本語テスト"
	decoded, err := B64Decode(B64Encode(text))
	if err != nil {
		t.Fatal(err)
	}
	if decoded != text {
		t.Errorf("expected %q, got %q", text, decoded)
	}
}

func TestBase64InvalidRaises(t *testing.T) {
	_, err := B64Decode("!!!invalid!!!")
	if err == nil {
		t.Fatal("expected error for invalid base64")
	}
	var pe *ProtocolError
	if !errors.As(err, &pe) {
		t.Errorf("expected ProtocolError, got %T", err)
	}
}

func TestMakeMsgBasic(t *testing.T) {
	msg := MakeMsg(0, "auth", map[string]any{"token": "abc"})
	if GetChannel(msg) != 0 {
		t.Errorf("expected ch=0")
	}
	tp, _ := GetType(msg)
	if tp != "auth" {
		t.Errorf("expected type=auth, got %s", tp)
	}
	if getString(msg, "token") != "abc" {
		t.Errorf("expected token=abc")
	}
}

func TestMakeMsgNoExtra(t *testing.T) {
	msg := MakeMsg(1, "eval")
	if len(msg) != 2 {
		t.Errorf("expected 2 fields, got %d", len(msg))
	}
}

func TestGetChannelPresent(t *testing.T) {
	if GetChannel(Message{"ch": float64(3), "type": "x"}) != 3 {
		t.Error("expected channel 3")
	}
}

func TestGetChannelMissing(t *testing.T) {
	if GetChannel(Message{"type": "x"}) != 0 {
		t.Error("expected default channel 0")
	}
}

func TestGetTypePresent(t *testing.T) {
	tp, err := GetType(Message{"ch": 0, "type": "ping"})
	if err != nil {
		t.Fatal(err)
	}
	if tp != "ping" {
		t.Errorf("expected ping, got %s", tp)
	}
}

func TestGetTypeMissing(t *testing.T) {
	_, err := GetType(Message{"ch": 0})
	if err == nil {
		t.Fatal("expected error for missing type")
	}
}
