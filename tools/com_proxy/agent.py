from __future__ import annotations

import threading
import time
from contextlib import suppress
from dataclasses import dataclass
from typing import Callable

from .config import ProxySettings
from .models import DIR_CHIP_TO_PC, DIR_PC_TO_CHIP, RecordedChunk, SignalChange
from .template_builder import TemplateBuildResult, TemplateBuilder


def _import_serial():
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise RuntimeError("pyserial is required. Install it with `pip install pyserial`.") from exc
    return serial


@dataclass(frozen=True)
class AgentRuntimeStatus:
    running: bool
    capture_started: bool
    stop_reason: str
    chunk_count: int
    signal_count: int


class ComProxyAgent:
    """Bridge a virtual COM port and a real COM port while recording the session."""

    def __init__(
        self,
        settings: ProxySettings,
        *,
        time_ns_provider: Callable[[], int] | None = None,
        sleep_provider: Callable[[float], None] | None = None,
    ) -> None:
        self._settings = settings
        self._time_ns = time_ns_provider or time.time_ns
        self._sleep = sleep_provider or time.sleep
        self._chunks: list[RecordedChunk] = []
        self._signals: list[SignalChange] = []
        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._capture_started = False
        self._stop_reason = "not_started"
        self._threads: list[threading.Thread] = []
        self._last_activity_ns = 0
        self._started_at_ns = 0
        self._real_serial = None
        self._virtual_serial = None

    def run(self) -> None:
        """Run until timeout, idle auto-stop, disconnect, or explicit interruption."""

        self.start()
        try:
            self.wait_until_finished()
        finally:
            self.stop()

    def start(self) -> None:
        serial = _import_serial()
        bytesize = self._resolve_bytesize(serial, self._settings.serial.data_bits)
        parity = self._resolve_parity(serial, self._settings.serial.parity)
        stopbits = self._resolve_stopbits(serial, self._settings.serial.stop_bits)

        self._real_serial = serial.Serial(
            port=self._settings.serial.real_port,
            baudrate=self._settings.serial.baudrate,
            bytesize=bytesize,
            parity=parity,
            stopbits=stopbits,
            timeout=self._settings.serial.timeout_s,
            rtscts=self._settings.serial.flow_control == "rtscts",
        )
        self._virtual_serial = serial.Serial(
            port=self._settings.serial.virtual_port,
            baudrate=self._settings.serial.baudrate,
            bytesize=bytesize,
            parity=parity,
            stopbits=stopbits,
            timeout=self._settings.serial.timeout_s,
            rtscts=self._settings.serial.flow_control == "rtscts",
        )

        self._stop_reason = "running"
        self._started_at_ns = self._time_ns()
        self._last_activity_ns = self._started_at_ns
        self._stop_event.clear()
        self._threads = [
            threading.Thread(
                target=self._bridge_loop,
                args=(self._virtual_serial, self._real_serial, DIR_PC_TO_CHIP, "TX->"),
                daemon=True,
                name="bridge_pc_to_chip",
            ),
            threading.Thread(
                target=self._bridge_loop,
                args=(self._real_serial, self._virtual_serial, DIR_CHIP_TO_PC, "RX<-"),
                daemon=True,
                name="bridge_chip_to_pc",
            ),
        ]
        if self._settings.recording.record_rts_dtr:
            self._threads.append(
                threading.Thread(
                    target=self._signal_monitor_loop,
                    args=(self._virtual_serial,),
                    daemon=True,
                    name="signal_monitor",
                )
            )

        for thread in self._threads:
            thread.start()

    def wait_until_finished(self) -> None:
        max_session_ns = self._settings.recording.max_session_minutes * 60 * 1_000_000_000
        idle_timeout_ns = int(self._settings.recording.idle_timeout_s * 1_000_000_000)
        poll_interval_s = min(self._settings.serial.timeout_s, 0.1)

        while not self._stop_event.is_set():
            now_ns = self._time_ns()
            if now_ns - self._started_at_ns >= max_session_ns:
                self._stop_reason = "max_session_reached"
                self._stop_event.set()
                break
            if (
                self._settings.recording.auto_save_on_idle
                and self._capture_started
                and now_ns - self._last_activity_ns >= idle_timeout_ns
            ):
                self._stop_reason = "idle_timeout"
                self._stop_event.set()
                break
            self._sleep(poll_interval_s)

    def stop(self) -> None:
        self._stop_event.set()
        for thread in self._threads:
            if thread.is_alive():
                thread.join(timeout=0.5)
        self._threads = []

        for serial_port in (self._real_serial, self._virtual_serial):
            if serial_port is not None:
                with suppress(Exception):
                    serial_port.close()
        self._real_serial = None
        self._virtual_serial = None

    def export_template(
        self,
        *,
        chip_name: str,
        tool_name: str = "com-proxy-agent",
    ) -> TemplateBuildResult:
        builder = TemplateBuilder(self._settings)
        with self._lock:
            chunk_snapshot = list(self._chunks)
            signal_snapshot = list(self._signals)
        return builder.build(chunk_snapshot, signal_snapshot, chip_name=chip_name, tool_name=tool_name)

    def get_status(self) -> AgentRuntimeStatus:
        with self._lock:
            return AgentRuntimeStatus(
                running=not self._stop_event.is_set(),
                capture_started=self._capture_started,
                stop_reason=self._stop_reason,
                chunk_count=len(self._chunks),
                signal_count=len(self._signals),
            )

    def _bridge_loop(self, src, dst, direction: str, label: str) -> None:
        serial = _import_serial()
        while not self._stop_event.is_set():
            try:
                data = src.read(self._settings.recording.read_chunk_size)
                if not data:
                    continue
                dst.write(data)
                self._record_chunk(direction, bytes(data))
                print(f"[{label}] {self._format_hex_preview(data)}")
            except serial.SerialException as exc:
                self._stop_reason = f"serial_error:{exc}"
                self._stop_event.set()
                break
            except Exception as exc:
                self._stop_reason = f"bridge_error:{exc}"
                self._stop_event.set()
                break

    def _signal_monitor_loop(self, ser) -> None:
        previous_rts = None
        previous_dtr = None

        while not self._stop_event.is_set():
            try:
                current_rts = bool(ser.rts)
                current_dtr = bool(ser.dtr)
                if current_rts != previous_rts or current_dtr != previous_dtr:
                    if previous_rts is not None and previous_dtr is not None:
                        self._record_signal(current_rts, current_dtr)
                        print(f"[SIG] RTS={int(current_rts)} DTR={int(current_dtr)}")
                    previous_rts = current_rts
                    previous_dtr = current_dtr
                self._sleep(self._settings.recording.signal_poll_interval_s)
            except Exception as exc:
                self._stop_reason = f"signal_error:{exc}"
                self._stop_event.set()
                break

    def _record_chunk(self, direction: str, data: bytes) -> None:
        chunk = RecordedChunk(ts_ns=self._time_ns(), direction=direction, data=data)
        with self._lock:
            self._chunks.append(chunk)
            self._capture_started = True
            self._last_activity_ns = chunk.ts_ns

    def _record_signal(self, rts: bool, dtr: bool) -> None:
        signal = SignalChange(ts_ns=self._time_ns(), rts=rts, dtr=dtr)
        with self._lock:
            self._signals.append(signal)
            self._last_activity_ns = signal.ts_ns

    @staticmethod
    def _resolve_bytesize(serial_module, data_bits: int):
        mapping = {
            5: serial_module.FIVEBITS,
            6: serial_module.SIXBITS,
            7: serial_module.SEVENBITS,
            8: serial_module.EIGHTBITS,
        }
        return mapping[data_bits]

    @staticmethod
    def _resolve_parity(serial_module, parity: str):
        mapping = {
            "N": serial_module.PARITY_NONE,
            "E": serial_module.PARITY_EVEN,
            "O": serial_module.PARITY_ODD,
        }
        return mapping[parity.upper()]

    @staticmethod
    def _resolve_stopbits(serial_module, stop_bits: int):
        mapping = {
            1: serial_module.STOPBITS_ONE,
            2: serial_module.STOPBITS_TWO,
        }
        return mapping[stop_bits]

    @staticmethod
    def _format_hex_preview(data: bytes, limit: int = 16) -> str:
        preview = " ".join(f"{byte:02X}" for byte in data[:limit])
        if len(data) > limit:
            preview += " ..."
        return preview
