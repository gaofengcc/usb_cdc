from __future__ import annotations

from configparser import ConfigParser
from dataclasses import dataclass, replace
from pathlib import Path


@dataclass(frozen=True)
class SerialSettings:
    real_port: str = "COM3"
    virtual_port: str = "COM10"
    baudrate: int = 115200
    data_bits: int = 8
    parity: str = "N"
    stop_bits: int = 1
    timeout_s: float = 0.01
    flow_control: str = "none"


@dataclass(frozen=True)
class RecordingSettings:
    output_dir: str = "./templates"
    auto_save_on_idle: bool = True
    idle_timeout_s: float = 5.0
    max_session_minutes: int = 30
    record_rts_dtr: bool = True
    ack_timeout_ms: int = 50
    signal_poll_interval_s: float = 0.001
    read_chunk_size: int = 1024


@dataclass(frozen=True)
class ProxySettings:
    serial: SerialSettings = SerialSettings()
    recording: RecordingSettings = RecordingSettings()


class ConfigError(ValueError):
    """Raised when the proxy configuration is invalid."""


def load_proxy_settings(config_path: str | Path | None = None) -> ProxySettings:
    """Load proxy settings from an ini file if it exists."""

    parser = ConfigParser()
    if config_path is not None:
        parser.read(Path(config_path), encoding="utf-8")

    serial = SerialSettings(
        real_port=parser.get("proxy", "real_port", fallback=SerialSettings.real_port),
        virtual_port=parser.get("proxy", "virtual_port", fallback=SerialSettings.virtual_port),
        baudrate=parser.getint("proxy", "baudrate", fallback=SerialSettings.baudrate),
        data_bits=parser.getint("proxy", "data_bits", fallback=SerialSettings.data_bits),
        parity=parser.get("proxy", "parity", fallback=SerialSettings.parity).upper(),
        stop_bits=parser.getint("proxy", "stop_bits", fallback=SerialSettings.stop_bits),
        timeout_s=parser.getfloat("proxy", "timeout_s", fallback=SerialSettings.timeout_s),
        flow_control=parser.get("proxy", "flow_control", fallback=SerialSettings.flow_control).lower(),
    )
    recording = RecordingSettings(
        output_dir=parser.get("recording", "output_dir", fallback=RecordingSettings.output_dir),
        auto_save_on_idle=parser.getboolean(
            "recording",
            "auto_save_on_idle",
            fallback=RecordingSettings.auto_save_on_idle,
        ),
        idle_timeout_s=parser.getfloat(
            "recording",
            "idle_timeout_s",
            fallback=RecordingSettings.idle_timeout_s,
        ),
        max_session_minutes=parser.getint(
            "recording",
            "max_session_minutes",
            fallback=RecordingSettings.max_session_minutes,
        ),
        record_rts_dtr=parser.getboolean(
            "recording",
            "record_rts_dtr",
            fallback=RecordingSettings.record_rts_dtr,
        ),
        ack_timeout_ms=parser.getint(
            "recording",
            "ack_timeout_ms",
            fallback=RecordingSettings.ack_timeout_ms,
        ),
        signal_poll_interval_s=parser.getfloat(
            "recording",
            "signal_poll_interval_s",
            fallback=RecordingSettings.signal_poll_interval_s,
        ),
        read_chunk_size=parser.getint(
            "recording",
            "read_chunk_size",
            fallback=RecordingSettings.read_chunk_size,
        ),
    )
    settings = ProxySettings(serial=serial, recording=recording)
    _validate_settings(settings)
    return settings


def apply_overrides(
    settings: ProxySettings,
    *,
    real_port: str | None = None,
    virtual_port: str | None = None,
    baudrate: int | None = None,
    output_dir: str | None = None,
) -> ProxySettings:
    """Return a new settings object with CLI overrides applied."""

    serial = settings.serial
    recording = settings.recording
    if real_port is not None:
        serial = replace(serial, real_port=real_port)
    if virtual_port is not None:
        serial = replace(serial, virtual_port=virtual_port)
    if baudrate is not None:
        serial = replace(serial, baudrate=baudrate)
    if output_dir is not None:
        recording = replace(recording, output_dir=output_dir)

    merged = ProxySettings(serial=serial, recording=recording)
    _validate_settings(merged)
    return merged


def _validate_settings(settings: ProxySettings) -> None:
    if not settings.serial.real_port:
        raise ConfigError("real_port can not be empty.")
    if not settings.serial.virtual_port:
        raise ConfigError("virtual_port can not be empty.")
    if settings.serial.real_port == settings.serial.virtual_port:
        raise ConfigError("real_port and virtual_port must be different.")
    if settings.serial.baudrate <= 0:
        raise ConfigError("baudrate must be greater than 0.")
    if settings.serial.data_bits not in (5, 6, 7, 8):
        raise ConfigError("data_bits must be one of 5, 6, 7, 8.")
    if settings.serial.stop_bits not in (1, 2):
        raise ConfigError("stop_bits must be 1 or 2.")
    if settings.serial.timeout_s <= 0:
        raise ConfigError("timeout_s must be greater than 0.")
    if settings.recording.idle_timeout_s <= 0:
        raise ConfigError("idle_timeout_s must be greater than 0.")
    if settings.recording.max_session_minutes <= 0:
        raise ConfigError("max_session_minutes must be greater than 0.")
    if settings.recording.ack_timeout_ms <= 0:
        raise ConfigError("ack_timeout_ms must be greater than 0.")
    if settings.recording.signal_poll_interval_s <= 0:
        raise ConfigError("signal_poll_interval_s must be greater than 0.")
    if settings.recording.read_chunk_size <= 0:
        raise ConfigError("read_chunk_size must be greater than 0.")
