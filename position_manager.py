from __future__ import annotations

from typing import Any


class PositionManager:
    def __init__(self) -> None:
        self.positions: dict[int, dict[str, Any]] = {}

    def sync_positions(self, broker_positions: list[dict[str, Any]]) -> None:
        self.positions = {int(pos.get("ticket", idx)): pos for idx, pos in enumerate(broker_positions)}

    def manage_positions(self, positions: list[dict[str, Any]], stop_loss: float | None = None, take_profit: float | None = None) -> list[dict[str, Any]]:
        commands: list[dict[str, Any]] = []
        for pos in positions:
            if stop_loss is not None and pos.get("profit", 0) < 0:
                commands.append({"ticket": pos.get("ticket"), "action": "close", "reason": "Protected stop triggered"})
            elif take_profit is not None and pos.get("profit", 0) >= take_profit:
                commands.append({"ticket": pos.get("ticket"), "action": "close", "reason": "Take-profit reached"})
        return commands
