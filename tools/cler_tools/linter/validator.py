"""
Shared validation framework for Cler tools.
Provides a pluggable rule system for validating flowgraph structures.
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import List, Dict, Optional, Type, Set
from ..common.cpp_parser import FlowGraph, Block, Connection


@dataclass
class ValidationError:
    """Represents a validation error or warning"""
    type: str
    message: str
    file: str
    line: int
    column: int = 0
    severity: str = 'error'
    suggestion: Optional[str] = None
    
    def to_dict(self) -> Dict:
        """Convert to dictionary for JSON output"""
        return {
            'type': self.type,
            'message': self.message,
            'file': self.file,
            'line': self.line,
            'column': self.column,
            'severity': self.severity,
            'suggestion': self.suggestion
        }


class ValidationRule(ABC):
    """Abstract base class for validation rules"""
    
    def __init__(self, config: Optional[Dict] = None):
        self.config = config or {}
        self.severity = self.config.get('severity', 'error')
        self.enabled = self.config.get('enabled', True)
    
    @abstractmethod
    def get_rule_name(self) -> str:
        """Return the name of this rule"""
        pass
    
    @abstractmethod
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        """Run validation and return any errors found"""
        pass
    
    def create_error(self, message: str, file_path: str, line: int = 0, 
                    column: int = 0, suggestion: Optional[str] = None) -> ValidationError:
        """Helper to create a validation error with consistent formatting"""
        return ValidationError(
            type=self.get_rule_name(),
            message=message,
            file=file_path,
            line=line,
            column=column,
            severity=self.severity,
            suggestion=suggestion
        )


class MissingRunnerRule(ValidationRule):
    """Check for blocks that don't have BlockRunners (only in flowgraph mode)"""
    
    def get_rule_name(self) -> str:
        return "missing_runner"
    
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        errors = []
        
        # Check if this is streamlined mode (no flowgraph created)
        # If there are blocks but no connections and no blocks marked in_flowgraph, 
        # it's likely streamlined mode
        has_flowgraph = any(block.in_flowgraph for block in flowgraph.blocks.values())
        has_connections = len(flowgraph.connections) > 0
        
        # Only check for missing runners if we're in flowgraph mode
        if has_flowgraph or has_connections:
            for block_name, block in flowgraph.blocks.items():
                if not block.in_flowgraph:
                    errors.append(self.create_error(
                        f"Block '{block_name}' is declared but not added to flowgraph",
                        file_path,
                        block.line,
                        block.column,
                        f"Add 'cler::BlockRunner(&{block_name}, ...)' to your flowgraph"
                    ))
        
        return errors


class InvalidConnectionRule(ValidationRule):
    """Check for invalid connections between blocks"""
    
    def get_rule_name(self) -> str:
        return "invalid_connection"
    
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        errors = []
        
        
        for conn in flowgraph.connections:
            # Check if source block exists
            if conn.source_block not in flowgraph.blocks:
                errors.append(self.create_error(
                    f"Connection references undefined source block '{conn.source_block}'",
                    file_path,
                    suggestion=f"Check that block '{conn.source_block}' is properly declared"
                ))
                continue
            
            # Check if target block exists
            if conn.target_block not in flowgraph.blocks:
                errors.append(self.create_error(
                    f"Connection references undefined target block '{conn.target_block}'",
                    file_path,
                    suggestion=f"Check that block '{conn.target_block}' is properly declared"
                ))
                continue
            
            source_block = flowgraph.blocks[conn.source_block]
            target_block = flowgraph.blocks[conn.target_block]
            
            
            # Check if source has the output channel
            if conn.source_channel not in source_block.outputs:
                errors.append(self.create_error(
                    f"Block '{conn.source_block}' does not have output channel '{conn.source_channel}'",
                    file_path,
                    source_block.line,
                    source_block.column,
                    f"Available outputs: {', '.join(source_block.outputs) or 'none'}"
                ))
            
            # Check if target has the input channel (handle array channels)
            channel_found = conn.target_channel in target_block.inputs
            
            # If not found directly, check if it's an array channel access
            if not channel_found and conn.channel_index is not None:
                indexed_channel = f"{conn.target_channel}[{conn.channel_index}]"
                channel_found = indexed_channel in target_block.inputs
            
            # Also check if the channel base exists (e.g., 'in' when we have 'in[0]', 'in[1]')
            if not channel_found:
                base_channels = [ch.split('[')[0] for ch in target_block.inputs if '[' in ch]
                channel_found = conn.target_channel in base_channels
            
            if not channel_found:
                errors.append(self.create_error(
                    f"Block '{conn.target_block}' does not have input channel '{conn.target_channel}'",
                    file_path,
                    target_block.line,
                    target_block.column,
                    f"Available inputs: {', '.join(target_block.inputs) or 'none'}"
                ))
        
        return errors



