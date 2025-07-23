#!/usr/bin/env python3
"""
Cler Flowgraph Visualizer

Generates visual representations of Cler C++ flowgraphs using Graphviz.

Usage:
    cler-viz file.cpp [-o output.svg]
    cler-viz *.cpp --output-dir ./diagrams/
"""

import sys
import argparse
from pathlib import Path

from cler_tools.common import ClerParser
from cler_tools.viz.graphviz_renderer import GraphvizRenderer


def main():
    parser = argparse.ArgumentParser(
        description='Generate visual representations of Cler flowgraphs'
    )
    parser.add_argument(
        'files', 
        nargs='+', 
        help='C++ source files to visualize'
    )
    parser.add_argument(
        '-o', '--output',
        help='Output file (for single input file, without extension)'
    )
    parser.add_argument(
        '--output-dir',
        help='Output directory for multiple files'
    )
    parser.add_argument(
        '--layout',
        choices=['dot', 'neato', 'fdp', 'circo', 'twopi'],
        default='dot',
        help='Graphviz layout algorithm (default: dot)'
    )
    parser.add_argument(
        '--format',
        choices=['svg', 'png', 'pdf', 'ps'],
        default='svg',
        help='Output format (default: svg)'
    )
    
    args = parser.parse_args()
    
    # Validate arguments
    if len(args.files) > 1 and args.output:
        print("Error: Use --output-dir for multiple input files", file=sys.stderr)
        sys.exit(1)
    
    # Create output directory if needed
    if args.output_dir:
        output_dir = Path(args.output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)
    
    # Process each file
    cpp_parser = ClerParser()
    renderer = GraphvizRenderer()
    
    for filepath in args.files:
        path = Path(filepath)
        
        if not path.exists():
            print(f"Error: File not found: {filepath}", file=sys.stderr)
            continue
        
        try:
            # Parse the C++ file
            with open(path, 'r') as f:
                content = f.read()
            
            flowgraph = cpp_parser.parse_file(content, str(path))
            
            if not flowgraph.blocks:
                print(f"Warning: No blocks found in {filepath}", file=sys.stderr)
                continue
            
            # Determine output path (without extension)
            if args.output:
                output_path = args.output
            elif args.output_dir:
                output_path = str(output_dir / f"{path.stem}_flowgraph")
            else:
                # Default to current directory with filename
                output_path = f"{path.stem}_flowgraph"
            
            # Render using Graphviz
            generated_file = renderer.render(
                flowgraph=flowgraph,
                output_path=output_path,
                layout=args.layout,
                format=args.format
            )
            
            print(f"Generated: {generated_file}")
            
        except Exception as e:
            print(f"Error processing {filepath}: {e}", file=sys.stderr)
            if sys.stdout.isatty():
                import traceback
                traceback.print_exc()


if __name__ == '__main__':
    main()