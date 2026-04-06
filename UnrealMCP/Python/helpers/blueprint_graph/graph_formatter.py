"""
Filename: graph_formatter.py
Description: Python wrapper for Blueprint graph formatting/layout
"""

import logging
from typing import Dict, Any, Optional

logger = logging.getLogger("BlueprintGraph.GraphFormatter")


def format_graph(
    unreal_connection,
    blueprint_name: str,
    graph_name: Optional[str] = None,
    function_name: Optional[str] = None,
    spacing_x: float = 400.0,
    spacing_y: float = 160.0,
    start_x: float = 0.0,
    start_y: float = 0.0,
    direction: str = "left_to_right"
) -> Dict[str, Any]:
    """
    Format / layout a Blueprint graph.

    Args:
        unreal_connection: Connection to Unreal Engine
        blueprint_name: Name of the Blueprint to modify
        graph_name: Target graph name (optional, defaults to EventGraph)
        function_name: Function graph name (optional)
        spacing_x: Horizontal spacing between columns (default: 400)
        spacing_y: Vertical spacing between rows (default: 160)
        start_x: Start X position (default: 0)
        start_y: Start Y position (default: 0)
        direction: "left_to_right" or "top_to_bottom" (default: left_to_right)

    Returns:
        Dictionary containing success status, node_count, graph_name or error
    """
    try:
        params: Dict[str, Any] = {
            "blueprint_name": blueprint_name,
            "spacing_x": spacing_x,
            "spacing_y": spacing_y,
            "start_x": start_x,
            "start_y": start_y,
            "direction": direction
        }

        if graph_name:
            params["graph_name"] = graph_name
        if function_name:
            params["function_name"] = function_name

        response = unreal_connection.send_command("format_graph", params)

        if response.get("success"):
            logger.info(
                f"Formatted graph '{response.get('graph_name', graph_name or function_name or 'EventGraph')}' "
                f"in blueprint '{blueprint_name}', nodes: {response.get('node_count', 'unknown')}"
            )
        else:
            logger.error(
                f"Failed to format graph in {blueprint_name}: {response.get('error', 'Unknown error')}"
            )

        return response

    except Exception as e:
        logger.error(f"Exception in format_graph: {e}")
        return {
            "success": False,
            "error": str(e)
        }
