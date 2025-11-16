#include "sm_cpu_infra.h"
#include "types.h"
#include "snes/cpu.h"
#include "snes/snes.h"
#include "config.h"
#include "tracing.h"
#include "ida_types.h"
#include "variables.h"
#include "funcs.h"
#include "sm_rtl.h"
#include "util.h"
#include "logging.h"
#include "enemy_types.h"
#include <time.h>

void RtlRunFrameCompare(uint16 input, int run_what);

enum RunMode { RM_BOTH, RM_MINE, RM_THEIRS };
uint8 g_runmode = RM_BOTH;

enum {
  kBugCountdownFrames = 300,  // 5 seconds at 60 FPS (5 * 60)
};

extern GameContext g_game_ctx;

Snes *g_snes;
Cpu *g_cpu;

bool g_calling_asm_from_c;
int g_calling_asm_from_c_ret;
bool g_fail;
bool g_use_my_apu_code = true;

typedef struct Snapshot {
  uint16 a, x, y, sp, dp, pc;
  uint8 k, db, flags;

  uint16_t vTimer;

  uint8 ram[0x20000];
  uint16 vram[0x8000];
  uint8 sram[0x2000];

  uint16 oam[0x120];
} Snapshot;

static Snapshot g_snapshot_mine, g_snapshot_theirs, g_snapshot_before;
static uint32 hookmode, hookcnt, hookadr;
static uint32 hooked_func_pc;
static uint8 hook_orgbyte[1024];
static uint8 hook_fixbug_orgbyte[1024];

static void VerifySnapshotsEq(Snapshot *b, Snapshot *a, Snapshot *prev);
static void MakeSnapshot(Snapshot *s);
static void RestoreSnapshot(Snapshot *s);

void Call(uint32 addr) {
  assert(addr & 0x8000);
  RunAsmCode(addr, 0, 0, 0, 0);
}

uint8_t *SnesRomPtr(uint32 v) {
  return (uint8*)RomPtr(v);
}

bool ProcessHook(uint32 v) {
  uint8_t *rombyte = SnesRomPtr(v);
  switch (hookmode) {
  case 0: // remove hooks
    *rombyte = hook_orgbyte[hookcnt++];
    return false;
  case 1: // install hooks
    hook_orgbyte[hookcnt++] = *rombyte;
    *rombyte = 0;
    return false;
  case 2:  // run hook
    if (v == hookadr) {
      hookmode = 3;
      return true;
    }
    return false;
  }
  return false;
}

bool FixBugHook(uint32 addr) {
  switch (hookmode) {
  case 1: { // install hooks
    uint8_t *rombyte = SnesRomPtr(addr);
    hook_fixbug_orgbyte[hookcnt++] = *rombyte;
    *rombyte = 0;
    return false;
  }
  case 2:  // run hook
    if (addr == hookadr) {
      hookmode = 3;
      return true;
    }
    hookcnt++;
    return false;
  }
  return false;
}

// ROM addresses where carry flag needs to be explicitly cleared before ADC operations
// The original SNES code relied on CPU state that isn't preserved in the C reimplementation
// These patches ensure correct arithmetic by clearing the carry flag before addition
static const  uint32 kPatchedCarrys[] = {
  // Unknown function - ADC operations requiring carry clear
  0xa7ac33,
  0xa7ac36,
  0xa7ac39,
  0xa7ac42,
  0xa7ac45,
  // Ridley_Func_107 - Ridley boss logic ADC operations
  0xa6d6d1,
  0xa6d6d3,
  0xa6d6d5,
  0xa6d700,
  0xa6d702,
  0xa6d704,
  // Ridley_Func_106
  0xa6d665,
  0xa6d667,
  0xa6d669,
  0xa6d694,
  0xa6d696,
  0xa6d698,
  // DrawSpritemapWithBaseTile2
  0x818b65,
  0x818b6B,
  // DrawSpritemapWithBaseTileOffscreen
  0x818ba7,
  0x818bd9,
  0x818bdf,

  // EprojInit_BombTorizoLowHealthInitialDrool
  0x86a671,
  0x86a680,
  0x86a6a9,
  0x86a6ba,
  // HandleEarthquakeSoundEffect
  0x88B245,
  // Ridley_Func_104
  0xA6D565,
  0xA6D567,
  0xA6D599,
  0xA6D59B,
  // Ridley_Func_105
  0xA6D5DB,
  0xA6D5DD,
  0xA6D60F,
  0xA6D611,
  // Ridley_Func_86
  0xA6CEFF,
  // Shitroid_GraduallyAccelerateTowardsPt
  0xa9f4a5,
  0xa9f4a7,
  0xa9f4d6,
  0xa9f4d8,
  // Shitroid_GraduallyAccelerateHoriz
  0xa9f519,
  0xa9f51f,
  0xa9f521,
  0xa9f554,
  0xa9f55a,
  0xa9f55c,
  // Shitroid_Func_16
  0xA9F24D,

  // Various ADC operations in core game logic (position calculations, sprite handling)
  0x80AA6A,
  0x80A592,
  0x80A720,
  0x80A7D6,
  0x80A99B,
  0x818AA7,
  0x94B176,
  0x94B156,

  // MotherBrain - Final boss ADC operations
  0xA99413,

  // Room dimension calculations (room_width_in_blocks, room_height_in_blocks)
  // These ADC operations compute room boundaries and tile positions
  0x80ab5d,
  0x84865c,
  0x848d90,
  0x84ab60,
  0x84b567,
  0x84b588,
  0x84b606,
  0x84b615,
  0x84b624,
  0x84b9d3,
  0x84b9e2,
  0x84ba07,
  0x84ba1e,
  0x84ba35,
  0x84d6ae,
  0x84d6bf,
  0x84d812,
  0x84daae,
  0x84dbaa,
  0x84dbe1,
  0x84dc20,
  0x84dc52,
  0x84dc89,
  0x84dcc8,
  0x84deae,
  0x84dedd,
  0x84df0a,
  0x84df39,
  0x86893a,
  0x9483a7,
  0x948405,
  0x949592,
  0x94a13f,
  0x94a2b2,
  0x94a3d8,
  0xa0bc33,
  0xa0bdac,
  0xa0bf45,
  0xa0c725,
  0x88B486,
  0x88C578,
  0xA292E8,
  0x86F18E,
  0x888CB6,
  0x888FAA,
  0x88A483,
  0x91CC35,
  0x91CBFF,
  0xA09541,
  0xA09552,
  0xA49AE8,
  0xA6C297,
  0xA6C3AD,
  0xA9C5EC,
  0xA9D500,
  0xA9D537,
  0xA9DCDB,

  // Enemy/sprite positioning and movement calculations
  0xA0A31B,
  0x91D064,
  0x91D07A,

  // Scrolling and camera logic
  0x90C719,

  // Enemy AI calculations
  0xA6A80E,
  0xA6A816,

  // Projectile physics
  0xA4906E,
  0xA49071,

  // Samus movement and physics
  0x90BC75,
  0x90BC93,

  // Animation frame calculations (set 1)
  0xA8A459,
  0xA8A45F,
  0xA8A465,
  0xA8A46B,

  // Animation frame calculations (set 2)
  0xA8A477,
  0xA8A47D,
  0xA8A483,
  0xA8A489,

  // Sprite tile calculations
  0xA8a543,
  0xA8a54f,
  0xA8a55b,
  0xA8a567,
  0xA8a573,
  0xA8a57f,
  0xA8a58b,

  // PLM (Point of Lifeform Emergence) room width instructions
  0x84D7CB,
  0x84D7E2,
  0x84D7F4,
  0x84D803,

  // Sound effect handlers
  0x8888CD,
  0x8888F0,
  0x8888E3,

  // Graphics and rendering calculations
  0x80A5F3,
  0x80A845,
  0x80A925,
  0x80A6AA,

  // Enemy state machine transitions
  0x948D94,
  0x948E25,

  // Collision detection
  0x9082A8,
  0x9082AE,

  // Boss pattern calculations
  0xA48CA1,
  0xA48CA4,
};

