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
    
    def _extract_blocks(self, content: str):
        """Extract block instances from the code"""
        pattern = PATTERNS['block_instance']
        
        for match in re.finditer(pattern, content):
            block_type, var_name = match.groups()
            line, col = self._get_line_column(content, match.start())
            
            if var_name not in self.blocks:
                self.blocks[var_name] = Block(
                    name=var_name,
                    type=block_type,
                    line=line,
                    column=col
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
        channel_pattern = PATTERNS['channel_connection']
        
        # Split by commas but respect nested parentheses
        parts = self._split_arguments(connections_str)
        
        for i, part in enumerate(parts):
            # Skip the first part if it's just the procedure name
            if i == 0 and 'procedure' in part:
                continue
            
            # Look for channel references
            for match in re.finditer(channel_pattern, part):
                target_block = match.group(1)
                channel_name = match.group(2)
                channel_index = int(match.group(3)) if match.group(3) else None
                
                # Determine if this is input or output based on position
                # In BlockRunner, early arguments are typically outputs
                is_output = i < len(parts) // 2
                
                if is_output:
                    self.connections.append(Connection(
                        source_block=source_block,
                        source_channel=f"out_{i}",  # Generic output name
                        target_block=target_block,
                        target_channel=channel_name,
                        channel_index=channel_index
                    ))
                else:
                    self.connections.append(Connection(
                        source_block=target_block,
                        source_channel=channel_name,
                        target_block=source_block,
                        target_channel=f"in_{i-len(parts)//2}",  # Generic input name
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
        """Infer input/output channels from operations"""
        ops_pattern = PATTERNS['channel_ops']
        
        for match in re.finditer(ops_pattern, content):
            channel_var = match.group(1)
            operation = match.group(2)
            
            # Find which block this operation belongs to
            # This is a simplified approach - in practice we'd need scope analysis
            for block_name, block in self.blocks.items():
                # Check if operation appears near block definition
                if operation in INPUT_OPERATIONS:
                    if channel_var not in block.inputs:
                        block.inputs.append(channel_var)
                elif operation in OUTPUT_OPERATIONS:
                    if channel_var not in block.outputs:
                        block.outputs.append(channel_var)