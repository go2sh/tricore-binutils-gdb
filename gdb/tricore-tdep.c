#include "defs.h"
#include "dwarf2/frame.h"
#include "frame-unwind.h"
#include "frame.h"
#include "gdbarch.h"
#include "gdbcore.h"
#include "gdbsupport/common-regcache.h"
#include "gdbsupport/common-types.h"
#include "gdbsupport/gdb_assert.h"
#include "gdbtypes.h"

#include "arch-utils.h"
#include "reggroups.h"
#include "target-descriptions.h"
#include "trad-frame.h"
#include <cassert>

#include "features/tricore.c"
#include "tricore-tdep.h"

static enum return_value_convention
tricore_return_value (struct gdbarch *gdbarch, struct value *function,
                      struct type *valtype, struct regcache *regcache,
                      struct value **read_value, const gdb_byte *writebuf)
{

  if (valtype->code () == TYPE_CODE_UNION
      || valtype->code () == TYPE_CODE_ARRAY
      || valtype->code () == TYPE_CODE_STRUCT)
    {
      if (valtype->length () > 64)
        {
          if (read_value)
            {
            }

          return RETURN_VALUE_ABI_RETURNS_ADDRESS;
        }
    }

  return RETURN_VALUE_REGISTER_CONVENTION;
}

/* Check for aligment of type */
static ULONGEST
tricore_type_align (struct gdbarch *gdbarch, struct type *type)
{
  if (type->code () == TYPE_CODE_INT)
    {
      if (type->m_length == 1)
        return 1;
      if (type->m_length == 2)
        return 2;
      if (type->m_length == 4 || type->m_length == 8)
        return 4;
    }

  assert (false);
  return 0;
}

/*  TODO: Add 32bit debug */
constexpr gdb_byte tricore_default_breakpoint[] = { 0x00, 0xA0 };

typedef BP_MANIPULATION (tricore_default_breakpoint) tricore_breakpoint;

static CORE_ADDR
tricore_skip_prologue (struct gdbarch *gdbarch, CORE_ADDR ip)
{
  /* TODO: Find addi instruction to a10 */
  return ip;
}

static CORE_ADDR
tricore_frame_align (struct gdbarch *gdbarch, CORE_ADDR address)
{
  return align_down (address, 16);
}

static CORE_ADDR
tricore_push_dummy_code (struct gdbarch *gdbarch, CORE_ADDR sp,
                         CORE_ADDR funaddr, struct value **args, int nargs,
                         struct type *value_type, CORE_ADDR *real_pc,
                         CORE_ADDR *bp_addr, struct regcache *regcache)
{
  /* A nop instruction */
  static const gdb_byte nop_insn[] = { 0x0D, 0x00, 0x00, 0x00 };

  /* Allocate space for a breakpoint, and keep the stack correctly
     aligned.  The space allocated here must be at least big enough to
     accommodate the NOP_INSN defined above.  */
  sp -= 16;
  *bp_addr = sp;
  *real_pc = funaddr;

  target_write_memory (*bp_addr, nop_insn, sizeof (nop_insn));

  return sp;
}

static CORE_ADDR
tricore_push_dummy_call (struct gdbarch *gdbarch, struct value *function,
                         struct regcache *regcache, CORE_ADDR bp_addr,
                         int nargs, struct value **args, CORE_ADDR sp,
                         function_call_return_method return_method,
                         CORE_ADDR struct_addr)
{
  assert (false);
  return sp;
}

struct tricore_unwind_cache
{

  /* The base (stack) address for this frame.  This is the stack pointer
      value on entry to this frame before any adjustments are made.  */
  CORE_ADDR pcx;

  CORE_ADDR frame_ptr;

  /* Information about previous register values.  */
  trad_frame_saved_reg *regs;

  /* The id for this frame.  */
  struct frame_id this_id;
};