static uint8 kPatchedCarrysOrg[arraysize(kPatchedCarrys)];

/**
 * PatchBugs - Runtime bug fixes for original SNES code issues
 *
 * This function patches bugs in the original Super Metroid ROM code that become
 * visible when running the C reimplementation. The original SNES code relied on
 * uninitialized CPU state, undefined behavior, or had subtle logic errors that
 * were masked by the hardware's deterministic behavior.
 *
 * Each fix is triggered at a specific ROM address and corrects:
 * - Uninitialized CPU registers (A, X, Y)
 * - Incorrect flag assumptions (Z, C)
 * - Out-of-bounds memory access
 * - Logic errors in state machines
 *
 * @param mode Hook mode (execution context)
 * @param addr ROM address where the hook is triggered
 * @return New program counter address if control flow changes, 0 otherwise
 */
uint32 PatchBugs(uint32 mode, uint32 addr) {
  hookmode = mode, hookadr = addr, hookcnt = 0;

  // BUG: EprojInit_F336 - Uninitialized X register
  // FIX: Copy Y to X before use
  if (FixBugHook(0x86EF35)) {
    g_cpu->x = g_cpu->y;

  // BUG: EprojInit_F337 - Assumes zero flag is set based on A without comparison
  // FIX: Explicitly set zero flag after checking A
  // IMPACT: Fixes incorrect enemy projectile initialization
  } else if (FixBugHook(0x86EF45)) {
    g_cpu->z = (g_cpu->a == 0);

  // BUG: Graphics routine - Missing bounds check on Y
  // FIX: Skip problematic code path when Y is zero
  } else if (FixBugHook(0x818ab8)) {
    if (g_cpu->y == 0)
      g_cpu->pc = 0x8b1f;

  // BUG: Kraid_Arm_Shot - Y register contains garbage
  // FIX: Copy X (which has valid enemy index) to Y
  // IMPACT: Prevents Kraid arm projectiles from using invalid coordinates
  } else if (FixBugHook(0xa794ba)) {
    g_cpu->y = g_cpu->x;
  // BUG: KraidEnemy_ProcessInstrEnemyTimer - X register uninitialized
  // FIX: Set X to current enemy index
  } else if (FixBugHook(0xa7b968)) {
    g_cpu->x = cur_enemy_index;

  // BUG: KraidFoot_FirstPhase_Thinking - X register uninitialized
  // FIX: Set X to current enemy index
  } else if (FixBugHook(0xa7b963)) {
    g_cpu->x = cur_enemy_index;

  // BUG: Crocomire_Func_67 - Assumes A register is zero without initialization
  // FIX: Explicitly zero A register
  } else if (FixBugHook(0xA496C8)) {
    g_cpu->a = 0;

  // BUG: Samus_HandleSpeedBoosterAnimDelay - Destroys A register value
  // FIX: Restore A from speed_boost_counter
  // IMPACT: Speed booster animation timing works correctly
  } else if (FixBugHook(0x9085AA)) {
    g_cpu->a = speed_boost_counter;

  // BUG: MaridiaBeybladeTurtle_Func8 - Incorrectly assumes INC instruction sets carry flag
  // FIX: Set carry flag based on whether A wrapped to zero
  // IMPACT: Turtle enemy movement calculations work correctly
  } else if (FixBugHook(0xA29044) || FixBugHook(0xA2905D)) {
    g_cpu->c = (g_cpu->a == 0);

  // BUG: MaridiaBeybladeTurtle_Func8 - Performs one too many increment operations
  // FIX: Decrement A to compensate for extra INC
  } else if (FixBugHook(0xa29051)) {
    g_cpu->a--;
  } else if (FixBugHook(0xA5931C)) {  // Draygon_Func_35 needs cur_enemy_index in X
    g_cpu->x = cur_enemy_index;
  } else if (FixBugHook(0x80ADA4)) {  // DoorTransitionScrollingSetup_Down
    g_cpu->a = layer2_y_pos;
  } else if (FixBugHook(0x80ADD9)) {  // DoorTransitionScrollingSetup_Up
    g_cpu->a = layer2_y_pos;
  } else if (FixBugHook(0x80AD4d)) {  //  DoorTransitionScrollingSetup_Right
    g_cpu->a = layer2_x_pos;
  } else if (FixBugHook(0x80AD77)) {  //  DoorTransitionScrollingSetup_Left
    g_cpu->a = layer2_x_pos;
  } else if (FixBugHook(0x9381db)) {  // ProjectileInsts_GetValue reading from invalid memory for newly started ones
    int k = g_cpu->x;
    int ip = projectile_bomb_instruction_ptr[k >> 1];
    int delta = (projectile_bomb_instruction_timers[k >> 1] == 1 && !sign16(get_ProjectileInstr(ip)->timer)) ? 0 : -8;
    g_cpu->a += 8 + delta;
  } else if (FixBugHook(0x86b701)) { // EprojPreInstr_EyeDoorProjectile using destroyed X
    g_cpu->x = g_cpu->y;
  } else if (FixBugHook(0x8FC1B0)) {  // RoomCode_GenRandomExplodes X is garbage
    g_cpu->x = g_cpu->a;
  } else if (FixBugHook(0x80804F)) {
    //for(int i = 0; i < 5000; i++)
    //  snes_readBBusOrg(g_snes, (uint8_t)APUI00);
  } else if (FixBugHook(0x829325)) {
    // forgot to change bank
    g_cpu->db = 0x82;
  } else if (FixBugHook(0x848ACD)) {
    // PlmInstr_IncrementArgumentAndJGE A is not zeroed
    g_cpu->a = 0;
  } else if (FixBugHook(0xA7CEB2)) {
    // Phantoon_Main forgots to reload x
    g_cpu->x = cur_enemy_index;
  } else if (FixBugHook(0x91CD44)) {
    // Xray_SetupStage4_Func2 passes a bad value to Xray_GetXrayedBlock
    if (g_cpu->x == 0)
      g_cpu->pc = 0xCD52;

  // Fix VAR BEAM etc.
  // Prevent EquipmentScreenCategory_ButtonResponse from getting called when category changed
  } else if (FixBugHook(0x82AFD3)) {
    if ((uint8)pausemenu_equipment_category_item != 1)
      return 0x82AFD9;
  } else if (FixBugHook(0x82B0CD)) {
    if ((uint8)pausemenu_equipment_category_item != 2)
      return 0x82AFD9;
  } else if (FixBugHook(0x82B15B)) {
    if ((uint8)pausemenu_equipment_category_item != 3)
      return 0x82AFD9;
  } else if (FixBugHook(0xA2D38C)) {
    // MaridiaLargeSnail_Touch uses uninitialized X
    g_cpu->x = cur_enemy_index;
  } else if (FixBugHook(0xA4970F)) {
    // Crocomire_Func_67 does weird things
    g_cpu->a &= 0xff;
    g_cpu->y = g_cpu->x & 0x7;
  } else if (FixBugHook(0xA496E0)) {
    if (g_cpu->x > 48) {
      croco_cur_vline_idx = g_cpu->x;
      g_cpu->mf = 0;
      return 0xA497CE;
    }
  } else if (FixBugHook(0x91DA89)) {
    // Samus_HandleScrewAttackSpeedBoostingPals reads OOB
    if (special_samus_palette_frame > 6)
      special_samus_palette_frame = 6;
  } else if (FixBugHook(0x828D56)) {
    WriteReg(VMAIN, 0x80); // BackupBG2TilemapForPauseMenu lacks this
  } else if (FixBugHook(0x88AFCF)) {
    if (g_cpu->a & 0x8000)  // RoomMainAsm_ScrollingSky reads oob
      g_cpu->a = 0;
  } else if (FixBugHook(0x88AFF2)) {
    if (g_cpu->a < 256)  // RoomMainAsm_ScrollingSky reads oob
      g_cpu->a = 256;
  } else if (FixBugHook(0x8189bd)) {
    if (g_cpu->y == 0)  // DrawSamusSpritemap reads invalid ptr
      return 0x818A35;
  } else if (FixBugHook(0xA29BC1)) {
    g_cpu->a = 1;  // ThinHoppingBlobs_Func8 reads from R1 instead of #1
  } else if (FixBugHook(0x82E910)) {
    WORD(g_ram[22]) = 0; // SpawnDoorClosingPLM doesn't zero R22
  } else if (FixBugHook(0x90A4C8)) {
    WORD(g_ram[18]) = 0;  // Samus_InitJump overwrites R18 in Samus_Movement_03_SpinJumping
  } else if (FixBugHook(0xA99F60)) {
    WORD(g_ram[22]) = 1; // MotherBrain_Instr_SpawnLaserEproj doesn't set R22
  } else if (FixBugHook(0x94A85B)) {
    memset(g_ram + 0xd82, 0, 8); // grapple_beam_tmpD82 not cleared in BlockCollGrappleBeam
  } else if (FixBugHook(0xA0A35C)) {
    // ProcessEnemyPowerBombInteraction - R18 may get overwritten by the enemy death routine
    REMOVED_R18 = HIBYTE(power_bomb_explosion_radius);
    REMOVED_R20 = (REMOVED_R18 + (REMOVED_R18 >> 1)) >> 1;
  } else if (FixBugHook(0xA7B049)) {
    // Kraid_Shot_Mouth: The real game doesn't preserve R18, R20 so they're junk at this point.
    // Force getting out of the loop.
    g_cpu->x = 0;
  } else if (FixBugHook(0xa5a018)) {
    // Draygon_Func_42 uses undefined varE24 value
    REMOVED_varE24 = 0;
  } else if (FixBugHook(0xb39ddb)) {
    // Botwoon_Func_26 uses regs that are overwritten
    // This made the flicker slightly worse, so added hysteresis
    Enemy_Botwoon *E = Get_Botwoon(cur_enemy_index);
    REMOVED_R18 = E->base.x_pos - E->botwoon_var_56;
    REMOVED_R20 = E->base.y_pos - E->botwoon_var_57;
  } else if (FixBugHook(0xB39E13)) {
    // add botwoon hysteresis
    Enemy_Botwoon *E = Get_Botwoon(cur_enemy_index);
    REMOVED_R22 = E->botwoon_var_45 = (uint8)(E->botwoon_var_45 + (int8)(REMOVED_R22 - E->botwoon_var_45) * 3 / 4);
  }

  return 0;
}

