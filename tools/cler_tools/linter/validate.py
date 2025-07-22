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
from dataclasses import asdict
from typing import List, Dict, Set, Optional, Tuple
import yaml

from cler_tools.common import ClerParser
from cler_tools.linter.validator import ValidationError, RuleEngine


class ClerValidator:
    """Main validator class for Cler flowgraph C++ code"""
    
    def __init__(self, config_file: Optional[str] = None):
        self.parser = ClerParser()
        self.rule_engine = RuleEngine()
        self.config = self._load_config(config_file)
        self._configure_rules()
    
    def _load_config(self, config_file: Optional[str]) -> Dict:
        """Load validation rules from config file or use defaults"""
        default_config = {
            'rules': {
                'missing_runner': {'severity': 'error', 'enabled': True},
                'invalid_connection': {'severity': 'error', 'enabled': True},
                'channel_type_mismatch': {'severity': 'error', 'enabled': True},
                'blockrunner_order': {'severity': 'error', 'enabled': True}
            }
        }
        
        if config_file and Path(config_file).exists():
            with open(config_file, 'r') as f:
                user_config = yaml.safe_load(f)
                # Merge user config with defaults
                if 'rules' in user_config:
                    default_config['rules'].update(user_config['rules'])
        
        return default_config
    
    def _configure_rules(self):
        """Configure the rule engine with loaded configuration"""
        self.rule_engine.configure_from_dict(self.config)
    
    def validate_file(self, filepath: str) -> List[ValidationError]:
        """Validate a single C++ file"""
        try:
            with open(filepath, 'r') as f:
                content = f.read()
            
            # Parse the file
            flowgraph = self.parser.parse_file(content, filepath)
            
            # Run all configured validation rules
            errors = self.rule_engine.validate(flowgraph, filepath)
            
            return errors
            
        except Exception as e:
            return [ValidationError(
                type='parse_error',
                message=f"Failed to parse file: {e}",
                file=filepath,
                line=0,
                severity='error'
            )]


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