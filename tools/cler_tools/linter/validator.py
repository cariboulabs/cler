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
            
            # Check if target has the input channel
            if conn.target_channel not in target_block.inputs:
                errors.append(self.create_error(
                    f"Block '{conn.target_block}' does not have input channel '{conn.target_channel}'",
                    file_path,
                    target_block.line,
                    target_block.column,
                    f"Available inputs: {', '.join(target_block.inputs) or 'none'}"
                ))
        
        return errors


class ChannelTypeMismatchRule(ValidationRule):
    """Check for channel type mismatches between connected blocks"""
    
    def get_rule_name(self) -> str:
        return "channel_type_mismatch"
    
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        errors = []
        
        for conn in flowgraph.connections:
            # Skip if blocks don't exist (handled by other rules)
            if (conn.source_block not in flowgraph.blocks or 
                conn.target_block not in flowgraph.blocks):
                continue
            
            source_block = flowgraph.blocks[conn.source_block]
            target_block = flowgraph.blocks[conn.target_block]
            
            # Get channel types if available
            source_type = source_block.channel_types.get(conn.source_channel)
            target_type = target_block.channel_types.get(conn.target_channel)
            
            if source_type and target_type and source_type != target_type:
                if not self._are_compatible_types(source_type, target_type):
                    errors.append(self.create_error(
                        f"Type mismatch: {conn.source_block}.{conn.source_channel} "
                        f"({source_type}) → {conn.target_block}.{conn.target_channel} ({target_type})",
                        file_path,
                        target_block.line,
                        target_block.column,
                        f"Change channel types to match or add appropriate conversion"
                    ))
        
        return errors
    
    def _are_compatible_types(self, source_type: str, target_type: str) -> bool:
        """Check if two channel types are compatible"""
        # Normalize type strings
        source_normalized = source_type.strip().replace(' ', '')
        target_normalized = target_type.strip().replace(' ', '')
        
        # Exact match
        if source_normalized == target_normalized:
            return True
        
        # Add more sophisticated type compatibility checking here
        # For example: float and double might be compatible
        compatible_pairs = [
            ('float', 'double'),
            ('double', 'float'),
        ]
        
        for t1, t2 in compatible_pairs:
            if ((source_normalized == t1 and target_normalized == t2) or
                (source_normalized == t2 and target_normalized == t1)):
                return True
        
        return False


class BlockRunnerOrderRule(ValidationRule):
    """Check that BlockRunners are constructed with block first, then channels"""
    
    def get_rule_name(self) -> str:
        return "blockrunner_order"
    
    def validate(self, flowgraph: FlowGraph, file_path: str) -> List[ValidationError]:
        errors = []
        
        # This rule would need to analyze the raw C++ text to check BlockRunner construction
        # For now, we'll implement basic checks based on the parsed structure
        # More sophisticated parsing could be added later
        
        return errors


class RuleEngine:
    """Executes multiple validation rules on a flowgraph"""
    
    def __init__(self):
        self.rules: List[ValidationRule] = []
        self.rule_registry: Dict[str, Type[ValidationRule]] = {
            'missing_runner': MissingRunnerRule,
            'invalid_connection': InvalidConnectionRule,
            'channel_type_mismatch': ChannelTypeMismatchRule,
            'blockrunner_order': BlockRunnerOrderRule,
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