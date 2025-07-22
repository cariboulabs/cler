"""
Common C++ parsing functionality for Cler tools.
Extracts blocks, connections, and flowgraph structure.
"""

import re
from dataclasses import dataclass, field
from typing import List, Dict, Set, Optional, Tuple
from .patterns import PATTERNS, INPUT_OPERATIONS, OUTPUT_OPERATIONS


@dataclass
class Block:
    """Represents a Cler block instance"""
    name: str
    type: str
    line: int
    column: int = 0
    inputs: List[str] = field(default_factory=list)
    outputs: List[str] = field(default_factory=list)
    has_runner: bool = False
    in_flowgraph: bool = False
    template_params: Optional[str] = None
    constructor_args: List[str] = field(default_factory=list)
    channel_types: Dict[str, str] = field(default_factory=dict)
    
    def is_source(self) -> bool:
        """Check if this block is a source (no input channels)"""
        return len(self.inputs) == 0 and len(self.outputs) > 0
    
    def is_sink(self) -> bool:
        """Check if this block is a sink (no output channels)"""
        return len(self.inputs) > 0 and len(self.outputs) == 0
    
    def is_processing(self) -> bool:
        """Check if this block is a processing block (has both inputs and outputs)"""
        return len(self.inputs) > 0 and len(self.outputs) > 0


@dataclass
class Connection:
    """Represents a connection between blocks"""
    source_block: str
    source_channel: str
    target_block: str
    target_channel: str
    channel_index: Optional[int] = None


@dataclass
class FlowGraph:
    """Represents a complete flowgraph structure"""
    name: str
    blocks: Dict[str, Block] = field(default_factory=dict)
    connections: List[Connection] = field(default_factory=list)
    

