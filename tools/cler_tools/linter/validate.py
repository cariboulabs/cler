#!/usr/bin/env python3
"""
Cler Flowgraph Validator

A lightweight validation tool for Cler C++ flowgraph code that catches common
mistakes before compilation, preventing confusing template errors.

Usage:
    cler-validate file1.cpp [file2.cpp ...]
    cler-validate --json *.cpp
    cler-validate --config rules.yaml main.cpp
"""

import re
import sys
import json
import argparse
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import List, Dict, Set, Optional, Tuple
import yaml

from cler_tools.common import ClerParser, PATTERNS


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
        self.parser = ClerParser()
        self.errors: List[ValidationError] = []
        self.config = self._load_config(config_file)
    
    def _load_config(self, config_file: Optional[str]) -> Dict:
        """Load validation rules from config file or use defaults"""
        default_config = {
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
                if 'rules' in user_config:
                    default_config['rules'].update(user_config['rules'])
        
        return default_config
    
    def validate_file(self, filepath: str) -> List[ValidationError]:
        """Validate a single C++ file"""
        self.errors = []
        
        try:
            with open(filepath, 'r') as f:
                content = f.read()
            
            # Parse the file
            flowgraph = self.parser.parse_file(content, filepath)
            
            # Run validation checks
            self._check_missing_runners(flowgraph, filepath)
            self._check_invalid_connections(flowgraph, filepath)
            self._check_unconnected_channels(flowgraph, filepath)
            
        except Exception as e:
            self.errors.append(ValidationError(
                type='parse_error',
                message=f"Failed to parse file: {e}",
                file=filepath,
                line=0,
                severity='error'
            ))
        
        return self.errors
    
    def _check_missing_runners(self, flowgraph, filepath):
        """Check for blocks without runners"""
        for block_name, block in flowgraph.blocks.items():
            if not block.in_flowgraph:
                self.errors.append(ValidationError(
                    type='missing_runner',
                    message=f"Block '{block_name}' is not added to the flowgraph",
                    file=filepath,
                    line=block.line,
                    column=block.column,
                    severity=self.config['rules']['missing_runner']['severity'],
                    suggestion=f"Add BlockRunner(&{block_name}) to the flowgraph"
                ))
    
    def _check_invalid_connections(self, flowgraph, filepath):
        """Check for invalid connections"""
        for conn in flowgraph.connections:
            if conn.target_block not in flowgraph.blocks:
                self.errors.append(ValidationError(
                    type='invalid_connection',
                    message=f"Connection references unknown block '{conn.target_block}'",
                    file=filepath,
                    line=0,  # We'd need to track connection line numbers
                    severity=self.config['rules']['invalid_connection']['severity']
                ))
    
    def _check_unconnected_channels(self, flowgraph, filepath):
        """Check for unconnected input channels"""
        # Track which channels are connected
        connected_inputs = set()
        for conn in flowgraph.connections:
            connected_inputs.add((conn.target_block, conn.target_channel))
        
        # Check each block's inputs
        for block_name, block in flowgraph.blocks.items():
            for input_channel in block.inputs:
                if (block_name, input_channel) not in connected_inputs:
                    self.errors.append(ValidationError(
                        type='unconnected_input',
                        message=f"Input channel '{input_channel}' of block '{block_name}' is not connected",
                        file=filepath,
                        line=block.line,
                        severity=self.config['rules']['unconnected_input']['severity']
                    ))


def main():
    parser = argparse.ArgumentParser(
        description='Validate Cler C++ flowgraph code'
    )
    parser.add_argument(
        'files',
        nargs='+',
        help='C++ source files to validate'
    )
    parser.add_argument(
        '--json',
        action='store_true',
        help='Output results in JSON format'
    )
    parser.add_argument(
        '--config',
        help='Path to configuration file with custom rules'
    )
    parser.add_argument(
        '--no-warnings',
        action='store_true',
        help='Only show errors, not warnings'
    )
    
    args = parser.parse_args()
    
    validator = ClerValidator(args.config)
    all_errors = []
    
    for filepath in args.files:
        errors = validator.validate_file(filepath)
        
        if args.no_warnings:
            errors = [e for e in errors if e.severity == 'error']
        
        all_errors.extend(errors)
    
    # Output results
    if args.json:
        print(json.dumps([asdict(e) for e in all_errors], indent=2))
    else:
        for error in all_errors:
            level = error.severity.upper()
            print(f"{error.file}:{error.line}:{error.column}: {level}: {error.message}")
            if error.suggestion:
                print(f"  Suggestion: {error.suggestion}")
    
    # Exit with error code if there were errors
    error_count = sum(1 for e in all_errors if e.severity == 'error')
    sys.exit(1 if error_count > 0 else 0)


if __name__ == '__main__':
    main()