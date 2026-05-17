from com_proxy.config import ProxySettings, RecordingSettings, SerialSettings
from com_proxy.models import DIR_CHIP_TO_PC, DIR_PC_TO_CHIP, RecordedChunk, SignalChange
from com_proxy.template_builder import TemplateBuilder


def make_settings() -> ProxySettings:
    return ProxySettings(
        serial=SerialSettings(real_port="COM3", virtual_port="COM10", baudrate=115200),
        recording=RecordingSettings(ack_timeout_ms=50),
    )


def test_template_builder_pairs_ack_and_gap() -> None:
    builder = TemplateBuilder(make_settings())
    chunks = [
        RecordedChunk(ts_ns=1_000_000, direction=DIR_PC_TO_CHIP, data=b"\x7F"),
        RecordedChunk(ts_ns=2_200_000, direction=DIR_CHIP_TO_PC, data=b"\x79"),
        RecordedChunk(ts_ns=10_000_000, direction=DIR_PC_TO_CHIP, data=b"\x31\xCE"),
        RecordedChunk(ts_ns=11_000_000, direction=DIR_CHIP_TO_PC, data=b"\x79"),
    ]

    result = builder.build(chunks, [], chip_name="stm32f103")
    template = result.template

    assert template["total_frames"] == 2
    assert template["frames"][0]["tx_data"] == [0x7F]
    assert template["frames"][0]["rx_ack"] == [0x79]
    assert template["frames"][0]["ack_delay_us"] == 1200
    assert template["frames"][1]["interframe_gap_us"] == 9000
    assert template["statistics"]["ack_rate"] == 1.0


def test_template_builder_exports_reset_sequence() -> None:
    builder = TemplateBuilder(make_settings())
    chunks = [
        RecordedChunk(ts_ns=5_000_000, direction=DIR_PC_TO_CHIP, data=b"\x7F"),
    ]
    signals = [
        SignalChange(ts_ns=1_000_000, rts=False, dtr=True),
        SignalChange(ts_ns=3_000_000, rts=True, dtr=True),
        SignalChange(ts_ns=4_000_000, rts=True, dtr=False),
    ]

    result = builder.build(chunks, signals, chip_name="esp32")
    reset = result.template["reset"]

    assert reset["has_rts"] is True
    assert reset["has_dtr"] is True
    assert reset["reset_to_first_frame_us"] == 1000
    assert reset["sequence"][0] == {"pin": "RTS", "level": 0, "delay_us": 0}
    assert reset["sequence"][1] == {"pin": "DTR", "level": 1, "delay_us": 0}
    assert reset["sequence"][2] == {"pin": "RTS", "level": 1, "delay_us": 2000}
    assert reset["sequence"][3] == {"pin": "DTR", "level": 0, "delay_us": 1000}
