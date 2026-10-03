#!/usr/bin/env python3
"""Smart patcher for 'Splosion Man generated code.

For each REX_FATAL("Unresolved branch from X to Y"):
  - if label loc_Y is defined in the SAME C++ function -> replace with goto loc_Y
  - else -> replace with return
For REX_FATAL("Unresolved call ...") -> replace with return.
Also applies the r3 null-check before the bctrl callback loop.
"""
import re
import sys
from pathlib import Path

GENERATED = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("generated/default")

BRANCH_RE = re.compile(
    r'if \(([^)]+)\) REX_FATAL\("Unresolved branch from 0x[0-9A-Fa-f]+ to (0x[0-9A-Fa-f]+)"\);'
)
CALL_FATAL_RE = re.compile(r'\tREX_FATAL\("Unresolved call[^"]+"\);')
FUNC_START_RE = re.compile(r'^DEFINE_REX_FUNC\(')

def split_functions(lines):
    """Return list of (start_idx, end_idx) for each function body in lines."""
    starts = [i for i, ln in enumerate(lines) if FUNC_START_RE.match(ln)]
    spans = []
    for j, s in enumerate(starts):
        e = starts[j + 1] if j + 1 < len(starts) else len(lines)
        spans.append((s, e))
    return spans

def find_span(spans, idx):
    for (s, e) in spans:
        if s <= idx < e:
            return (s, e)
    return None

def frag_pass(lines):
    """Replace `sub_X(ctx, base); return;` with `goto loc_X;` when loc_X is
    defined in the same function body (codegen split one function into fragments
    and turned internal branches into calls that skip register restores)."""
    spans = split_functions(lines)
    n = 0
    for (s, e) in spans:
        labs = set()
        for i in range(s, e):
            m = re.match(r'^(loc_[0-9A-Fa-f]+):', lines[i])
            if m:
                labs.add(m.group(1))
        if not labs:
            continue
        i = s
        while i < e:
            ln = lines[i]
            # conditional block form:
            #   if (cond) {
            #       sub_X(ctx, base);
            #       return;
            #   }
            m = re.match(r'^(\t*)if \((.+)\) \{$', ln)
            if m and i + 3 < e:
                m2 = re.match(r'^\t*\tsub_([0-9A-Fa-f]+)\(ctx, base\);$', lines[i + 1])
                if m2 and re.match(r'^\t*\treturn;$', lines[i + 2]) and re.match(r'^\t*\}$', lines[i + 3]):
                    label = "loc_" + m2.group(1).upper()
                    if label in labs:
                        indent = m.group(1)
                        lines[i] = f"{indent}if ({m.group(2)}) goto {label}; // patched frag-call"
                        lines[i + 1] = ""
                        lines[i + 2] = ""
                        lines[i + 3] = ""
                        n += 1
                        i += 4
                        continue
            # unconditional form: sub_X(ctx, base); \n return;
            m2 = re.match(r'^(\t*)sub_([0-9A-Fa-f]+)\(ctx, base\);$', ln)
            if m2 and i + 1 < e and re.match(r'^\t*return;$', lines[i + 1]):
                label = "loc_" + m2.group(2).upper()
                if label in labs:
                    lines[i] = f"{m2.group(1)}goto {label}; // patched frag-call"
                    lines[i + 1] = ""
                    n += 1
                    i += 2
                    continue
            i += 1
    return n


def epilogue_pass(text):
    """For functions whose body can fall off the end (no top-level return at the
    end), synthesize the mirror epilogue from the prologue's saves.
    Codegen drops epilogues for ~27 functions; without them, guest stack and
    callee-saved registers leak on every call."""
    lines = text.split("\n")
    spans = split_functions(lines)
    added = 0
    skip = []
    for (s, e) in spans:
        # find closing brace: last non-empty line before span end
        j = e - 1
        while j > s and lines[j].strip() == "":
            j -= 1
        if j <= s or lines[j].strip() != "}":
            continue
        # last code line before closing brace
        k = j - 1
        while k > s and (lines[k].strip() == "" or lines[k].strip().startswith("//")):
            k -= 1
        if k <= s:
            continue
        if lines[k].strip() == "return;":
            continue  # ends with a return already
        body = lines[s:j]
        # Parse prologue
        frame = None
        saves = []  # (reg, offset) for std saves
        lr_saved = False
        rest_helper = None
        for ln in body[:40]:
            m = re.search(r'ea = (-?\d+) \+ ctx\.r1\.u32;', ln)
            if m and frame is None:
                frame = -int(m.group(1))
            m = re.search(r'REX_STORE_U64\(ctx\.r1\.u32 \+ (-?\d+), ctx\.(r\d+)\.u64\);', ln)
            if m:
                saves.append((m.group(2), int(m.group(1))))
            if 'REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);' in ln:
                lr_saved = True
            m = re.search(r'__(savegprlr_\d+)\(ctx, base\);', ln)
            if m:
                rest_helper = '__rest' + m.group(1)[len('save'):]
        name = lines[s]
        if frame is None:
            skip.append(name)
            continue
        epi = []
        epi.append(f"\t// synthesized epilogue (codegen dropped it)")
        epi.append(f"\tctx.r1.s64 = ctx.r1.s64 + {frame};")
        if rest_helper:
            epi.append(f"\t{rest_helper}(ctx, base);")
        else:
            if lr_saved:
                epi.append("\tctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);")
                epi.append("\tctx.lr = ctx.r12.u64;")
            for reg, off in saves:
                epi.append(f"\tctx.{reg}.u64 = REX_LOAD_U64(ctx.r1.u32 + {off});")
        epi.append("\treturn;")
        for n, el in enumerate(epi):
            lines.insert(j + n, el)
        added += 1
        # spans after this one shift; recompute not needed since we go forward
        # and inserts are tracked by mutating lines + adjusting subsequent spans
        for idx in range(len(spans)):
            s2, e2 = spans[idx]
            if s2 > s:
                spans[idx] = (s2 + len(epi), e2 + len(epi))
    return "\n".join(lines), added, skip


