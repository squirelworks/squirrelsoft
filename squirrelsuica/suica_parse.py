"""Suica-family helpers — thin wrappers over felica_parse (compat)."""

from __future__ import annotations

from felica_parse import (  # noqa: F401
    FelicaCard as SuicaCard,
    HistoryEntry,
    demo_suica as demo_card,
    parse_history_block,
    parse_history_blocks,
    suica_balance_090f as parse_balance_block,
)
