"""
SVG renderer for flowgraph visualization.
"""

from typing import Dict, List, Tuple
import math
from .graph_builder import Graph, Node, Edge
from .layout import HierarchicalLayout, CircularLayout


class SVGRenderer:
    """Renders graph structure as SVG"""
    
    def __init__(self, show_channels=True, compact=False, layout='hierarchical'):
        self.show_channels = show_channels
        self.compact = compact
        self.layout_type = layout
        
        # Style configuration
        self.node_rx = 5  # Corner radius
        self.font_size = 12 if not compact else 10
        self.padding = 20 if not compact else 10
        
        # Color scheme
        self.colors = {
            'node_fill': '#f0f4f8',
            'node_stroke': '#2d3748',
            'edge_stroke': '#4a5568',
            'text': '#1a202c',
            'grid': '#e2e8f0',
            'arrow': '#2d3748'
        }
    
    def render(self, graph: Graph) -> str:
        """Render graph to SVG string"""
        # Apply layout
        layout = self._get_layout()
        layout.apply(graph)
        
        # Calculate SVG dimensions
        width, height = self._calculate_dimensions(graph)
        
        # Build SVG
        svg_parts = [
            self._svg_header(width, height),
            self._svg_defs(),
            self._render_grid(width, height),
            self._render_edges(graph),
            self._render_nodes(graph),
            self._svg_footer()
        ]
        
        return '\n'.join(svg_parts)
    
    def _get_layout(self):
        """Get layout algorithm instance"""
        if self.layout_type == 'circular':
            return CircularLayout()
        else:  # Default to hierarchical
            return HierarchicalLayout(compact=self.compact)
    
    def _calculate_dimensions(self, graph: Graph) -> Tuple[float, float]:
        """Calculate required SVG dimensions"""
        if not graph.nodes:
            return 400, 300
        
        max_x = max(node.x + node.width for node in graph.nodes.values())
        max_y = max(node.y + node.height for node in graph.nodes.values())
        
        return max_x + self.padding * 2, max_y + self.padding * 2
    
    def _svg_header(self, width: float, height: float) -> str:
        """Generate SVG header"""
        return f'''<?xml version="1.0" encoding="UTF-8"?>
<svg width="{width}" height="{height}" 
     viewBox="0 0 {width} {height}"
     xmlns="http://www.w3.org/2000/svg">'''
    
    def _svg_defs(self) -> str:
        """Generate SVG definitions for reusable elements"""
        return '''  <defs>
    <marker id="arrowhead" markerWidth="10" markerHeight="7" 
            refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#2d3748" />
    </marker>
    <filter id="shadow" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur in="SourceAlpha" stdDeviation="2"/>
      <feOffset dx="1" dy="1" result="offsetblur"/>
      <feComponentTransfer>
        <feFuncA type="linear" slope="0.2"/>
      </feComponentTransfer>
      <feMerge>
        <feMergeNode/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>
  </defs>'''
    
    def _render_grid(self, width: float, height: float) -> str:
        """Render background grid"""
        if self.compact:
            return ''
            
        grid_lines = []
        grid_size = 50
        
        for x in range(0, int(width), grid_size):
            grid_lines.append(
                f'<line x1="{x}" y1="0" x2="{x}" y2="{height}" '
                f'stroke="{self.colors["grid"]}" stroke-width="0.5"/>'
            )
        
        for y in range(0, int(height), grid_size):
            grid_lines.append(
                f'<line x1="0" y1="{y}" x2="{width}" y2="{y}" '
                f'stroke="{self.colors["grid"]}" stroke-width="0.5"/>'
            )
        
        return f'  <g id="grid">\n    ' + '\n    '.join(grid_lines) + '\n  </g>'
    
    def _render_nodes(self, graph: Graph) -> str:
        """Render all nodes"""
        node_elements = []
        
        for node in graph.nodes.values():
            node_elements.append(self._render_node(node))
        
        return f'  <g id="nodes">\n' + '\n'.join(node_elements) + '\n  </g>'
    
    def _render_node(self, node: Node) -> str:
        """Render a single node"""
        # Split label by newline
        label_lines = node.label.split('\\n')
        
        elements = [
            f'    <g id="{node.id}" transform="translate({node.x},{node.y})">',
            f'      <rect width="{node.width}" height="{node.height}" '
            f'rx="{self.node_rx}" ry="{self.node_rx}" '
            f'fill="{self.colors["node_fill"]}" stroke="{self.colors["node_stroke"]}" '
            f'stroke-width="2" filter="url(#shadow)"/>',
        ]
        
        # Render text lines
        text_y_start = (node.height - len(label_lines) * self.font_size) / 2 + self.font_size
        for i, line in enumerate(label_lines):
            elements.append(
                f'      <text x="{node.width/2}" y="{text_y_start + i * self.font_size}" '
                f'text-anchor="middle" font-family="sans-serif" '
                f'font-size="{self.font_size}" fill="{self.colors["text"]}">{line}</text>'
            )
        
        elements.append('    </g>')
        
        return '\n'.join(elements)
    
    def _render_edges(self, graph: Graph) -> str:
        """Render all edges"""
        edge_elements = []
        
        for edge in graph.edges:
            edge_elements.append(self._render_edge(edge, graph))
        
        return f'  <g id="edges">\n' + '\n'.join(edge_elements) + '\n  </g>'
    
    def _render_edge(self, edge: Edge, graph: Graph) -> str:
        """Render a single edge"""
        source_node = graph.nodes[edge.source]
        target_node = graph.nodes[edge.target]
        
        # Calculate connection points
        x1, y1 = self._get_port_position(source_node, edge.source_port, 'output')
        x2, y2 = self._get_port_position(target_node, edge.target_port, 'input')
        
        # Create path
        path = self._create_edge_path(x1, y1, x2, y2)
        
        elements = [
            f'    <g id="{edge.id}">',
            f'      <path d="{path}" fill="none" '
            f'stroke="{self.colors["edge_stroke"]}" stroke-width="2" '
            f'marker-end="url(#arrowhead)"/>'
        ]
        
        # Add label if requested
        if self.show_channels and edge.label:
            mid_x = (x1 + x2) / 2
            mid_y = (y1 + y2) / 2
            elements.append(
                f'      <text x="{mid_x}" y="{mid_y - 5}" '
                f'text-anchor="middle" font-family="sans-serif" '
                f'font-size="{self.font_size - 2}" fill="{self.colors["text"]}">{edge.label}</text>'
            )
        
        elements.append('    </g>')
        
        return '\n'.join(elements)
    
    def _get_port_position(self, node: Node, port: str, port_type: str) -> Tuple[float, float]:
        """Get position of a port on a node"""
        if port_type == 'output':
            # Right side of node
            x = node.x + node.width
            y = node.y + node.height / 2
        else:
            # Left side of node
            x = node.x
            y = node.y + node.height / 2
        
        return x, y
    
    def _create_edge_path(self, x1: float, y1: float, x2: float, y2: float) -> str:
        """Create bezier curve path for edge"""
        # Calculate control points for smooth curve
        dx = x2 - x1
        dy = y2 - y1
        
        # Horizontal emphasis for flowgraph look
        ctrl_offset = abs(dx) * 0.5
        
        if dx > 0:  # Left to right
            cx1 = x1 + ctrl_offset
            cy1 = y1
            cx2 = x2 - ctrl_offset
            cy2 = y2
        else:  # Right to left (feedback)
            # Route around
            mid_y = (y1 + y2) / 2
            return f"M {x1} {y1} L {x1 + 20} {y1} L {x1 + 20} {mid_y} L {x2 - 20} {mid_y} L {x2 - 20} {y2} L {x2} {y2}"
        
        return f"M {x1} {y1} C {cx1} {cy1}, {cx2} {cy2}, {x2} {y2}"
    
    def _svg_footer(self) -> str:
        """Generate SVG footer"""
        return '</svg>'