# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

import ast
import os
from dataclasses import dataclass
from typing import Any, Dict, Optional


@dataclass
class MetaResolver:
    reference_dir: str = ""
    mode: str = "no_meta"

    def load_graph_meta(self, graph_filename: str) -> Optional[Dict[str, Any]]:
        if not self.reference_dir:
            self.mode = "no_meta"
            return None

        base, _ = os.path.splitext(os.path.basename(graph_filename))
        meta_path = os.path.join(self.reference_dir, f"{base}_meta.py")
        if not os.path.isfile(meta_path):
            if self.mode != "full":
                self.mode = "no_meta"
            return None

        self.mode = "full"
        return load_meta_file(meta_path)


def load_meta_file(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as handle:
        source = handle.read()

    tree = ast.parse(source, filename=path)
    for stmt in tree.body:
        if isinstance(stmt, ast.Assign):
            for target in stmt.targets:
                if isinstance(target, ast.Name) and target.id == "META":
                    return ast.literal_eval(stmt.value)
    return {}
