"""
Graph builder for converting parsed flowgraph data into a renderable graph structure.
"""

from dataclasses import dataclass, field
from typing import List, Dict, Set, Tuple, Optional
from cler_tools.common import FlowGraph, Block, Connection


@dataclass
class Node:
    """Represents a visual node in the graph"""
    id: str
    label: str
    type: str
    x: float = 0.0
    y: float = 0.0
    width: float = 120.0
    height: float = 60.0
    inputs: List[str] = field(default_factory=list)
    outputs: List[str] = field(default_factory=list)
    
    
@dataclass 
class Edge:
    """Represents a visual edge in the graph"""
    id: str
    source: str
    target: str
    source_port: Optional[str] = None
    target_port: Optional[str] = None
    label: Optional[str] = None
    

@dataclass
class Graph:
    """Complete graph structure ready for rendering"""
    nodes: Dict[str, Node] = field(default_factory=dict)
    edges: List[Edge] = field(default_factory=list)
    width: float = 800
    height: float = 600
    

class GraphBuilder:
    """Builds visual graph from parsed flowgraph data"""
    
    def __init__(self):
        self.edge_counter = 0
    
    def build(self, flowgraph: FlowGraph) -> Graph:
        """Convert flowgraph to visual graph"""
        graph = Graph()
        
        # Create nodes from blocks
        for block_name, block in flowgraph.blocks.items():
            node = Node(
                id=block_name,
                label=f"{block_name}\\n({block.type})",
                type=block.type,
                inputs=block.inputs.copy(),
                outputs=block.outputs.copy()
            )
            graph.nodes[block_name] = node
        
        # Create edges from connections
        for conn in flowgraph.connections:
            edge_id = f"edge_{self.edge_counter}"
            self.edge_counter += 1
            
            edge = Edge(
                id=edge_id,
                source=conn.source_block,
                target=conn.target_block,
                source_port=conn.source_channel,
                target_port=conn.target_channel,
                label=self._make_edge_label(conn)
            )
            graph.edges.append(edge)
        
        # Calculate graph dimensions based on nodes
        if graph.nodes:
            graph.width = max(800, len(graph.nodes) * 150)
            graph.height = max(600, self._calculate_height(graph))
        
        return graph
    
    def _make_edge_label(self, conn: Connection) -> Optional[str]:
        """Create label for edge from connection info"""
        if conn.source_channel and conn.target_channel:
            if conn.channel_index is not None:
                return f"{conn.target_channel}[{conn.channel_index}]"
            return conn.target_channel
        return None
    
    def _calculate_height(self, graph: Graph) -> float:
        """Calculate required height based on graph complexity"""
        # Simple heuristic: use depth of graph
        depths = self._calculate_node_depths(graph)
        max_depth = max(depths.values()) if depths else 1
        return max(600, (max_depth + 1) * 150)
    
    def _calculate_node_depths(self, graph: Graph) -> Dict[str, int]:
        """Calculate depth of each node in the graph"""
        depths = {}
        visited = set()
        
        # Find root nodes (no incoming edges)
        incoming = {node_id: 0 for node_id in graph.nodes}
        for edge in graph.edges:
            incoming[edge.target] = incoming.get(edge.target, 0) + 1
        
        roots = [node_id for node_id, count in incoming.items() if count == 0]
        
        # BFS to calculate depths
        queue = [(root, 0) for root in roots]
        
        while queue:
            node_id, depth = queue.pop(0)
            
            if node_id in visited:
                continue
                
            visited.add(node_id)
            depths[node_id] = depth
            
            # Find children
            for edge in graph.edges:
                if edge.source == node_id and edge.target not in visited:
                    queue.append((edge.target, depth + 1))
        
        # Handle unvisited nodes (cycles)
        for node_id in graph.nodes:
            if node_id not in depths:
                depths[node_id] = 0
        
        return depths