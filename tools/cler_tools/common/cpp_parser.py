"""
AST-based C++ parser for Cler tools.
Extracts blocks, connections, and flowgraph structure using tree-sitter.
"""

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser, Node
from typing import List, Dict, Optional, Tuple
from .flowgraph import FlowGraph, Block, Connection


class ClerParser:
    """AST-based C++ parser for Cler flowgraph code using tree-sitter"""
    
    def __init__(self):
        """Initialize AST-based parser"""
        self.language = Language(tscpp.language())
        self.parser = Parser(self.language)
        self.blocks = {}
        self.connections = []
        self.flowgraph_name = None
    
    def parse_file(self, content: str, filename: str) -> FlowGraph:
        """Parse C++ file and extract flowgraph structure"""
        self.blocks = {}
        self.connections = []
        self.flowgraph_name = None
        
        # Parse with tree-sitter
        tree = self.parser.parse(bytes(content, 'utf8'))
        
        # Extract blocks and connections
        self._extract_blocks(tree.root_node, content)
        self._extract_flowgraph(tree.root_node, content)
        
        # Infer channel directions from connections
        self._infer_channel_directions()
        
        # Determine flowgraph name
        name = self.flowgraph_name or filename.split('/')[-1].replace('.cpp', '')
        
        return FlowGraph(
            name=name,
            blocks=self.blocks.copy(),
            connections=self.connections.copy()
        )
    
    def _extract_blocks(self, node: Node, content: str):
        """Extract block declarations from AST"""
        for child in self._walk_ast(node):
            if child.type == 'declaration':
                self._process_declaration(child, content)
    
    def _extract_flowgraph(self, node: Node, content: str):
        """Extract flowgraph definition and connections"""
        for child in self._walk_ast(node):
            if child.type == 'call_expression':
                self._process_call_expression(child, content)
    
    def _walk_ast(self, node: Node):
        """Walk AST tree in depth-first order"""
        yield node
        for child in node.children:
            yield from self._walk_ast(child)
    
    def _process_declaration(self, node: Node, content: str):
        """Process variable declarations to find block instances"""
        # Look for declarations like: BlockType<Template> varname(args);
        declarator = self._find_child_by_type(node, 'init_declarator')
        if not declarator:
            return
        
        # Get the type and variable name
        type_node = self._find_child_by_type(node, 'type_identifier') or \
                   self._find_child_by_type(node, 'template_type')
        
        if not type_node:
            return
        
        # Extract type name
        type_name = self._get_node_text(type_node, content)
        
        # For template types, extract just the base type name
        if type_node.type == 'template_type':
            # Get the template name (first identifier)
            template_name = self._find_child_by_type(type_node, 'type_identifier')
            if template_name:
                type_name = self._get_node_text(template_name, content)
        
        # Only process if it looks like a block type
        if not (type_name.endswith('Block') or type_name.endswith('block')):
            return
        
        # Get variable name and constructor
        var_name = None
        constructor_args = []
        template_params = None
        
        # Find identifier (variable name)
        identifier = self._find_child_by_type(declarator, 'identifier')
        if identifier:
            var_name = self._get_node_text(identifier, content)
        
        # Find constructor call
        call_expr = self._find_child_by_type(declarator, 'call_expression')
        if call_expr:
            args = self._extract_call_arguments(call_expr, content)
            constructor_args = args
        
        # Extract template parameters if present
        if type_node.type == 'template_type':
            template_args = self._find_child_by_type(type_node, 'template_argument_list')
            if template_args:
                template_params = self._get_node_text(template_args, content)[1:-1]  # Remove < >
        
        # Create block
        if var_name and var_name not in self.blocks:
            line, col = self._get_position(declarator, content)
            self.blocks[var_name] = Block(
                name=var_name,
                type=type_name,
                line=line,
                column=col,
                template_params=template_params,
                constructor_args=constructor_args
            )
    
    def _process_call_expression(self, node: Node, content: str):
        """Process function calls to find flowgraph and BlockRunner calls"""
        # Get function name
        func_name = self._get_function_name(node, content)
        
        if 'make_' in func_name and '_flowgraph' in func_name:
            # Extract flowgraph name (handle namespaced calls like cler::make_desktop_flowgraph)
            if '_flowgraph' in func_name:
                # Remove namespace if present
                clean_name = func_name.split('::')[-1] if '::' in func_name else func_name
                self.flowgraph_name = clean_name.replace('make_', '').replace('_flowgraph', '')
            
            # Extract BlockRunner calls from arguments
            self._extract_blockrunners(node, content)
        
        elif 'BlockRunner' in func_name:
            # Direct BlockRunner call (handles both BlockRunner and cler::BlockRunner)
            self._extract_single_blockrunner(node, content)
    
    def _extract_blockrunners(self, node: Node, content: str):
        """Extract BlockRunner calls from flowgraph arguments"""
        args_list = self._find_child_by_type(node, 'argument_list')
        if not args_list:
            return
        
        for arg in args_list.children:
            if arg.type == 'call_expression':
                func_name = self._get_function_name(arg, content)
                if 'BlockRunner' in func_name:
                    self._extract_single_blockrunner(arg, content)
    
    def _extract_single_blockrunner(self, node: Node, content: str):
        """Extract connections from a single BlockRunner call"""
        args_list = self._find_child_by_type(node, 'argument_list')
        if not args_list:
            return
        
        # Get all arguments
        args = []
        for child in args_list.children:
            if child.type != ',':  # Skip comma separators
                arg_text = self._get_node_text(child, content)
                if arg_text.startswith('&'):
                    args.append(arg_text[1:])  # Remove &
        
        if len(args) < 1:
            return
        
        # First argument is source block
        source_block = args[0]
        
        # Mark source block as in flowgraph
        if source_block in self.blocks:
            self.blocks[source_block].in_flowgraph = True
        
        # Remaining arguments are target channels
        for target_arg in args[1:]:
            if '.' in target_arg:
                parts = target_arg.split('.')
                target_block = parts[0]
                target_channel = '.'.join(parts[1:])
                
                # Mark target block as in flowgraph
                if target_block in self.blocks:
                    self.blocks[target_block].in_flowgraph = True
                
                # Create connection
                conn = Connection(
                    source_block=source_block,
                    source_channel='out',  # Default output channel
                    target_block=target_block,
                    target_channel=target_channel
                )
                
                # Extract array index if present
                if '[' in target_channel and ']' in target_channel:
                    channel_base = target_channel.split('[')[0]
                    index_str = target_channel.split('[')[1].split(']')[0]
                    try:
                        conn.channel_index = int(index_str)
                        conn.target_channel = channel_base
                    except ValueError:
                        pass
                
                # Avoid duplicate connections (include channel_index in comparison)
                if not any(c.source_block == conn.source_block and 
                          c.target_block == conn.target_block and 
                          c.target_channel == conn.target_channel and
                          c.channel_index == conn.channel_index
                          for c in self.connections):
                    self.connections.append(conn)
    
    def _infer_channel_directions(self):
        """Infer input/output channels from connections"""
        for conn in self.connections:
            # Add output channels to source blocks
            source_block = self.blocks.get(conn.source_block)
            if source_block and conn.source_channel not in source_block.outputs:
                source_block.outputs.append(conn.source_channel)
            
            # Add input channels to target blocks
            target_block = self.blocks.get(conn.target_block)
            if target_block:
                channel_name = conn.target_channel
                if conn.channel_index is not None:
                    channel_name = f"{conn.target_channel}[{conn.channel_index}]"
                
                if channel_name not in target_block.inputs:
                    target_block.inputs.append(channel_name)
    
    def _find_child_by_type(self, node: Node, node_type: str) -> Optional[Node]:
        """Find first child of given type"""
        for child in node.children:
            if child.type == node_type:
                return child
        return None
    
    def _get_node_text(self, node: Node, content: str) -> str:
        """Get text content of a node"""
        return content[node.start_byte:node.end_byte]
    
    def _get_position(self, node: Node, content: str) -> Tuple[int, int]:
        """Get line and column position of node"""
        return node.start_point[0] + 1, node.start_point[1]
    
    def _get_function_name(self, node: Node, content: str) -> str:
        """Extract function name from call expression"""
        # The function is the first child of call_expression
        if node.children:
            func_expr = node.children[0]
            
            # Handle different types of function expressions
            if func_expr.type == 'identifier':
                return self._get_node_text(func_expr, content)
            elif func_expr.type == 'scoped_identifier':
                # For cler::make_desktop_flowgraph, get the full name
                return self._get_node_text(func_expr, content)
            elif func_expr.type == 'field_expression':
                # For object.method, get just the method name
                field_id = self._find_child_by_type(func_expr, 'field_identifier')
                if field_id:
                    return self._get_node_text(field_id, content)
            
            # Fallback: get the text of the entire function expression
            return self._get_node_text(func_expr, content)
        
        return ""
    
    def _extract_call_arguments(self, node: Node, content: str) -> List[str]:
        """Extract arguments from function call"""
        args_list = self._find_child_by_type(node, 'argument_list')
        if not args_list:
            return []
        
        args = []
        for child in args_list.children:
            if child.type != ',':  # Skip comma separators
                arg_text = self._get_node_text(child, content).strip()
                if arg_text:
                    args.append(arg_text)
        
        return args