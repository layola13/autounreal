# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

from dataclasses import dataclass


@dataclass
class CompileError(Exception):
    message: str
    file_path: str = ""
    line: int = 0
    column: int = 0
    graph_name: str = ""

    def __str__(self) -> str:
        if self.file_path and self.line > 0:
            location = f"{self.file_path}:{self.line}"
            if self.column > 0:
                location += f":{self.column}"
            if self.graph_name:
                location += f" [{self.graph_name}]"
            return f"{location}: {self.message}"
        if self.graph_name:
            return f"[{self.graph_name}] {self.message}"
        return self.message
