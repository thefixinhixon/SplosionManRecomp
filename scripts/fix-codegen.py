#!/usr/bin/env python3
"""Apply manual fixes to codegen output for unresolved branches/calls.

Codegen sometimes fails to resolve branch/call targets that do exist in the
generated code, emitting REX_FATAL instead. This script patches those
sites after codegen runs. It's idempotent - safe to run multiple times.

For each unresolved site where the target function exists in the generated
code, it adds a forward declaration and replaces the FATAL with a direct call.
"""
import os
import re
import sys
import glob

def get_function_map(gen_dir):
    """Map lowercase function name -> actual case for all defined functions."""
    funcs = {}
    for path in glob.glob(os.path.join(gen_dir, 'condemned2recomp_recomp.*.cpp')):
        with open(path) as f:
            for m in re.finditer(r'DEFINE_REX_FUNC\((sub_[0-9A-Fa-f]+)\)', f.read()):
                funcs[m.group(1).lower()] = m.group(1)
    return funcs

def patch_unresolved_calls(gen_dir, funcs):
    """Find and fix all unresolved call/branch sites with existing targets."""
    fixed = 0
    skipped = 0
    
    for path in glob.glob(os.path.join(gen_dir, 'condemned2recomp_recomp.*.cpp')):
        with open(path) as f:
            content = f.read()
        orig = content
        
        # Find all unresolved sites
        # Pattern 1: REX_FATAL("Unresolved call from 0xAAAA to 0xBBBB");
        for m in re.finditer(
            r'([ \t]*)// (b|bl) 0x[0-9a-f]+\n'
            r'[ \t]*// FATAL: unresolved function 0x([0-9A-Fa-f]+) \(no CallTarget in FunctionNode\)\n'
            r'[ \t]*REX_FATAL\("Unresolved call from 0x[0-9A-Fa-f]+ to 0x[0-9A-Fa-f]+"\);\n'
            r'[ \t]*return;',
            content
        ):
            target = m.group(3)
            func_lower = f"sub_{target}".lower()
            if func_lower in funcs:
                func = funcs[func_lower]  # Use actual case
                # Add forward declaration if not present
                decl = f"DECLARE_REX_FUNC({func});"
                if decl not in content:
                    # Insert after first #include
                    content = content.replace(
                        '\n\nDEFINE_REX_FUNC',
                        f'\n{decl}  // for manually resolved call\n\nDEFINE_REX_FUNC',
                        1
                    )
                # Replace the FATAL with direct call
                old = m.group(0)
                new = (
                    f"{m.group(1)}// {m.group(2)} 0x{target.lower()} (manually resolved)\n"
                    f"{m.group(1)}{func}(ctx, base);\n"
                    f"{m.group(1)}return;"
                )
                content = content.replace(old, new)
                fixed += 1
                print(f"  Fixed call to 0x{target} in {os.path.basename(path)}")
            else:
                skipped += 1
        
        # Pattern 2: conditional branch to function (any comment format)
        #   if (cond) REX_FATAL("Unresolved branch from 0xAAAA to 0xBBBB");
        for m in re.finditer(
            r'[ \t]*if \(([^)]+)\) REX_FATAL\("Unresolved branch from 0x[0-9A-Fa-f]+ to 0x([0-9A-Fa-f]+)"\);',
            content
        ):
            target = m.group(2)
            cond = m.group(1)
            func_lower = f"sub_{target}".lower()
            if func_lower in funcs:
                func = funcs[func_lower]  # Use actual case
                decl = f"DECLARE_REX_FUNC({func});"
                if decl not in content:
                    content = content.replace(
                        '\n\nDEFINE_REX_FUNC',
                        f'\n{decl}  // for manually resolved branch\n\nDEFINE_REX_FUNC',
                        1
                    )
                old = m.group(0)
                # Preserve leading whitespace
                ws = old[:len(old) - len(old.lstrip())]
                new = (
                    f"{ws}// conditional branch to 0x{target.lower()} (manually resolved)\n"
                    f"{ws}if ({cond}) {{\n"
                    f"{ws}\t{func}(ctx, base);\n"
                    f"{ws}\treturn;\n"
                    f"{ws}}}"
                )
                content = content.replace(old, new)
                fixed += 1
                print(f"  Fixed branch to 0x{target} in {os.path.basename(path)}")
            else:
                skipped += 1
        
        # Pattern 3: conditional branch to local label
        #   // beq 0xXXXXXXXX
        #   // ERROR: conditional branch to unknown address 0xXXXXXXXX
        #   if (cond) REX_FATAL("Unresolved branch from 0xAAAA to 0xXXXXXXXX");
        # where loc_XXXXXXXX: exists in the same file
        for m in re.finditer(
            r'([ \t]*)// b\w+ 0x[0-9a-f]+\n'
            r'[ \t]*// ERROR: conditional branch to unknown address 0x([0-9A-Fa-f]+)\n'
            r'[ \t]*if \(([^)]+)\) REX_FATAL\("Unresolved branch from 0x[0-9A-Fa-f]+ to 0x[0-9A-Fa-f]+"\);',
            content
        ):
            target = m.group(2)
            cond = m.group(3)
            label = f"loc_{target.upper()}:"
            # Check if the label exists in this file
            if label in content or f"loc_{target.lower()}:" in content:
                # Use the actual label case found
                actual_label = label if label in content else f"loc_{target.lower()}:"
                label_name = actual_label[:-1]  # Remove the colon
                old = m.group(0)
                new = (
                    f"{m.group(1)}// conditional branch to {label_name} (manually resolved)\n"
                    f"{m.group(1)}if ({cond}) goto {label_name};"
                )
                content = content.replace(old, new)
                fixed += 1
                print(f"  Fixed local branch to {label_name} in {os.path.basename(path)}")
            else:
                skipped += 1
        
        if content != orig:
            with open(path, 'w') as f:
                f.write(content)
    
    print(f"\nFixed {fixed} sites, skipped {skipped} (target not found)")
    return 0

def main():
    gen_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'generated', 'default')
    if not os.path.isdir(gen_dir):
        print(f"Generated dir not found: {gen_dir}", file=sys.stderr)
        return 1
    
    print("Building function map...")
    funcs = get_function_map(gen_dir)
    print(f"Found {len(funcs)} defined functions")
    
    print("\nPatching unresolved calls/branches...")
    return patch_unresolved_calls(gen_dir, funcs)

if __name__ == '__main__':
    sys.exit(main())
