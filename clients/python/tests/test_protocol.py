"""Tests for NDJSON protocol framing and helpers."""

import json
import pytest

from bashclient.protocol import (
    b64decode,
    b64encode,
    decode_frame,
    encode_frame,
    get_channel,
    get_type,
    make_msg,
)
from bashclient.errors import ProtocolError
from bashclient.types import CHAN_CONTROL, CHAN_COMMAND, FRAME_MAX_PAYLOAD


class TestEncodeFrame:
    def test_simple_message(self):
        msg = {"ch": 0, "type": "ping"}
        data = encode_frame(msg)
        assert data.endswith(b"\n")
        parsed = json.loads(data)
        assert parsed == msg

    def test_nested_message(self):
        msg = {"ch": 2, "type": "value", "data": {"name": "x", "value": "1"}}
        data = encode_frame(msg)
        parsed = json.loads(data)
        assert parsed["data"]["name"] == "x"

    def test_non_serializable_raises(self):
        with pytest.raises(ProtocolError, match="cannot encode"):
            encode_frame({"ch": 0, "bad": object()})


class TestDecodeFrame:
    def test_simple_message(self):
        line = b'{"ch":0,"type":"pong"}\n'
        msg = decode_frame(line)
        assert msg == {"ch": 0, "type": "pong"}

    def test_strips_whitespace(self):
        line = b'  {"ch":1,"type":"eval"}  \n'
        msg = decode_frame(line)
        assert msg["ch"] == 1

    def test_empty_raises(self):
        with pytest.raises(ProtocolError, match="empty"):
            decode_frame(b"")

    def test_invalid_json_raises(self):
        with pytest.raises(ProtocolError, match="invalid JSON"):
            decode_frame(b"not json\n")

    def test_non_object_raises(self):
        with pytest.raises(ProtocolError, match="expected JSON object"):
            decode_frame(b"[1,2,3]\n")

    def test_oversized_raises(self):
        line = b"{" + b"x" * (FRAME_MAX_PAYLOAD + 1) + b"}\n"
        with pytest.raises(ProtocolError, match="exceeds max payload"):
            decode_frame(line)


class TestBase64:
    def test_roundtrip(self):
        text = "hello world"
        encoded = b64encode(text)
        assert b64decode(encoded) == text

    def test_unicode_roundtrip(self):
        text = "日本語テスト"
        assert b64decode(b64encode(text)) == text

    def test_invalid_base64(self):
        with pytest.raises(ProtocolError, match="base64"):
            b64decode("!!!invalid!!!")


class TestMakeMsg:
    def test_basic(self):
        msg = make_msg(0, "auth", token="abc")
        assert msg == {"ch": 0, "type": "auth", "token": "abc"}

    def test_no_extra_fields(self):
        msg = make_msg(1, "eval")
        assert msg == {"ch": 1, "type": "eval"}


class TestGetChannel:
    def test_present(self):
        assert get_channel({"ch": 3, "type": "x"}) == 3

    def test_missing_defaults_to_zero(self):
        assert get_channel({"type": "x"}) == 0


class TestGetType:
    def test_present(self):
        assert get_type({"ch": 0, "type": "ping"}) == "ping"

    def test_missing_raises(self):
        with pytest.raises(ProtocolError, match="no 'type'"):
            get_type({"ch": 0})
