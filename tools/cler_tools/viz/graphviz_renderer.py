"""
Graphviz-based renderer for Cler flowgraph visualization.
Generates DOT graphs and renders them using Graphviz.
"""

from typing import Dict, Optional
import graphviz
from ..common.flowgraph import FlowGraph, Block, Connection


class GraphvizRenderer:
    """Renders flowgraphs using Graphviz"""
    
    def __init__(self):
        pass  # No color configuration needed since we're not using fills
        
    def render(self, flowgraph: FlowGraph, output_path: str, 
               layout: str = 'dot', format: str = 'svg') -> str:
        """
        Render flowgraph to file using Graphviz.
        
        Args:
            flowgraph: The flowgraph to render
            output_path: Output file path (without extension)
            layout: Graphviz layout engine ('dot', 'neato', 'fdp', 'circo', 'twopi')
            format: Output format ('svg', 'png', 'pdf', 'ps')
            
        Returns:
            Path to generated file
        """
        # Create DOT graph
        dot = self._create_dot_graph(flowgraph, layout)
        
        try:
            # Try to render to file
            output_file = dot.render(output_path, format=format, cleanup=True)
            return output_file
        except Exception as e:
            # If Graphviz executables aren't available, save DOT file instead
            dot_path = f"{output_path}.dot"
            with open(dot_path, 'w') as f:
                f.write(dot.source)
            
            raise RuntimeError(
                f"Graphviz executables not found. DOT file saved to {dot_path}. "
                f"Install Graphviz system package to render: sudo apt install graphviz"
            ) from e
    
    def _create_dot_graph(self, flowgraph: FlowGraph, layout: str) -> graphviz.Digraph:
        """Create Graphviz DOT representation of flowgraph"""
        dot = graphviz.Digraph(comment=f'Cler Flowgraph: {flowgraph.name}')
        
        # Set graph attributes
        dot.attr(rankdir='LR')  # Left to right layout
        dot.attr('node', shape='box', style='rounded', fontname='Arial')  # Remove 'filled' from default
        dot.attr('edge', fontname='Arial', fontsize='10')
        dot.attr(layout=layout)
        
        # Add nodes (blocks)
        for block_name, block in flowgraph.blocks.items():
            if not block.in_flowgraph:
                continue  # Skip blocks not in flowgraph
                
            self._add_block_node(dot, block)
        
        # Add edges (connections)
        for conn in flowgraph.connections:
            self._add_connection_edge(dot, conn)
        
        # Legend removed as per user request
        
        return dot
    
    def _add_block_node(self, dot: graphviz.Digraph, block: Block):
        """Add a block as a node to the DOT graph"""
        # Create node label with name and template parameters
        label_parts = [f"<B>{block.name}</B>"]
        
        # Add template parameters if available
        if block.template_params:
            escaped_type = block.template_params.replace('<', '&lt;').replace('>', '&gt;')
            label_parts.append(f"<FONT POINT-SIZE='9' COLOR='gray30'>&lt;{escaped_type}&gt;</FONT>")
        
        label = '<' + '<BR/>'.join(label_parts) + '>'
        
        # Add node to graph with no fill
        dot.node(
            block.name,
            label=label,
            tooltip=f"{block.type} at line {block.line}"
        )
    
    def _add_connection_edge(self, dot: graphviz.Digraph, conn: Connection):
        """Add a connection as an edge to the DOT graph"""
        # No labels needed - arrows are sufficient
        label = ''
        
        # All connections use the same style
        edge_attrs = {
            'label': label,
            'color': '#374151',  # Gray for all connections
            'penwidth': '1',
        }
        
        # Add tooltip with connection details
        if conn.channel_index is not None:
            tooltip = f"{conn.source_block}.{conn.source_channel} → {conn.target_block}.{conn.target_channel}[{conn.channel_index}]"
        else:
            tooltip = f"{conn.source_block}.{conn.source_channel} → {conn.target_block}.{conn.target_channel}"
        
        edge_attrs['tooltip'] = tooltip
        
        # Add edge to graph
        dot.edge(conn.source_block, conn.target_block, **edge_attrs)
    
    
