from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


CURRENT_DIR = Path(__file__).resolve().parent
TOOLS_DIR = CURRENT_DIR.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from com_proxy import ComProxyAgent, ConfigError, apply_overrides, load_proxy_settings


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Bridge virtual and real COM ports, then export a JSON template.")
    parser.add_argument("--config", default=str(CURRENT_DIR / "com_proxy.ini"), help="Path to the ini configuration file.")
    parser.add_argument("--real-port", help="Override the real COM port connected to the target board.")
    parser.add_argument("--virtual-port", help="Override the virtual COM port used by the download tool.")
    parser.add_argument("--baudrate", type=int, help="Override baudrate.")
    parser.add_argument("--output-dir", help="Override template output directory.")
    parser.add_argument("--chip-name", help="Chip name written into the exported template.")
    parser.add_argument("--tool-name", default="com-proxy-agent", help="Tool name written into the exported template.")
    return parser


def main() -> int:
    parser = build_argument_parser()
    args = parser.parse_args()

    try:
        settings = load_proxy_settings(args.config if Path(args.config).exists() else None)
        settings = apply_overrides(
            settings,
            real_port=args.real_port,
            virtual_port=args.virtual_port,
            baudrate=args.baudrate,
            output_dir=args.output_dir,
        )
    except ConfigError as exc:
        print(f"[ERROR] Invalid configuration: {exc}", file=sys.stderr)
        return 2

    chip_name = args.chip_name or _resolve_chip_name(settings.serial.real_port, settings.serial.baudrate)
    output_dir = Path(settings.recording.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 56)
    print(" COM Proxy Agent")
    print("=" * 56)
    print(f" Real port:    {settings.serial.real_port}")
    print(f" Virtual port: {settings.serial.virtual_port}")
    print(f" Baudrate:     {settings.serial.baudrate}")
    print(f" Output dir:   {output_dir}")
    print(" Stop with Ctrl+C, or wait for idle auto-save.")
    print("=" * 56)

    agent = ComProxyAgent(settings)

    try:
        agent.run()
    except KeyboardInterrupt:
        print("\n[INFO] Interrupted by user.")
        agent.stop()
    except Exception as exc:
        print(f"[ERROR] Proxy failed: {exc}", file=sys.stderr)
        agent.stop()
        return 1

    status = agent.get_status()
    if status.chunk_count == 0 and status.signal_count == 0:
        print("[ERROR] No traffic captured. JSON was not generated.", file=sys.stderr)
        return 1

    try:
        build_result = agent.export_template(chip_name=chip_name, tool_name=args.tool_name)
    except Exception as exc:
        print(f"[ERROR] Failed to build JSON template: {exc}", file=sys.stderr)
        return 1

    output_path = output_dir / _build_output_filename(chip_name)
    with output_path.open("w", encoding="utf-8") as file:
        json.dump(build_result.template, file, indent=2, ensure_ascii=False)

    print(f"[INFO] Stop reason: {status.stop_reason}")
    print(f"[INFO] Captured TX frames: {build_result.statistics['total_frames_tx']}")
    print(f"[INFO] Captured RX frames: {build_result.statistics['total_frames_rx']}")
    print(f"[INFO] ACK rate: {build_result.statistics['ack_rate'] * 100:.1f}%")
    print(f"[INFO] Template saved: {output_path}")
    return 0


def _resolve_chip_name(real_port: str, baudrate: int) -> str:
    normalized = real_port.replace("COM", "").replace("/", "_").replace("\\", "_")
    return f"chip_{normalized}_{baudrate}"


def _build_output_filename(chip_name: str) -> str:
    from datetime import datetime

    safe_chip_name = "".join(char if char.isalnum() or char in ("-", "_") else "_" for char in chip_name)
    return f"{safe_chip_name}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"


if __name__ == "__main__":
    raise SystemExit(main())