int RunPatchBugHook(uint32 addr) {
  uint32 new_pc = PatchBugs(2, addr);
  if (hookmode == 3) {
    if (new_pc == 0) {
      return hook_fixbug_orgbyte[hookcnt];
    } else {
      g_cpu->k = new_pc >> 16;
      g_cpu->pc = (new_pc & 0xffff) + 1;
      return *SnesRomPtr(new_pc);
    }
  }

  return -1;
}

int CpuOpcodeHook(uint32 addr) {
  for (size_t i = 0; i != arraysize(kPatchedCarrys); i++) {
    if (addr == kPatchedCarrys[i])
      return kPatchedCarrysOrg[i];
  }
  {
    int i = RunPatchBugHook(addr);
    if (i >= 0) return i;
  }
  assert(0);
  return 0;
}

bool HookedFunctionRts(int is_long) {
  if (g_calling_asm_from_c) {
    g_calling_asm_from_c_ret = is_long;
    g_calling_asm_from_c = false;
    return false;
  }
  assert(0);
  return false;
}

// Compare byte-based memory regions (RAM, SRAM) with smart byte/word formatting
static void CompareByteRegion(const char *region_name, const uint8 *mine, const uint8 *theirs,
                               const uint8 *prev, size_t size, int max_diffs) {
  if (memcmp(mine, theirs, size)) {
    LogError("@%d: %s compare failed (mine != theirs, prev):", snes_frame_counter, region_name);
    int j = 0;
    for (size_t i = 0; i < size; i++) {
      if (theirs[i] != mine[i]) {
        if (++j < max_diffs) {
          // Smart formatting: print as word if both bytes differ and properly aligned
          if (((i & 1) == 0 || i < 0x10000) && i + 1 < size && theirs[i + 1] != mine[i + 1]) {
            LogError("0x%.6X: %.4X != %.4X (%.4X)", (int)i,
                    WORD(mine[i]), WORD(theirs[i]), WORD(prev[i]));
            i++, j++;
          } else {
            LogError("0x%.6X: %.2X != %.2X (%.2X)", (int)i, mine[i], theirs[i], prev[i]);
          }
        }
      }
    }
    if (j)
      g_fail = true;
    LogError("  total of %d failed bytes", (int)j);
  }
}

