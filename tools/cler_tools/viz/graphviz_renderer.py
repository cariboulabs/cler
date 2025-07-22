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
        # Color scheme by data type
        self.datatype_colors = {
            'float': 'lightblue',
            'double': 'lightgreen',  
            'int': 'lightyellow',
            'complex<float>': 'lavender',
            'complex<double>': 'lightpink',
            'std::complex<float>': 'lavender',
            'std::complex<double>': 'lightpink',
            'uint8_t': 'wheat',
            'uint16_t': 'wheat',
            'uint32_t': 'wheat',
            'int8_t': 'mistyrose',
            'int16_t': 'mistyrose',
            'int32_t': 'mistyrose',
            'unknown': 'lightgray'
        }
        
        # Colors for custom/unknown types
        self.custom_colors = [
            'lightsalmon', 'lightsteelblue', 'lightseagreen', 'lightcoral',
            'lightskyblue', 'lightgoldenrod', 'lightcyan', 'lightpink',
            'lightblue', 'lightgreen', 'lightyellow', 'lavenderblush'
        ]
        self.custom_type_assignments = {}  # Track custom type colors
        
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
        for conn in flowgraph.connections:
            self._add_connection_edge(dot, conn)
        
        # Add legend for data types
        self._add_legend(dot, flowgraph)
        
        return dot
    
    def _add_block_node(self, dot: graphviz.Digraph, block: Block):
        """Add a block as a node to the DOT graph"""
        # Extract data type from template parameters
        data_type = self._extract_data_type(block)
        
        # Create node label with type and data type
        label_parts = [f"<B>{block.name}</B>"]
        label_parts.append(f"<FONT POINT-SIZE='10'>{block.type}</FONT>")
        
        # Add data type if available
        if data_type and data_type != 'unknown':
            escaped_type = data_type.replace('<', '&lt;').replace('>', '&gt;')
            label_parts.append(f"<FONT POINT-SIZE='9' COLOR='gray30'>&lt;{escaped_type}&gt;</FONT>")
        
        label = '<' + '<BR/>'.join(label_parts) + '>'
        
        # Get color based on data type
        color = self._get_color_for_type(data_type)
        
        # Add node to graph
        dot.node(
            block.name,
            label=label,
            fillcolor=color,
            tooltip=f"{block.type}<{data_type}> at line {block.line}"
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
    
    def _extract_data_type(self, block: Block) -> str:
        """Extract the primary data type from block template parameters"""
        if not block.template_params:
            return 'unknown'
        
        # Clean up the template parameter string
        template_str = block.template_params.strip()
        
        # Handle common cases
        if template_str in self.datatype_colors:
            return template_str
        
        # Handle complex template parameters like "std::complex<float>"
        # Take the first/main template parameter
        if ',' in template_str:
            template_str = template_str.split(',')[0].strip()
        
        return template_str if template_str else 'unknown'
    
    def _get_color_for_type(self, data_type: str) -> str:
        """Get color for data type, assigning new colors for custom types"""
        # Check if it's a known type
        if data_type in self.datatype_colors:
            return self.datatype_colors[data_type]
        
        # Check if we've already assigned a color to this custom type
        if data_type in self.custom_type_assignments:
            return self.custom_type_assignments[data_type]
        
        # Assign a new color for this custom type
        if len(self.custom_type_assignments) < len(self.custom_colors):
            color = self.custom_colors[len(self.custom_type_assignments)]
            self.custom_type_assignments[data_type] = color
            return color
        
        # Fallback to unknown color if we run out of custom colors
        return self.datatype_colors['unknown']
    
    def _add_legend(self, dot: graphviz.Digraph, flowgraph: FlowGraph):
        """Add a legend showing data type colors"""
        # Collect all data types used in this flowgraph
        used_types = set()
        for block in flowgraph.blocks.values():
            if block.in_flowgraph:
                data_type = self._extract_data_type(block)
                if data_type != 'unknown':
                    used_types.add(data_type)
        
        if not used_types:
            return  # No legend needed if no types detected
        
        # Create legend as a subgraph
        with dot.subgraph(name='cluster_legend') as legend:
            legend.attr(label='Data Types', style='rounded', color='gray')
            legend.attr('node', shape='box', style='filled', fontsize='10')
            
            # Add legend entries for used types only
            for i, dtype in enumerate(sorted(used_types)):
                color = self._get_color_for_type(dtype)
                escaped_type = dtype.replace('<', '&lt;').replace('>', '&gt;')
                legend.node(
                    f'legend_{i}',
                    label=f'&lt;{escaped_type}&gt;',
                    fillcolor=color,
                    fontname='monospace'
                )