"""
Common regex patterns for parsing Cler C++ code.
Shared between linter and visualization tools.
"""

PATTERNS = {
    # Block detection patterns
    'block_inheritance': r'struct\s+(\w+)\s*:\s*public\s+cler::BlockBase',
    'block_instance': r'(\w+(?:Block|block))\s*(?:<.*?>)?\s+(\w+)\s*\(',
    'block_instance_detailed': r'(\w+(?:Block|block))\s*(?:<([^>]*(?:<[^>]*>[^>]*)*?)>)?\s+(\w+)\s*\(([^)]*)\)',
    'block_instance_any': r'(\w+)\s+(\w+)\s*\([^)]*\);\s*$',
    'channel_member': r'cler::Channel<([^>]*(?:<[^>]*>[^>]*)*)>\s+(\w+);',
    
    # Procedure and connection patterns
    'procedure_signature': r'procedure\s*\(\s*(.*?)\s*\)',
    'channelbase_param': r'cler::ChannelBase<[^>]*>\s*\*\s*(\w+)',
    
    # Flowgraph patterns
    'flowgraph_call': r'make_\w+_flowgraph\s*\(',
    'blockrunner_call': r'BlockRunner\s*\(\s*&(\w+)(?:\s*,\s*([^)]+))?\)',
    
    # Channel operations for inferring I/O
    'channel_ops': r'(\w+)\.(push|pop|writeN|readN|commit_read|commit_write|size|space)\s*\(',
    
    # Connection parsing
    'channel_connection': r'&(\w+)\.(\w+)(?:\[(\d+)\])?',
    
    # Streamlined mode detection
    'manual_procedure': r'(\w+)\.procedure\s*\(',
    
    # Block type definitions (for future cler-flow)
    'block_struct': r'struct\s+(\w+)\s*(?::\s*public\s+cler::BlockBase)?\s*\{([^}]+)\}',
    'template_block': r'template\s*<[^>]+>\s*struct\s+(\w+)',
}

# Operation types for channel direction inference
INPUT_OPERATIONS = {'pop', 'readN', 'commit_read', 'size'}
OUTPUT_OPERATIONS = {'push', 'writeN', 'commit_write', 'space'}