#!/usr/bin/env python3
"""
Cler Flowgraph Visualizer

Generates visual representations of Cler C++ flowgraphs as SVG images.

Usage:
    cler-viz file.cpp [-o output.svg]
    cler-viz *.cpp --output-dir ./diagrams/
"""

import sys
import argparse
from pathlib import Path

from cler_tools.common import ClerParser
from .svg_renderer import SVGRenderer
from .graph_builder import GraphBuilder


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
        help='Output SVG file (for single input file)'
    )
    parser.add_argument(
        '--output-dir',
        help='Output directory for multiple files'
    )
    parser.add_argument(
        '--layout',
        choices=['hierarchical', 'circular', 'force'],
        default='hierarchical',
        help='Layout algorithm to use'
    )
    parser.add_argument(
        '--show-channels',
        action='store_true',
        help='Show channel names on connections'
    )
    parser.add_argument(
        '--compact',
        action='store_true',
        help='Use compact layout with minimal spacing'
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
            
            # Build graph structure
            graph_builder = GraphBuilder()
            graph = graph_builder.build(flowgraph)
            
            # Render to SVG
            renderer = SVGRenderer(
                show_channels=args.show_channels,
                compact=args.compact,
                layout=args.layout
            )
            svg_content = renderer.render(graph)
            
            # Determine output path
            if args.output:
                output_path = Path(args.output)
            elif args.output_dir:
                output_path = output_dir / f"{path.stem}_flowgraph.svg"
            else:
                output_path = path.with_suffix('.svg')
            
            # Write SVG file
            with open(output_path, 'w') as f:
                f.write(svg_content)
            
            print(f"Generated: {output_path}")
            
        except Exception as e:
            print(f"Error processing {filepath}: {e}", file=sys.stderr)
            import sys
            if sys.stdout.isatty():
                import traceback
                traceback.print_exc()


if __name__ == '__main__':
    main()