static struct tricore_unwind_cache *
tricore_frame_cache (frame_info_ptr this_frame, void **this_cache)
{
  struct tricore_unwind_cache *cache;
  // struct gdbarch *gdbarch = get_frame_arch (this_frame);
  // int numregs, regno;

  if ((*this_cache) != NULL)
    return (struct tricore_unwind_cache *)*this_cache;

  cache = FRAME_OBSTACK_ZALLOC (struct tricore_unwind_cache);
  cache->regs = trad_frame_alloc_saved_regs (this_frame);
  (*this_cache) = cache;

  cache->pcx = get_frame_register_unsigned (this_frame, TRICORE_PCX_REGNUM);

#if 0
  /* The prologue scanner sets the address of registers stored to the stack
     as the offset of that register from the frame base.  The prologue
     scanner doesn't know the actual frame base value, and so is unable to
     compute the exact address.  We do now know the frame base value, so
     update the address of registers stored to the stack.  */
  numregs = gdbarch_num_regs (gdbarch) + gdbarch_num_pseudo_regs (gdbarch);
  for (regno = 0; regno < numregs; ++regno)
    {
      if (cache->regs[regno].is_addr ())
        cache->regs[regno].set_addr (cache->regs[regno].addr ()
                                     + cache->frame_base);
    }

  /* The previous $pc can be found wherever the $ra value can be found.
     The previous $ra value is gone, this would have been stored be the
     previous frame if required.  */
  cache->regs[gdbarch_pc_regnum (gdbarch)] = cache->regs[RISCV_RA_REGNUM];
  cache->regs[RISCV_RA_REGNUM].set_unknown ();

  /* Build the frame id.  */
  cache->this_id = frame_id_build (cache->frame_base, start_addr);

  /* The previous $sp value is the frame base value.  */
  cache->regs[gdbarch_sp_regnum (gdbarch)].set_value (cache->frame_base);
#endif
  return cache;
}

static void
tricore_frame_this_id (frame_info_ptr this_frame, void **this_cache,
                       struct frame_id *this_id)
{
  struct tricore_unwind_cache *cache
      = tricore_frame_cache (this_frame, this_cache);

  if (cache->pcx == 0)
    return;

  (*this_id)
      = frame_id_build_special (cache->frame_ptr, cache->pcx, cache->pcx);
}

static struct value *
tricore_frame_prev_register (frame_info_ptr this_frame, void **this_cache,
                             int prev_regnum)
{
  struct tricore_unwind_cache *cache
      = tricore_frame_cache (this_frame, this_cache);

  return trad_frame_get_prev_register (this_frame, cache->regs, prev_regnum);
}

/* Structure defining the Tricore normal frame unwind functions.  Since we
   are the fallback unwinder (DWARF unwinder is used first), we use the
   default frame sniffer, which always accepts the frame.  */

static const struct frame_unwind tricore_frame_unwind = {
  /*.name          =*/"tricore prologue",
  /*.type          =*/NORMAL_FRAME,
  /*.stop_reason   =*/default_frame_unwind_stop_reason,
  /*.this_id       =*/tricore_frame_this_id,
  /*.prev_register =*/tricore_frame_prev_register,
  /*.unwind_data   =*/NULL,
  /*.sniffer       =*/default_frame_sniffer,
  /*.dealloc_cache =*/NULL,
  /*.prev_arch     =*/NULL,
};

// static const reggroup *csr_reggroup = nullptr;

static int
tricore_dwarf_reg_to_regnum (struct gdbarch *gdbarch, int reg)
{
  if (reg >= 0 && reg < 32)
    {
      return reg;
    }
  gdb_assert (reg);
  return reg;
}

static const char *const tricore_pseudo_register_names[] = {
  "e0", "e2", "e4", "e6", "e8", "e10", "e12", "e14",
  "p0", "p2", "p4", "p6", "p8", "p10", "p12", "p14",
};

/* Total number of pseudo registers.  */
#define TRICORE_NUM_PSEUDO_REGS ARRAY_SIZE (tricore_pseudo_register_names)

/* Return the name of pseudo register REGNUM.  */