// Compare word-based memory regions (VRAM, OAM)
static void CompareWordRegion(const char *region_name, const uint16 *mine, const uint16 *theirs,
                               const uint16 *prev, size_t word_count, int max_diffs) {
  if (memcmp(mine, theirs, sizeof(uint16) * word_count)) {
    LogError("@%d: %s compare failed (mine != theirs, prev):", snes_frame_counter, region_name);
    for (size_t i = 0, j = 0; i < word_count; i++) {
      if (theirs[i] != mine[i]) {
        LogError("0x%.6X: %.4X != %.4X (%.4X)", (int)i, mine[i], theirs[i], prev[i]);
        g_fail = true;
        if (++j >= max_diffs)
          break;
      }
    }
  }
}

static void VerifySnapshotsEq(Snapshot *b, Snapshot *a, Snapshot *prev) {
  memcpy(&b->ram[0x0], &a->ram[0x0], 0x51);  // r18, r20, R22 etc
  memcpy(&b->ram[0x1f5b], &a->ram[0x1f5b], 0x100 - 0x5b);  // stacck
  memcpy(&b->ram[0xad], &a->ram[0xad], 4);  // ptr_to_retaddr_parameters etc 
  memcpy(&b->ram[0x5e7], &a->ram[0x5e7], 14);  // bitmask, mult_tmp, mult_product_lo etc

  memcpy(&b->ram[0x5BC], &a->ram[0x5BC], 9);  // door_transition_vram_update etc
  memcpy(&a->ram[0x60B], &b->ram[0x60B], 6);  // eproj_init_param_2, remaining_enemy_hitbox_entries, REMOVED_num_projectiles_to_check_enemy_coll
  memcpy(&a->ram[0x611], &b->ram[0x611], 6);  // coroutine_state (copy from mine to theirs)
  memcpy(&b->ram[0x641], &a->ram[0x641], 2);  // apu_attempts_countdown
  memcpy(&a->ram[0x77e], &b->ram[0x77e], 5);  // my counter
  memcpy(&a->ram[0x78F], &b->ram[0x78F], 2);  // door_bts
  
  memcpy(&a->ram[0x7b7], &b->ram[0x7b7], 2);  // event_pointer
  memcpy(&a->ram[0x933], &b->ram[0x933], 10);  // var933 etc
  memcpy(&b->ram[0xA82], &a->ram[0xA82], 2);  // xray_angle
  memcpy(&b->ram[0xB24], &a->ram[0xB24], 4);  // xray_angle
  memcpy(&a->ram[0xd1e], &b->ram[0xd1e], 2);  // grapple_beam_unkD1E
  memcpy(&a->ram[0xd82], &b->ram[0xd82], 8);  // grapple_beam_tmpD82

  memcpy(&a->ram[0xd9c], &b->ram[0xd9c], 2);  // grapple_beam_tmpD82
  memcpy(&a->ram[0xdd2], &b->ram[0xdd2], 6);  // temp_collision_DD2 etc
  memcpy(&a->ram[0xd8a], &b->ram[0xd8a], 6);  // grapple_beam_tmpD8A
  memcpy(&a->ram[0xe20], &b->ram[0xe20], 0xe46 - 0xe20);  // temp vars
  memcpy(&a->ram[0xe54], &b->ram[0xe54], 2);  // cur_enemy_index
  
  memcpy(&a->ram[0xe02], &b->ram[0xe02], 2);  // samus_bottom_boundary_position
  memcpy(&a->ram[0xe4a], &b->ram[0xe4a], 2);  // new_enemy_index
  memcpy(&a->ram[0xe56], &b->ram[0xe56], 4);  // REMOVED_cur_enemy_index_backup etc
  
  
  memcpy(&a->ram[0x1784], &b->ram[0x1784], 8);  // enemy_ai_pointer etc
  memcpy(&a->ram[0x1790], &b->ram[0x1790], 4);  // set_to_rtl_when_loading_enemies_unused etc
  memcpy(&a->ram[0x17a8], &b->ram[0x17a8], 4);  // interactive_enemy_indexes_index
  
  memcpy(&a->ram[0x1834], &b->ram[0x1834], 8);  // distance_to_enemy_colliding_dirs
  memcpy(&a->ram[0x184A], &b->ram[0x184A], 18);  // samus_x_pos_colliding_solid etc
  memcpy(&a->ram[0x186E], &b->ram[0x186E], 16+8);  // REMOVED_enemy_spritemap_entry_pointer etc
  memcpy(&a->ram[0x18A6], &b->ram[0x18A6], 2);  // collision_detection_index
  memcpy(&a->ram[0x189A], &b->ram[0x189A], 12);  // samus_target_x_pos etc
  
  memcpy(&b->ram[0x1966], &a->ram[0x1966], 6);  // current_fx_entry_offset etc
  memcpy(&b->ram[0x1993], &a->ram[0x1993], 2);  // eproj_init_param
  memcpy(&b->ram[0x19b3], &a->ram[0x19b3], 2);  // mode7_spawn_param
  memcpy(&b->ram[0x1a93], &a->ram[0x1a93], 2);  // cinematic_spawn_param
  memcpy(&b->ram[0x1B9D], &a->ram[0x1B9D], 2);  // cinematic_spawn_param
  memcpy(&a->ram[0x1E77], &b->ram[0x1E77], 2);  // current_slope_bts

  memcpy(&a->ram[0x9100], &b->ram[0x9100], 0x1cc + 2);  // XrayHdmaFunc has some bug that i couldn't fix in asm
  memcpy(&a->ram[0x9800], &b->ram[0x9800], 0x1cc+2);  // XrayHdmaFunc has some bug that i couldn't fix in asm
  memcpy(&a->ram[0x99cc], &b->ram[0x99cc], 2);  // XrayHdmaFunc_BeamAimedL writes outside
  memcpy(&a->ram[0xEF74], &b->ram[0xEF74], 4);  // next_enemy_tiles_index
  memcpy(&a->ram[0xF37A], &b->ram[0xF37A], 6);  // word_7EF37A etc

  // Compare all memory regions and report differences
  CompareByteRegion("Memory", b->ram, a->ram, prev->ram, 0x20000, 256);
  CompareByteRegion("SRAM", b->sram, a->sram, prev->sram, 0x2000, 128);
#if 1
  CompareWordRegion("VRAM", b->vram, a->vram, prev->vram, 0x8000, 32);
  CompareWordRegion("VRAM OAM", b->oam, a->oam, prev->oam, 0x120, 16);
#endif
}

