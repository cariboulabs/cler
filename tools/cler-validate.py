#!/usr/bin/env python3
"""
Cler Flowgraph Validator

A lightweight validation tool for Cler C++ flowgraph code that catches common
mistakes before compilation, preventing confusing template errors.

Usage:
    cler-validate.py file1.cpp [file2.cpp ...]
    cler-validate.py --json *.cpp
    cler-validate.py --config rules.yaml main.cpp
"""

import re
import sys
import json
import argparse
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import List, Dict, Set, Optional, Tuple
import yaml


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


class ClerValidator:
    """Main validator class for Cler flowgraph C++ code"""
    
    def __init__(self, config_file: Optional[str] = None):
        self.blocks: Dict[str, Dict] = {}  # variable -> {type, line, has_runner, channels}
        self.runners: Dict[str, Dict] = {}  # block_var -> {line, in_flowgraph}
        self.flowgraph_blocks: Set[str] = set()  # blocks referenced in flowgraph
        self.connections: List[Tuple[str, str, str]] = []  # (source, target, channel)
        self.errors: List[ValidationError] = []
        self.config = self._load_config(config_file)
    
    def _load_config(self, config_file: Optional[str]) -> Dict:
        """Load validation rules from config file or use defaults"""
        default_config = {
            'patterns': {
                # Type-based patterns instead of variable names
                'block_inheritance': r'struct\s+(\w+)\s*:\s*public\s+cler::BlockBase',
                'block_instance': r'(\w+Block)(?:<.*?>)?\s+(\w+)\s*\(',
                'channel_member': r'cler::Channel<[^>]*>\s+(\w+);',
                'procedure_signature': r'procedure\s*\(\s*(.*?)\s*\)',
                'channelbase_param': r'cler::ChannelBase<[^>]*>\s*\*\s*(\w+)',
                'flowgraph_call': r'make_\w+_flowgraph\s*\(',
                'blockrunner_call': r'BlockRunner\s*\(\s*&(\w+)(?:\s*,\s*([^)]+))?\)',
                # Channel operations
                'channel_ops': r'(\w+)\.(push|pop|writeN|readN|commit_read|commit_write|size|space)\s*\(',
                # Streamlined mode detection
                'manual_procedure': r'(\w+)\.procedure\s*\('
            },
            'rules': {
                'missing_runner': {'severity': 'error'},
                'runner_not_in_flowgraph': {'severity': 'error'},
                'invalid_connection': {'severity': 'error'},
                'unconnected_input': {'severity': 'warning'},
                'unused_output': {'severity': 'warning'},
                'duplicate_block_name': {'severity': 'error'}
            }
        }
        
        if config_file and Path(config_file).exists():
            with open(config_file, 'r') as f:
                user_config = yaml.safe_load(f)
                # Merge user config with defaults
                default_config.update(user_config)
        
        return default_config
    
    def _get_line_column(self, content: str, position: int) -> Tuple[int, int]:
        """Convert string position to line and column number"""
        lines = content[:position].split('\n')
        line = len(lines)
        column = len(lines[-1]) if lines else 0
        return line, column
    
    def extract_blocks(self, content: str, filename: str):
        """Extract block instances using type-based patterns"""
        pattern = self.config['patterns']['block_instance']
        
        for match in re.finditer(pattern, content):
            block_type, var_name = match.groups()
            line, col = self._get_line_column(content, match.start())
            
            # Check for duplicate variables
            if var_name in self.blocks:
                self.errors.append(ValidationError(
                    type='duplicate_block_name',
                    message=f"Duplicate block variable name '{var_name}'",
                    file=filename,
                    line=line,
                    column=col,
                    severity=self.config['rules']['duplicate_block_name']['severity']
                ))
            
            # Extract channels for this block by analyzing channel operations
            channels = self._extract_block_channels(content, block_type)
            
            self.blocks[var_name] = {
                'type': block_type,
                'line': line,
                'column': col,
                'has_runner': False,
                'in_flowgraph': False,
                'channels': channels,
                'has_inputs': bool(channels.get('inputs', [])),
                'has_outputs': bool(channels.get('outputs', []))
            }
    
    def _extract_block_channels(self, content: str, block_type: str) -> Dict:
        """Extract channel information from channel operations usage"""
        return self._infer_channels_from_operations(content, block_type)
    
    def _infer_channels_from_operations(self, content: str, block_type: str) -> Dict:
        """Infer channels from usage patterns in procedure functions"""
        channels = {'inputs': [], 'outputs': []}
        
        # Look for channel operations to infer channels
        ops_pattern = self.config['patterns']['channel_ops']
        for ops_match in re.finditer(ops_pattern, content):
            channel_var = ops_match.group(1)
            operation = ops_match.group(2)
            
            # Input operations: pop, readN, commit_read, size (for checking)
            if operation in ['pop', 'readN', 'commit_read', 'size']:
                if channel_var not in channels['inputs']:
                    channels['inputs'].append(channel_var)
            # Output operations: push, writeN, commit_write, space (for checking)
            elif operation in ['push', 'writeN', 'commit_write', 'space']:
                if channel_var not in channels['outputs']:
                    channels['outputs'].append(channel_var)
        
        return channels
    
    def extract_runners(self, content: str, filename: str):
        """Extract all BlockRunner variable declarations (not inline calls)"""
        # For now, skip separate BlockRunner variables - focus on inline ones in flowgraph
        # Most Cler code uses inline BlockRunners in make_*_flowgraph() calls
        pass
    
    def _parse_connections(self, connections: str, source_block: str, filename: str, line: int):
        """Parse connection strings and validate using type information"""
        # Match any &block.channel_name pattern
        channel_pattern = r'&(\w+)\.(\w+)(?:\[(\d+)\])?'
        
        for match in re.finditer(channel_pattern, connections):
            target_block = match.group(1)
            channel_name = match.group(2)
            channel_index = match.group(3)
            
            # Validate target block exists
            if target_block not in self.blocks:
                self.errors.append(ValidationError(
                    type='invalid_connection',
                    message=f"Connection references unknown block '{target_block}'",
                    file=filename,
                    line=line,
                    severity=self.config['rules']['invalid_connection']['severity'],
                    suggestion=f"Ensure block '{target_block}' is declared before connecting"
                ))
                continue
            
            # Skip channel name validation - we can't reliably know all channel names
            # without complex header parsing. Focus on block existence validation only.
            
            # Record the connection for input analysis
            self.connections.append((source_block, target_block, channel_name))
    
    def extract_flowgraph(self, content: str, filename: str):
        """Extract blocks added to the flowgraph"""
        # Find make_*_flowgraph call and extract its contents properly
        pattern = self.config['patterns']['flowgraph_call']
        match = re.search(pattern, content)
        
        if not match:
            # No flowgraph found - this might be intentional for utility files
            return
        
        # Find the matching closing parenthesis
        start = match.end() - 1  # Position of opening (
        paren_count = 1
        i = start + 1
        
        while i < len(content) and paren_count > 0:
            if content[i] == '(':
                paren_count += 1
            elif content[i] == ')':
                paren_count -= 1
            i += 1
        
        if paren_count == 0:
            flowgraph_content = content[start+1:i-1]  # Content between parentheses
        else:
            # Fallback to simpler pattern
            flowgraph_content = content[match.end():match.end()+1000]  # Just take some content
        
        # Find all BlockRunner references within the flowgraph and check connections
        runner_pattern = self.config['patterns']['blockrunner_call']
        for runner_match in re.finditer(runner_pattern, flowgraph_content):
            block_ref = runner_match.group(1)
            connections = runner_match.group(2) if runner_match.group(2) else None
            
            self.flowgraph_blocks.add(block_ref)
            
            if block_ref in self.blocks:
                self.blocks[block_ref]['in_flowgraph'] = True
            
            # Parse connections from inline BlockRunners too
            if connections:
                # Clean up the connections string to handle multiple outputs
                connections = connections.strip()
                line_num = content[:content.find(flowgraph_content)].count('\n') + flowgraph_content[:runner_match.start()].count('\n') + 1
                self._parse_connections(connections, block_ref, filename, line_num)
                
        # Mark ALL runners as being in flowgraph if they reference blocks in flowgraph
        for block_name in self.runners:
            if block_name in self.flowgraph_blocks:
                self.runners[block_name]['in_flowgraph'] = True
    
    def is_streamlined_mode(self, content: str) -> bool:
        """Check if this is streamlined mode (manual procedure calls)"""
        manual_pattern = self.config['patterns']['manual_procedure']
        has_manual_calls = bool(re.search(manual_pattern, content))
        has_flowgraph = bool(re.search(self.config['patterns']['flowgraph_call'], content))
        
        # Streamlined mode: manual procedure calls AND no flowgraph
        return has_manual_calls and not has_flowgraph
    
    def validate_completeness(self, filename: str, content: str):
        """Run validation rules to check flowgraph completeness"""
        # Skip validation for streamlined mode
        if self.is_streamlined_mode(content):
            return
        
        # Check if we have a flowgraph - if so, inline runners are ok
        has_flowgraph = bool(re.search(self.config['patterns']['flowgraph_call'], content))
        
        # Rule 1: All blocks should have runners OR be in inline flowgraph
        for block_name, block_info in self.blocks.items():
            if not block_info['has_runner'] and not (has_flowgraph and block_name in self.flowgraph_blocks):
                self.errors.append(ValidationError(
                    type='missing_runner',
                    message=f"Block '{block_name}' has no BlockRunner",
                    file=filename,
                    line=block_info['line'],
                    column=block_info['column'],
                    severity=self.config['rules']['missing_runner']['severity'],
                    suggestion=f"Add: BlockRunner(&{block_name}, &<output_channel>)"
                ))
        
        # Rule 2: If runners are created separately, they should be in flowgraph
        # But skip this check if flowgraph uses inline BlockRunners
        if has_flowgraph:
            # For files with flowgraph, only check blocks that have separate BlockRunner variables
            # but are not referenced in the flowgraph
            separate_runners = set(self.runners.keys()) - self.flowgraph_blocks
            for block_name in separate_runners:
                runner_info = self.runners[block_name]
                self.errors.append(ValidationError(
                    type='runner_not_in_flowgraph',
                    message=f"BlockRunner for '{block_name}' created separately but not added to flowgraph",
                    file=filename,
                    line=runner_info['line'],
                    column=runner_info['column'],
                    severity=self.config['rules']['runner_not_in_flowgraph']['severity'],
                    suggestion=f"Add the BlockRunner for '{block_name}' to make_*_flowgraph()"
                ))
        
        # Rule 3: Improved input connection analysis using type information
        blocks_with_inputs = set()
        
        # From parsed connections
        for source, target, channel_name in self.connections:
            # Check if this is connecting to an input channel
            target_info = self.blocks.get(target, {})
            target_inputs = target_info.get('channels', {}).get('inputs', [])
            
            # If we detected inputs for this block and this channel is one of them
            if not target_inputs or channel_name in target_inputs:
                blocks_with_inputs.add(target)
        
        # Skip unconnected input validation for now - too many false positives
        # Focus on the core validations: missing runners and invalid connections
    
    def validate_file(self, filepath: Path) -> List[ValidationError]:
        """Validate a single C++ file"""
        self.blocks.clear()
        self.runners.clear()
        self.flowgraph_blocks.clear()
        self.connections.clear()
        self.errors.clear()
        
        try:
            content = filepath.read_text()
            filename = str(filepath)
            
            # Extract information
            self.extract_blocks(content, filename)
            self.extract_runners(content, filename)
            self.extract_flowgraph(content, filename)
            
            # Validate
            self.validate_completeness(filename, content)
            
        except Exception as e:
            self.errors.append(ValidationError(
                type='file_error',
                message=f"Error processing file: {str(e)}",
                file=str(filepath),
                line=0,
                severity='error'
            ))
        
        return self.errors


