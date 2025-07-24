"""
Mermaid-based renderer for Cler flowgraph visualization.
Generates Mermaid flowchart syntax for web-native rendering.
"""

from ..common.flowgraph import FlowGraph, Block


class MermaidRenderer:
    """Renders flowgraphs as Mermaid flowchart syntax"""
    
    def __init__(self, direction: str = 'LR', fence_style: str = 'backticks'):
        """
        Initialize Mermaid renderer.
        
        Args:
            direction: Graph direction ('LR', 'TD', 'BT', 'RL')
            fence_style: Fencing style ('backticks', 'colons', 'none')
        """
        self.direction = direction
        self.fence_style = fence_style
        self.node_counter = 0
        self.node_map = {}  # Map block names to Mermaid node IDs
        
    def render(self, flowgraph: FlowGraph, output_path: str) -> str:
        """
        Render flowgraph to Mermaid format.
        
        Args:
            flowgraph: The flowgraph to render
            output_path: Output file path (without extension)
            
        Returns:
            Path to generated file
        """
        mermaid_code = self._generate_mermaid(flowgraph)
        
        output_file = f"{output_path}.md"
        with open(output_file, 'w') as f:
            f.write(mermaid_code)
            
        return output_file
    
    def _generate_mermaid(self, flowgraph: FlowGraph) -> str:
        """Generate Mermaid flowchart syntax"""
        lines = []
        
        # Add opening fence based on style
        if self.fence_style == 'backticks':
            lines.append("```mermaid")
        elif self.fence_style == 'colons':
            lines.append("::: mermaid")
        # 'none' adds no fencing
        
        lines.append(f"flowchart {self.direction}")
        
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
            
            # Simple connections without channel labels
            lines.append(f"    {source_id} --> {target_id}")
        
        # Add styling
        lines.extend(self._generate_styling(flowgraph))
        
        # Add closing fence based on style
        if self.fence_style == 'backticks':
            lines.append("```")
        elif self.fence_style == 'colons':
            lines.append(":::")
        # 'none' adds no closing fence
        
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
        # Start with block name
        label = block.name
        
        # Always add type information 
        # Remove 'Block' suffix from type for cleaner display
        clean_type = block.type.replace('Block', '')
        label += f"\n({clean_type})"
        
        # Show template parameters after block type if they exist
        if block.template_params:
            # Use HTML-escaped angle brackets for Mermaid compatibility
            escaped_params = block.template_params.replace('<', '&lt;').replace('>', '&gt;')
            label += f"\n&lt;{escaped_params}&gt;"
        
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
    