static const char *
tricore_pseudo_register_name (struct gdbarch *gdbarch, int regnum)
{
  regnum -= gdbarch_num_regs (gdbarch);

  gdb_assert (regnum < TRICORE_NUM_PSEUDO_REGS);
  return tricore_pseudo_register_names[regnum];
}

static struct type *
tricore_pseudo_register_type (struct gdbarch *gdbarch, int regnum)
{
  regnum -= gdbarch_num_regs (gdbarch);
  gdb_assert (regnum >= TRICORE_E0_REGNUM && regnum < TRICORE_PSEUDO_NUM);

  return builtin_type (gdbarch)->builtin_uint64;
}

static int
tricore_pseudo_register_reggroup_p (struct gdbarch *gdbarch, int regnum,
                                    const struct reggroup *reggroup)
{
  regnum -= gdbarch_num_regs (gdbarch);
  if (regnum < TRICORE_P0_REGNUM)
    {
      if (reggroup == general_reggroup)
        {
          return 1;
        }
    }
  else if (regnum < TRICORE_PSEUDO_NUM)
    {
      if (reggroup == general_reggroup)
        {
          return 1;
        }
    }

  return 0;
}

static enum register_status
tricore_pseudo_register_read (struct gdbarch *gdbarch,
                              readable_regcache *regcache, int regnum,
                              gdb_byte *buf)
{
  // tricore_gdbarch_tdep *tdep = gdbarch_tdep<tricore_gdbarch_tdep>(gdbarch);
  regnum -= gdbarch_num_regs (gdbarch);
  int realnum
      = regnum < TRICORE_P0_REGNUM ? TRICORE_D0_REGNUM : TRICORE_A0_REGNUM;

  gdb_byte reg0[4], reg1[4];
  memset (buf, 0, register_size (gdbarch, regnum));
  enum register_status status = regcache->raw_read_part (realnum, 0, 4, reg0);

  if (status != REG_VALID)
    return status;
  status = regcache->raw_read_part (realnum + 1, 0, 4, reg1);

  if (status != REG_VALID)
    return status;

  memcpy (buf, reg0, 4);
  memcpy (buf + 4, reg1, 4);

  return REG_VALID;
}

static void
tricore_pseudo_register_write (struct gdbarch *gdbarch,
                               struct regcache *regcache, int regnum,
                               const gdb_byte *buf)
{
  regnum -= gdbarch_num_regs (gdbarch);
  int realnum
      = regnum < TRICORE_P0_REGNUM ? TRICORE_D0_REGNUM : TRICORE_A0_REGNUM;

  regcache->raw_write_part (realnum, 0, 4, buf);
  regcache->raw_write_part (realnum + 1, 0, 4, buf + 4);
}

static const char *const tricore_register_names[]
    = { "d0",  "d1",     "d2",    "d3",   "d4",  "d5",  "d6",  "d7",
        "d8",  "d9",     "d10",   "d11",  "d12", "d13", "d14", "d15",

        "a0",  "a1",     "a2",    "a3",   "a4",  "a5",  "a6",  "a7",
        "a8",  "a9",     "a10",   "a11",  "a12", "a13", "a14", "a15",
        "lcx", "fcx",    "pcx",   "psw",  "pc",  "icr", "isp", "btv",
        "biv", "syscon", "pcon0", "dcon0" };

static struct type *
tricore_register_type (struct gdbarch *gdbarch, int regnum)
{
  if (tdesc_has_registers (gdbarch_target_desc (gdbarch)))
    return tdesc_register_type (gdbarch, regnum);

  switch (regnum)
    {
    case TRICORE_D0_REGNUM ... TRICORE_D15_REGNUM:
      return builtin_type (gdbarch)->builtin_int;
    case TRICORE_A0_REGNUM ... TRICORE_A15_REGNUM:
      return builtin_type (gdbarch)->builtin_data_ptr;
    case TRICORE_PSW_REGNUM:
    case TRICORE_PCX_REGNUM:
      return builtin_type (gdbarch)->builtin_uint32;
    case TRICORE_PC_REGNUM:
    case TRICORE_BIV_REGNUM:
    case TRICORE_BTV_REGNUM:
      return builtin_type (gdbarch)->builtin_func_ptr;
    default:
      break;
    }
  if (regnum >= gdbarch_num_regs (gdbarch))
    return tricore_pseudo_register_type (gdbarch, regnum);

  return builtin_type (gdbarch)->builtin_int;
}