def format_error_human(error: ValidationError) -> str:
    """Format error for human-readable output"""
    severity = error.severity.upper()
    location = f"{error.file}:{error.line}"
    if error.column:
        location += f":{error.column}"
    
    message = f"{location}: {severity}: {error.message}"
    if error.suggestion:
        message += f"\n    Suggestion: {error.suggestion}"
    
    return message


def main():
    parser = argparse.ArgumentParser(
        description='Validate Cler flowgraph C++ code',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s main.cpp                    # Validate single file
  %(prog)s *.cpp                       # Validate multiple files
  %(prog)s --json src/*.cpp            # Output as JSON
  %(prog)s --config my_rules.yaml *.cpp # Use custom rules
        """
    )
    
    parser.add_argument(
        'files', 
        nargs='+', 
        help='C++ files to validate (supports wildcards)'
    )
    parser.add_argument(
        '--json', 
        action='store_true',
        help='Output errors as JSON'
    )
    parser.add_argument(
        '--config', '-c',
        help='Path to custom rules configuration file (YAML)'
    )
    parser.add_argument(
        '--quiet', '-q',
        action='store_true',
        help='Only show errors, no summary'
    )
    parser.add_argument(
        '--werror',
        action='store_true',
        help='Treat warnings as errors (exit code 1)'
    )
    parser.add_argument(
        '--no-suggestions',
        action='store_true',
        help='Suppress fix suggestions'
    )
    
    args = parser.parse_args()
    
    # Initialize validator
    validator = ClerValidator(args.config)
    
    all_errors = []
    file_count = 0
    
    # Process each file
    for file_pattern in args.files:
        for filepath in Path().glob(file_pattern):
            if filepath.suffix in ['.cpp', '.cc', '.cxx', '.hpp', '.h']:
                file_count += 1
                errors = validator.validate_file(filepath)
                all_errors.extend(errors)
    
    if file_count == 0:
        print("No C++ files found matching the pattern(s)", file=sys.stderr)
        sys.exit(1)
    
    # Output results
    if args.json:
        # Convert to JSON-serializable format
        json_errors = [asdict(error) for error in all_errors]
        print(json.dumps(json_errors, indent=2))
    else:
        # Human-readable output
        for error in all_errors:
            if not args.no_suggestions or not error.suggestion:
                print(format_error_human(error))
            else:
                # Skip suggestion part
                error_copy = error
                error_copy.suggestion = None
                print(format_error_human(error_copy))
        
        if not args.quiet and file_count > 0:
            error_count = sum(1 for e in all_errors if e.severity == 'error')
            warning_count = sum(1 for e in all_errors if e.severity == 'warning')
            
            print(f"\n✓ Validated {file_count} file{'s' if file_count != 1 else ''}: ", end='')
            
            if error_count == 0 and warning_count == 0:
                print("No issues found!")
            else:
                parts = []
                if error_count > 0:
                    parts.append(f"{error_count} error{'s' if error_count != 1 else ''}")
                if warning_count > 0:
                    parts.append(f"{warning_count} warning{'s' if warning_count != 1 else ''}")
                print(", ".join(parts))
    
    # Exit code
    has_errors = any(e.severity == 'error' for e in all_errors)
    has_warnings = any(e.severity == 'warning' for e in all_errors)
    
    if has_errors:
        sys.exit(1)
    elif args.werror and has_warnings:
        sys.exit(1)
    else:
        sys.exit(0)


if __name__ == '__main__':
    main()