class BlockRunnerOrderRule(ValidationRule):
    """Check that BlockRunners are constructed with block first, then channels"""
    
    def get_rule_name(self) -> str:
        return "blockrunner_order"
    
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        errors = []
        
        # Read the file content for text-based analysis
        try:
            with open(file_path, 'r') as f:
                content = f.read()
                lines = content.split('\n')
        except Exception:
            return errors
        
        # Pattern to match BlockRunner calls
        import re
        # Match BlockRunner or cler::BlockRunner with arguments
        blockrunner_pattern = re.compile(
            r'(?:cler::)?BlockRunner\s*\(\s*([^)]+)\s*\)',
            re.MULTILINE | re.DOTALL
        )
        
        for match in blockrunner_pattern.finditer(content):
            args_str = match.group(1)
            # Find line number
            line_num = content[:match.start()].count('\n') + 1
            
            # Parse arguments (handle multi-line)
            args = []
            current_arg = ''
            paren_depth = 0
            bracket_depth = 0
            
            for char in args_str:
                if char == '(' and not (paren_depth == 0 and bracket_depth == 0):
                    paren_depth += 1
                elif char == ')' and paren_depth > 0:
                    paren_depth -= 1
                elif char == '[':
                    bracket_depth += 1
                elif char == ']':
                    bracket_depth -= 1
                elif char == ',' and paren_depth == 0 and bracket_depth == 0:
                    args.append(current_arg.strip())
                    current_arg = ''
                    continue
                
                current_arg += char
            
            # Don't forget the last argument
            if current_arg.strip():
                args.append(current_arg.strip())
            
            # Check if first argument looks like a channel (contains '.')
            if args and len(args) > 0:
                first_arg = args[0].strip()
                # Remove leading & if present
                if first_arg.startswith('&'):
                    first_arg = first_arg[1:]
                
                # Check if it looks like a channel access (contains '.')
                if '.' in first_arg and not first_arg.endswith(')'):
                    # This looks like a channel as first argument, which is wrong
                    errors.append(self.create_error(
                        f"BlockRunner arguments in wrong order: channel '{first_arg}' appears before block pointer",
                        file_path,
                        line_num,
                        0,
                        "BlockRunner expects block pointer first, then channels: BlockRunner(&block, &other_block.channel)"
                    ))
        
        return errors


class MultipleConnectionsRule(ValidationRule):
    """Check for multiple connections to the same input channel"""
    
    def get_rule_name(self) -> str:
        return "multiple_connections"
    
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        errors = []
        
        # Track which input channels have been connected
        connected_inputs = set()
        
        for conn in flowgraph.connections:
            # Create a unique key for the input channel
            input_key = f"{conn.target_block}.{conn.target_channel}"
            if conn.channel_index is not None:
                input_key += f"[{conn.channel_index}]"
            
            if input_key in connected_inputs:
                errors.append(self.create_error(
                    f"Multiple connections to input channel '{input_key}'",
                    file_path,
                    0,  # We don't have line info for connections
                    0,
                    "Each input channel can only have one connection"
                ))
            else:
                connected_inputs.add(input_key)
        
        return errors


class DuplicateDisplayNamesRule(ValidationRule):
    """Warn about blocks with duplicate display names (constructor argument)"""
    
    def get_rule_name(self) -> str:
        return "duplicate_display_names"
    
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        errors = []
        
        # Track display names from constructor args
        display_names = {}
        
        for block_name, block in flowgraph.blocks.items():
            # The first constructor argument is typically the display name
            if block.constructor_args and len(block.constructor_args) > 0:
                display_name = block.constructor_args[0].strip('"\'')
                
                if display_name in display_names:
                    # This is a warning, not an error
                    errors.append(ValidationError(
                        type=self.get_rule_name(),
                        message=f"Duplicate display name '{display_name}' used by blocks '{display_names[display_name]}' and '{block_name}'",
                        file=file_path,
                        line=block.line,
                        column=block.column,
                        severity='warning',
                        suggestion="Consider using unique display names for easier debugging"
                    ))
                else:
                    display_names[display_name] = block_name
        
        return errors