static void MakeSnapshot(Snapshot *s) {
  Cpu *c = g_cpu;
  s->a = c->a, s->x = c->x, s->y = c->y;
  s->sp = c->sp, s->dp = c->dp, s->db = c->db;
  s->pc = c->pc, s->k = c->k;
  s->flags = cpu_getFlags(c);
  s->vTimer = g_snes->vTimer;
  memcpy(s->ram, g_snes->ram, 0x20000);
  memcpy(s->sram, g_snes->cart->ram, g_snes->cart->ramSize);
  memcpy(s->vram, g_snes->ppu->vram, sizeof(uint16) * 0x8000);
  memcpy(s->oam, g_snes->ppu->oam, sizeof(uint16) * 0x120);
}

static void MakeMySnapshot(Snapshot *s) {
  memcpy(s->ram, g_snes->ram, 0x20000);
  memcpy(s->sram, g_snes->cart->ram, g_snes->cart->ramSize);
  memcpy(s->vram, g_snes->ppu->vram, sizeof(uint16) * 0x8000);
  memcpy(s->oam, g_snes->ppu->oam, sizeof(uint16) * 0x120);
}

static void RestoreSnapshot(Snapshot *s) {
  Cpu *c = g_cpu;
  c->a = s->a, c->x = s->x, c->y = s->y;
  c->sp = s->sp, c->dp = s->dp, c->db = s->db;
  c->pc = s->pc, c->k = s->k;
  g_snes->vTimer = s->vTimer;
  cpu_setFlags(c, s->flags);
  memcpy(g_snes->ram, s->ram, 0x20000);
  memcpy(g_snes->cart->ram, s->sram, 0x2000);
  memcpy(g_snes->ppu->vram, s->vram, sizeof(uint16) * 0x8000);
  memcpy(g_snes->ppu->oam, s->oam, sizeof(uint16) * 0x120);
}

int RunAsmCode(uint32 pc, uint16 a, uint16 x, uint16 y, int flags) {
  uint16 org_sp = g_cpu->sp;
  uint16 org_pc = g_cpu->pc;
  uint8 org_b = g_cpu->db;
  uint16 org_dp = g_cpu->dp;

  printf("RunAsmCode!\n");
  g_ram[0x1ffff] = 1;

  bool dc = g_snes->debug_cycles;
  g_cpu->db = pc >> 16;

  g_cpu->a = a;
  g_cpu->x = x;
  g_cpu->y = y;
  g_cpu->spBreakpoint = g_cpu->sp;
  g_cpu->k = (pc >> 16);
  g_cpu->pc = (pc & 0xffff);
  g_cpu->mf = (flags & 1);
  g_cpu->xf = (flags & 2) >> 1;
  g_calling_asm_from_c = true;
  while (g_calling_asm_from_c) {
    uint32 pc = g_snes->cpu->k << 16 | g_snes->cpu->pc;
    if (g_snes->debug_cycles) {
      char line[80];
      getProcessorStateCpu(g_snes, line);
      puts(line);
      line[0] = 0;
    }
    cpu_runOpcode(g_cpu);
    while (g_snes->dma->dmaBusy)
      dma_doDma(g_snes->dma);

    if (flags & 1) {
      for(int i = 0; i < 10; i++)
        apu_cycle(g_snes->apu);
    }
  }
  g_cpu->dp = org_dp;
  g_cpu->sp = org_sp;
  g_cpu->db = org_b;
  g_cpu->pc = org_pc;

  g_snes->debug_cycles = dc;

  return g_calling_asm_from_c_ret;
}

static bool loadRom(const char *name, Snes *snes) {
  size_t length = 0;
  uint8_t *file = NULL;
  file = ReadWholeFile(name, &length);
  if (file == NULL) {
    puts("Failed to read file");
    return false;
  }
  bool result = snes_loadRom(snes, file, (int)length);
  free(file);
  return result;
}

void PatchBytes(uint32 addr, const uint8 *value, size_t n) {
  for(size_t i = 0; i != n; i++)
    SnesRomPtr(addr)[i] = value[i];
}

typedef struct RomPatch {
  uint32 addr;
  const uint8 *data;
  size_t size;
} RomPatch;

// Patches add/sub to ignore carry
void FixupCarry(uint32 addr) {
  *SnesRomPtr(addr) = 0;
}

uint16 currently_installed_bug_fix_counter;

void RtlUpdateSnesPatchForBugfix() {
  currently_installed_bug_fix_counter = bug_fix_counter;
  // Patch HandleMessageBoxInteraction logic
  { uint8 t[] = { 0x20, 0x50, 0x96, 0x60 }; PatchBytes(0x8584A3, t, sizeof(t)); }
  // while ((bug_fix_counter < 1 ? joypad1_newkeys : joypad1_lastkeys) == 0);
  { uint8 t[] = { 0x20, 0x36, 0x81, 0x22, 0x59, 0x94, 0x80, 0xc2, 0x30, 0xa5, (bug_fix_counter < 1) ? 0x8f : 0x8b, 0xf0, 0xf3, 0x60 }; PatchBytes(0x859650, t, sizeof(t)); }
  { uint8 t[] = { 0x18, 0x18 }; PatchBytes(0x8584CC, t, sizeof(t)); }  // Don't wait 2 loops
}