SETJMP_ADDR = "825FAC50"
LONGJMP_ADDR = "825F7CD0"


SETJMP_DECL = """#include <csetjmp>
struct RexJmpSlot { uint32_t buf; std::jmp_buf host; };
inline thread_local RexJmpSlot rex_jmp_slots[32];
inline thread_local int rex_jmp_depth = 0;"""


def setjmp_pass():
    """Implement CRT setjmp/longjmp on top of host setjmp/longjmp.
    sub_825FAC50 = setjmp (capture), sub_825F7CD0 = longjmp (full context
    restore). Flattened into normal calls, longjmp corrupts every frame it
    should unwind. Fix: at each setjmp call site, register the guest buffer
    in a thread-local slot stack and take a host setjmp; the guest longjmp
    restores the guest ctx, looks up the slot, resets the depth, and host
    longjmps back into the setjmp's frame, where execution falls through
    right after the capture call with the restored context. Functions with a
    registered site pop the depth at each subsequent return. Sites whose
    goto structure could reach a return without passing the site are left
    unregistered (their longjmps fall back to restore+return)."""
    n_reg = 0
    n_skip = 0
    call_line = f"\tsub_{SETJMP_ADDR}(ctx, base);"
    for cpp in sorted(GENERATED.glob("*recomp*.cpp")):
        lines = cpp.read_text().split("\n")
        spans = split_functions(lines)
        if not spans:
            continue
        needs_decl = False
        if any(f"DEFINE_REX_FUNC(sub_{LONGJMP_ADDR})" in l for l in lines):
            s = next(i for i, l in enumerate(lines) if f"DEFINE_REX_FUNC(sub_{LONGJMP_ADDR})" in l)
            e = next((i for i in range(s + 1, len(lines)) if lines[i].startswith("DEFINE_REX_FUNC(")), len(lines))
            for i in range(s, min(s + 6, e)):
                if "REX_FUNC_PROLOGUE();" in lines[i]:
                    lines.insert(i + 1, "\tconst uint32_t lj_buf_ = ctx.r3.u32;")
                    e += 1
                    break
            for i in range(s, e - 2):
                if (lines[i].strip() == "ctx.r3.u64 = ctx.r6.u64;"
                        and lines[i + 1].strip() == "// blr"
                        and lines[i + 2].strip() == "return;"):
                    lines[i + 1] = "\t// longjmp: resume at the matching setjmp site with restored context"
                    lines[i + 2] = ("\tfor (int li_ = rex_jmp_depth - 1; li_ >= 0; --li_) { if (rex_jmp_slots[li_].buf == lj_buf_) { rex_jmp_depth = li_ + 1; std::longjmp(rex_jmp_slots[li_].host, 1); } }\n"
                                    "\treturn;")
                    break
            spans = split_functions(lines)
            needs_decl = True
        sites = [i for i, l in enumerate(lines) if l == call_line]
        for sj in sorted(sites, reverse=True):
            span = find_span(spans, sj)
            if not span:
                continue
            s, e = span
            labels_before = set()
            gotos_before = set()
            for i in range(s, sj):
                m = re.match(r'^(loc_[0-9A-Fa-f]+):', lines[i])
                if m:
                    labels_before.add(m.group(1))
                for gm in re.finditer(r'goto (loc_[0-9A-Fa-f]+);', lines[i]):
                    gotos_before.add(gm.group(1))
            labels_inside = set()
            gotos_after = set()
            for i in range(sj + 1, e):
                m = re.match(r'^(loc_[0-9A-Fa-f]+):', lines[i])
                if m:
                    labels_inside.add(m.group(1))
                for gm in re.finditer(r'goto (loc_[0-9A-Fa-f]+);', lines[i]):
                    gotos_after.add(gm.group(1))
            if (gotos_after & labels_before) or (gotos_before & labels_inside):
                n_skip += 1
                print(f"  setjmp registration skipped in {cpp.name} (goto/label conflict)")
                continue
            lines[sj] = ("\t{ if (rex_jmp_depth < 32) { RexJmpSlot& sj_slot_ = rex_jmp_slots[rex_jmp_depth++]; sj_slot_.buf = ctx.r3.u32; "
                         f"if (setjmp(sj_slot_.host) == 0) {{ sub_{SETJMP_ADDR}(ctx, base); }} }} else {{ sub_{SETJMP_ADDR}(ctx, base); }} }}")
            for i in range(sj + 1, e):
                if lines[i].strip() == "return;":
                    indent = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
                    lines[i] = f"{indent}rex_jmp_depth -= 1;\n" + lines[i]
            close = e - 1
            while close > sj and lines[close].strip() != "}":
                close -= 1
            if close > sj:
                prev = close - 1
                while prev > sj and (lines[prev].strip() == "" or lines[prev].strip().startswith("//")):
                    prev -= 1
                if lines[prev].strip() != "return;":
                    lines.insert(close, "\trex_jmp_depth -= 1;")
                    e += 1
            spans = split_functions(lines)
            needs_decl = True
            n_reg += 1
        if needs_decl:
            t = "\n".join(lines)
            head = t.split("DEFINE_REX_FUNC")[0]
            if "rex_jmp_slots" not in head:
                lns = t.split("\n")
                ins = 0
                for i, ln in enumerate(lns):
                    if ln.startswith("#include"):
                        ins = i + 1
                for n, dl in enumerate(SETJMP_DECL.split("\n")):
                    lns.insert(ins + n, dl)
                t = "\n".join(lns)
            cpp.write_text(t)
    print(f"setjmp sites registered: {n_reg}, skipped: {n_skip}")
