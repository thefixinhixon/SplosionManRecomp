#!/usr/bin/env python3
"""Fix missing loc_XXXXXXXX labels in generated code.

Codegen sometimes emits 'goto loc_XXXXXXXX' where the label was never
generated (target outside function bounds). This adds the missing labels
at the end of the containing function so the code compiles.
"""
import re
import glob
import os

def fix_file(path):
    with open(path) as f:
        content = f.read()
    
    # Find all goto loc_XXXXXXXX
    gotos = set(re.findall(r'goto (loc_[0-9A-Fa-f]+);', content))
    # Find all defined labels
    labels = set(re.findall(r'^(loc_[0-9A-Fa-f]+):', content, re.MULTILINE))
    
    missing = gotos - labels
    if not missing:
        return 0
    
    # For each missing label, add it before the closing brace of the containing function
    # Simplest: add all missing labels at the end of each function that references them
    lines = content.split('\n')
    output = []
    current_func = None
    func_missing = set()
    
    for i, line in enumerate(lines):
        # Track function start
        m = re.match(r'DEFINE_REX_FUNC\((\w+)\)', line)
        if m:
            # If previous function had missing labels, add them before its closing brace
            # (we'll do this by tracking)
            current_func = m.group(1)
            func_missing = set()
        
        # Check if this line has a goto to missing label
        gm = re.search(r'goto (loc_[0-9A-Fa-f]+);', line)
        if gm and gm.group(1) in missing:
            func_missing.add(gm.group(1))
        
        # If this is a closing brace at function level and we have missing labels, add them
        if line.strip() == '}' and current_func and func_missing:
            # Add labels before the closing brace
            for label in sorted(func_missing):
                output.append(f'{label}:')
                output.append('\t// (auto-added missing label)')
            func_missing = set()
        
        output.append(line)
    
    with open(path, 'w') as f:
        f.write('\n'.join(output))
    
    return len(missing)

if __name__ == '__main__':
    gen_dir = os.path.join(os.path.dirname(__file__), '..', 'generated', 'default')
    total = 0
    for path in glob.glob(os.path.join(gen_dir, 'splosionman_recomp.*.cpp')):
        fixed = fix_file(path)
        if fixed:
            print(f"Fixed {fixed} labels in {os.path.basename(path)}")
            total += fixed
    print(f"Total: {total} labels fixed")