Snes *SnesInit(const char *filename) {
  g_snes = snes_init(g_ram);

  g_cpu = g_snes->cpu;

  bool loaded = loadRom(filename, g_snes);
  if (!loaded) {
    return NULL;
  }

  g_sram = g_snes->cart->ram;
  g_rom = g_snes->cart->rom;

  RtlSetupEmuCallbacks(NULL, &RtlRunFrameCompare, NULL);

  // Ensure it will run reset first.
  coroutine_state_0 = 1;

#if 1
  // Helper macro to reduce boilerplate in ROM patches
  #define PATCH(addr, ...) { \
    static const uint8 data[] = { __VA_ARGS__ }; \
    PatchBytes(addr, data, sizeof(data)); \
  }

  PATCH(0x82896b, 0x20, 0x0f, 0xf7);
  PATCH(0x82F70F, 0x7c, 0x81, 0x89);

  // Some code called by GameState_37_CeresGoesBoomWithSamus_ forgets to clear the M flag
  PATCH(0x8BA362, 0x5f, 0xf7);
  PATCH(0x8BF760, 0xc2, 0x20, 0x4c, 0x67, 0xa3);
  //PATCH(0x808028, 0x60);  // Apu_UploadBank hangs
  PATCH(0x8584B2, 0x0a, 0x0a);  // HandleMessageBoxInteraction has a loop

  // LoadRoomPlmGfx passes bad value
  PATCH(0x84efd3, 0xc0, 0x00, 0x00, 0xf0, 0x03, 0x20, 0x64, 0x87, 0x60);
  PATCH(0x848243, 0xd3, 0xef);

  // EprojColl_8676 doesn't initialize Y
  PATCH(0x86f4a6, 0xac, 0x91, 0x19, 0x4c, 0x76, 0x86);
  PATCH(0x8685bd, 0xa6, 0xf4);

  // Fix so main code is in a function.
  PATCH(0x82f713, 0xc2, 0x30, 0x22, 0x59, 0x94, 0x80, 0x20, 0x48, 0x89, 0x22, 0x38, 0x83, 0x80, 0x4C, 0x13, 0xF7);
  PATCH(0x828944, 0x58, 0x4c, 0x13, 0xf7);
  PATCH(0x82897a, 0x28, 0x60);

  // Remove IO_HVBJOY loop in ReadJoypadInput
  PATCH(0x80945C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);

  // Fix so NorfairLavaMan_Func_12 initializes Y
  PATCH(0xa8b237, 0xbc, 0xaa, 0x0f, 0xc9, 0x6c, 0x00, 0x10, 0x1a);

  // MaridiaBeybladeTurtle_Func8 negate
  PATCH(0xa2904b, 0x49, 0xff, 0xff, 0x69, 0x00, 0x00);
  PATCH(0xa29065, 0x49, 0xff, 0xff, 0x69, 0x00, 0x00);

  // Remove DebugLoadEnemySetData
  PATCH(0xA0896F, 0x6b);
  // MotherBrainsTubesFalling_Falling wrong X value
  PATCH(0xA98C12, 0x18, 0x18, 0x18);

  PATCH(0x8085F6, 0x60);

  // Remove 4 frames of delay in reset routine
  PATCH(0x80843C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);
  PATCH(0x808475, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);
  PATCH(0x808525, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);

  // Remove WaitUntilEndOfVblank in WaitUntilEndOfVblankAndClearHdma - We run frame by frame.
  PATCH(0x8882A1, 0x18, 0x18, 0x18, 0x18);

  // Remove WaitForNMI in GameState_41_TransitionToDemo.
  PATCH(0x828533, 0x18, 0x18, 0x18, 0x18);

  // WaitForNMI in ScreenOfWaitNmi / ScreenOnWaitNMI
  PATCH(0x80837B, 0x18, 0x18, 0x18, 0x18);
  PATCH(0x80838E, 0x18, 0x18, 0x18, 0x18);

  // WaitUntilEndOfVblankAndEnableIrq
  PATCH(0x82DF6C, 0x18, 0x18, 0x18, 0x18);

  // Remove loops based on door_transition_vram_update_enabled
  // Replace with a call to Irq_DoorTransitionVramUpdate
  PATCH(0x80d000, 0x20, 0x32, 0x96, 0x6b);
  PATCH(0x82E02C, 0x22, 0x00, 0xd0, 0x80, 0x18);
  PATCH(0x82E06B, 0x22, 0x00, 0xd0, 0x80, 0x18);
  PATCH(0x82E50D, 0x22, 0x00, 0xd0, 0x80, 0x18);
  PATCH(0x82E609, 0x22, 0x00, 0xd0, 0x80, 0x18);

  // Remove infinite loop polling door_transition_flag (AD 31 09 10 FB)
  PATCH(0x82E526, 0x22, 0x04, 0xd0, 0x80, 0x18);
  PATCH(0x80d004, 0x22, 0x38, 0x83, 0x80, 0xad, 0x31, 0x09, 0x10, 0xf7, 0x6b);

  // Remove WaitForNMI in DoorTransitionFunction_LoadMoreThings_Async
  PATCH(0x82E540, 0x18, 0x18, 0x18, 0x18);

  // Remove WaitForNMI in CinematicFunctionBlackoutFromCeres
  PATCH(0x8BC11E, 0x18, 0x18, 0x18, 0x18);

  // Remove WaitForNMI in CinematicFunctionEscapeFromCeres
  PATCH(0x8BD487, 0x18, 0x18, 0x18, 0x18);

  // Patch InitializePpuForMessageBoxes
  PATCH(0x858148, 0x18, 0x18, 0x18);  // WaitForLagFrame
  PATCH(0x8581b2, 0x18, 0x18, 0x18);  // WaitForLagFrame
  PATCH(0x8581EA, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);  // HandleMusicQueue etc

  // Patch ClearMessageBoxBg3Tilemap
  PATCH(0x858203, 0x18, 0x18, 0x18);  // WaitForLagFrame
  PATCH(0x858236, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);  // HandleMusicQueue etc

  // Patch WriteMessageTilemap
  PATCH(0x8582B8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);

  // Patch SetupPpuForActiveMessageBox
  PATCH(0x858321, 0x18, 0x18, 0x18);  // WaitForLagFrame
  PATCH(0x85835A, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);  // InitializePpuForMessageBoxes

  // Patch ToggleSaveConfirmationSelection
  PATCH(0x858532, 0x18, 0x18, 0x18);  // WaitForNMI_NoUpdate
  PATCH(0x85856b, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);  // HandleMusicQueue etc.

  // Patch DisplayMessageBox
  PATCH(0x858096, 0x18, 0x18, 0x18);  // Remove MsgBoxDelayFrames_2
  PATCH(0x8580B4, 0x18, 0x18, 0x18);  // Remove MsgBoxDelayFrames_2
  PATCH(0x8580DC, 0x18, 0x18, 0x18);  // Remove MsgBoxDelayFrames_2
  PATCH(0x8580F2, 0x18, 0x18, 0x18);  // Remove MsgBoxDelayFrames_2

  // Patch RestorePpuForMessageBox
  PATCH(0x85861C, 0x18, 0x18, 0x18);  // WaitForNMI_NoUpdate
  PATCH(0x858651, 0x18, 0x18, 0x18);  // WaitForNMI_NoUpdate
  PATCH(0x858692, 0x18, 0x18, 0x18, 0x18);  // HdmaObjectHandler
  PATCH(0x858696, 0x18, 0x18, 0x18, 0x18);  // HandleSoundEffects

  // Patch Fix_MsgBoxMakeHdmaTable_NoSleep
  PATCH(0x859660, 0x08, 0xc2, 0x30, 0x4c, 0xa9, 0x85);
  PATCH(0x8583BA, 0x20, 0x60, 0x96);  // MsgBoxMakeHdmaTable

  // Patch GunshipTop_13 to not block
  PATCH(0x859670, 0x22, 0x81, 0x96, 0x85, 0xc9, 0xff, 0xff, 0xd0, 0x04, 0x5c, 0x5f, 0xab, 0xa2, 0x5c, 0x26, 0xab, 0xa2);  // DisplayMessageBox_DoubleRet
  PATCH(0x859681, 0xcd, 0x1f, 0x1c, 0xd0, 0x08, 0x9c, 0x1f, 0x1c, 0xad, 0xf9, 0x05, 0x6b, 0xff, 0x8d, 0xc8, 0x0d, 0xa9, 0xff, 0xff, 0x6b);  // DisplayMessageBox_Poll
  PATCH(0xa2ab22, 0x5c, 0x70, 0x96, 0x85);  // GunshipTop_13

  // EnemyMain_WithCheckMsgBox
  PATCH(0x8596a0, 0x22, 0xd4, 0x8f, 0xa0, 0xad, 0xc8, 0x0d, 0xf0, 0x07, 0x22, 0x95, 0x96, 0x85, 0x9c, 0xc8, 0x0d, 0x6b);
  PATCH(0x828b65, 0x22, 0xa0, 0x96, 0x85);  // EnemyMain -> EnemyMain_WithCheckMsgBox

  // CloseMessageBox_ResetMsgBoxIdx
  PATCH(0x8596C0, 0x20, 0x89, 0x85, 0xa9, 0x1c, 0x00, 0x8d, 0x1f, 0x1c, 0x60);
  PATCH(0x8580E5, 0x20, 0xC0, 0x96);

  // ProcessPlm_CheckMessage
  PATCH(0x84EFDC, 0xad, 0xc8, 0x0d, 0xf0, 0x11, 0x98, 0x9d, 0x27, 0x1d, 0xad, 0xc8, 0x0d, 0x22, 0x95, 0x96, 0x85, 0x9c, 0xc8, 0x0d, 0xbc, 0x27, 0x1d, 0x4c, 0xee, 0x85);
  PATCH(0x8485f7, 0xf4, 0xdb, 0xef);

  // Hook DisplayMessageBox so it writes to queued_message_box_index instead
  PATCH(0x859695, 0x08, 0x8b, 0xda, 0x5a, 0x5c, 0x84, 0x80, 0x85);  // DisplayMessageBox_Org
  PATCH(0x858080, 0x8d, 0xc8, 0x0d, 0x6b);  // Hook

  // PlmInstr_ActivateSaveStationAndGotoIfNo_Fixed
  PATCH(0x84f000, 0x22, 0x81, 0x96, 0x85, 0xc9, 0xff, 0xff, 0xf0, 0x04, 0x5c, 0xfa, 0x8c, 0x84, 0x7a, 0xfa, 0x88, 0x88, 0x60);  // Restart if -1
  PATCH(0x848cf6, 0x5c, 0x00, 0xf0, 0x84);  // PlmInstr_ActivateSaveStationAndGotoIfNo

  // SoftReset
  PATCH(0x81F000, 0xa9, 0xff, 0xff, 0x8d, 0x98, 0x09, 0x60);
  PATCH(0x819027, 0x5c, 0x00, 0xf0, 0x81);
  PATCH(0x819112, 0x5c, 0x00, 0xf0, 0x81);
  PATCH(0x8194e9, 0x5c, 0x00, 0xf0, 0x81);

  // Remove ReadJoypadInputs from Vector_NMI
  PATCH(0x8095E1, 0x18, 0x18, 0x18, 0x18);  // callf   ReadJoypadInputs

  // Remove APU_UploadBank
  if (g_use_my_apu_code)
    PATCH(0x808028, 0x60);

  // Remove reads from IO_APUI01 etc
  PATCH(0x828A59, 0x18, 0x18, 0x18, 0x80);  // SfxHandlers_1_WaitForAck
  PATCH(0x828A72, 0x18, 0x18, 0x18);  // SfxHandlers_2_ClearRequest
  PATCH(0x828A80, 0x18, 0x18, 0x18, 0x80);  // SfxHandlers_3_WaitForAck
  PATCH(0x828A67, 0x06);  // sfx_clear_delay

  // LoadStdBG3andSpriteTilesClearTilemaps does DMA from RAM
  PATCH(0x82831E, 0x00, 0x2E);

  PATCH(0x91C234, 0xa5, 0x25);  // Bugfix in XrayHdmaFunc_BeamAimedUUL

  // Remove call to InitializeMiniMapBroken
  PATCH(0x809AF3, 0x18, 0x18, 0x18, 0x18);  // callf   InitializeMiniMapBroken

  // NormalEnemyShotAiSkipDeathAnim_CurEnemy version that preserves R18 etc.
  PATCH(0xA7FF82, 0xA5, 0x12, 0x48, 0xA5, 0x14, 0x48, 0xA5, 0x16, 0x48, 0x22, 0xA7, 0xA6, 0xA0, 0x68, 0x85, 0x16, 0x68, 0x85, 0x14, 0x68, 0x85, 0x12, 0x6B);
  PATCH(0xa7b03a, 0x22, 0x82, 0xff, 0xa7);

  RtlUpdateSnesPatchForBugfix();

  for (size_t i = 0; i != arraysize(kPatchedCarrys); i++) {
    uint8 t = *SnesRomPtr(kPatchedCarrys[i]);
    if (t) {
      kPatchedCarrysOrg[i] = t;
      FixupCarry(kPatchedCarrys[i]);
    } else {
      printf("0x%x double patched!\n", kPatchedCarrys[i]);
    }
  }

  PatchBugs(1, 0);
  #undef PATCH  // Clean up PATCH macro
