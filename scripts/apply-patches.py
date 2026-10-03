#!/usr/bin/env python3
"""Apply manual patches to generated code after codegen."""
import re
import sys
from pathlib import Path

GENERATED_DIR = Path("/home/hatch/workspace/splosion-man/generated/default")

def patch_file(filepath, old, new, desc):
    text = filepath.read_text()
    count = text.count(old)
    if count == 0:
        print(f"SKIP {desc}: pattern not found in {filepath.name}")
        return False
    if count > 1:
        print(f"WARN {desc}: found {count} matches in {filepath.name}, patching all")
    text = text.replace(old, new)
    filepath.write_text(text)
    print(f"OK {desc}: patched {filepath.name}")
    return True

def main():
    # Patch 1: 0x82604FB4 unresolved - it's a loop header in sub_82604FA4
    # The branch from 0x82605040 jumps to the loop. We can't goto across
    # functions, so call the function (it will re-enter the loop).
    # Declaration is in splosionman_funcs.h (added to per-file headers manually).
    for cpp in GENERATED_DIR.glob("*.cpp"):
        text = cpp.read_text()
        if "0x82604FB4" in text and "FATAL: unresolved" in text:
            old = '// FATAL: unresolved function 0x82604FB4 (no CallTarget in FunctionNode)\n\tREX_FATAL("Unresolved call from 0x82605040 to 0x82604FB4");'
            new = '// Patched: branch to loop header 0x82604FB4 in sub_82604FA4\n\tsub_82604FA4(ctx, base);\n\treturn;'
            if old in text:
                patch_file(cpp, old, new, "82604FB4 FATAL")
    
    # Patch 2: Null check for r3 before bctrl at 0x825F807C
    # Prevents null pointer dereference
    target = GENERATED_DIR / "splosionman_recomp.84.cpp"
    if target.exists():
        old = """\t// beq cr6,0x825f807c
\tif (ctx.cr6.eq) goto loc_825F807C;
\t// mtctr r11
\tctx.ctr.u64 = ctx.r11.u64;
\t// bctrl 
\tctx.lr = 0x825F807C;
\tREX_CALL_INDIRECT_FUNC(ctx.ctr.u32);"""
        new = """\t// beq cr6,0x825f807c
\tif (ctx.cr6.eq) goto loc_825F807C;
\t// Patched: skip if r3 is null (avoid null deref)
\tif (ctx.r3.u32 == 0) goto loc_825F807C;
\t// mtctr r11
\tctx.ctr.u64 = ctx.r11.u64;
\t// bctrl 
\tctx.lr = 0x825F807C;
\tREX_CALL_INDIRECT_FUNC(ctx.ctr.u32);"""
        patch_file(target, old, new, "r3 null check")
    
    print("Done.")

if __name__ == "__main__":
    main()
