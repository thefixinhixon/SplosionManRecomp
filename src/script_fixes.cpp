// script_fixes.cpp - fixes applied to the game's Lua scripts as they load.
//
// The game compiles its Lua scripts from text at run time: every script goes
// through lua_load with the standard in-memory reader (getS), whose data is a
// {text pointer, size} pair. This hooks lua_load, recognizes a script by its
// opening line, and hands Lua an edited copy of the text when a fix applies.
//
// World select menu (Time Trial, DLC):
//   DLC packs put their world at a fixed slot of the level table: the
//   Prologue to Ms. 'Splosion Man pack (ContentPack2) uses slot 4, leaving
//   slot 3 (reserved for the first pack) empty. The menu assumes the worlds
//   are numbered 0..n-1: it counts four worlds, then reads slot 3, which is
//   missing, so the script stops with an error before it names the DLC tile
//   (the tile keeps its "BUFFERBUFFER..." placeholder text) and selecting the
//   tile asks for an empty world. The fix maps menu positions to the worlds
//   that exist, in order; the DLC world then shows as world 5.
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

REX_EXTERN(__imp__sub_8219B678);  // lua_load(L, reader, data, chunkname)

namespace {

constexpr uint32_t kGetS = 0x8219CEF0;  // the in-memory reader lua_load is given

struct Replace {
  std::string_view from;
  std::string_view to;
  int count;  // expected number of matches; anything else leaves the script alone
};

struct ScriptFix {
  const char* name;
  std::string_view signature;  // text the script starts with
  std::string_view prelude;    // prepended on the first line (keeps line numbers)
  std::vector<Replace> edits;
};

const std::vector<ScriptFix>& Fixes() {
  static const std::vector<ScriptFix> fixes = {
      {"world select menu: map menu positions to the worlds that exist",
       R"(print("Starting worldselectmenu script...");)",
       "local function smr_worlds() local t = {} "
       "for k in pairs(c_apoLevelTable[g_nCurrentSelectedLevelSet]) do t[#t + 1] = k end "
       "table.sort(t) return t end "
       "local function smr_world_key(i) return smr_worlds()[i + 1] or i end "
       "local function smr_menu_index(k) "
       "for i, v in ipairs(smr_worlds()) do if v == k then return i - 1 end end return -1 end ",
       {
           // tile label: the world's own number
           {R"("" .. (nWorldIndex+1))", R"("" .. (smr_world_key(nWorldIndex)+1))", 1},
           // level counts, completion and times per tile
           {"[g_nCurrentSelectedLevelSet][nWorldIndex]",
            "[g_nCurrentSelectedLevelSet][smr_world_key(nWorldIndex)]", 2},
           // cake state per tile
           {"[g_nCurrentSelectedLevelSet][i]", "[g_nCurrentSelectedLevelSet][smr_world_key(i)]", 1},
           // restoring the selection when the menu opens
           {"if(g_nCurrentSelectedWorld >=0 and g_nCurrentSelectedWorld < _me.m_nNumItems) then",
            "if(smr_menu_index(g_nCurrentSelectedWorld) >= 0) then", 1},
           {"_me.m_nCurrentSelection = g_nCurrentSelectedWorld;",
            "_me.m_nCurrentSelection = smr_menu_index(g_nCurrentSelectedWorld);", 1},
           // the world the rest of the game loads levels from
           {"g_nCurrentSelectedWorld = _me.m_nCurrentSelection;",
            "g_nCurrentSelectedWorld = smr_world_key(_me.m_nCurrentSelection);", 6},
       }},
  };
  return fixes;
}

int CountOf(std::string_view text, std::string_view what) {
  int n = 0;
  for (size_t at = text.find(what); at != std::string_view::npos; at = text.find(what, at + what.size()))
    ++n;
  return n;
}

// Returns the edited script, or an empty string when the text isn't the
// version the edits were written for.
std::string Apply(const ScriptFix& fix, std::string_view text) {
  for (const Replace& r : fix.edits) {
    if (CountOf(text, r.from) != r.count) {
      REXLOG_WARN("Script fix skipped ({}): expected {} x '{}', found {}", fix.name, r.count, r.from,
                  CountOf(text, r.from));
      return {};
    }
  }
  std::string out(fix.prelude);
  out.append(text);
  for (const Replace& r : fix.edits) {
    for (size_t at = out.find(r.from, fix.prelude.size()); at != std::string::npos;
         at = out.find(r.from, at + r.to.size()))
      out.replace(at, r.from.size(), r.to);
  }
  return out;
}

// Guest copies of edited scripts, kept for the life of the process: a script
// can be loaded more than once, and each copy only needs to be made once.
// A zero address records a script that couldn't be edited.
struct GuestText {
  uint32_t addr = 0;
  uint32_t size = 0;
};

std::map<std::string, GuestText, std::less<>>& GuestCopies() {
  static std::map<std::string, GuestText, std::less<>> copies;
  return copies;
}

GuestText EditedCopy(const ScriptFix& fix, std::string_view text) {
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  auto& copies = GuestCopies();
  if (auto it = copies.find(text); it != copies.end())
    return it->second;
  GuestText copy;
  if (const std::string edited = Apply(fix, text); !edited.empty()) {
    copy.addr = REX_KERNEL_MEMORY()->SystemHeapAlloc(static_cast<uint32_t>(edited.size()));
    if (copy.addr != 0) {
      std::memcpy(REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(copy.addr), edited.data(), edited.size());
      copy.size = static_cast<uint32_t>(edited.size());
      REXLOG_INFO("Script fix applied: {}", fix.name);
    }
  }
  copies.emplace(std::string(text), copy);
  return copy;
}

}  // namespace

REX_HOOK_RAW(sub_8219B678) {
  const uint32_t reader = ctx.r4.u32;
  const uint32_t data = ctx.r5.u32;
  if (reader == kGetS && data != 0) {
    // getS's data: { const char* text; size_t size; } (guest, big-endian)
    auto* load_s = reinterpret_cast<rex::be<uint32_t>*>(base + data);
    const uint32_t text_addr = load_s[0];
    const uint32_t text_size = load_s[1];
    if (text_addr != 0 && text_size != 0) {
      const std::string_view text(reinterpret_cast<const char*>(base + text_addr), text_size);
      for (const ScriptFix& fix : Fixes()) {
        if (!text.starts_with(fix.signature))
          continue;
        if (const GuestText copy = EditedCopy(fix, text); copy.addr != 0) {
          load_s[0] = copy.addr;
          load_s[1] = copy.size;
        }
        break;
      }
    }
  }
  __imp__sub_8219B678(ctx, base);
}
