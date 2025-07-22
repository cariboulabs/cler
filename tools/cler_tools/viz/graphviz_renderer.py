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
        self.block_colors = {
            'source': '#e8f5e8',  # Light green for sources
            'sink': '#ffe8e8',    # Light red for sinks  
            'processing': '#e8f0ff',  # Light blue for processing
            'default': '#f0f0f0'  # Light gray default
        }
        
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
        dot.attr('node', shape='box', style='rounded,filled', fontname='Arial')
        dot.attr('edge', fontname='Arial', fontsize='10')
        dot.attr(layout=layout)
        
        # Add nodes (blocks)
        for block_name, block in flowgraph.blocks.items():
            if not block.in_flowgraph:
                continue  # Skip blocks not in flowgraph
                
            self._add_block_node(dot, block)
        
        # Add edges (connections)
        connection_counts = self._count_connections(flowgraph.connections)
        
        for conn in flowgraph.connections:
            self._add_connection_edge(dot, conn, connection_counts)
        
        return dot
    
    def _add_block_node(self, dot: graphviz.Digraph, block: Block):
        """Add a block as a node to the DOT graph"""
        # Determine block category for coloring
        if block.is_source():
            category = 'source'
        elif block.is_sink():
            category = 'sink'
        elif block.is_processing():
            category = 'processing'
        else:
            category = 'default'
        
        # Create node label with type and I/O info
        label_parts = [f"<B>{block.name}</B>"]
        label_parts.append(f"<FONT POINT-SIZE='10'>{block.type}</FONT>")
        
        # Add input/output info if present
        if block.inputs:
            inputs_str = ', '.join(block.inputs)
            label_parts.append(f"<FONT POINT-SIZE='9' COLOR='#666'>in: {inputs_str}</FONT>")
        
        if block.outputs:
            outputs_str = ', '.join(block.outputs)
            label_parts.append(f"<FONT POINT-SIZE='9' COLOR='#666'>out: {outputs_str}</FONT>")
        
        label = '<' + '<BR/>'.join(label_parts) + '>'
        
        # Add node to graph
        dot.node(
            block.name,
            label=label,
            fillcolor=self.block_colors[category],
            tooltip=f"{block.type} at line {block.line}"
        )
    
    def _count_connections(self, connections: list) -> Dict[tuple, int]:
        """Count connections between each pair of blocks"""
        counts = {}
        for conn in connections:
            key = (conn.source_block, conn.target_block)
            counts[key] = counts.get(key, 0) + 1
        return counts
    
    def _add_connection_edge(self, dot: graphviz.Digraph, conn: Connection, 
                           connection_counts: Dict[tuple, int]):
        """Add a connection as an edge to the DOT graph"""
        # Create edge label
        label_parts = []
        
        # Add channel info
        if conn.source_channel and conn.source_channel != 'out':
            label_parts.append(conn.source_channel)
        
        if conn.target_channel:
            if conn.channel_index is not None:
                label_parts.append(f"{conn.target_channel}[{conn.channel_index}]")
            else:
                label_parts.append(conn.target_channel)
        
        label = ' → '.join(label_parts) if label_parts else ''
        
        # Handle multiple connections between same blocks
        key = (conn.source_block, conn.target_block)
        is_multiple = connection_counts[key] > 1
        
        # Style edges differently for multiple connections
        if is_multiple:
            # For multiple connections, use different colors/styles
            edge_attrs = {
                'label': label,
                'color': '#2563eb',  # Blue for multiple connections
                'penwidth': '2',
            }
        else:
            edge_attrs = {
                'label': label,
                'color': '#374151',  # Gray for single connections  
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