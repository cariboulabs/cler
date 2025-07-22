"""
Data structures for representing parsed flowgraph information.
"""

from dataclasses import dataclass, field
from typing import List, Dict, Optional


@dataclass
class Block:
    """Represents a Cler block instance"""
    name: str  # Variable name (e.g., "source1", "adder", "plot")
    type: str  # Block type name (e.g., "SourceCWBlock", "AddBlock")
    line: int  # Line number where block is declared in source file
    column: int = 0  # Column position where block is declared
    inputs: List[str] = field(default_factory=list)  # Input channel names (e.g., ["in[0]", "in[1]"])
    outputs: List[str] = field(default_factory=list)  # Output channel names (e.g., ["out"])
    has_runner: bool = False  # Legacy field, not used by AST parser
    in_flowgraph: bool = False  # True if block is referenced in flowgraph BlockRunner calls
    template_params: Optional[str] = None  # Template parameters (e.g., "float" from "Block<float>")
    constructor_args: List[str] = field(default_factory=list)  # Constructor arguments list
    channel_types: Dict[str, str] = field(default_factory=dict)  # Channel name -> data type mapping
    
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
    source_block: str  # Name of source block (e.g., "source1")
    source_channel: str  # Source channel name (usually "out")
    target_block: str  # Name of target block (e.g., "adder")
    target_channel: str  # Target channel name (e.g., "in")
    channel_index: Optional[int] = None  # Array index if connecting to indexed channel (e.g., 0 for "in[0]")


@dataclass
class FlowGraph:
    """Complete flowgraph structure"""
    name: str  # Flowgraph name (e.g., "desktop" from make_desktop_flowgraph or filename)
    blocks: Dict[str, Block] = field(default_factory=dict)  # Block name -> Block mapping
    connections: List[Connection] = field(default_factory=list)  # List of all connections between blocks