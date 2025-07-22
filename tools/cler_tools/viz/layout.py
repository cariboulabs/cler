"""
Layout algorithms for positioning graph nodes.
"""

from typing import Dict, List, Set
import math
from cler_tools.viz.graph_builder import Graph, Node


class LayoutAlgorithm:
    """Base class for layout algorithms"""
    
    def apply(self, graph: Graph):
        """Apply layout to graph nodes"""
        raise NotImplementedError


class HierarchicalLayout(LayoutAlgorithm):
    """Hierarchical/layered layout (Sugiyama-style)"""
    
    def __init__(self, compact=False):
        self.compact = compact
        self.layer_spacing = 150 if not compact else 100
        self.node_spacing = 50 if not compact else 30
    
    def apply(self, graph: Graph):
        """Apply hierarchical layout"""
        if not graph.nodes:
            return
        
        # Assign layers to nodes
        layers = self._assign_layers(graph)
        
        # Position nodes within layers
        self._position_nodes(graph, layers)
    
    def _assign_layers(self, graph: Graph) -> Dict[int, List[str]]:
        """Assign nodes to layers based on dependencies"""
        node_layers = {}
        visited = set()
        
        # Find source nodes (no incoming edges)
        incoming_edges = {node_id: [] for node_id in graph.nodes}
        for edge in graph.edges:
            incoming_edges[edge.target].append(edge)
        
        sources = [node_id for node_id, edges in incoming_edges.items() if not edges]
        
        # BFS to assign layers
        queue = [(node, 0) for node in sources]
        
        while queue:
            node_id, layer = queue.pop(0)
            
            if node_id in visited:
                continue
            
            visited.add(node_id)
            node_layers[node_id] = layer
            
            # Find children
            for edge in graph.edges:
                if edge.source == node_id and edge.target not in visited:
                    queue.append((edge.target, layer + 1))
        
        # Handle unvisited nodes (place at bottom)
        max_layer = max(node_layers.values()) if node_layers else 0
        for node_id in graph.nodes:
            if node_id not in node_layers:
                node_layers[node_id] = max_layer + 1
        
        # Group by layer
        layers = {}
        for node_id, layer in node_layers.items():
            if layer not in layers:
                layers[layer] = []
            layers[layer].append(node_id)
        
        return layers
    
    def _position_nodes(self, graph: Graph, layers: Dict[int, List[str]]):
        """Position nodes within their assigned layers"""
        # Calculate total width needed for each layer
        layer_widths = {}
        for layer, nodes in layers.items():
            total_width = sum(graph.nodes[n].width for n in nodes)
            total_spacing = (len(nodes) - 1) * self.node_spacing
            layer_widths[layer] = total_width + total_spacing
        
        max_width = max(layer_widths.values()) if layer_widths else 0
        
        # Position each layer
        for layer, node_ids in sorted(layers.items()):
            layer_width = layer_widths[layer]
            start_x = (max_width - layer_width) / 2 + 50  # Center layer
            
            current_x = start_x
            for node_id in node_ids:
                node = graph.nodes[node_id]
                node.x = current_x
                node.y = layer * self.layer_spacing + 50
                current_x += node.width + self.node_spacing


class CircularLayout(LayoutAlgorithm):
    """Circular layout for showing cyclic relationships"""
    
    def __init__(self):
        self.radius_factor = 2.5
    
    def apply(self, graph: Graph):
        """Apply circular layout"""
        if not graph.nodes:
            return
        
        nodes = list(graph.nodes.values())
        count = len(nodes)
        
        # Calculate radius based on node count
        radius = count * 30 * self.radius_factor / (2 * math.pi)
        center_x = radius + 100
        center_y = radius + 100
        
        # Position nodes in circle
        angle_step = 2 * math.pi / count
        
        for i, node in enumerate(nodes):
            angle = i * angle_step - math.pi / 2  # Start at top
            node.x = center_x + radius * math.cos(angle) - node.width / 2
            node.y = center_y + radius * math.sin(angle) - node.height / 2
        
        # Update graph dimensions
        graph.width = (radius + 100) * 2
        graph.height = (radius + 100) * 2


class ForceDirectedLayout(LayoutAlgorithm):
    """Force-directed layout using spring forces"""
    
    def __init__(self, iterations=100):
        self.iterations = iterations
        self.spring_constant = 0.1
        self.repulsion_constant = 5000
        self.damping = 0.85
    
    def apply(self, graph: Graph):
        """Apply force-directed layout"""
        if not graph.nodes:
            return
        
        # Initialize positions randomly
        import random
        for node in graph.nodes.values():
            node.x = random.uniform(100, 700)
            node.y = random.uniform(100, 500)
        
        # Run simulation
        for _ in range(self.iterations):
            forces = {node_id: {'x': 0, 'y': 0} for node_id in graph.nodes}
            
            # Calculate repulsive forces between all nodes
            nodes_list = list(graph.nodes.items())
            for i, (id1, node1) in enumerate(nodes_list):
                for j, (id2, node2) in enumerate(nodes_list[i+1:], i+1):
                    dx = node2.x - node1.x
                    dy = node2.y - node1.y
                    dist = math.sqrt(dx*dx + dy*dy)
                    
                    if dist > 0:
                        force = self.repulsion_constant / (dist * dist)
                        fx = force * dx / dist
                        fy = force * dy / dist
                        
                        forces[id1]['x'] -= fx
                        forces[id1]['y'] -= fy
                        forces[id2]['x'] += fx
                        forces[id2]['y'] += fy
            
            # Calculate attractive forces along edges
            for edge in graph.edges:
                node1 = graph.nodes[edge.source]
                node2 = graph.nodes[edge.target]
                
                dx = node2.x - node1.x
                dy = node2.y - node1.y
                dist = math.sqrt(dx*dx + dy*dy)
                
                if dist > 0:
                    force = self.spring_constant * dist
                    fx = force * dx / dist
                    fy = force * dy / dist
                    
                    forces[edge.source]['x'] += fx
                    forces[edge.source]['y'] += fy
                    forces[edge.target]['x'] -= fx
                    forces[edge.target]['y'] -= fy
            
            # Apply forces with damping
            for node_id, node in graph.nodes.items():
                node.x += forces[node_id]['x'] * self.damping
                node.y += forces[node_id]['y'] * self.damping
                
                # Keep nodes in bounds
                node.x = max(50, min(750, node.x))
                node.y = max(50, min(550, node.y))