#endif
  return g_snes;
}

void DebugGameOverMenu(void) {
  assert(0);
}

uint32 RunCpuUntilPC(uint32 pc1, uint32 pc2) {
  for(;;) {
    if (g_snes->debug_cycles) {
      char line[80];
      getProcessorStateCpu(g_snes, line);
      puts(line);
    }
    cpu_runOpcode(g_cpu);

    uint32 addr = g_snes->cpu->k << 16 | g_snes->cpu->pc;
    if (addr == pc1 || addr == pc2)
      return addr;
  }
}

void RunOneFrameOfGame_Emulated(void) {
  uint16 bug_fix_bak = bug_fix_counter;
  // Execute until either WaitForNMI or WaitForLagFrame
  RunCpuUntilPC(0x808343, 0x85813C);

  // Trigger nmi, then run until WaitForNMI or WaitForLagFrame returns
  g_snes->cpu->nmiWanted = true;
  RunCpuUntilPC(0x80834A, 0x858142);

  bug_fix_counter = bug_fix_bak;
}

void DrawFrameToPpu(void) {
  g_snes->hPos = g_snes->vPos = 0;
  while (!g_snes->cpu->nmiWanted) {
    do {
      snes_handle_pos_stuff(g_snes);
    } while (g_snes->hPos != 0);
    if (g_snes->vIrqEnabled && (g_snes->vPos - 1) == g_snes->vTimer) {
      Vector_IRQ();
    }
  }
  g_snes->cpu->nmiWanted = false;
}

