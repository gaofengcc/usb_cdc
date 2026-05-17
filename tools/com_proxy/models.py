from __future__ import annotations

from dataclasses import dataclass


DIR_PC_TO_CHIP = "pc_to_chip"
DIR_CHIP_TO_PC = "chip_to_pc"
DIR_SIGNAL = "signal"


@dataclass(frozen=True)
class RecordedChunk:
    """A raw byte chunk seen on one bridge direction."""

    ts_ns: int
    direction: str
    data: bytes

    @property
    def length(self) -> int:
        return len(self.data)


@dataclass(frozen=True)
class SignalChange:
    """A sampled RTS and DTR state snapshot."""

    ts_ns: int
    rts: bool
    dtr: bool
