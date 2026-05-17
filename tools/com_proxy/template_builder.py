from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Iterable

from .config import ProxySettings
from .models import DIR_CHIP_TO_PC, DIR_PC_TO_CHIP, SignalChange, RecordedChunk


@dataclass(frozen=True)
class TemplateBuildResult:
    template: dict
    statistics: dict


class TemplateBuilder:
    """Convert captured bridge events into the JSON template consumed by ESP32."""

    def __init__(self, settings: ProxySettings) -> None:
        self._settings = settings

    def build(
        self,
        chunks: Iterable[RecordedChunk],
        signals: Iterable[SignalChange],
        *,
        chip_name: str,
        tool_name: str = "com-proxy-agent",
    ) -> TemplateBuildResult:
        tx_frames = sorted(
            [chunk for chunk in chunks if chunk.direction == DIR_PC_TO_CHIP],
            key=lambda item: item.ts_ns,
        )
        rx_frames = sorted(
            [chunk for chunk in chunks if chunk.direction == DIR_CHIP_TO_PC],
            key=lambda item: item.ts_ns,
        )
        signal_events = sorted(signals, key=lambda item: item.ts_ns)
        if not tx_frames and not rx_frames and not signal_events:
            raise ValueError("No serial activity captured. Template can not be generated.")

        frame_list = self._build_frame_list(tx_frames, rx_frames)
        statistics = self._build_statistics(tx_frames, rx_frames, frame_list)
        reset = self._build_reset_section(signal_events, tx_frames)

        template = {
            "template_version": "1.0",
            "chip_name": chip_name,
            "tool_name": tool_name,
            "learn_timestamp": datetime.now(timezone.utc).isoformat(),
            "uart": {
                "baudrate": self._settings.serial.baudrate,
                "data_bits": self._settings.serial.data_bits,
                "parity": self._normalize_parity(self._settings.serial.parity),
                "stop_bits": self._settings.serial.stop_bits,
                "flow_control": self._settings.serial.flow_control,
            },
            "reset": reset,
            "frames": frame_list,
            "total_frames": len(tx_frames),
            "total_bytes": statistics["total_bytes_tx"],
            "total_duration_us": statistics["total_duration_us"],
            "statistics": statistics,
        }
        return TemplateBuildResult(template=template, statistics=statistics)

    def _build_frame_list(
        self,
        tx_frames: list[RecordedChunk],
        rx_frames: list[RecordedChunk],
    ) -> list[dict]:
        frame_list: list[dict] = []
        rx_index = 0
        ack_timeout_ns = self._settings.recording.ack_timeout_ms * 1_000_000
        start_ts_ns = tx_frames[0].ts_ns if tx_frames else 0

        for index, tx_frame in enumerate(tx_frames):
            while rx_index < len(rx_frames) and rx_frames[rx_index].ts_ns <= tx_frame.ts_ns:
                rx_index += 1

            ack_frame = None
            if rx_index < len(rx_frames):
                rx_candidate = rx_frames[rx_index]
                ack_delta = rx_candidate.ts_ns - tx_frame.ts_ns
                if 0 < ack_delta <= ack_timeout_ns:
                    ack_frame = rx_candidate
                    rx_index += 1

            interframe_gap_us = 0
            if index > 0:
                interframe_gap_us = int((tx_frame.ts_ns - tx_frames[index - 1].ts_ns) / 1000)

            ack_data = list(ack_frame.data) if ack_frame is not None else []
            ack_delay_us = int((ack_frame.ts_ns - tx_frame.ts_ns) / 1000) if ack_frame else 0
            frame_list.append(
                {
                    "index": index,
                    "timestamp_us": int((tx_frame.ts_ns - start_ts_ns) / 1000) if tx_frames else 0,
                    "tx_data": list(tx_frame.data),
                    "tx_len": tx_frame.length,
                    "rx_ack": ack_data,
                    "ack_len": len(ack_data),
                    "ack_delay_us": ack_delay_us,
                    "interframe_gap_us": interframe_gap_us,
                }
            )

        return frame_list

    def _build_statistics(
        self,
        tx_frames: list[RecordedChunk],
        rx_frames: list[RecordedChunk],
        frame_list: list[dict],
    ) -> dict:
        total_bytes_tx = sum(frame.length for frame in tx_frames)
        total_bytes_rx = sum(frame.length for frame in rx_frames)
        total_frames_tx = len(tx_frames)
        total_frames_rx = len(rx_frames)
        acked_frames = sum(1 for frame in frame_list if frame["ack_len"] > 0)
        total_duration_us = 0
        if tx_frames and rx_frames:
            total_duration_us = int((max(tx_frames[-1].ts_ns, rx_frames[-1].ts_ns) - min(tx_frames[0].ts_ns, rx_frames[0].ts_ns)) / 1000)
        elif tx_frames:
            total_duration_us = int((tx_frames[-1].ts_ns - tx_frames[0].ts_ns) / 1000)
        elif rx_frames:
            total_duration_us = int((rx_frames[-1].ts_ns - rx_frames[0].ts_ns) / 1000)

        gaps = [frame["interframe_gap_us"] for frame in frame_list[1:]]
        return {
            "total_frames_tx": total_frames_tx,
            "total_frames_rx": total_frames_rx,
            "total_bytes_tx": total_bytes_tx,
            "total_bytes_rx": total_bytes_rx,
            "total_duration_us": total_duration_us,
            "ack_rate": round((acked_frames / total_frames_tx), 3) if total_frames_tx else 0.0,
            "max_frame_len": max((frame["tx_len"] for frame in frame_list), default=0),
            "avg_frame_gap_us": int(sum(gaps) / len(gaps)) if gaps else 0,
        }

    def _build_reset_section(
        self,
        signal_events: list[SignalChange],
        tx_frames: list[RecordedChunk],
    ) -> dict:
        sequence: list[dict] = []
        has_rts = False
        has_dtr = False
        previous_event: SignalChange | None = None

        for signal_event in signal_events:
            delay_us = int((signal_event.ts_ns - previous_event.ts_ns) / 1000) if previous_event else 0
            changed = False
            if previous_event is None or signal_event.rts != previous_event.rts:
                sequence.append({"pin": "RTS", "level": int(signal_event.rts), "delay_us": delay_us if not changed else 0})
                has_rts = True
                changed = True
            if previous_event is None or signal_event.dtr != previous_event.dtr:
                sequence.append({"pin": "DTR", "level": int(signal_event.dtr), "delay_us": delay_us if not changed else 0})
                has_dtr = True
                changed = True
            previous_event = signal_event

        reset_to_first_frame_us = 0
        if signal_events and tx_frames:
            reset_to_first_frame_us = max(0, int((tx_frames[0].ts_ns - signal_events[-1].ts_ns) / 1000))

        return {
            "has_rts": has_rts,
            "has_dtr": has_dtr,
            "sequence": sequence,
            "reset_to_first_frame_us": reset_to_first_frame_us,
        }

    @staticmethod
    def _normalize_parity(parity: str) -> str:
        mapping = {
            "N": "none",
            "E": "even",
            "O": "odd",
        }
        return mapping.get(parity.upper(), "none")