void SaveBugSnapshot() {
  if (!g_game_ctx.emulator_debug_flag && g_game_ctx.got_mismatch_count == 0) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "saves/bug-%d.sav", (int)time(NULL));
    RtlSaveSnapshot(buffer, true);
  }
  g_game_ctx.got_mismatch_count = kBugCountdownFrames;
}

void RunOneFrameOfGame_Both(void) {
  g_snes->ppu = g_snes->snes_ppu;
  MakeSnapshot(&g_snapshot_before);

  // Run orig version then snapshot
again_theirs:
  g_snes->runningWhichVersion = 1;
  RunOneFrameOfGame_Emulated();
  DrawFrameToPpu();
  MakeSnapshot(&g_snapshot_theirs);

  // Run my version and snapshot
//again_mine:
  g_snes->ppu = g_snes->my_ppu;
  RestoreSnapshot(&g_snapshot_before);

  g_snes->runningWhichVersion = 2;
  RunOneFrameOfGame();
  DrawFrameToPpu();
  MakeSnapshot(&g_snapshot_mine);

  g_snes->runningWhichVersion = 0xff;

  // Compare both snapshots
  VerifySnapshotsEq(&g_snapshot_mine, &g_snapshot_theirs, &g_snapshot_before);

  if (g_fail) {
    g_fail = false;

    printf("Verify failure!\n");

    g_snes->ppu = g_snes->snes_ppu;
    RestoreSnapshot(&g_snapshot_before);

    if (g_game_ctx.emulator_debug_flag)
      goto again_theirs;

    SaveBugSnapshot();
    RunOneFrameOfGame_Emulated();
    goto getout;
  }

  g_snes->ppu = g_snes->snes_ppu;
  RestoreSnapshot(&g_snapshot_theirs);
getout:
  g_snes->ppu = g_game_ctx.other_image ? g_snes->my_ppu : g_snes->snes_ppu;
  g_snes->runningWhichVersion = 0;

  // Trigger soft reset?
  if (game_state == 0xffff) {
    g_snes->cpu->k = 0x80;
    g_snes->cpu->pc = 0x8462;
    coroutine_state_0 = 3;
  }

  if (menu_index & 0xff00) {
    printf("MENU INDEX TOO BIG!\n");
    SaveBugSnapshot();
    menu_index &= 0xff;
  }


  if (g_game_ctx.got_mismatch_count)
    g_game_ctx.got_mismatch_count--;
}

static const char *kAreaNames[] = { "Crateria", "Brinstar", "Norfair", "WreckedShip", "Maridia", "Tourian", "Ceres", "Debug" };

static void PrintDebugInfo(void) {
  static uint32 frame_counter = 0;
  static uint32 last_print_frame = 0;
  extern Config g_config;
  extern Snes *g_snes;

  frame_counter++;

  if (!g_config.debug_display)
    return;

  // Throttle to once per second (60 frames)
  if (frame_counter - last_print_frame < 60)
    return;
  last_print_frame = frame_counter;

  const char *area_name = (area_index < 8) ? kAreaNames[area_index] : "Unknown";
  Ppu *ppu = g_snes ? g_snes->my_ppu : NULL;

  printf("[Frame:%u Room:0x%04X Area:%d(%s) ScrollX:%d(%d-%d) ScrollY:%d(%d-%d)",
         frame_counter, room_index, area_index, area_name,
         layer1_x_pos, map_min_x_scroll, map_max_x_scroll,
         layer1_y_pos, map_min_y_scroll, map_max_y_scroll);

  if (ppu && ppu->extraLeftRight > 0) {
    printf(" WS:L%d/R%d/%d", ppu->extraLeftCur, ppu->extraRightCur, ppu->extraLeftRight);
  }

  printf(" Samus:(%d,%d)]\n", samus_x_pos, samus_y_pos);
}

static void ConfigurePpuSideSpace(void) {
  // Dynamically adjust widescreen boundaries based on room scroll limits
  extern Snes *g_snes;
  if (!g_snes || !g_snes->my_ppu || !g_snes->snes_ppu)
    return;

  Ppu *ppu = g_snes->my_ppu;
  if (ppu->extraLeftRight == 0)
    return; // Widescreen not enabled

  // Get current scroll position and room limits
  uint16 scroll_x = layer1_x_pos;
  uint16 min_x = map_min_x_scroll;
  uint16 max_x = map_max_x_scroll;

  // Calculate how much extra space we can show on each side
  // Left: can show extra if we're not at the left edge
  int left_space = (scroll_x > min_x) ? ppu->extraLeftRight : 0;

  // Right: can show extra if we're not at the right edge
  // Standard screen width is 256, so right edge is at scroll_x + 256
  int right_space = (scroll_x + 256 < max_x) ? ppu->extraLeftRight : 0;

  // Set the calculated boundaries
  PpuSetExtraSideSpace(ppu, left_space, right_space);
  PpuSetExtraSideSpace(g_snes->snes_ppu, left_space, right_space);
}

void RtlRunFrameCompare(uint16 input, int run_what) {
  g_snes->input1->currentState = input;

  if (g_runmode == RM_THEIRS) {
    RunOneFrameOfGame_Emulated();
    DrawFrameToPpu();

  } else if (g_runmode == RM_MINE) {
    g_use_my_apu_code = true;

    g_snes->runningWhichVersion = 0xff;
    RunOneFrameOfGame();
    DrawFrameToPpu();
    g_snes->runningWhichVersion = 0;
  } else {
    g_use_my_apu_code = true;
    RunOneFrameOfGame_Both();
  }

  // Configure widescreen boundaries dynamically based on scroll position
  ConfigurePpuSideSpace();

  // Print debug info if enabled
  PrintDebugInfo();
}