def main():
    total_goto = 0
    total_return = 0
    total_call = 0
    total_frag = 0
    total_epi = 0
    for cpp in sorted(GENERATED.glob("*.cpp")):
        text = cpp.read_text()
        lines = text.split("\n")
        spans = split_functions(lines)
        if not spans:
            # No functions (e.g. init/register) - only fix standalone call FATALs
            new = CALL_FATAL_RE.sub('\t// patched: unresolved call skipped', text)
            if new != text:
                cpp.write_text(new)
            continue

        # Collect label definitions per span
        labels_in_span = {}
        for (s, e) in spans:
            labs = set()
            for i in range(s, e):
                m = re.match(r'^(loc_[0-9A-Fa-f]+):', lines[i])
                if m:
                    labs.add(m.group(1))
            labels_in_span[(s, e)] = labs

        changed = False
        for i, ln in enumerate(lines):
            m = BRANCH_RE.search(ln)
            if m:
                cond, dst = m.group(1), m.group(2)
                label = "loc_" + dst[2:].upper()
                span = find_span(spans, i)
                if span and label in labels_in_span.get(span, set()):
                    lines[i] = ln.replace(
                        m.group(0), f'if ({cond}) goto {label}; // patched branch'
                    )
                    total_goto += 1
                else:
                    lines[i] = ln.replace(
                        m.group(0), f'if ({cond}) return; // patched branch (xfunc)'
                    )
                    total_return += 1
                changed = True
        # Fragment-call pass: sub_X(); return; -> goto loc_X when label is local
        nf = frag_pass(lines)
        if nf:
            total_frag += nf
            changed = True
        new_text = "\n".join(lines)
        # Standalone unresolved call FATALs -> skip the call (no-op), keep executing
        new2 = CALL_FATAL_RE.sub('\t// patched: unresolved call skipped', new_text)
        if new2 != new_text:
            total_call += 1
            new_text = new2
            changed = True
        if changed or new_text != text:
            cpp.write_text(new_text)

    # Epilogue synthesis pass (whole-file)
    for cpp in sorted(GENERATED.glob("*.cpp")):
        if "recomp" not in cpp.name:
            continue
        t = cpp.read_text()
        t2, n_added, n_skip = epilogue_pass(t)
        if n_added:
            cpp.write_text(t2)
            total_epi += n_added
        for sk in n_skip:
            print(f"  epilogue skipped (no frame): {sk.strip()}")

    # r3 null check before the callback bctrl (loc_825F807C pattern)
    pat = "\tif (ctx.cr6.eq) goto loc_825F807C;\n\t// mtctr r11"
    rep = "\tif (ctx.cr6.eq) goto loc_825F807C;\n\tif (ctx.r3.u32 == 0) goto loc_825F807C; // patched: skip null callback arg\n\t// mtctr r11"
    n = 0
    for cpp in sorted(GENERATED.glob("*.cpp")):
        t = cpp.read_text()
        if pat in t and "skip null callback arg" not in t:
            cpp.write_text(t.replace(pat, rep))
            n += 1
    print(f"r3 null check applied to {n} file(s)")
    setjmp_pass()
    print(f"Branch->goto: {total_goto}, Branch->return (cross-func): {total_return}, Call files patched: {total_call}, Frag-calls->goto: {total_frag}, Epilogues synthesized: {total_epi}")

if __name__ == "__main__":
    main()