static const char *
tricore_register_name (struct gdbarch *gdbarch, int regnum)
{
  if (tdesc_has_registers (gdbarch_target_desc (gdbarch)))
    return tdesc_register_name (gdbarch, regnum);

  if (regnum < gdbarch_num_regs (gdbarch))
    return tricore_register_names[regnum];

  return tricore_pseudo_register_name (gdbarch, regnum);
}

static std::string
tricore_gcc_target_options (struct gdbarch *gdbarch)
{
  return "";
}

static const char *
tricore_gnu_triplet_regexp (struct gdbarch *gdbarch)
{
  return "tricore";
}

/* Initialize the current architecture based on INFO.  If possible,
   re-use an architecture from ARCHES, which is a list of
   architectures already created during this debugging session.

   Called e.g. at program startup, when reading a core file, and when
   reading a binary file.  */
static struct gdbarch *
tricore_gdbarch_init (struct gdbarch_info info, struct gdbarch_list *arches)
{
  tdesc_arch_data_up tdesc_data;
  const struct target_desc *tdesc = info.target_desc;

  /* If there is already a candidate, use it.  */
  arches = gdbarch_list_lookup_by_info (arches, &info);
  if (arches != NULL)
    return arches->gdbarch;
  if (tdesc == NULL)
    tdesc = tdesc_tricore;

  /* Check any target description for validity.  */
  if (tdesc_has_registers (tdesc))
    {
      const struct tdesc_feature *feature;
      int valid_p;
      int i;

      feature = tdesc_find_feature (tdesc, "org.gnu.gdb.tricore.core");
      if (feature == NULL)
        return NULL;
      tdesc_data = tdesc_data_alloc ();

      valid_p = 1;
      for (i = 0; i < TRICORE_NUM_REGS; i++)
        valid_p &= tdesc_numbered_register (feature, tdesc_data.get (), i,
                                            tricore_register_names[i]);

      if (!valid_p)
        return NULL;
    }

  gdbarch *gdbarch
      = gdbarch_alloc (&info, gdbarch_tdep_up (new tricore_gdbarch_tdep));
  // tricore_gdbarch_tdep *tdep = gdbarch_tdep<tricore_gdbarch_tdep>(gdbarch);
  // const struct target_desc *tdesc = info.target_desc;

  /* Target data types.  */
  set_gdbarch_short_bit (gdbarch, 16);
  set_gdbarch_int_bit (gdbarch, 32);
  set_gdbarch_long_bit (gdbarch, 32);
  set_gdbarch_long_long_bit (gdbarch, 64);
  set_gdbarch_float_bit (gdbarch, 32);
  set_gdbarch_double_bit (gdbarch, 64);
  set_gdbarch_long_double_bit (gdbarch, 64);
  set_gdbarch_long_double_format (gdbarch, floatformats_ieee_double);
  set_gdbarch_ptr_bit (gdbarch, 32);
  set_gdbarch_char_signed (gdbarch, 1);
  set_gdbarch_type_align (gdbarch, tricore_type_align);

  /* Information about the target architecture.  */
  set_gdbarch_return_value_as_value (gdbarch, tricore_return_value);
  set_gdbarch_breakpoint_kind_from_pc (gdbarch,
                                       tricore_breakpoint::kind_from_pc);
  set_gdbarch_sw_breakpoint_from_kind (gdbarch,
                                       tricore_breakpoint::bp_from_kind);
  set_gdbarch_have_nonsteppable_watchpoint (gdbarch, 1);

  /* Functions to analyze frames.  */
  set_gdbarch_skip_prologue (gdbarch, tricore_skip_prologue);
  set_gdbarch_inner_than (gdbarch, core_addr_lessthan);
  set_gdbarch_frame_align (gdbarch, tricore_frame_align);

  /* Functions handling dummy frames.  */
  set_gdbarch_call_dummy_location (gdbarch, ON_STACK);
  set_gdbarch_push_dummy_code (gdbarch, tricore_push_dummy_code);
  set_gdbarch_push_dummy_call (gdbarch, tricore_push_dummy_call);

  /* Frame unwinders.  Use DWARF debug info if available, otherwise use our own
     unwinder.  */
  dwarf2_append_unwinders (gdbarch);
  frame_unwind_append_unwinder (gdbarch, &tricore_frame_unwind);

  /* Register architecture.  */
  // riscv_add_reggroups(gdbarch);

  /* Internal <-> external register number maps.  */
  set_gdbarch_dwarf2_reg_to_regnum (gdbarch, tricore_dwarf_reg_to_regnum);

  /* We reserve all possible register numbers for the known registers.
     This means the target description mechanism will add any target
     specific registers after this number.  This helps make debugging GDB
     just a little easier.  */
  set_gdbarch_num_regs (gdbarch, TRICORE_NUM_REGS);

  /* Some specific register numbers GDB likes to know about.  */
  set_gdbarch_sp_regnum (gdbarch, TRICORE_A10_REGNUM);
  set_gdbarch_pc_regnum (gdbarch, TRICORE_PC_REGNUM);

  // set_gdbarch_print_registers_info(gdbarch, tricore_print_registers_info);

  set_tdesc_pseudo_register_name (gdbarch, tricore_pseudo_register_name);
  set_tdesc_pseudo_register_type (gdbarch, tricore_pseudo_register_type);
  set_tdesc_pseudo_register_reggroup_p (gdbarch,
                                        tricore_pseudo_register_reggroup_p);
  set_gdbarch_pseudo_register_read (gdbarch, tricore_pseudo_register_read);
  set_gdbarch_pseudo_register_write (gdbarch, tricore_pseudo_register_write);
  set_gdbarch_num_pseudo_regs (gdbarch, TRICORE_PSEUDO_NUM);

  set_gdbarch_register_type (gdbarch, tricore_register_type);
  set_gdbarch_register_name (gdbarch, tricore_register_name);
  // set_gdbarch_cannot_store_register(gdbarch, riscv_cannot_store_register);
  // set_gdbarch_register_reggroup_p(gdbarch, riscv_register_reggroup_p);

  ///* Create register aliases for alternative register names.  We only
  //   create aliases for registers which were mentioned in the target
  //   description.  */
  // for (const auto &alias : pending_aliases)
  //  alias.create(gdbarch);

  /* Compile command hooks.  */
  set_gdbarch_gcc_target_options (gdbarch, tricore_gcc_target_options);
  set_gdbarch_gnu_triplet_regexp (gdbarch, tricore_gnu_triplet_regexp);

  /* Disassembler options support.  */
  // set_gdbarch_valid_disassembler_options(gdbarch,
  // disassembler_options_riscv()); set_gdbarch_disassembler_options(gdbarch,
  // &riscv_disassembler_options);

#if 0
  /* SystemTap Support.  */
  set_gdbarch_stap_is_single_operand(gdbarch, riscv_stap_is_single_operand);
  set_gdbarch_stap_register_indirection_prefixes(
      gdbarch, stap_register_indirection_prefixes);
  set_gdbarch_stap_register_indirection_suffixes(
      gdbarch, stap_register_indirection_suffixes);
#endif

  /* Hook in OS ABI-specific overrides, if they have been registered.  */
  gdbarch_init_osabi (info, gdbarch);

  if (tdesc_data != NULL)
    tdesc_use_registers (gdbarch, tdesc, std::move (tdesc_data));

  return gdbarch;
}

void _initialize_tricore_tdep ();
void
_initialize_tricore_tdep ()
{
  initialize_tdesc_tricore ();

  gdbarch_register (bfd_arch_tricore, tricore_gdbarch_init, NULL);
}