class ClerParser:
    """Parses Cler C++ code to extract flowgraph structure"""
    
    def __init__(self):
        self.blocks: Dict[str, Block] = {}
        self.connections: List[Connection] = []
        self.flowgraph_name: Optional[str] = None
    
    def parse_file(self, content: str, filename: str) -> FlowGraph:
        """Parse a C++ file and return the flowgraph structure"""
        self.blocks.clear()
        self.connections.clear()
        
        # Extract components in order
        self._extract_blocks(content)
        self._extract_flowgraph(content)
        self._infer_channel_directions(content)
        
        # Determine flowgraph name from function name or filename
        name = self.flowgraph_name or filename.split('/')[-1].replace('.cpp', '')
        
        return FlowGraph(
            name=name,
            blocks=self.blocks.copy(),
            connections=self.connections.copy()
        )
    
    def _get_line_column(self, content: str, position: int) -> Tuple[int, int]:
        """Convert string position to line and column number"""
        lines = content[:position].split('\n')
        line = len(lines)
        column = len(lines[-1]) if lines else 0
        return line, column
    
    def _parse_constructor_args(self, args_str: str) -> List[str]:
        """Parse constructor arguments, handling nested parentheses and brackets"""
        if not args_str.strip():
            return []
        
        args = []
        current_arg = ""
        depth = 0
        in_quotes = False
        quote_char = None
        
        for char in args_str:
            if char in '"\'':
                if not in_quotes:
                    in_quotes = True
                    quote_char = char
                elif char == quote_char:
                    in_quotes = False
                    quote_char = None
                current_arg += char
            elif in_quotes:
                current_arg += char
            elif char in '({[':
                depth += 1
                current_arg += char
            elif char in ')}]':
                depth -= 1
                current_arg += char
            elif char == ',' and depth == 0:
                args.append(current_arg.strip())
                current_arg = ""
            else:
                current_arg += char
        
        # Add the last argument
        if current_arg.strip():
            args.append(current_arg.strip())
        
        return args
    
    def _find_matching_bracket(self, content: str, start: int, open_char: str, close_char: str) -> int:
        """Find the matching closing bracket for an opening bracket"""
        depth = 1
        i = start + 1
        
        while i < len(content) and depth > 0:
            if content[i] == open_char:
                depth += 1
            elif content[i] == close_char:
                depth -= 1
            i += 1
        
        return i - 1 if depth == 0 else -1
    
    def _find_block_declaration(self, content: str, block_name: str) -> Optional[Block]:
        """Find the declaration of a block by name"""
        # Look for patterns like: SomeBlockType<...> block_name(...)
        # First find the block name followed by parentheses, then work backwards
        pattern = rf'\b{re.escape(block_name)}\s*\('
        
        for match in re.finditer(pattern, content):
            # Work backwards to find the type and template parameters
            start_pos = match.start()
            
            # Find start of this line to get the full declaration
            line_start = content.rfind('\n', 0, start_pos) + 1
            line_content = content[line_start:match.end()]
            
            # Parse the line: TypeName<template> varname(
            type_pattern = r'(\w+)\s*(?:<([^>]*(?:<[^>]*>[^>]*)*?)>)?\s+' + re.escape(block_name) + r'\s*\('
            type_match = re.search(type_pattern, line_content)
            
            if type_match:
                block_type = type_match.group(1)
                template_params = type_match.group(2)
                
                # Find constructor arguments
                constructor_start = match.end() - 1
                constructor_end = self._find_matching_bracket(content, constructor_start, '(', ')')
                constructor_args = []
                if constructor_end > constructor_start:
                    args_str = content[constructor_start+1:constructor_end].strip()
                    if args_str:
                        constructor_args = self._parse_constructor_args(args_str)
                
                line, col = self._get_line_column(content, line_start)
                return Block(
                    name=block_name,
                    type=block_type,
                    line=line,
                    column=col,
                    template_params=template_params.strip() if template_params else None,
                    constructor_args=constructor_args
                )
        
        return None
    
    def _extract_blocks(self, content: str):
        """Extract block instances from declarations and BlockRunner usage"""
        # First, find all block declarations using the general pattern
        pattern = PATTERNS['block_instance_detailed']
        
        for match in re.finditer(pattern, content):
            block_type = match.group(1)
            template_params = match.group(2)
            var_name = match.group(3)
            constructor_args_str = match.group(4)
            
            # Parse constructor arguments
            constructor_args = []
            if constructor_args_str and constructor_args_str.strip():
                constructor_args = self._parse_constructor_args(constructor_args_str)
            
            # Create the block if not already exists
            if var_name not in self.blocks:
                line, col = self._get_line_column(content, match.start())
                self.blocks[var_name] = Block(
                    name=var_name,
                    type=block_type,
                    line=line,
                    column=col,
                    template_params=template_params.strip() if template_params else None,
                    constructor_args=constructor_args
                )
    
    def _extract_flowgraph(self, content: str):
        """Extract flowgraph definition and connections"""
        # Find make_*_flowgraph function
        pattern = PATTERNS['flowgraph_call']
        match = re.search(pattern, content)
        
        if not match:
            return
        
        # Extract flowgraph name from function
        func_match = re.search(r'make_(\w+)_flowgraph', content)
        if func_match:
            self.flowgraph_name = func_match.group(1)
        
        # Find the complete flowgraph call
        start = match.end() - 1
        flowgraph_content = self._extract_balanced_parens(content, start)
        
        if not flowgraph_content:
            return
        
        # Extract BlockRunner calls
        self._extract_runners(flowgraph_content)
    
    def _extract_balanced_parens(self, content: str, start: int) -> str:
        """Extract content within balanced parentheses"""
        paren_count = 1
        i = start + 1
        
        while i < len(content) and paren_count > 0:
            if content[i] == '(':
                paren_count += 1
            elif content[i] == ')':
                paren_count -= 1
            i += 1
        
        return content[start:i] if paren_count == 0 else ""
    
    def _extract_runners(self, flowgraph_content: str):
        """Extract BlockRunner declarations and their connections"""
        runner_pattern = PATTERNS['blockrunner_call']
        
        for match in re.finditer(runner_pattern, flowgraph_content):
            block_name = match.group(1)
            connections_str = match.group(2)
            
            if block_name in self.blocks:
                self.blocks[block_name].has_runner = True
                self.blocks[block_name].in_flowgraph = True
            
            if connections_str:
                self._parse_connections(connections_str, block_name)
    
    def _parse_connections(self, connections_str: str, source_block: str):
        """Parse connection strings from BlockRunner"""
        if not connections_str:
            return
            
        channel_pattern = PATTERNS['channel_connection']
        
        # Split by commas but respect nested parentheses
        parts = self._split_arguments(connections_str)
        
        for part in parts:
            # Look for channel references like &block.channel
            for match in re.finditer(channel_pattern, part):
                target_block = match.group(1)
                channel_name = match.group(2)
                channel_index = int(match.group(3)) if match.group(3) else None
                
                # In BlockRunner(&source, &target.channel), 
                # source is the source block, target.channel is the destination
                self.connections.append(Connection(
                    source_block=source_block,
                    source_channel="out",  # Generic output name for source
                    target_block=target_block,
                    target_channel=channel_name,
                    channel_index=channel_index
                ))
    
    def _split_arguments(self, args_str: str) -> List[str]:
        """Split arguments respecting parentheses and brackets"""
        parts = []
        current = ""
        depth = 0
        
        for char in args_str:
            if char in '([':
                depth += 1
            elif char in ')]':
                depth -= 1
            elif char == ',' and depth == 0:
                parts.append(current.strip())
                current = ""
                continue
            current += char
        
        if current.strip():
            parts.append(current.strip())
        
        return parts
    
    def _infer_channel_directions(self, content: str):
        """Infer input/output channels from member declarations and connections"""
        # Extract channel member declarations for each block
        self._extract_channel_members(content)
        # Infer directions from BlockRunner connections
        self._infer_from_connections()
    
    def _extract_channel_members(self, content: str):
        """Extract channel member variables from block definitions"""
        # Find all channel member declarations
        channel_pattern = PATTERNS['channel_member']
        
        # First, find block class definitions
        for block_name, block in self.blocks.items():
            # Look for the struct definition of this block type
            struct_pattern = rf'struct\s+{re.escape(block.type)}.*?\{{([^}}]+)\}}'
            
            struct_match = re.search(struct_pattern, content, re.DOTALL)
            if struct_match:
                struct_body = struct_match.group(1)
                
                # Find channel declarations in the struct
                for channel_match in re.finditer(channel_pattern, struct_body):
                    channel_type = channel_match.group(1)
                    channel_name = channel_match.group(2)
                    
                    # Store the channel type for validation
                    if channel_name not in block.channel_types:
                        block.channel_types[channel_name] = channel_type.strip()
                    
                    # Channels declared as members are typically inputs
                    if channel_name not in block.inputs:
                        block.inputs.append(channel_name)
    
    def _infer_from_connections(self):
        """Simply record what channels are used in connections"""
        # Just add channels as they appear in connections
        # Validation rules can determine if they're correct or not
        for conn in self.connections:
            source_block = self.blocks.get(conn.source_block)
            target_block = self.blocks.get(conn.target_block)
            
            # Add output channels as used
            if source_block and conn.source_channel not in source_block.outputs:
                source_block.outputs.append(conn.source_channel)
            
            # Add input channels as used  
            if target_block and conn.target_channel not in target_block.inputs:
                target_block.inputs.append(conn.target_channel)