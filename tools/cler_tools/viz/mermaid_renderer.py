"""
Mermaid-based renderer for Cler flowgraph visualization.
Generates Mermaid flowchart syntax for web-native rendering.
"""

from typing import Dict, Optional
from ..common.flowgraph import FlowGraph, Block, Connection


class MermaidRenderer:
    """Renders flowgraphs as Mermaid flowchart syntax"""
    
    def __init__(self, direction: str = 'LR'):
        """
        Initialize Mermaid renderer.
        
        Args:
            direction: Graph direction ('LR', 'TD', 'BT', 'RL')
        """
        self.direction = direction
        self.node_counter = 0
        self.node_map = {}  # Map block names to Mermaid node IDs
        
    def render(self, flowgraph: FlowGraph, output_path: str, 
               format: str = 'mmd') -> str:
        """
        Render flowgraph to Mermaid format.
        
        Args:
            flowgraph: The flowgraph to render
            output_path: Output file path (without extension)
            format: Output format ('mmd' for Mermaid, 'html' for embedded HTML)
            
        Returns:
            Path to generated file
        """
        mermaid_code = self._generate_mermaid(flowgraph)
        
        if format == 'html':
            content = self._wrap_html(mermaid_code, flowgraph.name)
            file_ext = 'html'
        else:
            content = mermaid_code
            file_ext = 'mmd'
            
        output_file = f"{output_path}.{file_ext}"
        with open(output_file, 'w') as f:
            f.write(content)
            
        return output_file
    
    def _generate_mermaid(self, flowgraph: FlowGraph) -> str:
        """Generate Mermaid flowchart syntax"""
        lines = [f"flowchart {self.direction}"]
        
        # Add nodes
        for block_name, block in flowgraph.blocks.items():
            if not block.in_flowgraph:
                continue
                
            node_id = self._get_node_id(block_name)
            node_label = self._create_node_label(block)
            shape_start, shape_end = self._get_node_shape(block)
            
            lines.append(f"    {node_id}{shape_start}\"{node_label}\"{shape_end}")
        
        # Add edges
        for conn in flowgraph.connections:
            source_id = self._get_node_id(conn.source_block)
            target_id = self._get_node_id(conn.target_block)
            
            # Add connection with optional label
            if conn.target_channel and conn.channel_index is not None:
                label = f"{conn.target_channel}[{conn.channel_index}]"
                lines.append(f"    {source_id} -->|\"{label}\"| {target_id}")
            elif conn.target_channel:
                lines.append(f"    {source_id} -->|\"{conn.target_channel}\"| {target_id}")
            else:
                lines.append(f"    {source_id} --> {target_id}")
        
        # Add styling
        lines.extend(self._generate_styling(flowgraph))
        
        return '\n'.join(lines)
    
    def _get_node_id(self, block_name: str) -> str:
        """Get or create Mermaid node ID for block"""
        if block_name not in self.node_map:
            # Create valid Mermaid ID (alphanumeric + underscore)
            clean_name = ''.join(c if c.isalnum() else '_' for c in block_name)
            self.node_map[block_name] = f"node_{clean_name}"
        return self.node_map[block_name]
    
    def _create_node_label(self, block: Block) -> str:
        """Create display label for block"""
        label = block.name
        
        # Add type information if different from name
        if not block.name.lower().startswith(block.type.lower().replace('block', '')):
            # Remove 'Block' suffix from type for cleaner display
            clean_type = block.type.replace('Block', '')
            label += f"\\n({clean_type})"
            
        # Add template parameters (escape angle brackets)
        if block.template_params:
            escaped_params = block.template_params.replace('<', '&lt;').replace('>', '&gt;')
            label += f"\\n&lt;{escaped_params}&gt;"
            
        return label
    
    def _get_node_shape(self, block: Block) -> tuple:
        """Get Mermaid shape syntax for block type"""
        if block.is_source():
            return "([", "])"  # Stadium shape for sources
        elif block.is_sink():
            return "[/", "/]"  # Trapezoid for sinks  
        else:
            return "[", "]"   # Rectangle for processing blocks
    
    def _generate_styling(self, flowgraph: FlowGraph) -> list:
        """Generate Mermaid CSS styling"""
        styles = []
        
        # Style different block types
        for block_name, block in flowgraph.blocks.items():
            if not block.in_flowgraph:
                continue
                
            node_id = self._get_node_id(block_name)
            
            if block.is_source():
                styles.append(f"    style {node_id} fill:#e1f5fe")
            elif block.is_sink():
                styles.append(f"    style {node_id} fill:#f3e5f5")
            else:
                styles.append(f"    style {node_id} fill:#e8f5e8")
        
        return styles
    
    def _wrap_html(self, mermaid_code: str, title: str) -> str:
        """Wrap Mermaid code in HTML for standalone rendering"""
        return f"""<!DOCTYPE html>
<html>
<head>
    <title>{title} - Cler Flowgraph</title>
    <script src="https://cdn.jsdelivr.net/npm/mermaid/dist/mermaid.min.js"></script>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 20px; }}
        .mermaid {{ text-align: center; }}
        h1 {{ color: #333; }}
    </style>
</head>
<body>
    <h1>{title}</h1>
    <div class="mermaid">
{mermaid_code}
    </div>
    <script>
        mermaid.initialize({{ startOnLoad: true, theme: 'default' }});
    </script>
</body>
</html>"""