class UnconnectedBlocksRule(ValidationRule):
    """Check for blocks that are in flowgraph but not properly connected"""
    
    def get_rule_name(self) -> str:
        return "unconnected_blocks"
    
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        errors = []
        
        # Only check blocks that are in the flowgraph
        for block_name, block in flowgraph.blocks.items():
            if not block.in_flowgraph:
                continue
            
            # Check if block has any connections
            has_input_connection = any(
                conn.target_block == block_name 
                for conn in flowgraph.connections
            )
            has_output_connection = any(
                conn.source_block == block_name 
                for conn in flowgraph.connections
            )
            
            # Use naming conventions to determine block type
            block_type_lower = block.type.lower()
            
            # Sources typically have "source" in their name
            if 'source' in block_type_lower and not has_output_connection:
                errors.append(self.create_error(
                    f"Source block '{block_name}' has no output connections",
                    file_path,
                    block.line,
                    block.column,
                    "Connect the source output to another block"
                ))
            
            # Sinks typically have "sink" in their name
            elif 'sink' in block_type_lower and not has_input_connection:
                errors.append(self.create_error(
                    f"Sink block '{block_name}' has no input connections",
                    file_path,
                    block.line,
                    block.column,
                    "Connect an output to this sink's input"
                ))
            
            # For other blocks, if they have both input and output channels referenced
            # in connections elsewhere, but this instance is not connected
            elif not ('source' in block_type_lower or 'sink' in block_type_lower):
                # Check if this type of block typically has inputs/outputs based on other instances
                other_blocks_of_type = [b for n, b in flowgraph.blocks.items() 
                                       if b.type == block.type and n != block_name]
                
                typically_has_inputs = any(b.inputs for b in other_blocks_of_type)
                typically_has_outputs = any(b.outputs for b in other_blocks_of_type)
                
                if typically_has_inputs and not has_input_connection:
                    errors.append(self.create_error(
                        f"Block '{block_name}' has no input connections",
                        file_path,
                        block.line,
                        block.column,
                        "Connect outputs from other blocks to this block's inputs"
                    ))
                if typically_has_outputs and not has_output_connection:
                    errors.append(self.create_error(
                        f"Block '{block_name}' has no output connections",
                        file_path,
                        block.line,
                        block.column,
                        "Connect this block's outputs to other blocks"
                    ))
        
        return errors


class RuleEngine:
    """Executes multiple validation rules on a flowgraph"""
    
    def __init__(self):
        self.rules: List[ValidationRule] = []
        self.rule_registry: Dict[str, Type[ValidationRule]] = {
            'missing_runner': MissingRunnerRule,
            'invalid_connection': InvalidConnectionRule,
            'blockrunner_order': BlockRunnerOrderRule,
            'multiple_connections': MultipleConnectionsRule,
            'unconnected_blocks': UnconnectedBlocksRule,
            'duplicate_display_names': DuplicateDisplayNamesRule,
        }
    
    def add_rule(self, rule: ValidationRule):
        """Add a validation rule to the engine"""
        if rule.enabled:
            self.rules.append(rule)
    
    def add_rule_by_name(self, rule_name: str, config: Optional[Dict] = None):
        """Add a rule by name with optional configuration"""
        if rule_name in self.rule_registry:
            rule_class = self.rule_registry[rule_name]
            rule = rule_class(config)
            self.add_rule(rule)
        else:
            raise ValueError(f"Unknown rule: {rule_name}")
    
    def configure_from_dict(self, config: Dict):
        """Configure rules from a configuration dictionary"""
        rules_config = config.get('rules', {})
        
        for rule_name, rule_config in rules_config.items():
            if rule_config.get('enabled', True):
                self.add_rule_by_name(rule_name, rule_config)
    
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        """Run all enabled rules and return combined errors"""
        all_errors = []
        
        for rule in self.rules:
            try:
                errors = rule.validate(flowgraph, file_path)
                all_errors.extend(errors)
            except Exception as e:
                # If a rule fails, create an error for it
                all_errors.append(ValidationError(
                    type='rule_error',
                    message=f"Rule '{rule.get_rule_name()}' failed: {e}",
                    file=file_path,
                    line=0,
                    severity='error'
                ))
        
        return all_errors